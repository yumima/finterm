"""Anthropic-path runtime — drives Claude via the claude-agent-sdk.

The SDK is open-source (Apache 2.0) but wraps the `claude` CLI as a
subprocess.  Auth happens through Claude Code (env ANTHROPIC_API_KEY
or `claude login`).  No API key handling lives here; finterm's
SecureStorage path is wired separately by Track 3 item 10.

Track 3 item 9 — minimum viable integration:
- `run_turn(fixture)` accepts an evals fixture and returns a Trace.
- Streams SDK messages, maps content blocks to Trace fields.
- ResultMessage closes the turn — model id, cost, latency, errors.

Later tracks fill in: MCP registration (item 11), SKILL.md loading
(item 12), full streaming-event bridge (item 13).
"""
from __future__ import annotations

import asyncio
import uuid
from datetime import datetime, timezone
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from evals.fixture import Fixture
    from evals.trace import Trace


class AnthropicRuntimeUnavailable(RuntimeError):
    """Raised when the SDK can't be imported or the CLI is missing."""


def run_turn(fixture: "Fixture") -> "Trace":
    """Execute a fixture's input on the Anthropic profile.

    Sync wrapper around the async SDK call.  Raises
    AnthropicRuntimeUnavailable if the SDK or its CLI dependency is
    missing — the eval harness treats that as a skip, not a failure.
    """
    return asyncio.run(_run_async(fixture))


async def _run_async(fixture: "Fixture") -> "Trace":
    try:
        from claude_agent_sdk import (
            AssistantMessage,
            ClaudeAgentOptions,
            CLINotFoundError,
            ResultMessage,
            SystemMessage,
            TextBlock,
            ThinkingBlock,
            ToolResultBlock,
            ToolUseBlock,
            UserMessage,
            query,
        )
    except ImportError as exc:
        raise AnthropicRuntimeUnavailable(
            "claude-agent-sdk not installed — run `pip install claude-agent-sdk`"
        ) from exc

    from evals.trace import Budget, Citation, ToolCall, ToolResult, Trace

    trace = Trace(
        turn_id=uuid.uuid4().hex,
        runtime="anthropic",
        model="",
        fixture_name=fixture.name,
        prompts=[fixture.input.message],
        started_at=datetime.now(timezone.utc).isoformat(),
    )

    options = ClaudeAgentOptions(
        max_turns=fixture.expected.trace.max_iterations or 10,
    )

    try:
        async for message in query(prompt=fixture.input.message, options=options):
            if isinstance(message, AssistantMessage):
                trace.iterations += 1
                for block in getattr(message, "content", []):
                    _absorb_block(block, trace, TextBlock, ToolUseBlock, ToolResultBlock, ThinkingBlock)
            elif isinstance(message, UserMessage):
                # Tool results often come back as UserMessage blocks.
                for block in getattr(message, "content", []):
                    _absorb_block(block, trace, TextBlock, ToolUseBlock, ToolResultBlock, ThinkingBlock)
            elif isinstance(message, SystemMessage):
                # Currently ignored — system frames don't shape the trace.
                pass
            elif isinstance(message, ResultMessage):
                _absorb_result(message, trace)
    except CLINotFoundError as exc:
        raise AnthropicRuntimeUnavailable(
            f"`claude` CLI not on PATH — install Claude Code: {exc}"
        ) from exc
    except Exception as exc:  # noqa: BLE001 — surface any SDK error in the trace
        trace.errors.append(f"{type(exc).__name__}: {exc}")

    if not trace.finished_at:
        trace.finished_at = datetime.now(timezone.utc).isoformat()
    return trace


def _absorb_block(block, trace, TextBlock, ToolUseBlock, ToolResultBlock, ThinkingBlock) -> None:
    """Map one SDK content block onto the Trace."""
    from evals.trace import ToolCall, ToolResult

    if isinstance(block, TextBlock):
        trace.output += block.text
    elif isinstance(block, ToolUseBlock):
        trace.tool_calls.append(
            ToolCall(
                id=getattr(block, "id", ""),
                name=getattr(block, "name", ""),
                arguments=dict(getattr(block, "input", {}) or {}),
            )
        )
    elif isinstance(block, ToolResultBlock):
        output = getattr(block, "content", None)
        error = None
        if getattr(block, "is_error", False):
            error = str(output) if output is not None else "tool error"
        trace.tool_results.append(
            ToolResult(
                call_id=getattr(block, "tool_use_id", ""),
                output=output,
                error=error,
            )
        )
    elif isinstance(block, ThinkingBlock):
        # Thinking blocks are dropped from output but counted via iterations.
        # Track 3 item 13 / Track 14 may surface them in the trace directly.
        pass


def _absorb_result(message, trace) -> None:
    """Map the SDK's terminating ResultMessage onto the Trace."""
    from evals.trace import Budget

    trace.latency_ms = float(getattr(message, "duration_ms", 0) or 0)

    usage = getattr(message, "usage", None) or {}
    if isinstance(usage, dict):
        trace.budget = Budget(
            token_in=int(usage.get("input_tokens") or usage.get("prompt_tokens") or 0),
            token_out=int(usage.get("output_tokens") or usage.get("completion_tokens") or 0),
            cost_usd=float(getattr(message, "total_cost_usd", 0.0) or 0.0),
            cache_hit_tokens=int(
                usage.get("cache_read_input_tokens")
                or usage.get("cache_creation_input_tokens")
                or 0
            ),
        )

    # Best-effort model id — ResultMessage has model_usage as a dict keyed by model name.
    model_usage = getattr(message, "model_usage", None) or {}
    if isinstance(model_usage, dict) and model_usage:
        trace.model = next(iter(model_usage.keys()))

    if getattr(message, "is_error", False):
        result = getattr(message, "result", None) or ""
        trace.errors.append(f"sdk error: {result}")

    errors = getattr(message, "errors", None) or []
    for err in errors:
        trace.errors.append(str(err))

    trace.finished_at = datetime.now(timezone.utc).isoformat()

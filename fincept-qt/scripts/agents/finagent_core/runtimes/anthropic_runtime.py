"""Anthropic-path runtime — drives Claude via the claude-agent-sdk.

The SDK is open-source (Apache 2.0) but wraps the `claude` CLI as a
subprocess.  Auth happens through Claude Code (env ANTHROPIC_API_KEY
or `claude login`).  No API key handling lives here; finterm's
SecureStorage path is wired separately by Track 3 item 10.

Track 3 item 9 — minimum viable integration:
- `run_turn(fixture)` accepts an evals fixture and returns a Trace.
- Streams SDK messages, maps content blocks to Trace fields.
- ResultMessage closes the turn — model id, cost, latency, errors.

Track 3 item 11 — MCP registration:
- Optional `tool_bridge` kwarg supplies the McpService catalog as an
  in-process SDK MCP server (see mcp_bridge.py).  Default is an
  empty bridge so eval-harness runs without tools still work.

Track 3 item 13 — streaming events:
- Optional `stream_handler` kwarg receives typed events as the turn
  unfolds (text, thinking, tool_use, tool_result, done).  Default is
  NoopStreamHandler; production AgentService passes a handler that
  forwards events as Qt signals to the chat surface.
- ThinkingBlock content now lands in trace.thinking (was dropped).

Later: SKILL.md loading (item 12).
"""
from __future__ import annotations

import asyncio
import uuid
from datetime import datetime, timezone
from typing import TYPE_CHECKING

from .mcp_bridge import EmptyToolBridge, ToolBridge, build_sdk_mcp_server
from .stream_handler import NoopStreamHandler, StreamHandler

if TYPE_CHECKING:
    from evals.fixture import Fixture
    from evals.trace import Trace


class AnthropicRuntimeUnavailable(RuntimeError):
    """Raised when the SDK can't be imported or the CLI is missing."""


def run_turn(
    fixture: "Fixture",
    *,
    tool_bridge: ToolBridge | None = None,
    stream_handler: StreamHandler | None = None,
) -> "Trace":
    """Execute a fixture's input on the Anthropic profile.

    Sync wrapper around the async SDK call.  Raises
    AnthropicRuntimeUnavailable if the SDK or its CLI dependency is
    missing — the eval harness treats that as a skip, not a failure.

    `tool_bridge` exposes finterm's McpService catalog to the SDK as
    an in-process MCP server.  Default is EmptyToolBridge (no tools),
    so fixtures that don't need tools work without any wiring; tests
    inject a fake bridge to exercise specific tool flows; production
    AgentService passes a bridge backed by the McpService IPC.

    `stream_handler` receives typed events as the turn runs.  Default
    is NoopStreamHandler (silent); production passes a handler that
    forwards each event to the chat surface.
    """
    bridge = tool_bridge if tool_bridge is not None else EmptyToolBridge()
    handler = stream_handler if stream_handler is not None else NoopStreamHandler()
    return asyncio.run(_run_async(fixture, bridge, handler))


async def _run_async(
    fixture: "Fixture", tool_bridge: ToolBridge, handler: StreamHandler
) -> "Trace":
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

    # Translate the McpService catalog (via the bridge) into an
    # in-process SDK MCP server.  Empty catalogs collapse to no
    # mcp_servers entry so the SDK doesn't register an empty server.
    mcp_servers: dict = {}
    sdk_server = build_sdk_mcp_server(tool_bridge, fixture.input.agent)
    if sdk_server is not None:
        mcp_servers["finterm"] = sdk_server

    options_kwargs: dict = {"max_turns": fixture.expected.trace.max_iterations or 10}
    if mcp_servers:
        options_kwargs["mcp_servers"] = mcp_servers
    options = ClaudeAgentOptions(**options_kwargs)

    try:
        async for message in query(prompt=fixture.input.message, options=options):
            if isinstance(message, AssistantMessage):
                trace.iterations += 1
                for block in getattr(message, "content", []):
                    _absorb_block(
                        block, trace, handler,
                        TextBlock, ToolUseBlock, ToolResultBlock, ThinkingBlock,
                    )
            elif isinstance(message, UserMessage):
                # Tool results often come back as UserMessage blocks.
                for block in getattr(message, "content", []):
                    _absorb_block(
                        block, trace, handler,
                        TextBlock, ToolUseBlock, ToolResultBlock, ThinkingBlock,
                    )
            elif isinstance(message, SystemMessage):
                # Currently ignored — system frames don't shape the trace.
                pass
            elif isinstance(message, ResultMessage):
                _absorb_result(message, trace)
            else:
                # StreamEvent / RateLimitEvent etc. — forward the raw
                # event payload to the handler so consumers that opt
                # into include_partial_messages get them.  Trace state
                # is unaffected at this granularity.
                payload = getattr(message, "event", None)
                if isinstance(payload, dict):
                    handler.on_partial_event(payload)
    except CLINotFoundError as exc:
        raise AnthropicRuntimeUnavailable(
            f"`claude` CLI not on PATH — install Claude Code: {exc}"
        ) from exc
    except Exception as exc:  # noqa: BLE001 — surface any SDK error in the trace
        trace.errors.append(f"{type(exc).__name__}: {exc}")

    if not trace.finished_at:
        trace.finished_at = datetime.now(timezone.utc).isoformat()
    handler.on_done(trace)
    return trace


def _absorb_block(
    block, trace, handler, TextBlock, ToolUseBlock, ToolResultBlock, ThinkingBlock,
) -> None:
    """Map one SDK content block onto the Trace and notify the handler."""
    from evals.trace import ToolCall, ToolResult

    if isinstance(block, TextBlock):
        trace.output += block.text
        handler.on_text(block.text)
    elif isinstance(block, ToolUseBlock):
        call = ToolCall(
            id=getattr(block, "id", ""),
            name=getattr(block, "name", ""),
            arguments=dict(getattr(block, "input", {}) or {}),
        )
        trace.tool_calls.append(call)
        handler.on_tool_use(call)
    elif isinstance(block, ToolResultBlock):
        output = getattr(block, "content", None)
        error = None
        if getattr(block, "is_error", False):
            error = str(output) if output is not None else "tool error"
        result = ToolResult(
            call_id=getattr(block, "tool_use_id", ""),
            output=output,
            error=error,
        )
        trace.tool_results.append(result)
        handler.on_tool_result(result)
    elif isinstance(block, ThinkingBlock):
        text = getattr(block, "thinking", "")
        if text:
            trace.thinking.append(text)
            handler.on_thinking(text)


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

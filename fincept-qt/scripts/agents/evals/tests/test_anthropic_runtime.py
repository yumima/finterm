"""Tests for the Anthropic runtime adapter.

The adapter wraps claude_agent_sdk.query (an async generator over
SDK message types).  We monkeypatch query() to yield fake messages
so the tests don't hit the network and don't depend on the
`claude` CLI being installed or authenticated.
"""
from __future__ import annotations

import asyncio
import importlib
from dataclasses import dataclass
from pathlib import Path

import pytest

from evals.fixture import load_fixture
from evals.runtime_adapter import Runtime, RuntimeNotReady, run_turn

# Locate the walking-skeleton fixture once.
_HELLO_WORLD = Path(__file__).resolve().parents[1] / "e2e" / "hello_world"


def _install_fake_sdk(monkeypatch, fake_messages):
    """Monkeypatch claude_agent_sdk so query() yields our fake messages.

    Returns the reloaded anthropic_runtime module so tests can poke
    at it directly.
    """
    try:
        import claude_agent_sdk
    except ImportError:
        pytest.skip("claude_agent_sdk not installed")

    async def fake_query(*, prompt, options=None, transport=None):
        for msg in fake_messages:
            yield msg

    monkeypatch.setattr(claude_agent_sdk, "query", fake_query)

    # Anthropic runtime imports lazily inside the async function, so
    # the patched symbol is what it'll resolve when run.
    from finagent_core.runtimes import anthropic_runtime
    importlib.reload(anthropic_runtime)
    return anthropic_runtime


def test_runtime_local_still_skips() -> None:
    fixture = load_fixture(_HELLO_WORLD)
    with pytest.raises(RuntimeNotReady, match="local runtime"):
        run_turn(fixture, Runtime.LOCAL)


def test_anthropic_runtime_builds_trace_from_text(monkeypatch) -> None:
    """A simple text-only assistant reply produces a complete trace."""
    try:
        from claude_agent_sdk import (
            AssistantMessage,
            ResultMessage,
            TextBlock,
        )
    except ImportError:
        pytest.skip("claude_agent_sdk not installed")

    fake_msgs = [
        AssistantMessage(
            content=[TextBlock(text="Hello, world.")],
            model="claude-test",
            parent_tool_use_id=None,
            uuid="msg-1",
        ),
        ResultMessage(
            subtype="success",
            duration_ms=42,
            duration_api_ms=30,
            is_error=False,
            num_turns=1,
            session_id="s1",
            stop_reason="end_turn",
            total_cost_usd=0.001,
            usage={"input_tokens": 5, "output_tokens": 3},
            result="ok",
            structured_output=None,
            model_usage={"claude-test": {}},
            permission_denials=None,
            deferred_tool_use=None,
            errors=None,
            api_error_status=None,
            uuid="res-1",
        ),
    ]

    _install_fake_sdk(monkeypatch, fake_msgs)
    fixture = load_fixture(_HELLO_WORLD)

    trace = run_turn(fixture, Runtime.ANTHROPIC)

    assert trace.runtime == "anthropic"
    assert trace.output == "Hello, world."
    assert trace.iterations == 1
    assert trace.latency_ms == 42.0
    assert trace.budget.token_in == 5
    assert trace.budget.token_out == 3
    assert trace.budget.cost_usd == pytest.approx(0.001)
    assert trace.model == "claude-test"
    assert trace.finished_at  # populated
    assert trace.errors == []


def test_anthropic_runtime_captures_tool_use(monkeypatch) -> None:
    """A turn that emits ToolUseBlock + ToolResultBlock builds the right Trace."""
    try:
        from claude_agent_sdk import (
            AssistantMessage,
            ResultMessage,
            TextBlock,
            ToolResultBlock,
            ToolUseBlock,
            UserMessage,
        )
    except ImportError:
        pytest.skip("claude_agent_sdk not installed")

    fake_msgs = [
        AssistantMessage(
            content=[
                ToolUseBlock(id="tu-1", name="int:markets.quote", input={"ticker": "AAPL"}),
            ],
            model="claude-test",
            parent_tool_use_id=None,
            uuid="msg-1",
        ),
        UserMessage(
            content=[
                ToolResultBlock(
                    tool_use_id="tu-1",
                    content={"price": 100.0},
                    is_error=False,
                ),
            ],
            parent_tool_use_id=None,
        ),
        AssistantMessage(
            content=[TextBlock(text="AAPL is at $100.")],
            model="claude-test",
            parent_tool_use_id=None,
            uuid="msg-2",
        ),
        ResultMessage(
            subtype="success",
            duration_ms=200,
            duration_api_ms=150,
            is_error=False,
            num_turns=2,
            session_id="s1",
            stop_reason="end_turn",
            total_cost_usd=0.002,
            usage={"input_tokens": 10, "output_tokens": 8},
            result="ok",
            structured_output=None,
            model_usage={"claude-test": {}},
            permission_denials=None,
            deferred_tool_use=None,
            errors=None,
            api_error_status=None,
            uuid="res-1",
        ),
    ]

    _install_fake_sdk(monkeypatch, fake_msgs)
    fixture = load_fixture(_HELLO_WORLD)
    trace = run_turn(fixture, Runtime.ANTHROPIC)

    assert "int:markets.quote" in trace.tool_call_names()
    assert len(trace.tool_calls) == 1
    assert trace.tool_calls[0].arguments == {"ticker": "AAPL"}
    assert len(trace.tool_results) == 1
    assert trace.tool_results[0].call_id == "tu-1"
    assert trace.tool_results[0].error is None
    assert trace.output == "AAPL is at $100."
    assert trace.iterations == 2  # two AssistantMessages


def test_anthropic_runtime_records_sdk_errors(monkeypatch) -> None:
    """A ResultMessage with is_error=True ends up in trace.errors."""
    try:
        from claude_agent_sdk import ResultMessage
    except ImportError:
        pytest.skip("claude_agent_sdk not installed")

    fake_msgs = [
        ResultMessage(
            subtype="error",
            duration_ms=10,
            duration_api_ms=5,
            is_error=True,
            num_turns=0,
            session_id="s1",
            stop_reason="error",
            total_cost_usd=0.0,
            usage=None,
            result="rate limited",
            structured_output=None,
            model_usage=None,
            permission_denials=None,
            deferred_tool_use=None,
            errors=["upstream 429"],
            api_error_status=429,
            uuid="res-1",
        ),
    ]

    _install_fake_sdk(monkeypatch, fake_msgs)
    fixture = load_fixture(_HELLO_WORLD)
    trace = run_turn(fixture, Runtime.ANTHROPIC)

    assert trace.errors, "expected an error to be recorded"
    assert any("rate limited" in e for e in trace.errors)
    assert any("upstream 429" in e for e in trace.errors)


def test_missing_sdk_surfaces_as_skip(monkeypatch) -> None:
    """If the SDK fails to import, the harness sees RuntimeNotReady."""
    # Sabotage the import inside the async runner by stripping the SDK
    # from sys.modules and shadowing it with a non-importable name.
    import sys

    real_module = sys.modules.pop("claude_agent_sdk", None)
    sentinel = object()
    monkeypatch.setitem(sys.modules, "claude_agent_sdk", sentinel)

    fixture = load_fixture(_HELLO_WORLD)
    try:
        with pytest.raises(RuntimeNotReady, match="claude-agent-sdk"):
            run_turn(fixture, Runtime.ANTHROPIC)
    finally:
        if real_module is not None:
            sys.modules["claude_agent_sdk"] = real_module
        else:
            sys.modules.pop("claude_agent_sdk", None)

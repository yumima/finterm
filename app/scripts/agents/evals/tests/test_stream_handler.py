"""Tests for the streaming-event handler bridge (Track 3 #13)."""
from __future__ import annotations

import importlib
from pathlib import Path

import pytest

pytest.importorskip("claude_agent_sdk")

from finagent_core.runtimes.stream_handler import (
    NoopStreamHandler,
    RecordingStreamHandler,
    StreamHandler,
)

_HELLO_WORLD = Path(__file__).resolve().parents[1] / "e2e" / "hello_world"


def test_noop_handler_satisfies_protocol() -> None:
    assert isinstance(NoopStreamHandler(), StreamHandler)


def test_recording_handler_captures_in_order() -> None:
    h = RecordingStreamHandler()
    h.on_text("hello")
    h.on_thinking("planning")
    h.on_text(" world")
    h.on_partial_event({"raw": True})
    assert [e[0] for e in h.events] == ["text", "thinking", "text", "partial"]


def _install_fake_query(monkeypatch, fake_messages):
    import claude_agent_sdk

    async def fake_query(*, prompt, options=None, transport=None):
        for msg in fake_messages:
            yield msg

    monkeypatch.setattr(claude_agent_sdk, "query", fake_query)
    from finagent_core.runtimes import anthropic_runtime
    importlib.reload(anthropic_runtime)
    return anthropic_runtime


def test_thinking_block_lands_in_trace_and_handler(monkeypatch) -> None:
    from claude_agent_sdk import (
        AssistantMessage, ResultMessage, TextBlock, ThinkingBlock,
    )
    from evals.fixture import load_fixture

    fake_msgs = [
        AssistantMessage(
            content=[
                ThinkingBlock(thinking="let me work this out", signature="sig"),
                TextBlock(text="the answer is 4"),
            ],
            model="claude-test", parent_tool_use_id=None, uuid="m1",
        ),
        ResultMessage(
            subtype="success", duration_ms=1, duration_api_ms=1, is_error=False,
            num_turns=1, session_id="s", stop_reason="end_turn",
            total_cost_usd=0.0, usage=None, result="", structured_output=None,
            model_usage=None, permission_denials=None, deferred_tool_use=None,
            errors=None, api_error_status=None, uuid="r",
        ),
    ]
    runtime = _install_fake_query(monkeypatch, fake_msgs)
    handler = RecordingStreamHandler()

    trace = runtime.run_turn(load_fixture(_HELLO_WORLD), stream_handler=handler)

    # Trace: thinking is captured, output is the text-only portion.
    assert trace.thinking == ["let me work this out"]
    assert trace.output == "the answer is 4"

    # Handler: thinking before text, then done last (ResultMessage closed).
    kinds = [e[0] for e in handler.events]
    assert kinds == ["thinking", "text", "done"]
    assert handler.events[0][1] == "let me work this out"
    assert handler.events[1][1] == "the answer is 4"
    assert handler.events[-1][1] is trace  # on_done passes the final trace


def test_handler_sees_tool_use_and_result(monkeypatch) -> None:
    from claude_agent_sdk import (
        AssistantMessage, ResultMessage, TextBlock,
        ToolResultBlock, ToolUseBlock, UserMessage,
    )
    from evals.fixture import load_fixture

    fake_msgs = [
        AssistantMessage(
            content=[ToolUseBlock(id="tu1", name="int:quote", input={"t": "AAPL"})],
            model="claude-test", parent_tool_use_id=None, uuid="m1",
        ),
        UserMessage(
            content=[ToolResultBlock(tool_use_id="tu1", content={"p": 100.0}, is_error=False)],
            parent_tool_use_id=None,
        ),
        AssistantMessage(
            content=[TextBlock(text="AAPL is $100")],
            model="claude-test", parent_tool_use_id=None, uuid="m2",
        ),
        ResultMessage(
            subtype="success", duration_ms=1, duration_api_ms=1, is_error=False,
            num_turns=2, session_id="s", stop_reason="end_turn",
            total_cost_usd=0.0, usage=None, result="", structured_output=None,
            model_usage=None, permission_denials=None, deferred_tool_use=None,
            errors=None, api_error_status=None, uuid="r",
        ),
    ]
    runtime = _install_fake_query(monkeypatch, fake_msgs)
    handler = RecordingStreamHandler()
    runtime.run_turn(load_fixture(_HELLO_WORLD), stream_handler=handler)

    kinds = [e[0] for e in handler.events]
    assert kinds == ["tool_use", "tool_result", "text", "done"]
    # tool_use carries the call object with name + args populated.
    _, call = handler.events[0]
    assert call.name == "int:quote"
    assert call.arguments == {"t": "AAPL"}
    # tool_result carries the linked call_id.
    _, res = handler.events[1]
    assert res.call_id == "tu1"


def test_on_done_fires_even_on_error(monkeypatch) -> None:
    """If a turn fails partway, the handler still gets on_done with the
    partial trace — UI can render whatever was collected."""
    from claude_agent_sdk import (
        AssistantMessage, TextBlock,
    )
    from evals.fixture import load_fixture

    class BadMessage:
        # No content attr — forces a Python exception inside the loop.
        pass

    fake_msgs = [
        AssistantMessage(
            content=[TextBlock(text="partial")],
            model="claude-test", parent_tool_use_id=None, uuid="m1",
        ),
        BadMessage(),  # triggers the except branch
    ]
    runtime = _install_fake_query(monkeypatch, fake_msgs)
    handler = RecordingStreamHandler()
    trace = runtime.run_turn(load_fixture(_HELLO_WORLD), stream_handler=handler)

    assert any(e[0] == "text" for e in handler.events)
    # The BadMessage hits the `else` branch (not AssistantMessage etc.)
    # — payload is None so on_partial_event isn't called; on_done fires
    # at the bottom regardless.
    assert handler.events[-1][0] == "done"
    # Output captured up to the failure point.
    assert "partial" in trace.output

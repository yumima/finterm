"""StreamHandler — typed event sink for runtime-emitted streaming events.

Both runtimes (anthropic_runtime today, local-minimal-loop in Track 2)
build a Trace as messages arrive *and* notify a `StreamHandler` on
each typed event.  Two consumers:

  - The eval harness uses NoopStreamHandler (default) — only the
    final Trace matters.
  - Production AgentService passes a handler that translates events
    into Qt signals so the C++ chat surface shows token-by-token
    text, thinking indicators, tool-call cards, and tool results
    as they happen.

The handler is intentionally synchronous + non-async — runtimes call
it from inside the async message loop, so it must not block.  Long
work (UI signals, IPC writes) should be queued / posted, not done in
the call.

Item 13 ships block-level events (text, thinking, tool_use,
tool_result, done).  Finer-grained partial-message deltas (the
SDK's `StreamEvent`) are accepted by `on_partial_event` as opaque
dicts so the handler can opt in via ClaudeAgentOptions.
include_partial_messages later without breaking the interface.
"""
from __future__ import annotations

from typing import Any, Protocol, TYPE_CHECKING, runtime_checkable

if TYPE_CHECKING:
    from evals.trace import ToolCall, ToolResult, Trace


@runtime_checkable
class StreamHandler(Protocol):
    """Receives typed events as a runtime processes a turn."""

    def on_text(self, delta: str) -> None:
        """A TextBlock's text just arrived.  delta is the full block
        (not a per-token chunk) at this granularity."""

    def on_thinking(self, delta: str) -> None:
        """A ThinkingBlock's content just arrived."""

    def on_tool_use(self, call: "ToolCall") -> None:
        """The model emitted a tool-call request."""

    def on_tool_result(self, result: "ToolResult") -> None:
        """A tool result was observed (typically as a UserMessage block)."""

    def on_partial_event(self, event: dict[str, Any]) -> None:
        """Raw SDK StreamEvent payload — opt-in via the SDK's
        include_partial_messages option.  Default-off; handlers
        that don't care can leave the no-op implementation."""

    def on_done(self, trace: "Trace") -> None:
        """The runtime finished a turn; trace is the final record."""


class NoopStreamHandler:
    """Default handler — drops everything.  Used by the eval harness
    where only the final Trace matters."""

    def on_text(self, delta: str) -> None:  # noqa: ARG002
        pass

    def on_thinking(self, delta: str) -> None:  # noqa: ARG002
        pass

    def on_tool_use(self, call) -> None:  # noqa: ARG002
        pass

    def on_tool_result(self, result) -> None:  # noqa: ARG002
        pass

    def on_partial_event(self, event: dict[str, Any]) -> None:  # noqa: ARG002
        pass

    def on_done(self, trace) -> None:  # noqa: ARG002
        pass


class RecordingStreamHandler:
    """Test helper — captures every event in order so tests can
    assert sequence + content."""

    def __init__(self) -> None:
        self.events: list[tuple[str, Any]] = []

    def on_text(self, delta: str) -> None:
        self.events.append(("text", delta))

    def on_thinking(self, delta: str) -> None:
        self.events.append(("thinking", delta))

    def on_tool_use(self, call) -> None:
        self.events.append(("tool_use", call))

    def on_tool_result(self, result) -> None:
        self.events.append(("tool_result", result))

    def on_partial_event(self, event: dict[str, Any]) -> None:
        self.events.append(("partial", event))

    def on_done(self, trace) -> None:
        self.events.append(("done", trace))

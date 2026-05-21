"""Runtime adapter — selects local or Anthropic execution.

Track 0 ships the interface and a NotImplementedError stub.  Track
2 (local runtime) and Track 3 (Anthropic SDK adoption) wire up the
real implementations against this contract.
"""
from __future__ import annotations

from enum import Enum

from .fixture import Fixture
from .trace import Trace


class Runtime(str, Enum):
    LOCAL = "local"
    ANTHROPIC = "anthropic"


class RuntimeNotReady(NotImplementedError):
    """Raised when a runtime is selected but not yet wired in."""


def run_turn(fixture: Fixture, runtime: Runtime) -> Trace:
    """Execute a fixture's input through the chosen runtime.

    Track 2 (local) still raises RuntimeNotReady.  Track 3 (Anthropic)
    is wired — dispatches to the claude-agent-sdk adapter.  If the
    SDK isn't installed or the `claude` CLI is missing, the adapter
    raises AnthropicRuntimeUnavailable which the harness treats as
    a skip (exit code 2), not a failure.
    """
    if runtime is Runtime.LOCAL:
        raise RuntimeNotReady(
            "local runtime not wired yet — see Track 2 in "
            "plans/ai-stack-free-local.md"
        )
    if runtime is Runtime.ANTHROPIC:
        try:
            from finagent_core.runtimes.anthropic_runtime import (
                AnthropicRuntimeUnavailable,
                run_turn as anthropic_run_turn,
            )
        except ImportError as exc:
            raise RuntimeNotReady(
                f"finagent_core.runtimes.anthropic_runtime import failed: {exc}"
            ) from exc

        try:
            return anthropic_run_turn(fixture)
        except AnthropicRuntimeUnavailable as exc:
            raise RuntimeNotReady(str(exc)) from exc
    raise ValueError(f"unknown runtime: {runtime!r}")

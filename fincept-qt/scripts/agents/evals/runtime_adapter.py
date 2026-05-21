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

    Stub until Tracks 2 and 3 land. The return-type shape here is
    the contract those tracks implement against.
    """
    if runtime is Runtime.LOCAL:
        raise RuntimeNotReady(
            "local runtime not wired yet — see Track 2 in "
            "plans/ai-stack-free-local.md"
        )
    if runtime is Runtime.ANTHROPIC:
        raise RuntimeNotReady(
            "Anthropic runtime not wired yet — see Track 3 in "
            "plans/ai-stack-free-local.md"
        )
    raise ValueError(f"unknown runtime: {runtime!r}")

"""Runtime adapter — selects local or Anthropic execution.

Both runtimes implement the same contract: take a Fixture, return
a Trace.  `RuntimeNotReady` is the harness signal that a runtime
exists but isn't available right now (SDK missing, local server
down) — the runner treats it as skip (exit 2), not failure.
"""
from __future__ import annotations

import uuid
from datetime import datetime, timezone
from enum import Enum

from .fixture import Fixture
from .trace import Trace


class Runtime(str, Enum):
    LOCAL = "local"
    ANTHROPIC = "anthropic"


class RuntimeNotReady(NotImplementedError):
    """Raised when a runtime is selected but not yet wired in or not
    available right now (SDK missing / local server unreachable)."""


def run_turn(fixture: Fixture, runtime: Runtime) -> Trace:
    """Execute a fixture's input through the chosen runtime.

    Both runtimes raise their own *Unavailable exception when the
    backend is down; we translate those to RuntimeNotReady so the
    eval harness's skip-vs-fail logic is uniform.
    """
    if runtime is Runtime.LOCAL:
        try:
            from finagent_core.runtimes.local_runtime import (
                LocalRuntimeUnavailable,
                run_text as local_run_text,
            )
        except ImportError as exc:
            raise RuntimeNotReady(
                f"finagent_core.runtimes.local_runtime import failed: {exc}"
            ) from exc

        # Local runtime today is plain chat (no tool-use loop yet);
        # the harness treats it as a degenerate trace with the model's
        # output captured.  Tool-use parity with anthropic_runtime is
        # a Track 9 follow-up.
        trace = Trace(
            turn_id=uuid.uuid4().hex,
            runtime="local",
            model="",
            fixture_name=fixture.name,
            prompts=[fixture.input.message],
            started_at=datetime.now(timezone.utc).isoformat(),
        )
        try:
            text = local_run_text(fixture.input.message)
        except LocalRuntimeUnavailable as exc:
            raise RuntimeNotReady(str(exc)) from exc
        trace.output = text
        trace.iterations = 1
        trace.finished_at = datetime.now(timezone.utc).isoformat()
        return trace

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

"""Trace data structure for agent turn evaluation.

A trace is a structured record of one chat turn: prompts, tool
calls and their results, the final output, per-turn metrics
(latency, tokens, cost), runtime and model snapshot, citations,
and any errors.  Aligned with R21 in
plans/ai-stack-free-local.md — both runtimes write to this schema.

TraceAssertions runs fixture-driven checks; it returns
AssertionResult instead of raising so the harness can collect all
failures per fixture rather than failing on the first.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .fixture import Expected


@dataclass(frozen=True)
class ToolCall:
    id: str
    name: str
    arguments: dict[str, Any]


@dataclass(frozen=True)
class ToolResult:
    call_id: str
    output: Any
    error: str | None = None
    duration_ms: float = 0.0


@dataclass(frozen=True)
class Citation:
    source: str
    span: str
    offset: tuple[int, int] | None = None


@dataclass(frozen=True)
class Budget:
    token_in: int = 0
    token_out: int = 0
    cost_usd: float = 0.0
    cache_hit_tokens: int = 0


@dataclass
class Trace:
    """One agent turn's full record.

    Built up incrementally by the runtime adapter; treated as
    read-only by assertion code.
    """
    turn_id: str
    runtime: str
    model: str
    fixture_name: str
    prompts: list[str] = field(default_factory=list)
    tool_calls: list[ToolCall] = field(default_factory=list)
    tool_results: list[ToolResult] = field(default_factory=list)
    output: str = ""
    citations: list[Citation] = field(default_factory=list)
    budget: Budget = field(default_factory=Budget)
    latency_ms: float = 0.0
    iterations: int = 0
    errors: list[str] = field(default_factory=list)
    started_at: str = ""
    finished_at: str = ""

    def tool_call_names(self) -> set[str]:
        return {tc.name for tc in self.tool_calls}

    def is_complete(self) -> bool:
        """A complete trace has matched tool results and end-of-turn
        bookkeeping populated."""
        if len(self.tool_calls) != len(self.tool_results):
            return False
        return bool(self.finished_at and self.turn_id)


@dataclass(frozen=True)
class AssertionResult:
    passed: bool
    message: str = ""

    def __bool__(self) -> bool:
        return self.passed


class TraceAssertions:
    """Fixture-driven assertions against a Trace."""

    def __init__(self, trace: Trace, expected: Expected) -> None:
        self.trace = trace
        self.expected = expected

    def check_all(self) -> list[AssertionResult]:
        return [
            self.check_no_errors(),
            self.check_complete_record(),
            self.check_tool_calls_include(),
            self.check_tool_calls_exclude(),
            self.check_max_tool_calls(),
            self.check_max_iterations(),
            self.check_output_columns(),
            self.check_output_strings(),
        ]

    def check_no_errors(self) -> AssertionResult:
        if not self.expected.trace.must_not_error:
            return AssertionResult(True)
        if self.trace.errors:
            return AssertionResult(
                False, f"trace has errors: {self.trace.errors}"
            )
        return AssertionResult(True)

    def check_complete_record(self) -> AssertionResult:
        if not self.expected.trace.requires_complete_record:
            return AssertionResult(True)
        if self.trace.is_complete():
            return AssertionResult(True)
        return AssertionResult(
            False,
            f"trace incomplete: "
            f"tool_calls={len(self.trace.tool_calls)}, "
            f"tool_results={len(self.trace.tool_results)}, "
            f"finished_at={self.trace.finished_at!r}, "
            f"turn_id={self.trace.turn_id!r}",
        )

    def check_tool_calls_include(self) -> AssertionResult:
        required = set(self.expected.tool_calls.must_include)
        if not required:
            return AssertionResult(True)
        missing = required - self.trace.tool_call_names()
        if missing:
            return AssertionResult(
                False, f"required tool calls missing: {sorted(missing)}"
            )
        return AssertionResult(True)

    def check_tool_calls_exclude(self) -> AssertionResult:
        forbidden = set(self.expected.tool_calls.must_not_include)
        if not forbidden:
            return AssertionResult(True)
        present = forbidden & self.trace.tool_call_names()
        if present:
            return AssertionResult(
                False, f"forbidden tool calls present: {sorted(present)}"
            )
        return AssertionResult(True)

    def check_max_tool_calls(self) -> AssertionResult:
        limit = self.expected.trace.max_tool_calls
        if limit is None:
            return AssertionResult(True)
        actual = len(self.trace.tool_calls)
        if actual > limit:
            return AssertionResult(
                False, f"tool calls {actual} exceed limit {limit}"
            )
        return AssertionResult(True)

    def check_max_iterations(self) -> AssertionResult:
        limit = self.expected.trace.max_iterations
        if limit is None:
            return AssertionResult(True)
        if self.trace.iterations > limit:
            return AssertionResult(
                False,
                f"iterations {self.trace.iterations} exceed limit {limit}",
            )
        return AssertionResult(True)

    def check_output_columns(self) -> AssertionResult:
        required = self.expected.output.contains_table_with_columns
        if not required:
            return AssertionResult(True)
        missing = [c for c in required if c not in self.trace.output]
        if missing:
            return AssertionResult(
                False, f"output missing table columns: {missing}"
            )
        return AssertionResult(True)

    def check_output_strings(self) -> AssertionResult:
        required = self.expected.output.contains_strings
        if not required:
            return AssertionResult(True)
        missing = [s for s in required if s not in self.trace.output]
        if missing:
            return AssertionResult(
                False, f"output missing required strings: {missing}"
            )
        return AssertionResult(True)


def trace_to_dict(trace: Trace) -> dict[str, Any]:
    """Serialize a Trace to a JSON-compatible dict for snapshots."""
    return {
        "turn_id": trace.turn_id,
        "runtime": trace.runtime,
        "model": trace.model,
        "fixture_name": trace.fixture_name,
        "prompts": list(trace.prompts),
        "tool_calls": [
            {"id": tc.id, "name": tc.name, "arguments": tc.arguments}
            for tc in trace.tool_calls
        ],
        "tool_results": [
            {
                "call_id": tr.call_id,
                "output": tr.output,
                "error": tr.error,
                "duration_ms": tr.duration_ms,
            }
            for tr in trace.tool_results
        ],
        "output": trace.output,
        "citations": [
            {
                "source": c.source,
                "span": c.span,
                "offset": list(c.offset) if c.offset else None,
            }
            for c in trace.citations
        ],
        "budget": {
            "token_in": trace.budget.token_in,
            "token_out": trace.budget.token_out,
            "cost_usd": trace.budget.cost_usd,
            "cache_hit_tokens": trace.budget.cache_hit_tokens,
        },
        "latency_ms": trace.latency_ms,
        "iterations": trace.iterations,
        "errors": list(trace.errors),
        "started_at": trace.started_at,
        "finished_at": trace.finished_at,
    }

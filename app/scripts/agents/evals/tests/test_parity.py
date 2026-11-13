"""Unit tests for the cross-runtime parity comparator."""
from __future__ import annotations

from pathlib import Path

from evals.fixture import (
    AllowedDivergence,
    Expected,
    ExpectedOutput,
    Fixture,
    FixtureInput,
)
from evals.parity import compare_traces
from evals.trace import Citation, ToolCall, ToolResult, Trace


def _fixture(**overrides) -> Fixture:
    defaults: dict = {
        "name": "t",
        "path": Path("nowhere"),
        "input": FixtureInput(agent="a", message="m"),
        "expected": Expected(),
    }
    defaults.update(overrides)
    return Fixture(**defaults)


def _trace(runtime: str, **overrides) -> Trace:
    base = Trace(
        turn_id=f"t-{runtime}",
        runtime=runtime,
        model="m",
        fixture_name="t",
        finished_at="now",
    )
    for k, v in overrides.items():
        setattr(base, k, v)
    return base


def test_parity_passes_when_tool_calls_match() -> None:
    local = _trace("local", tool_calls=[ToolCall("c1", "int:x", {})],
                   tool_results=[ToolResult("c1", None)])
    anthropic = _trace("anthropic",
                       tool_calls=[ToolCall("c2", "int:x", {})],
                       tool_results=[ToolResult("c2", None)])
    report = compare_traces(local, anthropic, _fixture())
    assert report.passed, [f.message for f in report.failures]


def test_parity_fails_on_divergent_tool_calls() -> None:
    local = _trace("local", tool_calls=[ToolCall("c1", "int:a", {})],
                   tool_results=[ToolResult("c1", None)])
    anthropic = _trace("anthropic",
                       tool_calls=[ToolCall("c2", "int:b", {})],
                       tool_results=[ToolResult("c2", None)])
    report = compare_traces(local, anthropic, _fixture())
    assert not report.passed
    assert any("diverge" in f.message for f in report.failures)


def test_parity_fails_on_divergent_output_columns() -> None:
    local = _trace("local", output="| ticker | revenue |")
    anthropic = _trace("anthropic", output="| ticker |")
    expected = Expected(
        output=ExpectedOutput(contains_table_with_columns=("ticker", "revenue"))
    )
    report = compare_traces(local, anthropic, _fixture(expected=expected))
    assert not report.passed
    assert any("'revenue'" in f.message for f in report.failures)


def test_parity_allows_citations_anthropic_only() -> None:
    local = _trace("local", output="x")
    anthropic = _trace("anthropic", output="x",
                       citations=[Citation("src", "span")])
    fixture = _fixture(
        allowed_divergence=AllowedDivergence(citation_presence="anthropic_only")
    )
    report = compare_traces(local, anthropic, fixture)
    assert report.passed, [f.message for f in report.failures]


def test_parity_flags_unexpected_local_citations() -> None:
    local = _trace("local", output="x",
                   citations=[Citation("src", "span")])
    anthropic = _trace("anthropic", output="x")
    fixture = _fixture(
        allowed_divergence=AllowedDivergence(citation_presence="anthropic_only")
    )
    report = compare_traces(local, anthropic, fixture)
    assert not report.passed
    assert any("anthropic_only" in f.message for f in report.failures)

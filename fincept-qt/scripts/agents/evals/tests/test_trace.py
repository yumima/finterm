"""Unit tests for the trace data + assertion library."""
from __future__ import annotations

from evals.fixture import (
    Expected,
    ExpectedOutput,
    ExpectedToolCalls,
    ExpectedTrace,
)
from evals.trace import (
    ToolCall,
    ToolResult,
    Trace,
    TraceAssertions,
    trace_to_dict,
)


def _make_trace(**overrides) -> Trace:
    base = Trace(
        turn_id="t1",
        runtime="local",
        model="qwen2.5:14b",
        fixture_name="x",
        finished_at="2026-05-20T12:00:00Z",
    )
    for k, v in overrides.items():
        setattr(base, k, v)
    return base


def test_complete_trace_passes() -> None:
    trace = _make_trace()
    expected = Expected()
    results = TraceAssertions(trace, expected).check_all()
    assert all(r.passed for r in results), [r.message for r in results if not r]


def test_incomplete_trace_fails() -> None:
    trace = _make_trace(finished_at="")
    expected = Expected(trace=ExpectedTrace(requires_complete_record=True))
    results = TraceAssertions(trace, expected).check_all()
    failures = [r for r in results if not r]
    assert any("incomplete" in r.message for r in failures)


def test_trace_errors_fail() -> None:
    trace = _make_trace(errors=["boom"])
    expected = Expected()
    results = TraceAssertions(trace, expected).check_all()
    failures = [r for r in results if not r]
    assert any("errors" in r.message for r in failures)


def test_must_include_tool_call_passes() -> None:
    trace = _make_trace(
        tool_calls=[ToolCall("c1", "int:markets.quote", {"ticker": "AAPL"})],
        tool_results=[ToolResult("c1", {"price": 100.0})],
    )
    expected = Expected(
        tool_calls=ExpectedToolCalls(must_include=("int:markets.quote",))
    )
    assert all(r.passed for r in TraceAssertions(trace, expected).check_all())


def test_must_include_tool_call_missing() -> None:
    trace = _make_trace()
    expected = Expected(
        tool_calls=ExpectedToolCalls(must_include=("int:markets.quote",))
    )
    failures = [
        r for r in TraceAssertions(trace, expected).check_all() if not r
    ]
    assert any("required tool calls missing" in r.message for r in failures)


def test_forbidden_tool_call_present() -> None:
    trace = _make_trace(
        tool_calls=[ToolCall("c1", "ext:fetch.url", {"url": "x"})],
        tool_results=[ToolResult("c1", "ok")],
    )
    expected = Expected(
        tool_calls=ExpectedToolCalls(must_not_include=("ext:fetch.url",))
    )
    failures = [
        r for r in TraceAssertions(trace, expected).check_all() if not r
    ]
    assert any("forbidden tool calls present" in r.message for r in failures)


def test_max_tool_calls_enforced() -> None:
    trace = _make_trace(
        tool_calls=[ToolCall(f"c{i}", "x", {}) for i in range(5)],
        tool_results=[ToolResult(f"c{i}", None) for i in range(5)],
    )
    expected = Expected(trace=ExpectedTrace(max_tool_calls=3))
    failures = [
        r for r in TraceAssertions(trace, expected).check_all() if not r
    ]
    assert any("exceed limit" in r.message for r in failures)


def test_output_columns_check() -> None:
    trace = _make_trace(output="| ticker | revenue | margin |\n| AAPL | 1 | 2 |")
    expected = Expected(
        output=ExpectedOutput(contains_table_with_columns=("ticker", "revenue"))
    )
    assert all(r.passed for r in TraceAssertions(trace, expected).check_all())

    missing_expected = Expected(
        output=ExpectedOutput(contains_table_with_columns=("ticker", "eps"))
    )
    failures = [
        r for r in TraceAssertions(trace, missing_expected).check_all()
        if not r
    ]
    assert any("missing table columns" in r.message for r in failures)


def test_trace_to_dict_roundtrips_basics() -> None:
    trace = _make_trace(
        tool_calls=[ToolCall("c1", "int:x", {"k": "v"})],
        tool_results=[ToolResult("c1", {"y": 1}, duration_ms=10.0)],
        output="hello",
    )
    d = trace_to_dict(trace)
    assert d["turn_id"] == "t1"
    assert d["tool_calls"][0]["name"] == "int:x"
    assert d["tool_results"][0]["call_id"] == "c1"
    assert d["output"] == "hello"

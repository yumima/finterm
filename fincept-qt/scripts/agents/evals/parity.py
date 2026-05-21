"""Cross-runtime parity check.

For a fixture run on both local and Anthropic profiles, assert
that:
- the same set of tool calls was made (set comparison, not order)
- the output has the same shape (table columns / required strings)

Divergences explicitly allowed by the fixture's allowed_divergence
block are not flagged.  Citation presence on the Anthropic profile
is expected (and allowed-anthropic-only by default).

The parity check is the central invariant — see
plans/testing-strategy.md §3.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .fixture import Fixture
from .trace import AssertionResult, Trace


@dataclass
class ParityReport:
    fixture_name: str
    passed: bool = True
    failures: list[AssertionResult] = field(default_factory=list)

    def fail(self, message: str) -> None:
        self.passed = False
        self.failures.append(AssertionResult(False, message))


def compare_traces(
    local: Trace, anthropic: Trace, fixture: Fixture
) -> ParityReport:
    report = ParityReport(fixture_name=fixture.name)

    local_calls = local.tool_call_names()
    anthropic_calls = anthropic.tool_call_names()
    if local_calls != anthropic_calls:
        only_local = sorted(local_calls - anthropic_calls)
        only_anthropic = sorted(anthropic_calls - local_calls)
        report.fail(
            f"tool call sets diverge — local-only: {only_local}, "
            f"anthropic-only: {only_anthropic}"
        )

    for column in fixture.expected.output.contains_table_with_columns:
        in_local = column in local.output
        in_anthropic = column in anthropic.output
        if in_local != in_anthropic:
            present_in = "local" if in_local else "anthropic"
            report.fail(
                f"output column {column!r} present in {present_in} only"
            )

    for required in fixture.expected.output.contains_strings:
        in_local = required in local.output
        in_anthropic = required in anthropic.output
        if in_local != in_anthropic:
            present_in = "local" if in_local else "anthropic"
            report.fail(
                f"required string {required!r} present in {present_in} only"
            )

    citation_rule = fixture.allowed_divergence.citation_presence
    if citation_rule == "anthropic_only" and local.citations:
        report.fail(
            "local trace has citations, but fixture marks "
            "citations as anthropic_only"
        )
    elif citation_rule == "local_only" and anthropic.citations:
        report.fail(
            "anthropic trace has citations, but fixture marks "
            "citations as local_only"
        )
    elif citation_rule == "none" and (local.citations or anthropic.citations):
        report.fail("fixture forbids citations on either runtime")

    return report

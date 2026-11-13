"""Fixture loader for end-to-end agent evaluation.

A fixture is a YAML file at e2e/<name>/fixture.yaml that describes
the user input, the expected behavior, and which runtime
divergences are allowed.  See plans/testing-strategy.md §4 for the
schema and rationale.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml


@dataclass(frozen=True)
class FixtureInput:
    agent: str
    message: str
    skills_loaded: tuple[str, ...] = ()


@dataclass(frozen=True)
class ExpectedToolCalls:
    must_include: tuple[str, ...] = ()
    must_not_include: tuple[str, ...] = ()


@dataclass(frozen=True)
class ExpectedOutput:
    contains_table_with_columns: tuple[str, ...] = ()
    min_peer_count: int | None = None
    contains_strings: tuple[str, ...] = ()


@dataclass(frozen=True)
class ExpectedTrace:
    requires_complete_record: bool = True
    max_tool_calls: int | None = None
    max_iterations: int | None = None
    must_not_error: bool = True


@dataclass(frozen=True)
class Expected:
    tool_calls: ExpectedToolCalls = field(default_factory=ExpectedToolCalls)
    output: ExpectedOutput = field(default_factory=ExpectedOutput)
    trace: ExpectedTrace = field(default_factory=ExpectedTrace)


@dataclass(frozen=True)
class AllowedDivergence:
    text_wording: bool = True
    citation_presence: str = "any"
    thinking_blocks: str = "any"
    cost_usd: str = "any"
    latency_ms: str = "any"


@dataclass(frozen=True)
class Fixture:
    name: str
    path: Path
    input: FixtureInput
    expected: Expected
    allowed_divergence: AllowedDivergence = field(
        default_factory=AllowedDivergence
    )
    description: str = ""


class FixtureError(ValueError):
    """Raised when a fixture file is malformed."""


_DIVERGENCE_PRESENCE = frozenset({"any", "anthropic_only", "local_only", "none"})
_DIVERGENCE_TOLERANCE = frozenset({"any"})  # placeholder for future tolerances


def load_fixture(path: Path | str) -> Fixture:
    """Load and validate a fixture.

    `path` may be either the fixture directory (containing
    fixture.yaml) or the YAML file itself.  Accepts str or Path.
    """
    path = Path(path)
    yaml_path = path / "fixture.yaml" if path.is_dir() else path
    if not yaml_path.exists():
        raise FixtureError(f"fixture not found: {yaml_path}")

    with yaml_path.open() as fh:
        try:
            raw = yaml.safe_load(fh)
        except yaml.YAMLError as exc:
            raise FixtureError(f"invalid YAML in {yaml_path}: {exc}") from exc

    if not isinstance(raw, dict):
        raise FixtureError(
            f"fixture root must be a mapping in {yaml_path}, got {type(raw).__name__}"
        )

    return _build_fixture(raw, yaml_path)


def _build_fixture(raw: dict[str, Any], path: Path) -> Fixture:
    try:
        name = raw["name"]
        input_raw = raw["input"]
    except KeyError as exc:
        raise FixtureError(f"missing required key {exc} in {path}") from exc

    try:
        fixture_input = FixtureInput(
            agent=input_raw["agent"],
            message=input_raw["message"],
            skills_loaded=tuple(input_raw.get("skills_loaded") or ()),
        )
    except KeyError as exc:
        raise FixtureError(
            f"missing required input key {exc} in {path}"
        ) from exc

    expected_raw = raw.get("expected") or {}
    expected = Expected(
        tool_calls=_build_tool_calls(expected_raw.get("tool_calls") or {}),
        output=_build_output(expected_raw.get("output") or {}),
        trace=_build_trace(expected_raw.get("trace") or {}),
    )

    divergence = _build_divergence(raw.get("allowed_divergence") or {}, path)

    return Fixture(
        name=name,
        path=path,
        input=fixture_input,
        expected=expected,
        allowed_divergence=divergence,
        description=raw.get("description", "") or "",
    )


def _build_tool_calls(raw: dict[str, Any]) -> ExpectedToolCalls:
    return ExpectedToolCalls(
        must_include=tuple(raw.get("must_include") or ()),
        must_not_include=tuple(raw.get("must_not_include") or ()),
    )


def _build_output(raw: dict[str, Any]) -> ExpectedOutput:
    return ExpectedOutput(
        contains_table_with_columns=tuple(
            raw.get("contains_table_with_columns") or ()
        ),
        min_peer_count=raw.get("min_peer_count"),
        contains_strings=tuple(raw.get("contains_strings") or ()),
    )


def _build_trace(raw: dict[str, Any]) -> ExpectedTrace:
    return ExpectedTrace(
        requires_complete_record=bool(raw.get("requires_complete_record", True)),
        max_tool_calls=raw.get("max_tool_calls"),
        max_iterations=raw.get("max_iterations"),
        must_not_error=bool(raw.get("must_not_error", True)),
    )


def _build_divergence(raw: dict[str, Any], path: Path) -> AllowedDivergence:
    citation = raw.get("citation_presence", "any")
    thinking = raw.get("thinking_blocks", "any")
    cost = raw.get("cost_usd", "any")
    latency = raw.get("latency_ms", "any")

    for field_name, value in (("citation_presence", citation), ("thinking_blocks", thinking)):
        if value not in _DIVERGENCE_PRESENCE:
            raise FixtureError(
                f"allowed_divergence.{field_name}={value!r} in {path} "
                f"must be one of {sorted(_DIVERGENCE_PRESENCE)}"
            )
    for field_name, value in (("cost_usd", cost), ("latency_ms", latency)):
        if value not in _DIVERGENCE_TOLERANCE:
            raise FixtureError(
                f"allowed_divergence.{field_name}={value!r} in {path} "
                f"must be one of {sorted(_DIVERGENCE_TOLERANCE)}"
            )

    return AllowedDivergence(
        text_wording=bool(raw.get("text_wording", True)),
        citation_presence=citation,
        thinking_blocks=thinking,
        cost_usd=cost,
        latency_ms=latency,
    )

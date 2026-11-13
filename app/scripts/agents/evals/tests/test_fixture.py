"""Unit tests for the fixture loader."""
from __future__ import annotations

import textwrap
from pathlib import Path

import pytest

from evals.fixture import FixtureError, load_fixture


def _write(path: Path, content: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content).lstrip())
    return path


def test_load_minimal_fixture(tmp_path: Path) -> None:
    yaml_path = _write(
        tmp_path / "hello" / "fixture.yaml",
        """
        name: hello
        input:
          agent: default
          message: "hi"
        """,
    )
    f = load_fixture(yaml_path)
    assert f.name == "hello"
    assert f.input.agent == "default"
    assert f.input.message == "hi"
    assert f.input.skills_loaded == ()


def test_load_from_directory(tmp_path: Path) -> None:
    _write(
        tmp_path / "hello" / "fixture.yaml",
        """
        name: hello
        input:
          agent: default
          message: "hi"
        """,
    )
    f = load_fixture(tmp_path / "hello")
    assert f.name == "hello"


def test_load_full_fixture(tmp_path: Path) -> None:
    yaml_path = _write(
        tmp_path / "comps" / "fixture.yaml",
        """
        name: comps_aapl
        description: Comparable-company analysis
        input:
          agent: equity_research
          message: "/comps AAPL"
          skills_loaded: [comps-analysis]
        expected:
          tool_calls:
            must_include: [fd:get_income_statements]
            must_not_include: [ext:fetch.url]
          output:
            contains_table_with_columns: [ticker, revenue]
            min_peer_count: 3
          trace:
            max_tool_calls: 20
            must_not_error: true
        allowed_divergence:
          citation_presence: anthropic_only
        """,
    )
    f = load_fixture(yaml_path)
    assert f.name == "comps_aapl"
    assert f.input.skills_loaded == ("comps-analysis",)
    assert "fd:get_income_statements" in f.expected.tool_calls.must_include
    assert "ext:fetch.url" in f.expected.tool_calls.must_not_include
    assert f.expected.output.min_peer_count == 3
    assert f.expected.trace.max_tool_calls == 20
    assert f.allowed_divergence.citation_presence == "anthropic_only"


def test_missing_file(tmp_path: Path) -> None:
    with pytest.raises(FixtureError, match="not found"):
        load_fixture(tmp_path / "nope" / "fixture.yaml")


def test_invalid_yaml(tmp_path: Path) -> None:
    yaml_path = _write(tmp_path / "bad" / "fixture.yaml", "name: [unclosed\n")
    with pytest.raises(FixtureError, match="invalid YAML"):
        load_fixture(yaml_path)


def test_missing_required_key(tmp_path: Path) -> None:
    yaml_path = _write(
        tmp_path / "bad" / "fixture.yaml",
        """
        input:
          agent: x
          message: y
        """,
    )
    with pytest.raises(FixtureError, match="missing required key"):
        load_fixture(yaml_path)


def test_invalid_divergence_value(tmp_path: Path) -> None:
    yaml_path = _write(
        tmp_path / "bad" / "fixture.yaml",
        """
        name: bad
        input:
          agent: x
          message: y
        allowed_divergence:
          citation_presence: sometimes
        """,
    )
    with pytest.raises(FixtureError, match="citation_presence"):
        load_fixture(yaml_path)


def test_invalid_tolerance_value(tmp_path: Path) -> None:
    yaml_path = _write(
        tmp_path / "bad" / "fixture.yaml",
        """
        name: bad
        input:
          agent: x
          message: y
        allowed_divergence:
          cost_usd: 5.0
        """,
    )
    with pytest.raises(FixtureError, match="cost_usd"):
        load_fixture(yaml_path)


def test_root_must_be_mapping(tmp_path: Path) -> None:
    yaml_path = _write(tmp_path / "bad" / "fixture.yaml", "- just a list\n")
    with pytest.raises(FixtureError, match="must be a mapping"):
        load_fixture(yaml_path)

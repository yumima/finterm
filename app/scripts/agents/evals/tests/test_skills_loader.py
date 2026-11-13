"""Tests for the SKILL.md discovery + resolution helpers (Track 3 #12)."""
from __future__ import annotations

import importlib
import textwrap
from pathlib import Path

import pytest

from finagent_core.runtimes.skills_loader import (
    list_available_skills,
    resolve_skills,
)


def _write_skill(root: Path, vertical: str, name: str, body: str = "# stub") -> Path:
    d = root / vertical / name
    d.mkdir(parents=True)
    skill = d / "SKILL.md"
    skill.write_text(textwrap.dedent(body))
    return skill


def test_list_returns_empty_for_missing_root(tmp_path: Path) -> None:
    assert list_available_skills(tmp_path / "does-not-exist") == []


def test_list_returns_empty_for_empty_root(tmp_path: Path) -> None:
    assert list_available_skills(tmp_path) == []


def test_list_discovers_skills_by_directory_name(tmp_path: Path) -> None:
    _write_skill(tmp_path, "equity-research", "comps-analysis")
    _write_skill(tmp_path, "equity-research", "dcf-model")
    _write_skill(tmp_path, "wealth-management", "tax-loss-harvesting")
    names = list_available_skills(tmp_path)
    assert names == ["comps-analysis", "dcf-model", "tax-loss-harvesting"]


def test_list_ignores_directories_without_skill_md(tmp_path: Path) -> None:
    (tmp_path / "not-a-skill").mkdir()
    (tmp_path / "not-a-skill" / "README.md").write_text("nope")
    _write_skill(tmp_path, "v", "real-skill")
    assert list_available_skills(tmp_path) == ["real-skill"]


def test_list_deduplicates_skill_names_across_verticals(tmp_path: Path) -> None:
    # Two verticals shouldn't ever ship the same skill name, but
    # if they do, we surface the name once.
    _write_skill(tmp_path, "a", "shared")
    _write_skill(tmp_path, "b", "shared")
    assert list_available_skills(tmp_path) == ["shared"]


def test_resolve_filters_unknown_names() -> None:
    available = ["comps-analysis", "dcf-model", "morning-note"]
    assert resolve_skills(["comps-analysis", "bogus"], available) == ["comps-analysis"]
    assert resolve_skills(["bogus"], available) == []
    assert resolve_skills([], available) == []


def test_resolve_preserves_available_order() -> None:
    available = ["a", "b", "c", "d"]
    # Requested order doesn't matter — output follows `available`.
    assert resolve_skills(["d", "a"], available) == ["a", "d"]


def test_run_turn_passes_resolved_skills_to_options(monkeypatch, tmp_path: Path) -> None:
    """Fixture's skills_loaded → options.skills, after filtering."""
    pytest.importorskip("claude_agent_sdk")
    import claude_agent_sdk
    from claude_agent_sdk import ResultMessage

    from evals.fixture import Fixture, FixtureInput, Expected
    from finagent_core.runtimes import anthropic_runtime, skills_loader

    # Set up a fake skills root with one real skill.
    _write_skill(tmp_path, "v", "comps-analysis")
    monkeypatch.setattr(skills_loader, "SKILLS_ROOT", tmp_path)

    captured: dict = {}

    async def fake_query(*, prompt, options=None, transport=None):
        captured["options"] = options
        yield ResultMessage(
            subtype="success", duration_ms=1, duration_api_ms=1, is_error=False,
            num_turns=0, session_id="s", stop_reason="end_turn",
            total_cost_usd=0.0, usage=None, result="", structured_output=None,
            model_usage=None, permission_denials=None, deferred_tool_use=None,
            errors=None, api_error_status=None, uuid="r",
        )

    monkeypatch.setattr(claude_agent_sdk, "query", fake_query)
    importlib.reload(anthropic_runtime)

    fixture = Fixture(
        name="t",
        path=tmp_path,
        input=FixtureInput(
            agent="default",
            message="hi",
            skills_loaded=("comps-analysis", "unknown-skill"),
        ),
        expected=Expected(),
    )

    anthropic_runtime.run_turn(fixture)

    # Unknown skill dropped; known skill present.
    assert captured["options"].skills == ["comps-analysis"]


def test_run_turn_omits_skills_option_when_none_resolve(monkeypatch, tmp_path: Path) -> None:
    pytest.importorskip("claude_agent_sdk")
    import claude_agent_sdk
    from claude_agent_sdk import ResultMessage

    from evals.fixture import Fixture, FixtureInput, Expected
    from finagent_core.runtimes import anthropic_runtime, skills_loader

    monkeypatch.setattr(skills_loader, "SKILLS_ROOT", tmp_path)  # empty

    captured: dict = {}

    async def fake_query(*, prompt, options=None, transport=None):
        captured["options"] = options
        yield ResultMessage(
            subtype="success", duration_ms=1, duration_api_ms=1, is_error=False,
            num_turns=0, session_id="s", stop_reason="end_turn",
            total_cost_usd=0.0, usage=None, result="", structured_output=None,
            model_usage=None, permission_denials=None, deferred_tool_use=None,
            errors=None, api_error_status=None, uuid="r",
        )

    monkeypatch.setattr(claude_agent_sdk, "query", fake_query)
    importlib.reload(anthropic_runtime)

    fixture = Fixture(
        name="t",
        path=tmp_path,
        input=FixtureInput(
            agent="default",
            message="hi",
            skills_loaded=("nothing-vendored-yet",),
        ),
        expected=Expected(),
    )

    anthropic_runtime.run_turn(fixture)
    # Nothing resolves → SDK option stays at its default None.
    assert captured["options"].skills is None

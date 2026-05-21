"""CLI smoke tests for the eval runner."""
from __future__ import annotations

from pathlib import Path

import pytest

from evals.runner import main

_HELLO_WORLD = Path(__file__).resolve().parents[1] / "e2e" / "hello_world"


def test_run_hello_world_skips_when_runtime_not_ready(capsys) -> None:
    # Runtimes are stubs until Tracks 2/3 land; CLI should report
    # skip with exit code 2, not crash.
    exit_code = main(["run", str(_HELLO_WORLD), "--runtime=local"])
    assert exit_code == 2
    captured = capsys.readouterr()
    assert "[skip] hello_world" in captured.err


def test_snapshot_requires_update_flag(capsys) -> None:
    exit_code = main([
        "snapshot", str(_HELLO_WORLD), "--runtime=local",
    ])
    assert exit_code == 1
    captured = capsys.readouterr()
    assert "--update is required" in captured.err


def test_run_all_under_e2e_skips_cleanly(capsys, monkeypatch, tmp_path: Path) -> None:
    # Copy the hello_world fixture to a tmp root so run-all has
    # exactly one fixture to discover.
    fixture_dir = tmp_path / "hello_world"
    fixture_dir.mkdir()
    (fixture_dir / "fixture.yaml").write_text(
        (_HELLO_WORLD / "fixture.yaml").read_text()
    )

    # Force the Anthropic adapter to skip — otherwise it would try to
    # spawn the `claude` CLI and could hit a paid endpoint. We sabotage
    # the import inside the lazy load path.
    import sys
    real_sdk = sys.modules.pop("claude_agent_sdk", None)
    monkeypatch.setitem(sys.modules, "claude_agent_sdk", object())
    try:
        exit_code = main(["run-all", str(tmp_path), "--parity"])
    finally:
        if real_sdk is not None:
            sys.modules["claude_agent_sdk"] = real_sdk
        else:
            sys.modules.pop("claude_agent_sdk", None)

    # Skips are not failures; exit code stays 0.
    assert exit_code == 0
    captured = capsys.readouterr()
    assert "[skip] hello_world" in captured.err


def test_unknown_command_errors() -> None:
    with pytest.raises(SystemExit):
        main(["wat"])

"""CLI for running evaluation fixtures.

Usage (from scripts/agents/):
    python -m evals.runner run e2e/hello_world --runtime=local
    python -m evals.runner run-all e2e --parity
    python -m evals.runner snapshot e2e/hello_world --runtime=local --update

Exit codes:
    0  all assertions passed
    1  one or more failures
    2  runtime not yet wired (NotReady) — treated as skip, not failure
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .fixture import Fixture, load_fixture
from .parity import ParityReport, compare_traces
from .result_store import record_run
from .runtime_adapter import Runtime, RuntimeNotReady, run_turn
from .trace import Trace, TraceAssertions, trace_to_dict


_EXIT_OK = 0
_EXIT_FAIL = 1
_EXIT_SKIP = 2


def cmd_run(args: argparse.Namespace) -> int:
    fixture = load_fixture(Path(args.fixture))
    runtime = Runtime(args.runtime)
    try:
        trace = run_turn(fixture, runtime)
    except RuntimeNotReady as exc:
        print(f"[skip] {fixture.name} ({runtime.value}): {exc}", file=sys.stderr)
        record_run(
            trace=None, runtime=runtime.value,
            fixture_path=str(args.fixture), status="skip",
            failed_assertions=[str(exc)],
        )
        return _EXIT_SKIP

    failed = [r for r in TraceAssertions(trace, fixture.expected).check_all() if not r]
    if failed:
        print(f"[fail] {fixture.name} ({runtime.value}):")
        for r in failed:
            print(f"  - {r.message}")
        record_run(
            trace=trace, runtime=runtime.value,
            fixture_path=str(args.fixture), status="fail",
            failed_assertions=[r.message for r in failed],
        )
        return _EXIT_FAIL

    if args.write_snapshot:
        _write_snapshot(fixture, trace, runtime)
    print(f"[ok] {fixture.name} ({runtime.value})")
    record_run(
        trace=trace, runtime=runtime.value,
        fixture_path=str(args.fixture), status="ok",
    )
    return _EXIT_OK


def cmd_run_all(args: argparse.Namespace) -> int:
    root = Path(args.fixtures_root)
    fixtures = _discover_fixtures(root)
    if not fixtures:
        print(f"no fixtures found under {root}", file=sys.stderr)
        return _EXIT_FAIL

    exit_code = _EXIT_OK
    for fixture in fixtures:
        per_runtime: dict[Runtime, Trace | None] = {Runtime.LOCAL: None, Runtime.ANTHROPIC: None}
        skipped = False
        for runtime in (Runtime.LOCAL, Runtime.ANTHROPIC):
            try:
                per_runtime[runtime] = run_turn(fixture, runtime)
            except RuntimeNotReady as exc:
                print(
                    f"[skip] {fixture.name} ({runtime.value}): {exc}",
                    file=sys.stderr,
                )
                skipped = True
                record_run(
                    trace=None, runtime=runtime.value,
                    fixture_path=fixture.name, status="skip",
                    failed_assertions=[str(exc)],
                )

        for runtime, trace in per_runtime.items():
            if trace is None:
                continue
            failed = [
                r for r in TraceAssertions(trace, fixture.expected).check_all()
                if not r
            ]
            if failed:
                print(f"[fail] {fixture.name} ({runtime.value}):")
                for r in failed:
                    print(f"  - {r.message}")
                exit_code = _EXIT_FAIL
                record_run(
                    trace=trace, runtime=runtime.value,
                    fixture_path=fixture.name, status="fail",
                    failed_assertions=[r.message for r in failed],
                )
            else:
                record_run(
                    trace=trace, runtime=runtime.value,
                    fixture_path=fixture.name, status="ok",
                )

        if args.parity and not skipped:
            local_trace = per_runtime[Runtime.LOCAL]
            anthropic_trace = per_runtime[Runtime.ANTHROPIC]
            assert local_trace is not None and anthropic_trace is not None
            report = compare_traces(local_trace, anthropic_trace, fixture)
            _print_parity(report)
            if not report.passed:
                exit_code = _EXIT_FAIL

    return exit_code


def cmd_snapshot(args: argparse.Namespace) -> int:
    if not args.update:
        print(
            "--update is required to rewrite snapshots (guard against accidents)",
            file=sys.stderr,
        )
        return _EXIT_FAIL
    fixture = load_fixture(Path(args.fixture))
    runtime = Runtime(args.runtime)
    try:
        trace = run_turn(fixture, runtime)
    except RuntimeNotReady as exc:
        print(f"[skip] {fixture.name} ({runtime.value}): {exc}", file=sys.stderr)
        return _EXIT_SKIP

    path = _write_snapshot(fixture, trace, runtime)
    print(f"wrote {path}")
    return _EXIT_OK


def _write_snapshot(fixture: Fixture, trace: Trace, runtime: Runtime) -> Path:
    snapshot_dir = fixture.path.parent
    path = snapshot_dir / f"snapshot.{runtime.value}.json"
    path.write_text(
        json.dumps(trace_to_dict(trace), indent=2, sort_keys=True) + "\n"
    )
    return path


def _discover_fixtures(root: Path) -> list[Fixture]:
    return [load_fixture(p) for p in sorted(root.rglob("fixture.yaml"))]


def _print_parity(report: ParityReport) -> None:
    if report.passed:
        print(f"[parity-ok] {report.fixture_name}")
        return
    print(f"[parity-fail] {report.fixture_name}:")
    for failure in report.failures:
        print(f"  - {failure.message}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="evals", description="Run agent evaluation fixtures."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    runtimes = [r.value for r in Runtime]

    p_run = sub.add_parser("run", help="run a single fixture")
    p_run.add_argument("fixture", help="fixture directory or fixture.yaml path")
    p_run.add_argument("--runtime", choices=runtimes, required=True)
    p_run.add_argument(
        "--write-snapshot",
        action="store_true",
        help="write snapshot.<runtime>.json next to the fixture on success",
    )
    p_run.set_defaults(func=cmd_run)

    p_all = sub.add_parser("run-all", help="run every fixture under a root")
    p_all.add_argument("fixtures_root")
    p_all.add_argument(
        "--parity",
        action="store_true",
        help="cross-runtime parity check after both runtimes complete",
    )
    p_all.set_defaults(func=cmd_run_all)

    p_snap = sub.add_parser("snapshot", help="re-record a snapshot deliberately")
    p_snap.add_argument("fixture")
    p_snap.add_argument("--runtime", choices=runtimes, required=True)
    p_snap.add_argument("--update", action="store_true", required=False)
    p_snap.set_defaults(func=cmd_snapshot)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

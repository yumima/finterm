"""Append-only result store for eval runs (Track 14 #37).

One JSON line per (fixture × runtime) execution.  The runner calls
`record_run` after every trace; the file is grep-able / jq-able /
loadable into the Workbench System tab later without any schema
migration.

Default location is `~/.fincept/eval_runs.jsonl`; override with
the FINCEPT_EVAL_RESULTS env var (e.g. for CI to write under the
workspace).  Persistence failures are logged, never raised — a
broken results file mustn't sink a CI run.

Why jsonl instead of SQLite: the trace shape evolves quickly
during the eval-bench's first weeks.  JSON Lines absorbs new
fields without a migration; SQLite would force one every time we
add a new trace column.  Move to SQLite when the schema stabilises
and the Workbench needs indexed queries.
"""
from __future__ import annotations

import json
import logging
import os
import sys
from dataclasses import asdict, is_dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)


def results_path() -> Path:
    override = os.environ.get("FINCEPT_EVAL_RESULTS")
    if override:
        return Path(override)
    return Path.home() / ".fincept" / "eval_runs.jsonl"


def _trace_summary(trace: Any) -> dict:
    """Project a Trace into a compact serialisable summary.

    Treats `trace` defensively — accepts anything dataclass-shaped
    plus a few specific fields we care about.  Future trace fields
    flow through `as_dict_fallback` so we don't lose data when the
    Trace class grows.
    """
    if is_dataclass(trace):
        full = asdict(trace)
    else:
        full = {k: getattr(trace, k, None) for k in (
            "turn_id", "runtime", "model", "fixture_name", "output",
            "errors", "latency_ms", "iterations", "started_at", "finished_at",
        )}
    return {
        "turn_id": full.get("turn_id", ""),
        "runtime": full.get("runtime", ""),
        "model": full.get("model", ""),
        "fixture_name": full.get("fixture_name", ""),
        "iterations": full.get("iterations", 0),
        "latency_ms": full.get("latency_ms", 0.0),
        "started_at": full.get("started_at", ""),
        "finished_at": full.get("finished_at", ""),
        "errors": list(full.get("errors", []) or []),
        # Output truncated — full text often runs into kilobytes;
        # the Workbench shows the head, full goes to the trace store.
        "output_preview": (full.get("output", "") or "")[:500],
        "tool_call_count": len(full.get("tool_calls", []) or []),
    }


def record_run(
    trace: Any,
    *,
    runtime: str,
    fixture_path: str,
    status: str,
    failed_assertions: list[str] | None = None,
) -> None:
    """Append one record describing a single fixture × runtime run.

    `status` is one of {"ok", "fail", "skip"} matching the runner's
    exit-code semantics.  `failed_assertions` lists assertion
    messages that didn't pass — empty / None when status == "ok".
    """
    record = {
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "runtime": runtime,
        "fixture_path": fixture_path,
        "status": status,
        "failed_assertions": list(failed_assertions or []),
        "trace": _trace_summary(trace) if trace is not None else None,
    }
    path = results_path()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(record, default=str) + "\n")
    except OSError as exc:
        # Defensive: a broken results file should never fail the run.
        logger.warning("eval result_store: write to %s failed: %s", path, exc)
        # Echo to stderr too so CI logs surface the issue even when
        # the logging level is high.
        print(f"[result_store] write failed: {exc}", file=sys.stderr)

"""
cli.py — the app's questions about RD-Agent jobs (AIQuantLabService).

    cli.py check_status
    cli.py list_tasks             '{"status"}'
    cli.py get_task_status        '{"task_id"}'
    cli.py get_discovered_factors '{"task_id"}'
    cli.py get_optimized_model    '{"task_id"}'

Starting, resuming and stopping a job is worker.py's. One JSON object on
stdout; logs go to stderr.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import jobs  # noqa: E402
import worker  # noqa: E402


def _job(params: dict[str, Any]) -> dict[str, Any]:
    job = jobs.load(str(params.get("task_id") or ""))
    if not job:
        raise ValueError(f"No such task: {params.get('task_id')}")
    return job


def _items(job: dict[str, Any], kind: str) -> list[dict[str, Any]]:
    out = []
    for r in jobs.read_json(jobs.folder(job["task_id"]) / "results.json") or []:
        for item in r["items"]:
            if item["kind"] == kind:
                out.append({**item, **r["metrics"], "iteration": r["iteration"], "accepted": r["accepted"]})
    return out


def cmd_check_status(_params: dict[str, Any]) -> dict[str, Any]:
    try:
        from importlib.metadata import version
        rdagent = version("rdagent")
    except Exception:  # noqa: BLE001
        rdagent = None
    # The profile is checked when a job starts; this is about the computer.
    lacks = [m for m in worker.missing({"llm_provider": "hearth", "llm_model": "-", "llm_base_url": "-"})]
    return {"success": True, "rdagent_version": rdagent, "ready": not lacks, "missing": lacks,
            "running": [j["task_id"] for j in jobs.list_jobs() if j["status"] == "running"],
            "workspace_base": str(jobs.workspace_base())}


def cmd_list_tasks(params: dict[str, Any]) -> dict[str, Any]:
    tasks = [jobs.summary(j) for j in jobs.list_jobs() if not params.get("status") or j["status"] == params["status"]]
    return {"success": True, "tasks": tasks, "total": len(tasks)}


def cmd_get_task_status(params: dict[str, Any]) -> dict[str, Any]:
    return {"success": True, **jobs.summary(_job(params))}


def cmd_get_discovered_factors(params: dict[str, Any]) -> dict[str, Any]:
    job = _job(params)
    return {"success": True, "task_id": job["task_id"], "status": job["status"], "factors": _items(job, "factor"),
            "best_ic": job.get("best_ic")}


def cmd_get_optimized_model(params: dict[str, Any]) -> dict[str, Any]:
    job = _job(params)
    return {"success": True, "task_id": job["task_id"], "status": job["status"], "models": _items(job, "model"),
            "best_ic": job.get("best_ic")}


_COMMANDS = {
    "check_status": cmd_check_status,
    "list_tasks": cmd_list_tasks,
    "get_task_status": cmd_get_task_status,
    "get_discovered_factors": cmd_get_discovered_factors,
    "get_optimized_model": cmd_get_optimized_model,
}


def main() -> None:
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    try:
        if command not in _COMMANDS:
            raise ValueError(f"Unknown command {command!r}. Available: {', '.join(_COMMANDS)}")
        out = _COMMANDS[command](json.loads(sys.argv[2]) if len(sys.argv) > 2 else {})
    except Exception as e:  # noqa: BLE001 — the reader gets the reason
        print(json.dumps({"success": False, "error": str(e)}))
        sys.exit(1)
    print(json.dumps(out, default=str), flush=True)


if __name__ == "__main__":
    main()

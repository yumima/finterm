"""
jobs.py — RD-Agent jobs on disk: one folder each, in the user's data.

    <workspaces>/<task_id>/job.json     what was asked and how it stands
                           results.json what it has found so far
                           worker.log   what rdagent printed
                           log/         rdagent's own trace (its log viewer, resuming)

A job runs in a process of its own (worker.py) that the app starts and ends;
every other command reads these files. No key is ever written here.
"""

from __future__ import annotations

import json
import os
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

sys.path.append(str(Path(__file__).resolve().parents[2]))  # scripts/
from finterm_paths import feature_dir  # noqa: E402

TYPES = ("factor_mining", "model_optimization", "quant_research")
FINAL = ("completed", "failed", "stopped")


def workspace_base() -> Path:
    return feature_dir("rdagents", "workspaces")


def folder(task_id: str) -> Path:
    if not re.fullmatch(r"[A-Za-z0-9_]{1,80}", task_id or ""):
        raise ValueError(f"not a task ID: {task_id!r}")
    return workspace_base() / task_id


def new_task_id(kind: str) -> str:
    base = f"{kind.split('_')[0]}_{datetime.now():%Y%m%d_%H%M%S}"
    task_id, n = base, 1
    while (workspace_base() / task_id).exists():
        n += 1
        task_id = f"{base}_{n}"
    return task_id


def write_json(path: Path, obj: Any) -> None:
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=False, indent=1), encoding="utf-8")
    os.replace(tmp, path)


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


class Lock:
    """Held by a job's worker for as long as it lives, and let go by the system
    however it ends: that is how a job whose worker is gone is told apart."""

    def __init__(self, task_id: str):
        self.path = folder(task_id) / "worker.lock"
        self.fd: int | None = None

    def acquire(self) -> bool:
        self.fd = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o600)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.fd, msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            os.close(self.fd)
            self.fd = None
            return False

    def release(self) -> None:
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None


def is_running(task_id: str) -> bool:
    if not (folder(task_id) / "worker.lock").exists():
        return False
    lock = Lock(task_id)
    if lock.acquire():
        lock.release()
        return False
    return True


def load(task_id: str) -> dict[str, Any] | None:
    """The job as it stands. One that says it runs while its worker is gone
    was interrupted: the app closed, or the machine did."""
    job = read_json(folder(task_id) / "job.json")
    if not isinstance(job, dict):
        return None
    if job.get("status") not in FINAL and not is_running(task_id):
        job["status"] = "interrupted"
    return job


def list_jobs() -> list[dict[str, Any]]:
    out = []
    for d in workspace_base().iterdir() if workspace_base().is_dir() else []:
        if d.is_dir() and re.fullmatch(r"[A-Za-z0-9_]{1,80}", d.name):
            job = load(d.name)
            if job:
                out.append(job)
    return sorted(out, key=lambda j: j.get("created_at") or "", reverse=True)


def update(task_id: str, **fields: Any) -> dict[str, Any]:
    path = folder(task_id) / "job.json"
    job = read_json(path) or {}
    job.update(fields)
    write_json(path, job)
    return job


def elapsed(job: dict[str, Any]) -> str:
    try:
        start = datetime.fromisoformat(job["started_at"])
        end = datetime.fromisoformat(job["ended_at"]) if job.get("ended_at") else datetime.now()
    except (KeyError, TypeError, ValueError):
        return "-"
    if job.get("status") == "interrupted":
        return "-"
    minutes = int((end - start).total_seconds() // 60)
    return f"{minutes // 60}h {minutes % 60:02d}m" if minutes >= 60 else f"{minutes}m"


def summary(job: dict[str, Any]) -> dict[str, Any]:
    n, done = job.get("max_iterations") or 0, job.get("iterations_completed") or 0
    return {
        "task_id": job.get("task_id"), "type": job.get("type"), "status": job.get("status"),
        "progress": min(1.0, done / n) if n else None,
        "iterations_completed": done, "max_iterations": n, "current_step": job.get("current_step") or "-",
        "best_ic": job.get("best_ic"), "elapsed_time": elapsed(job), "error": job.get("error"),
    }

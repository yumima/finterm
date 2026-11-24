"""
worker.py — one RD-Agent job, from start to end, in a process of its own.

    worker.py start  '{"type", …what is asked…, "config"}'
    worker.py resume '{"task_id", "config"}'

The first line of stdout is the answer to "did it start": {"success", "task_id",
…} or {"success": false, "error"}. After it, everything rdagent prints goes to
the job's worker.log and how it stands to job.json (jobs.py). The app ends the
process to stop the job.

rdagent's Qlib loops work on one market, China's CSI 300, with data it
downloads; they backtest in a Docker container and run the factor code the
model writes on this computer. Nothing of finterm's credentials is in this
process (see llm.py and PythonRunner::start_background).
"""

from __future__ import annotations

import json
import os
import signal
import sys
import threading
from datetime import datetime
from pathlib import Path
from typing import Any

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

import jobs  # noqa: E402
import llm  # noqa: E402

_LOOPS = {
    "factor_mining": ("rdagent.app.qlib_rd_loop.factor", "FactorRDLoop", "FACTOR_PROP_SETTING", "QLIB_FACTOR_scen", "FactorScenario"),
    "model_optimization": ("rdagent.app.qlib_rd_loop.model", "ModelRDLoop", "MODEL_PROP_SETTING", "QLIB_MODEL_scen", "ModelScenario"),
    "quant_research": ("rdagent.app.qlib_rd_loop.quant", "QuantRDLoop", "QUANT_PROP_SETTING", "QLIB_QUANT_scen", "QuantScenario"),
}


def answer(obj: dict[str, Any]) -> None:
    print(json.dumps(obj), flush=True)


def missing(config: dict[str, Any]) -> list[str]:
    """What a job needs and this computer or the profile lacks."""
    out = []
    try:
        llm.resolve(config)
    except ValueError as e:
        out.append(str(e))
    if sys.platform.startswith("win"):
        out.append("RD-Agent runs on Linux and macOS only")
        return out
    import importlib.util
    if importlib.util.find_spec("rdagent") is None:
        out.append("The rdagent package is not installed (Settings > Python Env)")
    try:
        import docker
        docker.from_env(timeout=10).ping()
    except ImportError:
        out.append("The docker package is not installed (Settings > Python Env)")
    except Exception as e:  # noqa: BLE001 — docker raises many kinds
        out.append(f"Docker is not running, or not yours to use: RD-Agent backtests in a container ({str(e)[:160]})")
    return out


def goal_text(kind: str, params: dict[str, Any]) -> str:
    """What the user asked for, in words: rdagent has no settings for it."""
    parts = []
    text = str(params.get("task_description") or params.get("research_goal") or "").strip()
    if text:
        parts.append(text)
    if kind == "factor_mining" and params.get("target_ic"):
        parts.append(f"Aim for an information coefficient of at least {float(params['target_ic']):g}.")
    if kind == "model_optimization":
        if params.get("model_type"):
            parts.append(f"Work on a {params['model_type']} model.")
        if params.get("optimization_target"):
            parts.append(f"Optimise for {str(params['optimization_target']).replace('_', ' ')}.")
    return " ".join(parts)


def use_our_python(home: Path) -> None:
    """rdagent runs the factor code in "the current conda environment", asking
    conda where that is. There is no conda: what answers is this file, with the
    Python this process runs on. (Without it the code ran on /usr/bin's, or on
    none.)"""
    bin_dir = home / "bin"
    bin_dir.mkdir(exist_ok=True)
    conda = bin_dir / "conda"
    conda.write_text("#!/bin/sh\n"
                     "# conda run -n NAME --no-capture-output COMMAND...\n"
                     '[ "$1 $2 $4" = "run -n --no-capture-output" ] || { echo "finterm: no conda here" >&2; exit 1; }\n'
                     'shift 4\nexec "$@"\n', encoding="utf-8")
    conda.chmod(0o755)
    os.environ["PATH"] = os.pathsep.join([str(Path(sys.executable).parent), str(bin_dir), os.environ.get("PATH", "")])
    os.environ["CONDA_DEFAULT_ENV"] = "finterm"


# ── What it has found, off the loop's trace ──────────────────────────────────

def _num(series: Any, key: str) -> float | None:
    try:
        v = float(series[key])
        return v if v == v else None
    except Exception:  # noqa: BLE001 — no result, no such metric, not a number
        return None


def results_of(hist: list[Any]) -> list[dict[str, Any]]:
    out = []
    for i, (exp, feedback) in enumerate(list(hist)):
        res = getattr(exp, "result", None)
        metrics = {
            "ic": _num(res, "IC"),
            "annual_return": _num(res, "1day.excess_return_with_cost.annualized_return"),
            "information_ratio": _num(res, "1day.excess_return_with_cost.information_ratio"),
            "max_drawdown": _num(res, "1day.excess_return_with_cost.max_drawdown"),
        }
        tasks = list(getattr(exp, "sub_tasks", None) or [])
        spaces = list(getattr(exp, "sub_workspace_list", None) or [])
        items = []
        for j, task in enumerate(tasks):
            ws = spaces[j] if j < len(spaces) else None
            items.append({
                "name": str(getattr(task, "name", None) or getattr(task, "factor_name", None) or f"item {j + 1}"),
                "kind": "model" if hasattr(task, "architecture") else "factor",
                "description": str(getattr(task, "description", None) or getattr(task, "factor_description", None) or ""),
                "code": str(getattr(ws, "all_codes", "") or "") if ws is not None else "",
            })
        out.append({"iteration": i + 1, "accepted": bool(getattr(feedback, "decision", False)),
                    "reason": str(getattr(feedback, "reason", "") or ""), "metrics": metrics, "items": items})
    return out


def best_ic(results: list[dict[str, Any]]) -> float | None:
    return max((r["metrics"]["ic"] for r in results if r["metrics"]["ic"] is not None), default=None)


class Recorder(threading.Thread):
    """Writes the job's state every few seconds, off the loop's own records."""

    def __init__(self, task_id: str, loop: Any):
        super().__init__(daemon=True)
        self.task_id, self.loop, self.done = task_id, loop, threading.Event()
        self._write = threading.Lock()
        self._seen = -1

    def record(self) -> None:
        with self._write:
            hist = list(getattr(getattr(self.loop, "trace", None), "hist", []) or [])
            fields: dict[str, Any] = {"iterations_completed": len(hist)}
            trace = dict(getattr(self.loop, "loop_trace", {}) or {})
            steps = getattr(self.loop, "steps", []) or []
            if trace:
                at = len(trace[max(trace)])
                fields["current_step"] = steps[at] if at < len(steps) else "-"
            if len(hist) != self._seen:
                self._seen = len(hist)
                results = results_of(hist)
                fields["best_ic"] = best_ic(results)
                jobs.write_json(jobs.folder(self.task_id) / "results.json", results)
            jobs.update(self.task_id, **fields)

    def run(self) -> None:
        while not self.done.wait(5):
            try:
                self.record()
            except Exception as e:  # noqa: BLE001 — the next one may do
                print(f"recording failed: {e}", file=sys.stderr, flush=True)


# ── The job ──────────────────────────────────────────────────────────────────

def make_loop(job: dict[str, Any]) -> Any:
    """The loop, new or as its last session left it. LLM and paths are set."""
    import importlib
    module, cls, setting, scen_env, scen = _LOOPS[job["type"]]
    os.environ[scen_env] = f"finterm_scenarios.{scen}"
    os.environ["FINTERM_RD_GOAL"] = job.get("goal") or ""
    Loop = getattr(importlib.import_module(module), cls)
    log = Path(os.environ["LOG_TRACE_PATH"])
    if (log / "__session__").is_dir() and any((log / "__session__").glob("*/*_*")):
        return Loop.load(str(log), checkout=True)
    return Loop(getattr(importlib.import_module("rdagent.app.qlib_rd_loop.conf"), setting))


def run(job: dict[str, Any]) -> None:
    import asyncio
    task_id = job["task_id"]
    recorder = None
    status, error = "failed", None
    try:
        loop = make_loop(job)
        recorder = Recorder(task_id, loop)
        recorder.start()
        left = job["max_iterations"] - len(loop.trace.hist)
        if left > 0:
            asyncio.run(loop.run(loop_n=left))
        status = "completed"
    except (SystemExit, KeyboardInterrupt):
        status = "stopped"
    except BaseException as e:  # noqa: BLE001
        import traceback
        traceback.print_exc()
        error = f"{type(e).__name__}: {e}"[:500]
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)  # let this be written
        if recorder:
            recorder.done.set()
            try:
                recorder.record()
            except Exception as e:  # noqa: BLE001
                print(f"recording failed: {e}", file=sys.stderr, flush=True)
        jobs.update(task_id, status=status, error=error, ended_at=datetime.now().isoformat(timespec="seconds"))


def main() -> None:
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    try:
        params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    except ValueError as e:
        answer({"success": False, "error": f"Invalid JSON params: {e}"})
        sys.exit(1)
    config = params.pop("config", None) or {}

    if command == "resume":
        try:
            job = jobs.load(str(params.get("task_id") or ""))
        except ValueError as e:
            answer({"success": False, "error": str(e)})
            sys.exit(1)
        if not job:
            answer({"success": False, "error": f"No such task: {params.get('task_id')}"})
            sys.exit(1)
        if job["status"] == "completed":
            answer({"success": False, "error": f"{job['task_id']} is complete"})
            sys.exit(1)
    elif command == "start" and params.get("type") in jobs.TYPES:
        kind = params["type"]
        try:
            n = max(1, min(100, int(params.get("max_iterations") or params.get("iterations") or 10)))
            goal = goal_text(kind, params)
        except (TypeError, ValueError) as e:
            answer({"success": False, "error": f"bad parameter: {e}"})
            sys.exit(1)
        job = {"task_id": jobs.new_task_id(kind), "type": kind, "goal": goal, "max_iterations": n,
               "created_at": datetime.now().isoformat(timespec="seconds"), "iterations_completed": 0}
    else:
        answer({"success": False, "error": f"unknown command or type: {command} {params.get('type')}"})
        sys.exit(1)

    lacks = missing(config)
    if lacks:
        answer({"success": False, "error": "Not started. " + "; ".join(lacks) + "."})
        sys.exit(1)

    home = jobs.folder(job["task_id"])
    home.mkdir(parents=True, exist_ok=True)
    lock = jobs.Lock(job["task_id"])
    if not lock.acquire():
        answer({"success": False, "error": f"{job['task_id']} is already running"})
        sys.exit(1)
    job.update(status="running", error=None, started_at=datetime.now().isoformat(timespec="seconds"), ended_at=None,
               model=config.get("llm_model"))
    jobs.write_json(home / "job.json", job)

    # rdagent keeps its workspaces, caches and log where it runs.
    os.chdir(home)
    os.environ["LOG_TRACE_PATH"] = str(home / "log")
    use_our_python(home)
    os.environ["MODEL_CoSTEER_env_type"] = "docker"  # its other choice creates a conda environment
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    answer({"success": True, "task_id": job["task_id"], "status": "started", "max_iterations": job["max_iterations"],
            "message": f"{job['task_id']} started", "log": str(home / "worker.log")})
    log = os.open(home / "worker.log", os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    os.dup2(log, 1)
    os.dup2(log, 2)

    try:
        llm.apply(config)
    except Exception as e:  # noqa: BLE001
        jobs.update(job["task_id"], status="failed", error=f"{type(e).__name__}: {e}"[:500],
                    ended_at=datetime.now().isoformat(timespec="seconds"))
        sys.exit(1)
    config.clear()
    run(job)


if __name__ == "__main__":
    main()

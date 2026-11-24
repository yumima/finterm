"""
cli.py — the app's entry point for Deep Agents (AIQuantLabService).

    cli.py check_status
    cli.py execute_task '{"task", "agent_type", "thread_id", "config"}'

`config` is the LLM profile (llm_provider, llm_model, llm_api_key,
llm_base_url) and the app's tools (terminal_mcp_endpoint, terminal_tools).

Prints one JSON object: {"success", "result", "thread_id", "tool_calls",
"error"}. Logs go to stderr.
"""

from __future__ import annotations

import json
import logging
import re
import sys
import uuid
from pathlib import Path
from typing import Any

logging.basicConfig(level=logging.WARNING, format="%(levelname)-8s %(name)s: %(message)s", stream=sys.stderr, force=True)

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))
sys.path.append(str(_HERE.parents[1]))  # scripts/

from finterm_paths import feature_dir  # noqa: E402
from models import create_model, extract_text  # noqa: E402
from subagents import list_agent_types, list_subagent_names  # noqa: E402

logger = logging.getLogger(__name__)

_KEEP_TURNS = 10  # of a thread, as the context of its next task


def _error(msg: str, thread_id: str = "") -> dict[str, Any]:
    return {"success": False, "result": "", "thread_id": thread_id, "error": msg}


# ── Threads: what was asked and answered under one id ────────────────────────

def _thread_file(thread_id: str) -> Path:
    return feature_dir("deepagents", "threads") / f"{thread_id}.json"


def _load_thread(thread_id: str) -> list[list[str]]:
    try:
        turns = json.loads(_thread_file(thread_id).read_text(encoding="utf-8"))
        return [t for t in turns if isinstance(t, list) and len(t) == 2][-_KEEP_TURNS:]
    except FileNotFoundError:
        return []
    except (OSError, ValueError) as exc:
        logger.warning("thread %s is unreadable, starting anew: %s", thread_id, exc)
        return []


def _save_thread(thread_id: str, turns: list[list[str]]) -> None:
    path = _thread_file(thread_id)
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(turns[-_KEEP_TURNS:], ensure_ascii=False), encoding="utf-8")
    tmp.replace(path)


# ── Commands ─────────────────────────────────────────────────────────────────

def cmd_check_status(_params: dict[str, Any]) -> dict[str, Any]:
    try:
        from deepagents import create_deep_agent  # noqa: F401
        available, error = True, None
    except ImportError as exc:
        available, error = False, str(exc)
    return {"success": True, "available": available, "error": error,
            "agent_types": list_agent_types(), "subagent_names": list_subagent_names()}


def cmd_execute_task(params: dict[str, Any]) -> dict[str, Any]:
    task = str(params.get("task") or "").strip()
    if not task:
        return _error("task is required")
    agent_type = str(params.get("agent_type") or "general")
    if agent_type not in list_agent_types():
        return _error(f"unknown agent_type {agent_type!r}; one of {', '.join(list_agent_types())}")
    thread_id = str(params.get("thread_id") or "").strip() or uuid.uuid4().hex[:8]
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", thread_id) or thread_id.startswith("."):
        return _error("a thread ID is letters, digits, '-', '_' and '.' (64 at most)")
    config = params.get("config") or {}

    try:
        from agent import ToolLog, build_tools, create_agent, grounding

        turns = _load_thread(thread_id)
        log = ToolLog()
        agent = create_agent(create_model(config), agent_type, build_tools(config, log))
        messages = [m for q, a in turns for m in (("user", q), ("assistant", a))] + [("user", task)]
        state = agent.invoke({"messages": messages}, config={"recursion_limit": 1000})
    except Exception as exc:  # noqa: BLE001 — the reader gets the reason, whatever it was
        logger.exception("execute_task failed")
        return _error(str(exc) or type(exc).__name__, thread_id)

    text = answer_text(state)
    if not text:
        return _error("the model ended without an answer", thread_id)
    # A model may quote a figure, never originate one: each is checked against
    # what it was given — the tools' answers, the task, and the thread so far
    # (checked when it was written). What sub-agents wrote is no evidence.
    result = grounding.ground(text, task, [list(t) for t in turns], list(log))
    _save_thread(thread_id, turns + [[task, result]])
    return {"success": True, "result": result, "thread_id": thread_id, "tool_calls": len(log), "error": None}


def answer_text(state: dict[str, Any]) -> str:
    """The last message, and any file the agent wrote that it does not contain."""
    text = ""
    for msg in reversed(state.get("messages") or []):
        if getattr(msg, "type", "") == "ai" and extract_text(msg.content).strip():
            text = extract_text(msg.content).strip()
            break
    for name, data in sorted((state.get("files") or {}).items()):
        content = data.get("content") if isinstance(data, dict) else data
        body = ("\n".join(content) if isinstance(content, list) else str(content or "")).strip()
        if body and body not in text:
            text += f"\n\n── {name} ──\n{body}"
    return text.strip()


_COMMANDS = {"check_status": cmd_check_status, "execute_task": cmd_execute_task}


def main() -> None:
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command not in _COMMANDS:
        print(json.dumps(_error(f"Unknown command {command!r}. Available: {', '.join(_COMMANDS)}")))
        sys.exit(1)
    try:
        params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    except ValueError as exc:
        print(json.dumps(_error(f"Invalid JSON params: {exc}")))
        sys.exit(1)
    print(json.dumps(_COMMANDS[command](params), default=str), flush=True)


if __name__ == "__main__":
    main()

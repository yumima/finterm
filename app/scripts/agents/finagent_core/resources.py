"""
Path derivation for per-persona runtime state.

Every stateful resource (sessions, memory, knowledge, agentic memory) lives
under a path keyed by (user_id, agent_id). This module is the single source
of truth for those paths — no other module should compose them by hand.
"""

import os
import re
from pathlib import Path
from typing import Optional


_UNSAFE_RE = re.compile(r"[^A-Za-z0-9_\-]")


def sanitize(raw: Optional[str]) -> str:
    """Sanitise an id for safe filesystem use.

    Raises ValueError on None or empty/whitespace-only input.
    """
    if raw is None or not str(raw).strip():
        raise ValueError("id must be non-empty")
    return _UNSAFE_RE.sub("_", str(raw).strip())


def base_dir() -> str:
    """
    Root directory for all finagent persona state.

    Overridable via FINAGENT_DATA_DIR env var (the app sets it). Otherwise
    the user's agents folder: <data root>/user/agents/finagent.
    """
    env = os.getenv("FINAGENT_DATA_DIR")
    if env:
        return str(Path(env).expanduser())
    # Agent memory is personal: the user/ folder (finterm_paths), same as the
    # app sets FINAGENT_DATA_DIR to.
    from finterm_paths import agents_dir
    return str(agents_dir("finagent"))


def persona_dir(user_id: str, agent_id: str) -> str:
    """Return the directory for one persona's state. Does not create it."""
    u = sanitize(user_id)
    a = sanitize(agent_id)
    return str(Path(base_dir()) / "users" / u / "personas" / a)


def ensure_persona_dir(user_id: str, agent_id: str) -> str:
    """Create the persona directory tree (if missing) and return the path."""
    d = persona_dir(user_id, agent_id)
    Path(d).mkdir(parents=True, exist_ok=True)
    return d


def sessions_db(user_id: str, agent_id: str) -> str:
    """Path to the SQLite file storing Agno session history for this persona."""
    return str(Path(persona_dir(user_id, agent_id)) / "sessions.db")


def memory_db(user_id: str, agent_id: str) -> str:
    """Path to the SQLite file for Agno long-term memory for this persona."""
    return str(Path(persona_dir(user_id, agent_id)) / "memory.db")


def knowledge_dir(user_id: str, agent_id: str) -> str:
    """Directory for this persona's knowledge-base vector store."""
    return str(Path(persona_dir(user_id, agent_id)) / "knowledge")


def agentic_memory_db(user_id: str, agent_id: str) -> str:
    """Path to the SQLite file for AgenticMemoryModule storage for this persona."""
    return str(Path(persona_dir(user_id, agent_id)) / "agentic_memory.db")

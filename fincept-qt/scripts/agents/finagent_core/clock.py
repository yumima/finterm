"""Per-request wall-clock grounding for agent system prompts.

Models have no idea what day it is; without this line they answer "latest"
questions from their training cutoff.  Computed on every call (never cached at
import) so a long-lived process still reports the right date.

The line is day-granular and APPENDED after the instructions: a volatile
leading block would change the prompt prefix and defeat prefix/prompt caching
(hearth/Ollama, Anthropic).
"""
from __future__ import annotations

from datetime import datetime
from typing import Any

_MARKER = "Current date: "


def current_date_line() -> str:
    """'Current date: YYYY-MM-DD (Weekday), timezone TZ'."""
    now = datetime.now().astimezone()
    tz = now.tzname() or now.strftime("%z")
    return f"{_MARKER}{now:%Y-%m-%d} ({now:%A}), timezone {tz}"


def _strip_date_lines(text: str) -> str:
    """Drop any existing date line (leading, trailing or standalone)."""
    kept = [ln for ln in text.split("\n") if not ln.startswith(_MARKER)]
    return "\n".join(kept).strip("\n")


def with_current_date(instructions: Any) -> Any:
    """Append the current date line to instructions (str or list of str).

    Idempotent per call chain: an existing date line is replaced, not duplicated.
    """
    line = current_date_line()
    if instructions is None or instructions == "":
        return line
    if isinstance(instructions, list):
        rest = [i for i in instructions if not (isinstance(i, str) and i.startswith(_MARKER))]
        return [*rest, line]
    if isinstance(instructions, str):
        instructions = _strip_date_lines(instructions)
        return f"{instructions}\n\n{line}" if instructions else line
    return instructions

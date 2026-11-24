"""Skills: how an agent does one kind of work, as text in its instructions.

A skill is a folder with a SKILL.md, `<vertical>/<name>/SKILL.md`. The shipped
ones are part of the program. One the user edited is their own copy, laid out
the same under their data, and wins (SkillStore on the app's side); a skill of
their own needs no shipped one.

An agent's config names its skills. What it is given is what they say: a model
knows nothing by a name. A name with no skill is dropped (agents are seeded
with some that are not vendored yet).
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

# Default location for vendored SKILL.md files.  Override via
# `list_available_skills(root=...)` in tests.
SKILLS_ROOT = (
    Path(__file__).resolve().parent.parent / "skills"
)


def user_skills_root() -> Path:
    env = os.environ.get("FINTERM_USER_DIR")
    if env:
        return Path(env) / "skills"
    scripts = str(Path(__file__).resolve().parents[3])
    if scripts not in sys.path:
        sys.path.append(scripts)
    from finterm_paths import user_root
    return user_root() / "skills"


def skill_files(root: Path | None = None) -> dict[str, Path]:
    """Skill name -> the SKILL.md in use. Missing roots are empty ones."""
    roots = [root] if root is not None else [SKILLS_ROOT, user_skills_root()]
    out: dict[str, Path] = {}
    for base in roots:  # the user's last: it wins
        if base.is_dir():
            for skill_md in sorted(base.rglob("SKILL.md")):
                if skill_md.is_file():
                    out[skill_md.parent.name] = skill_md
    return out


def list_available_skills(root: Path | None = None) -> list[str]:
    """Skill names, sorted (e.g. `comps-analysis`)."""
    return sorted(skill_files(Path(root) if root is not None else None))


def resolve_skills(requested: list[str], available: list[str]) -> list[str]:
    """`requested` without the names there is no skill for."""
    if not requested:
        return []
    wanted = set(requested)
    return [name for name in available if name in wanted]


def _body(text: str) -> str:
    """A SKILL.md without its front matter."""
    text = text.lstrip("\ufeff")
    if text.startswith("---"):
        end = text.find("\n---", 3)
        if end >= 0:
            nl = text.find("\n", end + 1)
            return text[nl + 1:].strip() if nl >= 0 else ""
    return text.strip()


def skills_text(requested: list[str] | None, root: Path | None = None) -> str:
    """The named skills' text, for an agent's instructions. "" when there is none."""
    files = skill_files(root)
    parts = []
    for name in dict.fromkeys(n for n in (requested or []) if isinstance(n, str)):
        if name in files:
            try:
                body = _body(files[name].read_text(encoding="utf-8"))
            except (OSError, UnicodeDecodeError):
                continue
            if body:
                parts.append(f'<skill name="{name}">\n{body}\n</skill>')
    if not parts:
        return ""
    return "Your skills: when the request is this kind of work, do it this way.\n\n" + "\n\n".join(parts)

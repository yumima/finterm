"""SKILL.md discovery for runtime adapters.

Track 7 will vendor `anthropics/financial-services` SKILL.md files
into `scripts/agents/finagent_core/skills/<vertical>/<name>/SKILL.md`.
Track 3 item 12 ships the loader that finds them and the wiring
that hands the names to the SDK so each turn loads the right set.

The SDK's `skills` option (claude_agent_sdk.ClaudeAgentOptions) is
a `list[str] | Literal['all'] | None`.  We pass a filtered list
of names — unknown names would otherwise produce CLI warnings that
end up cluttering trace logs.  The SDK / Claude Code resolves the
actual file via Claude Code's setting sources (user / project /
local).  Materializing finterm's skills tree under
Claude Code's expected layout (.claude/skills/<name>/SKILL.md) is
deferred to Track 7 when content actually lands.
"""
from __future__ import annotations

from pathlib import Path

# Default location for vendored SKILL.md files.  Override via
# `list_available_skills(root=...)` in tests.
SKILLS_ROOT = (
    Path(__file__).resolve().parent.parent / "skills"
)


def list_available_skills(root: Path | None = None) -> list[str]:
    """Discover skill names by walking the skills tree.

    A skill is any directory containing a `SKILL.md` file.  Returns
    the directory names (e.g. `comps-analysis`).  Missing root, or
    a root that doesn't exist yet (pre-Track-7), returns [].
    """
    base = Path(root) if root is not None else SKILLS_ROOT
    if not base.exists() or not base.is_dir():
        return []

    names = sorted({
        skill_md.parent.name
        for skill_md in base.rglob("SKILL.md")
        if skill_md.is_file()
    })
    return names


def resolve_skills(requested: list[str], available: list[str]) -> list[str]:
    """Filter `requested` to only names present in `available`.

    Unknown names are dropped — better silently than to confuse the
    SDK loader.  Order of available is preserved; requested order
    doesn't matter for SDK consumption.  Returns [] for an empty
    `requested` so callers can omit the SDK option when no skills
    apply.
    """
    if not requested:
        return []
    wanted = set(requested)
    return [name for name in available if name in wanted]

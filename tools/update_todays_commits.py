#!/usr/bin/env python3
"""Maintain the "Today's commits (YYYY-MM-DD)" section in README.md.

Lists today's commits (local time), oldest first, with GitHub links and a
one-line abstract derived from each commit's conventional-commits subject.
Replaces an existing "Today's commits" section if present; otherwise
inserts a fresh one after the title tagline. Leaves the README untouched
if there are no commits today.

Designed to be invoked from .githooks/pre-commit. Because the in-flight
commit isn't in `git log` yet, commit N's README reflects commits 1..N-1
of today; commit N's own entry lands in commit N+1's README. That's the
intentional simple semantic — see docs/design discussion for the
amend-based alternative if you ever want HEAD-perfect.
"""

from __future__ import annotations

import datetime
import pathlib
import re
import subprocess
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
README = REPO_ROOT / "README.md"


def github_base() -> str | None:
    """Return https://github.com/<owner>/<repo> for origin, or None."""
    try:
        url = subprocess.check_output(
            ["git", "remote", "get-url", "origin"],
            cwd=REPO_ROOT, text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    m = re.match(r"^git@github\.com:(.+?)(?:\.git)?$", url)
    if m:
        return f"https://github.com/{m.group(1)}"
    m = re.match(r"^https://github\.com/(.+?)(?:\.git)?$", url)
    if m:
        return f"https://github.com/{m.group(1)}"
    return None


def _changed_files(sha: str) -> list[str]:
    """Return the list of files touched by a single commit."""
    try:
        out = subprocess.check_output(
            ["git", "show", "--pretty=", "--name-only", sha],
            cwd=REPO_ROOT, text=True,
        ).strip()
    except subprocess.CalledProcessError:
        return []
    return [line for line in out.splitlines() if line]


def _is_readme_only(sha: str) -> bool:
    """True if the commit touched only README.md.

    Used to suppress self-referential noise: every time this script runs as
    part of the pre-commit hook it re-stages README.md, and any standalone
    README-only commit (typo fix, section edit, etc.) is meta to the section
    we're regenerating. Listing it would be circular.
    """
    return _changed_files(sha) == ["README.md"]


def todays_commits() -> list[tuple[str, str]]:
    """Return [(short_sha, subject), ...] for today's commits, oldest first.

    Excludes README-only commits — see _is_readme_only.
    """
    today = datetime.date.today().isoformat()
    try:
        out = subprocess.check_output(
            ["git", "log", f"--since={today} 00:00",
             "--reverse", "--no-merges", "--pretty=format:%h|%s"],
            cwd=REPO_ROOT, text=True,
        ).strip()
    except subprocess.CalledProcessError:
        return []
    pairs: list[tuple[str, str]] = []
    for line in out.splitlines():
        sha, sep, subject = line.partition("|")
        if not (sep and sha and subject):
            continue
        if _is_readme_only(sha):
            continue
        pairs.append((sha, subject))
    return pairs


# Recognise the conventional-commits prefix so we can bold the type+scope
# and use the rest as the abstract. Falls back to the full subject if the
# commit doesn't follow the convention.
CONV_RE = re.compile(
    r"^(?P<type>fix|feat|perf|ui|docs|chore|refactor|test|build|ci|style|revert)"
    r"(?P<scope>\([^)]+\))?:\s*(?P<abstract>.+)$"
)


def format_line(gh_base: str, sha: str, subject: str) -> str:
    m = CONV_RE.match(subject)
    if m:
        prefix = f"{m['type']}{m['scope'] or ''}"
        return f"- [`{sha}`]({gh_base}/commit/{sha}) **{prefix}:** {m['abstract']}"
    return f"- [`{sha}`]({gh_base}/commit/{sha}) {subject}"


def build_section(gh_base: str, today: str,
                  commits: list[tuple[str, str]]) -> str:
    parts = [f"## Today's commits ({today})", "", "Oldest first.", ""]
    parts.extend(format_line(gh_base, sha, subj) for sha, subj in commits)
    parts.extend(["", f"[See all commits →]({gh_base}/commits/main)", ""])
    # Trailing "\n" ensures a blank line separates this section from the
    # next "## " heading after SECTION_RE.sub splices it back in.
    return "\n".join(parts) + "\n"


# Matches an existing "Today's commits (YYYY-MM-DD)" block up to (but not
# including) the next "## " heading. Dotall + multiline so the section can
# span any number of lines.
SECTION_RE = re.compile(
    r"^## Today's commits \(\d{4}-\d{2}-\d{2}\).*?(?=^## )",
    re.MULTILINE | re.DOTALL,
)

# Fallback insertion point: after the title line + blank line + one tagline
# paragraph + blank line, immediately before the first "## " section.
INSERT_RE = re.compile(
    r"^(# [^\n]+\n\n[^\n]+\n\n)(?=## )",
    re.MULTILINE,
)


def update_readme(new_section: str) -> bool:
    """Replace or insert the section. Return True iff the file changed."""
    text = README.read_text()
    if SECTION_RE.search(text):
        new_text = SECTION_RE.sub(new_section, text, count=1)
    elif INSERT_RE.search(text):
        new_text = INSERT_RE.sub(rf"\g<1>{new_section}\n", text, count=1)
    else:
        print("update_todays_commits: could not locate insertion point in "
              "README.md — leaving file untouched", file=sys.stderr)
        return False
    if new_text == text:
        return False
    README.write_text(new_text)
    return True


def main() -> int:
    if not README.exists():
        return 0
    gh_base = github_base()
    if not gh_base:
        return 0
    commits = todays_commits()
    if not commits:
        # Per spec rule 3: no commits today → no change.
        return 0
    today = datetime.date.today().isoformat()
    section = build_section(gh_base, today, commits)
    update_readme(section)
    return 0


if __name__ == "__main__":
    sys.exit(main())

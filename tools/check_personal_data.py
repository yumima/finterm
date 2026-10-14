#!/usr/bin/env python3
"""Pre-commit guard: personal data and secrets never reach the repository.

Scans only what is being committed (the staged diff's added lines and the
staged file names) and refuses the commit on any of:

  * the user's own markers — literal strings, one per line, in
    .git/info/personal-patterns (inside .git, so the list itself can never be
    committed or pushed): email addresses, real name, account ids, …;
  * the user's home directory path;
  * any secret currently stored in finterm's OS keyring (Linux, when the
    libsecret Python bindings are available) — so a real API key cannot be
    pasted into source by accident;
  * key-shaped strings (OpenAI/Anthropic, AWS, GitHub, Google, Slack tokens,
    private-key blocks);
  * personal-data files forced past .gitignore (databases, .env, config,
    workspaces, key material).

With --message FILE it checks a commit message instead (commit-msg hook):
messages are pushed with the code and are just as public.

Matches are reported masked. A genuine false positive can be committed with
`git commit --no-verify` after checking it really is not personal.
"""

import os
import re
import subprocess
import sys

BLOCKED_FILES = re.compile(
    # Databases in every on-disk shape: db/sqlite/sqlite3 plus companions
    # (-wal/-shm/-journal), conflict copies (.1) and backups (.bak…).
    r"(^|/)(\.env(\..*)?|.*\.(db|sqlite3?)([.-].*)?|.*\.(conf|fwsp|pem|key|p12|pfx|jsonl)"
    r"|.*\.fincept(\.[0-9]+)?|session\.txt|valuation_overrides\.json)$",
    re.IGNORECASE,  # Portfolio.DB, .ENV
)

SECRET_SHAPES = [
    ("OpenAI/Anthropic-style key", re.compile(r"\bsk-(?:ant-)?[A-Za-z0-9_\-]{20,}")),
    ("AWS access key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("GitHub token", re.compile(r"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36}\b|\bgithub_pat_[A-Za-z0-9_]{40,}")),
    ("Google API key", re.compile(r"\bAIza[0-9A-Za-z_\-]{35}\b")),
    ("Slack token", re.compile(r"\bxox[abprs]-[A-Za-z0-9\-]{10,}")),
    ("private key", re.compile(r"-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----")),
    ("Hugging Face token", re.compile(r"\bhf_[A-Za-z0-9]{30,}")),
    ("Stripe key", re.compile(r"\b(?:sk|rk|pk)_(?:live|test)_[A-Za-z0-9]{16,}")),
    ("Databento key", re.compile(r"\bdb-[A-Za-z0-9]{24,}")),
    # A long literal assigned to a key-like name: catches the many providers
    # (FRED, Polygon, Finnhub, …) whose keys have no recognisable prefix.
    ("hard-coded credential",
     re.compile(r"(?i)(?:api[_-]?key|apikey|secret|token|password|passwd)[\"']?\s*[:=]\s*[\"'][A-Za-z0-9_\-]{16,}[\"']")),
]


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def personal_markers(repo):
    markers = []
    # --git-path resolves the shared .git dir, so worktrees use the same list.
    path = os.path.join(repo, git("rev-parse", "--git-path", "info/personal-patterns").strip())
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            markers = [l.strip() for l in fh if l.strip() and not l.lstrip().startswith("#")]
    home = os.path.expanduser("~")
    if home and home != "/":
        markers.append(home + "/")
    return markers


def keyring_secrets():
    """Values stored in finterm's keyring (never printed). Says so when it
    cannot check, rather than passing silently."""
    try:
        import gi

        gi.require_version("Secret", "1")
        from gi.repository import Secret

        schema = Secret.Schema.new(
            "com.fincept.terminal", Secret.SchemaFlags.NONE, {"fincept-key": Secret.SchemaAttributeType.STRING}
        )
        items = Secret.password_search_sync(
            schema, {}, Secret.SearchFlags.ALL | Secret.SearchFlags.LOAD_SECRETS | Secret.SearchFlags.UNLOCK, None)
        out = []
        for it in items:
            sec = it.get_secret()
            val = sec.get_text() if sec else None
            # Short values (a flag, a PIN-attempt counter) would match noise.
            if val and len(val) >= 12:
                out.append(val)
        return out
    except Exception as e:
        print(f"pre-commit: note — keyring check skipped ({type(e).__name__}); "
              "pasted API keys are only caught by their shape.", file=sys.stderr)
        return []


def mask(s):
    return s[:3] + "…" if len(s) > 6 else "…"


def check_text(where, text, markers, secrets, problems):
    for m in markers:
        if m and m.lower() in text.lower():
            problems.append(f"{where}: contains a personal marker ({mask(m)})")
    for s in secrets:
        if s in text:
            problems.append(f"{where}: contains a value stored in your keyring ({mask(s)})")
    for label, rx in SECRET_SHAPES:
        if rx.search(text):
            problems.append(f"{where}: looks like a {label}")


def report(problems):
    if not problems:
        return 0
    print("personal data or secrets found — commit refused:", file=sys.stderr)
    for p in dict.fromkeys(problems):
        print("  - " + p, file=sys.stderr)
    print("Remove them (config belongs in Settings / the OS keyring), or, if this is a\n"
          "verified false positive, commit with --no-verify.", file=sys.stderr)
    return 1


def main():
    repo = git("rev-parse", "--show-toplevel").strip()
    problems = []
    if len(sys.argv) == 3 and sys.argv[1] == "--message":
        with open(sys.argv[2], encoding="utf-8") as fh:
            msg = "".join(l for l in fh if not l.startswith("#"))
        check_text("commit message", msg, personal_markers(repo), keyring_secrets(), problems)
        return report(problems)

    # Plain unified diffs only: an external diff tool or textconv driver
    # (difftastic, GIT_EXTERNAL_DIFF) would hide every added line from us.
    plain = ("--no-ext-diff", "--no-textconv")
    # -z: real paths, never C-quoted ("r\303\251union.db" would slip past $).
    for name in filter(None, git("diff", "--cached", *plain, "--name-only", "-z", "--diff-filter=ACMRT").split("\0")):
        if BLOCKED_FILES.search(name):
            problems.append(f"{name}: personal-data file type must not be committed")

    markers = personal_markers(repo)
    secrets = keyring_secrets()
    diff = git("diff", "--cached", *plain, "-U0", "--no-color", "--diff-filter=ACMRT")
    current = None
    # Binary files show no text in the diff: scan their staged content.
    # -z with --no-renames gives one real path per entry (no "{a => b}"
    # rename notation, no quoting of unusual characters).
    raw = git("diff", "--cached", *plain, "--numstat", "-z", "--no-renames", "--diff-filter=ACMRT")
    for entry in raw.split("\0"):
        parts = entry.split("\t", 2)
        if len(parts) == 3 and parts[0] == "-":
            name = parts[2]
            blob = subprocess.run(["git", "show", f":{name}"], capture_output=True).stdout[: 5 * 1024 * 1024]
            check_text(name, blob.decode("utf-8", "ignore"), markers, secrets, problems)
            if b"\x00" in blob:  # UTF-16 text reads as "binary" too
                for enc in ("utf-16-le", "utf-16-be"):
                    check_text(name, blob.decode(enc, "ignore"), markers, secrets, problems)

    # File headers ("--- a/x", "+++ b/x") only occur between "diff --git" and
    # the file's first "@@"; inside a hunk, "+++ …" is an added line whose
    # text starts with "++ " and must be scanned like any other.
    in_header = False
    for line in diff.splitlines():
        if line.startswith("diff --git "):
            in_header = True
            continue
        if line.startswith("@@"):
            in_header = False
            continue
        if in_header:
            if line.startswith("+++ "):
                current = line[6:] if line.startswith("+++ b/") else line[4:]
            continue
        if line.startswith("+"):
            check_text(current, line[1:], markers, secrets, problems)

    return report(problems)


if __name__ == "__main__":
    sys.exit(main())

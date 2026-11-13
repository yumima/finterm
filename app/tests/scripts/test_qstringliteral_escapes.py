#!/usr/bin/env python3
r"""No UTF-8 byte escapes inside QStringLiteral.

QStringLiteral builds a UTF-16 literal (u"..."), where each \xNN escape is ONE
code unit — so "\xe2\x80\x94" became three characters ("â\u0080\u0094")
instead of an em dash. Eleven labels showed that garbage (arrows, bullets,
"…", "—", emoji) on every platform until 2026-11-13. Write — / \U0001f4d0
instead. Plain "..." literals passed to QString are UTF-8 and are fine.
"""

import pathlib
import re
import sys

SRC = pathlib.Path(__file__).resolve().parents[2] / "src"
LIT = r'"(?:[^"\\\n]|\\.)*"'
CALL = re.compile(r"QStringLiteral\(\s*((?:" + LIT + r"\s*)+)\)")
HIGH_BYTE = re.compile(r"(?<!\\)\\x[89a-fA-F][0-9a-fA-F]")

bad = []
for p in sorted(SRC.rglob("*")):
    if p.suffix not in (".cpp", ".h"):
        continue
    text = p.read_text(errors="replace")
    for m in CALL.finditer(text):
        if HIGH_BYTE.search(m.group(1)):
            line = text.count("\n", 0, m.start()) + 1
            bad.append(f"{p.relative_to(SRC.parent)}:{line}: {m.group(0)[:80]}")

if bad:
    print("UTF-8 byte escapes inside QStringLiteral (use \\uXXXX):")
    print("\n".join("  " + b for b in bad))
    sys.exit(1)
print("ok — no UTF-8 byte escapes in QStringLiteral")

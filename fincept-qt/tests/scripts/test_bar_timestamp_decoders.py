#!/usr/bin/env python3
r"""Every epoch-second timestamp must be decoded by the rule for its KIND.

This codebase carries two kinds, both qint64, both named `timestamp`:

  BAR stamps  (Candle::timestamp, last_bar_ts) are a daily session stamped at
              MIDNIGHT IN THE EXCHANGE'S ZONE. Only the calendar date means
              anything, and it must come from bar_date() / Candle::date().
              Read in UTC it is a day early for every exchange east of
              Greenwich; read in the viewer's local zone, a day early for every
              viewer west of the exchange.

  EVENT stamps (an earnings announcement, a news publication, an audit entry)
              carry a real time of day and belong in a real timezone.
              bar_date() is WRONG for these.

`QDateTime::fromSecsSinceEpoch(x)` compiles and looks correct for both, so the
distinction has only ever lived in the author's head — and it has been wrong in
five places across four files, twice within one file, once on a line whose
neighbour twenty lines up got it right. Fixing instances has not worked; this
makes a new one fail the build.

The rule enforced here: every fromSecsSinceEpoch() applied to a timestamp field
must either go through bar_date/date(), or be listed below with a reason. The
list is short on purpose — adding to it is a deliberate act at review time,
which is exactly the decision that kept being skipped.

EVERY fromSecsSinceEpoch() in the tree must be classified, because every one of
them is decoding one kind or the other. A call is accepted when it goes through
bar_date()/`.date()`, or when it carries an `// EVENT-STAMP: <reason>` marker on
its own line or within the three lines above it.

Classifying all of them, rather than pattern-matching the ones that "look like"
bar stamps, is deliberate. The first version of this test matched `\.timestamp`
and so was blind to `near->timestamp`: it printed PASS on a tree containing a
live instance of the exact bug it was written to catch, in a file that had just
been edited. A guard with a blind spot is worse than no guard, because it is
believed. There are ~24 call sites; classifying each once is cheap, and the
marker is the "deliberate act at review time" that the recurrence keeps skipping.

Self-contained: pure text scan, no build, no network.
"""

import os
import re
import sys

SRC = os.path.join(os.path.dirname(__file__), "..", "..", "src")

# The canonical decoder's own definition — it IS bar_date(), so it cannot be
# asked to call itself.
DEFINITION = "core/util/BarTime.h"

CALL = re.compile(r"fromSecsSinceEpoch\s*\(")
GOES_THROUGH_BAR_DATE = re.compile(r"bar_date\s*\(")
EVENT_MARKER = re.compile(r"//\s*EVENT-STAMP:\s*\S")

failures = []
marked = 0


def scan():
    global marked
    for root, _dirs, files in os.walk(SRC):
        for fn in sorted(files):
            if not fn.endswith((".cpp", ".h")):
                continue
            path = os.path.join(root, fn)
            rel = os.path.relpath(path, SRC).replace(os.sep, "/")
            if rel == DEFINITION:
                continue
            try:
                with open(path, encoding="utf-8") as fh:
                    lines = fh.readlines()
            except (OSError, UnicodeDecodeError):
                continue
            for i, line in enumerate(lines, 1):
                if line.lstrip().startswith(("//", "*", "///")):
                    continue          # a comment describing the rule is not a use
                if not CALL.search(line):
                    continue
                if GOES_THROUGH_BAR_DATE.search(line):
                    continue          # a bar stamp, decoded correctly
                # An event stamp must say so, here or just above.
                window = "".join(lines[max(0, i - 4):i])
                if EVENT_MARKER.search(window):
                    marked += 1
                    continue
                failures.append(f"{rel}:{i}: {line.strip()}")


def main():
    if not os.path.isdir(SRC):
        print(f"SKIP : no src tree at {SRC}")
        return 77
    scan()

    if failures:
        print("FAIL : unclassified timestamp decode(s). Every fromSecsSinceEpoch() is")
        print("       decoding either a BAR stamp or an EVENT stamp, and the two need")
        print("       different treatment:")
        print("         bar   (Candle / HistoryPoint / last_bar_ts) -> use .date(), or bar_date()")
        print("         event (an announcement, a publication, a tick, a fetch time)")
        print("               -> decode in a real timezone and mark the line:")
        print("                  // EVENT-STAMP: why this is an instant, not a session")
        for f in failures:
            print("       " + f)
        return 1

    print(f"PASS : every fromSecsSinceEpoch() classified — {marked} marked as event stamps, "
          f"the rest through bar_date()")
    print("all bar-timestamp decoder tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Launch posts for Reddit and Hacker News — ONE post per community, by hand.

The single copy of the drafts (plans/marketing/launch-posts.md points here):
the Social screen shows them with their rules, copies the text and opens the
submit page; nothing here is ever submitted by the app. Reddit's API needs
approval for new apps and most finance subs remove recurring links.

    python3 launch_posts.py      # print every draft as Markdown, for reading

Rules researched 2026-11-13; Reddit blocked direct reads, so most are
second-hand — read each sub's About › Rules before posting.
"""
import re
import urllib.parse

import weekly_brief_core as wb

REPO_URL = "https://github.com/yumima/finterm"
LIST_URL = f"{wb.SITE_URL}/insider-cluster-buys/"
STUDY_PATH = "plans/research/form4_forward_returns_2026-09.md"

# What every post must respect (shown beside each draft).
RULES = [
    "Read the sub's About › Rules in a browser first. If a rule says no, skip the sub.",
    "New, low-karma accounts are caught by AutoModerator age/karma filters on the big subs (r/stocks, "
    "r/investing, r/StockMarket, r/pennystocks): comment genuinely there for a couple of weeks first.",
    "Disclose AI assistance in every post (the drafts do). Being found to hide it is fatal in FOSS communities.",
    "One sub per day; answer every comment for the first few hours. Never cross-post the same text.",
    "Never ask for upvotes. Never post the weekly issue as a recurring link.",
]

# Communities not to post in, and why (the plan gave no reason for the last four).
SKIP = [
    ("r/investing", "bans self-promotion (permanent ban)"),
    ("r/opensource", "bans AI-assisted projects since Oct 2026; also needs a 1-year-old account and project"),
    ("r/quant", ""),
    ("r/SecurityAnalysis", ""),
    ("r/options", ""),
    ("r/linux", ""),
]

# The table of this week's rows; filled from the PUBLISHED issue.
TABLE = "{table}"

_STOCKMARKET_FOOTER = (
    "Filter: 2+ different insiders, transaction code P (open market), excluding 10% owners and 10b5-1 plans, "
    "$50K+ total, filings from the last 7 days. History (my backtest, Jul 2025–Jun 2026): such buys beat SPY "
    "by a median 1.8% over the next month 59% of the time, a coin flip by three months. Not advice; check the "
    f"filings. Full list + method: {LIST_URL}. Built with an open-source app I made with AI help."
)

_SIDEPROJECT_COMMENT = (
    "What it does: charts and technicals, fundamentals and earnings, insider buying scanned from every SEC "
    "Form 4, the IPO pipeline with each projected date's EDGAR basis, news with a local-AI summary, portfolio "
    "import from a broker CSV. No account, data stays local. It started from an AGPL open-source codebase and "
    "was rebuilt heavily with AI coding assistants — happy to talk about what that was like. Weekly "
    f"insider-buying list: {wb.SITE_URL}/"
)

TARGETS = [
    {
        "id": "reddit-algotrading", "network": "reddit", "community": "r/algotrading", "kind": "text",
        "title": "I backtested \"insider cluster buying\" on a year of SEC Form 4 filings — +1.8% median "
                 "excess at 1 month, gone by 3",
        "body": f"""I wanted to know if the classic "several insiders buying at once" signal holds up when you only use what was knowable on the filing date, so I measured it on every Form 4 filed July 2025 – June 2026 (SEC's quarterly Insider Transactions data sets).

Definitions (point in time, no hindsight):
- Scorable buy = code P, open market, not a 10% owner, not a 10b5-1 plan trade.
- Cluster = a scorable buy filed while a *different* insider's scorable buy at the same company was already on file within the trailing 30 days. The first buy of a run counts as a single buy, because nobody knew a second would follow.
- Return = filing-date close to close 7/30/91 calendar days later, minus SPY.

Results (excess over SPY, median / hit rate):

| | events | 1m | 3m |
|---|---:|---|---|
| cluster buy | 1,404 | +1.8% / 59% | −0.2% / 50% |
| single buyer | 3,291 | +1.2% / 55% | +0.3% / 51% |
| top-tercile $ value | 1,564 | +2.2% / 58% | +0.2% / 51% |
| 10% owner buys | 2,165 | −0.1% / 49% | −3.0% / 43% |
| 10b5-1 plan buys | 192 | −1.6% / 44% | −2.5% / 44% |

Takeaways: a small one-month edge, mostly not from "clustering" (singles do nearly as well); it fades by three months; 10% owners and plan trades are worse than nothing. Not corrected for market cap, costs or overlapping events — tell me what else you'd control for.

The scanner and the study script are part of an open-source (AGPL) desktop research app I've been building, with a lot of help from AI coding tools — methodology write-up and code: {REPO_URL} (see {STUDY_PATH}). Free, no account.""",
        "notes": [
            "Flair: a \"Discussion\"-style one.",
            "The only finance sub with a written open-source allowance: non-profit / open-source projects may "
            "post ONE GitHub link. Keep it to the one in the last paragraph.",
            "Lead with the method, not the product.",
        ],
    },
    {
        "id": "reddit-valueinvesting", "network": "reddit", "community": "r/ValueInvesting", "kind": "text",
        "title": "Free weekly list of companies where 2+ insiders bought on the open market (from SEC Form 4), "
                 "with the historical base rate stated",
        "body": f"""Each week I publish the companies where two or more insiders (officers and directors; not 10% funds, not pre-scheduled 10b5-1 plan trades) reported open-market purchases in the last 7 days of Form 4 filings, sorted by dollars. It's mechanical — no picks, no ratings — and every list carries the base rate from my own backtest, including the unflattering part: the one-month edge (median +1.8% vs SPY, 59% of the time) is gone by three months, and a single insider's buy did nearly as well.

This week's list: {LIST_URL}

I use it as a starting point for reading filings, not as a signal. It comes out of a free, open-source desktop research app I've built (with substantial AI assistance) — the same scan runs live in it. Feedback on what would make the list more useful for fundamental work is very welcome.""",
        "notes": ["Flair: \"Tools\" or \"Discussion\", if offered."],
    },
    {
        "id": "reddit-stockmarket", "network": "reddit", "community": "r/StockMarket", "kind": "text",
        "title": "Which companies had several insiders buying their own stock this week? (Form 4 data, "
                 "method inside)",
        "body": f"{TABLE}\n\n{_STOCKMARKET_FOOTER}",
        "rows": 10,
        "notes": [
            "Flair: \"Discussion\" or \"Education\".",
            "Data inline, one link. The table is this week's top 10 as published.",
            "AutoModerator filters new / low-karma accounts here.",
        ],
    },
    {
        "id": "reddit-smallstreetbets", "network": "reddit", "community": "r/smallstreetbets", "kind": "text",
        "title": "Which companies had several insiders buying their own stock this week? (Form 4 data, "
                 "method inside)",
        "body": f"{TABLE}\n\n{_STOCKMARKET_FOOTER}",
        "rows": 25,
        "notes": [
            "Flair: \"DD\" or \"Discussion\".",
            "Restrict the table to companies under ~$2B market cap: look each one up (the list has no market "
            "cap column), delete the larger ones, and say so in the first line. The table holds every "
            "published row to choose from.",
            "Reword it from the r/StockMarket post — never cross-post the same text.",
        ],
    },
    {
        "id": "reddit-sideproject", "network": "reddit", "community": "r/SideProject", "kind": "link",
        "title": "finterm — a free, open-source desktop research terminal for investors (Qt/C++, "
                 "Linux/Windows/macOS)",
        "url": REPO_URL,
        "first_comment": _SIDEPROJECT_COMMENT,
        "notes": [
            "Link post to the GitHub repo; add the comment right after posting.",
            "Shares its draft with r/coolgithubprojects — reword one of them (never cross-post the same text).",
        ],
    },
    {
        "id": "reddit-coolgithubprojects", "network": "reddit", "community": "r/coolgithubprojects",
        "kind": "link",
        "title": "finterm — a free, open-source desktop research terminal for investors (Qt/C++, "
                 "Linux/Windows/macOS)",
        "url": REPO_URL,
        "first_comment": _SIDEPROJECT_COMMENT,
        "notes": [
            "Link post to the GitHub repo; add the comment right after posting.",
            "Shares its draft with r/SideProject — reword one of them (never cross-post the same text).",
        ],
    },
    {
        "id": "hn-show", "network": "hackernews", "community": "Show HN", "kind": "link",
        # HN cuts titles at 80 characters.
        "title": "Show HN: Finterm – open-source research terminal; insider-buy backtest inside",
        "url": REPO_URL,
        "first_comment": f"""I've been building a desktop research terminal (Qt6/C++, Python for data) for my own investing and opened it up. The part I'd most like feedback on: it scans every SEC Form 4 as it's filed, and I backtested the "several insiders buying" signal point-in-time on a year of filings — small 1-month edge (+1.8% median vs SPY), gone at 3 months, and 10% owners/10b5-1 plans are negative. Write-up: {STUDY_PATH}.

Everything runs locally; the optional AI features can use a local model. Much of the code was written with AI assistants, which I reviewed and tested (there's a large ctest suite). Free installers for Linux/Windows/macOS. A weekly page publishes the insider list: {wb.SITE_URL}/""",
        "notes": [
            "Once, on a weekday morning US time.",
            "Post the first comment immediately.",
        ],
    },
]

TARGET_IDS = {t["id"] for t in TARGETS}


def target(target_id):
    return next((t for t in TARGETS if t["id"] == target_id), None)


def reddit_sub(community):
    return community[2:] if community.startswith("r/") else community


def rows_table(rows, limit):
    """This week's rows as a Reddit Markdown table (EDGAR names escaped)."""
    lines = ["| Symbol | Company | Insiders | Value | Trade dates |", "|---|---|---:|---:|---|"]
    for r in rows[:limit]:
        first, last = r.get("first_trade") or "", r.get("last_trade") or ""
        dates = last if not first or first == last else f"{first} – {last}"
        value = r.get("value")
        lines.append("| {} | {} | {} | {} | {} |".format(
            wb.md_escape(f"${r['symbol']}" if r.get("symbol") else "—"), wb.md_escape(r.get("issuer")),
            r.get("insiders", ""), wb.money(value) if isinstance(value, (int, float)) else "—", dates or "—"))
    return "\n".join(lines)


def render(t, issue):
    """@t with its {table} filled from @issue (the published issue.json, or
    None). data_missing: the draft still needs the week's table."""
    out = {k: v for k, v in t.items() if k != "rows"}
    out.setdefault("body", "")
    out.setdefault("url", "")
    out.setdefault("first_comment", "")
    out["needs_issue"] = TABLE in out["body"]
    out["data_missing"] = False
    if out["needs_issue"]:
        rows = (issue or {}).get("rows") or []
        if rows:
            out["body"] = out["body"].replace(TABLE, rows_table(rows, t.get("rows", 10)))
            out["issue"] = issue.get("issue", "")
        else:
            out["body"] = out["body"].replace(TABLE, "[this week's table — load the published issue, then reset "
                                                     "this draft]")
            out["data_missing"] = True
    return out


_REDDIT_HOSTS = {"reddit.com", "www.reddit.com", "old.reddit.com", "new.reddit.com", "sh.reddit.com"}


def validate_post_url(t, url):
    """"" if @url is a post on @t's community, else why not. Mirrors
    SocialModel.h validate_post_url."""
    u = str(url or "").strip()
    try:
        p = urllib.parse.urlsplit(u)
    except ValueError:
        return "That is not a URL."
    if p.scheme != "https" or not p.hostname:
        return "Paste the post's full https:// link."
    host = p.hostname.lower()
    if t["network"] == "reddit":
        if host not in _REDDIT_HOSTS:
            return "That is not a reddit.com link."
        sub = reddit_sub(t["community"]).lower()
        if not p.path.lower().startswith(f"/r/{sub}/"):
            return f"That link is not a post in {t['community']}."
        return ""
    if t["network"] == "hackernews":
        if host != "news.ycombinator.com":
            return "That is not a news.ycombinator.com link."
        q = urllib.parse.parse_qs(p.query)
        if p.path != "/item" or not re.fullmatch(r"\d+", (q.get("id") or [""])[0]):
            return "Paste the post's item link (news.ycombinator.com/item?id=…)."
        return ""
    return "unknown network"


def _markdown():
    out = ["# Launch posts (generated from app/scripts/launch_posts.py)", "", "**Before posting anywhere**", ""]
    out += [f"- {r}" for r in RULES]
    out += ["", "**Skip:** " + "; ".join(f"{c} ({why})" if why else c for c, why in SKIP), ""]
    for t in TARGETS:
        out += ["---", "", f"## {t['community']} — {t['kind']} post", ""]
        out += [f"- {n}" for n in t["notes"]]
        out += ["", f"**Title:** {t['title']}", ""]
        if t.get("url"):
            out += [f"**URL:** {t['url']}", ""]
        if t.get("body"):
            out += ["**Body:**", "", t["body"].replace(TABLE, "(this week's table)"), ""]
        if t.get("first_comment"):
            out += ["**First comment:**", "", t["first_comment"], ""]
    return "\n".join(out)


if __name__ == "__main__":
    print(_markdown())

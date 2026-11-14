# Launch posts — one each, posted by hand

Recurring channel: the website (weekly issue) + X (automatic, `tools/announce.py`).
Reddit and Hacker News get ONE launch post per community, by hand, from the
project owner's own account. Reddit's API needs approval for new apps and most
finance subs remove recurring links, so nothing here is automated.

**Before posting anywhere**

- Read the sub's *About › Rules* in a browser. The rules below were researched
  2026-11-13 but Reddit blocked direct reads; most are second-hand. If a rule
  says no, skip the sub.
- New, low-karma accounts are caught by AutoModerator age/karma filters on the
  big subs (r/stocks, r/investing, r/StockMarket, r/pennystocks). Comment
  genuinely in a sub for a couple of weeks before posting there.
- Disclose AI assistance in every post (the text below does). Being found to
  hide it is fatal in FOSS communities.
- One sub per day; answer every comment for the first few hours. Never
  cross-post the same text; each draft below is written for its sub.
- Never ask for upvotes. Never post the weekly issue as a recurring link.

**Skip:** r/investing (bans self-promotion, permanent ban), r/opensource
(bans AI-assisted projects since Oct 2026; also needs 1-yr-old account and
project), r/quant, r/SecurityAnalysis, r/options, r/linux.

---

## 1. r/algotrading — text post, "Discussion"-style flair, ONE GitHub link

The only finance sub with a written open-source allowance (non-profit / open
source may post one GitHub link). Lead with the method, not the product.

**Title:** I backtested "insider cluster buying" on a year of SEC Form 4 filings — +1.8% median excess at 1 month, gone by 3

**Body:**

I wanted to know if the classic "several insiders buying at once" signal holds
up when you only use what was knowable on the filing date, so I measured it on
every Form 4 filed July 2025 – June 2026 (SEC's quarterly Insider Transactions
data sets).

Definitions (point in time, no hindsight):
- Scorable buy = code P, open market, not a 10% owner, not a 10b5-1 plan trade.
- Cluster = a scorable buy filed while a *different* insider's scorable buy at
  the same company was already on file within the trailing 30 days. The first
  buy of a run counts as a single buy, because nobody knew a second would follow.
- Return = filing-date close to close 7/30/91 calendar days later, minus SPY.

Results (excess over SPY, median / hit rate):

| | events | 1m | 3m |
|---|---:|---|---|
| cluster buy | 1,404 | +1.8% / 59% | −0.2% / 50% |
| single buyer | 3,291 | +1.2% / 55% | +0.3% / 51% |
| top-tercile $ value | 1,564 | +2.2% / 58% | +0.2% / 51% |
| 10% owner buys | 2,165 | −0.1% / 49% | −3.0% / 43% |
| 10b5-1 plan buys | 192 | −1.6% / 44% | −2.5% / 44% |

Takeaways: a small one-month edge, mostly not from "clustering" (singles do
nearly as well); it fades by three months; 10% owners and plan trades are worse
than nothing. Not corrected for market cap, costs or overlapping events — tell
me what else you'd control for.

The scanner and the study script are part of an open-source (AGPL) desktop
research app I've been building, with a lot of help from AI coding tools —
methodology write-up and code: https://github.com/yumima/finterm (see
plans/research/form4_forward_returns_2026-09.md). Free, no account.

---

## 2. r/ValueInvesting — text post, "Tools"/"Discussion" flair if offered

**Title:** Free weekly list of companies where 2+ insiders bought on the open market (from SEC Form 4), with the historical base rate stated

**Body:**

Each week I publish the companies where two or more insiders (officers and
directors; not 10% funds, not pre-scheduled 10b5-1 plan trades) reported
open-market purchases in the last 7 days of Form 4 filings, sorted by dollars.
It's mechanical — no picks, no ratings — and every list carries the base rate
from my own backtest, including the unflattering part: the one-month edge
(median +1.8% vs SPY, 59% of the time) is gone by three months, and a single
insider's buy did nearly as well.

This week's list: https://yumima.github.io/finterm/insider-cluster-buys/

I use it as a starting point for reading filings, not as a signal. It comes
out of a free, open-source desktop research app I've built (with substantial
AI assistance) — the same scan runs live in it. Feedback on what would make
the list more useful for fundamental work is very welcome.

---

## 3. r/StockMarket — text post, flair "Discussion"/"Education"; data inline, one link

**Title:** Which companies had several insiders buying their own stock this week? (Form 4 data, method inside)

**Body:** paste the top 10 rows of this week's table from
https://yumima.github.io/finterm/insider-cluster-buys/ as a Reddit table, then:

> Filter: 2+ different insiders, transaction code P (open market), excluding
> 10% owners and 10b5-1 plans, $50K+ total, filings from the last 7 days.
> History (my backtest, Jul 2025–Jun 2026): such buys beat SPY by a median
> 1.8% over the next month 59% of the time, a coin flip by three months. Not
> advice; check the filings. Full list + method: [link]. Built with an
> open-source app I made with AI help.

---

## 4. r/smallstreetbets — text post, "DD"/"Discussion" flair

Same as r/StockMarket, but restrict the pasted table to companies under ~$2B
market cap (look them up; the list has no market cap column) and say so in the
first line.

---

## 5. r/SideProject and r/coolgithubprojects — link post to the GitHub repo

**Title:** finterm — a free, open-source desktop research terminal for investors (Qt/C++, Linux/Windows/macOS)

**Comment to add right after posting:**

> What it does: charts and technicals, fundamentals and earnings, insider
> buying scanned from every SEC Form 4, the IPO pipeline with each projected
> date's EDGAR basis, news with a local-AI summary, portfolio import from a
> broker CSV. No account, data stays local. It started from an AGPL
> open-source codebase and was rebuilt heavily with AI coding assistants —
> happy to talk about what that was like. Weekly insider-buying list:
> https://yumima.github.io/finterm/

---

## 6. Show HN — once, weekday morning US time

**Title:** Show HN: Finterm – open-source desktop research terminal; insider-buy backtest inside

**URL:** https://github.com/yumima/finterm

**First comment (post immediately):**

> I've been building a desktop research terminal (Qt6/C++, Python for data)
> for my own investing and opened it up. The part I'd most like feedback on:
> it scans every SEC Form 4 as it's filed, and I backtested the "several
> insiders buying" signal point-in-time on a year of filings — small 1-month
> edge (+1.8% median vs SPY), gone at 3 months, and 10% owners/10b5-1 plans
> are negative. Write-up: plans/research/form4_forward_returns_2026-09.md.
>
> Everything runs locally; the optional AI features can use a local model.
> Much of the code was written with AI assistants, which I reviewed and tested
> (there's a large ctest suite). Free installers for Linux/Windows/macOS.
> A weekly page publishes the insider list: https://yumima.github.io/finterm/

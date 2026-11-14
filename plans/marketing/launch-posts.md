# Launch posts — one each, posted by hand

Recurring channel: the website (weekly issue) + X (automatic, `tools/announce.py`).
Reddit and Hacker News get ONE launch post per community, by hand, from the
project owner's own account. Reddit's API needs approval for new apps and most
finance subs remove recurring links, so nothing here is automated.

**The drafts, their rules and the skip list live in one place:
[`app/scripts/launch_posts.py`](../../app/scripts/launch_posts.py).** Edit them
there. The app's SOCIAL screen shows them (Reddit / Hacker News in the composer):
it fills r/StockMarket's table from the published issue, copies title / body /
first comment, opens the submit page, and records each post once you paste its
link (`<data_root>/social/announced.json`, key `launch`) — it never submits
anything. To read them all as Markdown:

    python3 app/scripts/launch_posts.py

Communities covered: r/algotrading, r/ValueInvesting, r/StockMarket,
r/smallstreetbets, r/SideProject, r/coolgithubprojects, Show HN.

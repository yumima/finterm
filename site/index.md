---
title: finterm — free, open-source research terminal for investors
description: Free desktop research terminal for Linux, Windows and macOS. Equity research, insider buying from SEC filings, IPO pipeline, portfolio import from your broker, local AI. No account, no subscription.
permalink: /
---
<section class="hero">
  <h1>Research stocks like a terminal user. Pay nothing.</h1>
  <p class="lede">
    finterm is a free, open-source desktop research terminal for Linux, Windows and macOS: charts and
    technicals, fundamentals, earnings, insider buying straight from SEC filings, the IPO pipeline, news,
    and your own portfolio imported from your broker's CSV. Its AI runs on your machine if you want it to.
    No account, no subscription; your data stays on your computer.
  </p>
  <p>
    <a class="button" href="{{ site.download_url }}">Download for free</a>
    <a class="button ghost" href="{{ '/insider-cluster-buys/' | relative_url }}">See this week's insider buying</a>
  </p>
</section>

{% assign shots = site.static_files | where_exp: "f", "f.path contains '/assets/screens/'" | sort: "path" %}
{% if shots.size > 0 %}
<section class="shots">
{% for f in shots %}
  <figure><img src="{{ f.path | relative_url }}" alt="finterm's {{ f.basename | replace: '-', ' ' }} screen" loading="lazy"><figcaption>{{ f.basename | replace: '-', ' ' | capitalize }}</figcaption></figure>
{% endfor %}
</section>
{% endif %}

## What's in it

- **Equity research** — price, technicals with a rating you can audit, fundamentals, earnings history, extended-hours moves (pre-market, after-hours, overnight) and each earnings call playable in the app.
- **Company report** — one investor report per company from filings, estimates, prices and news: what changed, segments, valuation against its own past, risks; export to PDF.
- **Markets** — market conditions for every asset class on pages of widgets you arrange: the Treasury curve, credit stress, valuation and correlation; futures curves for 57 contracts with spreads, trader positioning and a paper account with working stops and targets.
- **Fund moves** — the largest stake changes by every 13F filer, corrected for splits and CUSIP changes and linked to the filing; 5% stakes (13D / 13G) as they are filed.
- **Insider buying** — every SEC Form 4 scanned as it lands; clusters of insiders buying their own stock surface on their own.
- **Private market** — marketplace prices and funding rounds for private companies, and IPO deals from filing to pricing with the SEC filing stage each projected date rests on.
- **Your portfolio** — import holdings from a broker CSV; nothing is uploaded anywhere.
- **Quant Lab, backtesting, screeners, news, maps** — and more screens, all in one window.
- **Bring your own data** — works on free data out of the box; plug in a Tiingo, Polygon, Alpaca, Finnhub or FMP key for prices if you have one.

## The weekly brief

Every week finterm publishes the companies where two or more insiders reported open-market purchases,
straight from SEC filings, with the method and how such clusters have done before.
[Read the latest]({{ '/insider-cluster-buys/' | relative_url }}) · [all issues]({{ '/brief/' | relative_url }}) ·
[RSS]({{ '/feed.xml' | relative_url }})

{% assign latest = site.posts.first %}
{% if latest %}Latest: [{{ latest.title }}]({{ latest.url | relative_url }}){% endif %}

---
title: finterm
description: Comprehensive, Bloomberg-style financial research terminal that runs on your own computer. Markets, company research, SEC ownership filings, futures, IPOs, news, screeners, portfolio, quant experiments, strategy building, optional AI. Free and open source, for Linux, Windows and macOS.
permalink: /
---
<section class="hero">
  <h1>Comprehensive financial research terminal</h1>
  <p class="lede">
    A Bloomberg-style terminal for markets, company research, SEC filings, futures, IPOs, news and portfolio,
    with quant experiments, strategy building and optional AI. It runs on your own computer and stores your
    portfolio, keys and settings there: no online account, no telemetry. Free and open source, for Linux,
    Windows and macOS.
  </p>
  <p>
    <a class="button" href="{{ site.download_url }}">Download</a>
    <a class="button ghost" href="{{ '/brief/' | relative_url }}">Weekly brief</a>
  </p>
  {% include counter.html %}
</section>

{% assign shots = site.static_files | where_exp: "f", "f.path contains '/assets/screens/'" | sort: "path" %}
{% if shots.size > 0 %}
<section class="shots">
{% for f in shots %}
  <figure><img src="{{ f.path | relative_url }}" alt="finterm's {{ f.basename | replace: '-', ' ' }} screen" loading="lazy"><figcaption>{{ f.basename | replace: '-', ' ' | capitalize }}</figcaption></figure>
{% endfor %}
</section>
{% endif %}

## Features

- **Equity research** — charts, technical rating with its components, fundamentals, earnings history with call audio, pre-market, after-hours and overnight moves.
- **Company report** — quarterly report from filings, estimates, prices and news: segments, valuation against its own history, risks. PDF and HTML export.
- **Markets** — configurable widget pages: Treasury curve, credit, volatility, valuation, cross-asset correlation.
- **Futures** — forward curves for 55 contracts, calendar spreads, roll yield, CFTC positioning, paper trading with stops and targets.
- **Ownership** — Form 4 insider trades with cluster detection; 13F changes adjusted for splits and CUSIP changes; 13D/13G stakes.
- **Private market** — secondary-market prices, funding rounds, and the IPO calendar by SEC filing stage.
- **Portfolio** — multiple accounts, performance, broker CSV/JSON import and export. Stored locally.
- **Quant experiments** — Quant Lab: factor discovery and evaluation, machine-learning models, forecasting, portfolio optimization.
- **Strategy building** — entry and exit conditions, stops and targets; backtests, parameter optimization, walk-forward tests.
- **Screeners, news, maps.**
- **AI** — optional; local OpenAI-compatible servers (Ollama, vLLM, llama.cpp, LM Studio) or cloud providers.
- **Data** — public sources by default; optional Tiingo, Polygon, Alpaca, Finnhub or FMP keys.

## Weekly brief

One page: the market at a glance, markets and the economy, the week ahead, and stocks — insider buying (Form 4), fund moves (13F), screen leaders with their backtest records.
[All issues]({{ '/brief/' | relative_url }}) · [insider buying, full list]({{ '/insider-cluster-buys/' | relative_url }}) ·
[RSS]({{ '/feed.xml' | relative_url }})

{% assign latest = site.posts.first %}
{% if latest %}Latest issue: [{{ latest.title }}]({{ latest.url | relative_url }}){% endif %}

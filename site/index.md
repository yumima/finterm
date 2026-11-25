---
title: finterm
description: Open-source desktop financial research terminal for Linux, Windows and macOS. Equity research, SEC ownership filings, futures, IPO pipeline, portfolio, local or cloud AI. Local-first, no telemetry.
permalink: /
---
<section class="hero">
  <h1>Desktop financial research terminal</h1>
  <p class="lede">
    Native C++20/Qt 6 application for Linux, Windows and macOS with a Python data layer. Local-first: no
    cloud account, no backend, no telemetry. Free, AGPL-3.0.
  </p>
  <p>
    <a class="button" href="{{ site.download_url }}">Download</a>
    <a class="button ghost" href="{{ '/insider-cluster-buys/' | relative_url }}">Weekly insider buying</a>
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
- **Quant Lab, backtesting, screeners, news, maps.**
- **AI** — optional; local OpenAI-compatible servers (Ollama, vLLM, llama.cpp, LM Studio) or cloud providers.
- **Data** — public sources by default; optional Tiingo, Polygon, Alpaca, Finnhub or FMP keys.

## Weekly brief

Companies where two or more insiders reported open-market purchases in SEC Form 4 filings, with method and historical results.
[Latest]({{ '/insider-cluster-buys/' | relative_url }}) · [all issues]({{ '/brief/' | relative_url }}) ·
[RSS]({{ '/feed.xml' | relative_url }})

{% assign latest = site.posts.first %}
{% if latest %}Latest issue: [{{ latest.title }}]({{ latest.url | relative_url }}){% endif %}

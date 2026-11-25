---
title: Track record
description: Every weekly stance as it was published, and what the S&P 500 did after it.
permalink: /track-record/
---
{% assign t = site.data.calls %}{% assign n = t.calls | size %}
The stance is one rule: full risk above the S&P 500's 200-day average, half below it. Each week's first published stance is listed as it was; a later edition that differs is shown beside it. Everything is measured from the first close after publication.

{% if n == 0 or t == nil %}
_No stance has been published yet._
{% else %}
**{{ n }} stance{% if n != 1 %}s{% endif %} since {{ t.calls.first.published | slice: 0, 10 | date: "%B %-d, %Y" }}.**{% if t.total.rule %} The rule {{ t.total.rule }}%, always invested {{ t.total.always }}%, to {{ t.total.to | date: "%B %-d, %Y" }} (price only; the idle half earns nothing here).{% endif %}

| Week | Published | Stance | A week later | A quarter later | 10% fall within it |
|---|---|---|---|---|---|
{% for c in t.calls reversed %}| [{{ c.issue }}]({{ c.url }}) | {{ c.published | slice: 0, 10 | date: "%b %-d" }} | {{ c.label }}: {% if c.stance == 1 %}full{% else %}half{% endif %} risk{% for r in c.revised %}; from {{ r.published | slice: 0, 10 | date: "%b %-d" }} {{ r.label }}{% endfor %} | {{ c.week | default: "pending" }} | {{ c.quarter | default: "pending" }} | {{ c.fall | default: "pending" }} |
{% endfor %}
{% endif %}

A handful of weeks says little. The rule's own history, 1990 to 2026, with what it gave up and why: [method and backtest](https://github.com/yumima/finterm/blob/main/plans/research/market_regime/README.md). It is a risk rule, not a return rule.

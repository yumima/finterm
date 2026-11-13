# Volkswagen Squeeze — October 2008

For 48 hours in October 2008, **Volkswagen** was briefly the **largest market-cap company in the world** — surpassing Exxon, Microsoft, and Apple combined. The stock spiked from €210 to €1,005 in two days as a short squeeze cornered hedge funds with no shares to deliver.

The case is the canonical study of **what happens when float gets cornered while shorts are heavily piled on** — and why every short-seller checks ownership concentration religiously now.

## The setup

Throughout 2008, hedge funds had been **shorting Volkswagen** on the thesis that:
- VW was an old-line auto in a recession
- VW preferred shares (the truly liquid ones) traded at a premium to the common
- The "right" relative-value trade was long preferred / short common
- Net short position grew to ~13% of VW common float

What hedge funds didn't know: **Porsche had been quietly accumulating VW common stock and call options** for years, intending eventually to take over.

## The corner

By late October 2008, here was the actual ownership:

```
Porsche:                       42.6% (open and disclosed)
Porsche options:               31.5% (UNDISCLOSED — held via cash-settled forwards)
Lower Saxony state government:  20.2% (long-term holder, won't sell)

Total locked-up:               ~94.3%

Float available to trade:       ~5.7%
Short interest as % of float:  >220%
```

The float was effectively cornered. **Shorts couldn't possibly cover** because there weren't enough shares.

## The squeeze (Oct 26–28, 2008)

- **Sunday Oct 26**: Porsche announced via press release that they had effectively control of 74.1% of VW (42.6% direct + 31.5% via options).
- **Monday Oct 27 morning**: Stock opens at €348 (vs €210 close Friday)
- **Monday Oct 27 close**: €520
- **Tuesday Oct 28 intraday peak**: **€1,005** — a 478% gain in two days from Friday's close
- **Tuesday Oct 28 close**: €945

Hedge funds with short positions faced theoretically unlimited losses — they simply could not buy back shares at any reasonable price.

## The damage

- Hedge funds lost an estimated **€20–30 billion combined** over those days
- Several mid-tier funds were essentially wiped out
- One investor, **Adolf Merckle** (German billionaire), reportedly lost ~€500M and **committed suicide** in January 2009 — the squeeze was a major contributing factor
- Porsche made paper gains of ~€30B+ on its position (but couldn't realize easily)

## The unwind

VW's "real" market cap was nowhere near €295B (the peak). Once the squeeze exhausted itself:
- Stock fell from €945 to €270 within months
- Porsche's gains evaporated; their leveraged position became unsustainable
- **Volkswagen ended up acquiring Porsche** (the reverse of the original plan)
- Porsche family faced years of regulatory and shareholder lawsuits

## Concepts illustrated

- [[float|Float]] — the actual tradable share count, not market cap
- [[market-cap|Market Cap]] — what looked like a €280B company was structurally <€20B in tradable equity
- [[liquidity|Liquidity]] — the most liquid stocks can become illiquid in cornered markets

## Lessons

1. **Always check ownership concentration before shorting.** If <30% of shares are truly tradable, even modest short interest creates squeeze risk.
2. **Disclosed ownership ≠ true ownership.** Porsche's 31.5% in cash-settled options was undisclosed (German law at the time didn't require it). Always assume the disclosed number understates real concentration.
3. **Fundamentally correct theses can lose 5× in days** if the trade structure has cornering risk.
4. **Short interest > 50% of float** is a flashing red light. Volkswagen's was 220%+.
5. **Regulators react after the fact.** German law was tightened in 2009 to require option-position disclosures.

## How it could have been avoided

- **Limit short positions** in any single name to a few % of available float
- **Monitor ownership filings** religiously, including 13-D/13-G equivalents in non-US markets
- **Use put options instead of cash shorts** to cap downside (max loss = premium paid)
- **Bilateral shorts only with locate confirmation** that doesn't disappear in stress

## Comparable corner attempts (mostly failed)

- **Hunt brothers / silver (1980)** — see [[case-hunt-silver-1980]]
- **Salomon Brothers / 2-yr Treasury (1991)** — Paul Mozer cornered ~94%; led to Buffett rescue
- **Citadel / various corners** — modern algorithmic corners last hours, not days
- **GameStop January 2021** — see [[case-gamestop-2021|the modern retail-driven version]]

## Read more

- Bloomberg's contemporaneous coverage (Oct 2008)
- *Faux Amis: The Volkswagen Squeeze and the Porsche Implosion* — academic case study
- BaFin (German regulator) report on the disclosure issue

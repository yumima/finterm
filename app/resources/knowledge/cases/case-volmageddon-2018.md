# Volmageddon — February 5, 2018

In a single trading day, **XIV** (the Credit Suisse "VelocityShares Daily Inverse VIX Short-Term ETN") **lost 96% of its value** — going from $115 to under $5. The product was terminated within days, and **$2 billion of investor money was vaporized** in 24 hours.

The case is the canonical study of **leveraged inverse-volatility products and the feedback loops they create**.

## The setup (2017)

2017 was the calmest year for US equities in decades:
- VIX averaged ~11 (vs long-run avg ~19)
- Realized S&P vol ~6.5% (vs long-run ~16%)
- "Selling vol" was viewed as the trade of the year
- XIV (and similar product SVXY) became wildly popular
- AUM in short-vol ETPs grew from ~$500M (2016) to **~$3B+ (early 2018)**

Retail investors, hedge funds, and even pension allocators piled in. The narrative: "VIX always mean-reverts down. Selling vol is the new carry trade."

## How XIV worked

XIV held a **rolling short position in front-month VIX futures**. Each day:

```
1. XIV borrows VIX futures (effectively short)
2. As VIX futures decline (in normal conditions), XIV gains
3. End of day: XIV computes net asset value, marks to market
```

In contango (most days), XIV gained from roll yield + falling vol. In 2017, XIV returned **+187%**.

But the prospectus contained a critical clause: **if XIV's daily loss exceeded 80%, Credit Suisse could accelerate (terminate) the product**.

## The trigger

**February 2, 2018** (Friday): A strong jobs report sparked rate-hike fears. S&P 500 fell 2%, VIX rose from 13 to 17.

**February 5, 2018** (Monday morning): S&P opens weak; VIX climbs steadily.

**February 5 close**: S&P 500 −4.1% (worst single day since 2011). VIX closes at 37.32.

**February 5 after-hours**: The mechanical short-vol unwind begins.

## The cascade

XIV (and similar products) had to **buy back VIX futures to maintain their target exposure** as VIX rose. The math:

```
XIV NAV at noon: ~$80 (down from $115 Friday)
                
By 4:00 PM:      XIV NAV ~$25 (-78%)
                
After hours:     VIX futures continue to climb
                XIV must buy more VIX futures (to maintain leverage ratio)
                Their buying pushes VIX even higher
                Which forces them to buy MORE
                
By 4:30 PM:      VIX at 50
                XIV NAV at ~$5 (-96%)
                
Tuesday morning: Credit Suisse announces XIV will accelerate (terminate)
                Final settlement value: $5.99
```

In one ~6-hour window, $2B of investor capital vaporized.

## The mechanics — feedback loop

The vol-selling unwind worked the same as 1987's portfolio insurance:

```
1. VIX rises (small initial trigger)
2. Short-vol products must buy VIX futures to maintain target leverage
3. Their buying pushes VIX futures higher
4. Higher VIX futures = bigger XIV losses
5. Bigger losses force more buying
6. Goto 2
```

**Each iteration accelerated the next.** XIV alone had to buy ~$200M of VIX futures in a single afternoon — into a market that had no sellers.

## The damage

- **XIV terminated**: $2B+ of investor capital wiped out
- **SVXY** (similar product, lower leverage): lost 90%+; survived but redesigned
- **Several short-vol hedge funds liquidated**
- **VIX itself spiked to 50** intraday — the highest since the 2015 China shock
- S&P 500 entered a 10% correction over subsequent weeks

## What went wrong

1. **Crowded short-vol positioning** — everyone was on the same side of the trade
2. **Mechanical leverage products amplified** the unwind
3. **Inverse ETN structure with acceleration clauses** wasn't well understood by retail
4. **VIX futures market** wasn't deep enough to absorb the forced buying
5. **Daily-rebalancing math** is asymmetric in extreme moves (path-dependent)

## Concepts illustrated

- [[vix|VIX]] — the underlying instrument; not directly tradable
- [[implied-volatility|Implied Volatility]] — how IV expansion crushes short-vol positions
- [[volatility|Volatility]] — vol of vol matters at extremes
- [[liquidity|Liquidity]] — VIX futures market couldn't absorb the demand

## Lessons

1. **Inverse-leveraged ETPs decay in volatile markets** — the math is unforgiving over time.
2. **"Acceleration clauses"** in ETN prospectuses are real risks; read them.
3. **Crowded trades unwind violently.** When everyone is short vol, no one can buy when it spikes.
4. **Mechanical strategies create feedback loops** under stress.
5. **VIX is not investable directly** — only its derivatives are, and they have their own dynamics.

## Modern echoes

The pattern repeated almost exactly:

- **August 2024** — Yen-carry unwind: short-yen carry trade was a similar widespread trade. When BOJ hinted at rate hikes, the unwind cascaded; VIX hit 65 intraday on Aug 5
- **October 2022** — UK gilt blowup: pension LDI strategies (quasi-leveraged short-vol on rates) had to unwind as rates spiked

## Aftermath

- XIV was discontinued; Credit Suisse's image took a hit
- SEC + CFTC studied the event but no major rule changes
- Inverse-vol products restructured with better safeguards
- Retail interest in short-vol ETPs largely vanished
- Many vol-selling hedge funds added explicit tail hedges

## Read more

- Bloomberg's "Volmageddon Anniversary" coverage
- Various academic papers on path-dependence of leveraged ETPs
- *The Crash of XIV* — postmortems from quant blogs (Quantocracy, etc.)

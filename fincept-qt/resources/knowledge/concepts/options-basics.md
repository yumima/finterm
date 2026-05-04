# Options — The Basics

An **option** is a contract giving the buyer the right (but not the obligation) to buy or sell an asset at a fixed price ("strike") on or before a fixed date ("expiration").

## The four building blocks

| Position | What it means | Max profit | Max loss |
|---|---|---|---|
| **Long Call** | Right to buy at strike | Unlimited (price rises) | Premium paid |
| **Long Put** | Right to sell at strike | Strike − 0 (price falls to 0) | Premium paid |
| **Short Call** | Obligation to sell if exercised | Premium received | **Unlimited** |
| **Short Put** | Obligation to buy if exercised | Premium received | Strike − 0 |

Every options strategy is a combination of these four primitives.

## Worked example — long call

```
Stock:        AAPL @ $175
Buy 30-day call at $180 strike for $2.50/share ($250 per contract)

Breakeven at expiry:  180 + 2.50 = $182.50
At $200 expiry:       Profit = (200 − 180) − 2.50 = $17.50/share = $1,750/contract
At $182.50 expiry:    Breakeven; lose nothing
At $182 expiry:       Lose $0.50/share = $50/contract
At ≤ $180 expiry:     Lose entire $250 premium
```

The asymmetry — capped downside, large upside — is the appeal. The cost is theta decay every day you hold and vega sensitivity if IV drops.

## Anatomy of a quote

```
AAPL 230 Call,  Exp 2026-06-19,  Bid 4.20  Ask 4.35  IV 28%  OI 12,450
```

- **Strike (230)**: the price at which the contract can be exercised
- **Expiration (2026-06-19)**: the last day the option is valid
- **Bid/Ask**: what buyers will pay vs. what sellers want
- **IV ([[implied-volatility|implied volatility]])**: the market's view of future price movement
- **OI ([[open-interest|open interest]])**: how many contracts are alive — a liquidity measure

One US equity option contract = **100 shares**. A $4.20 quote means $420 per contract.

## The four things that move an option's price (Greeks)

| Greek | What it measures | Long position |
|---|---|---|
| **Delta** | Δ option price per $1 of underlying | Long calls: 0 → 1; long puts: −1 → 0 |
| **Gamma** | Rate of change of delta | Long options have positive gamma (delta moves with you) |
| **Theta** | Daily time decay | Long options bleed (negative theta); short options collect |
| **Vega** | Δ option price per 1% change in IV | Long options long vega; short options short |
| **Rho** | Δ option price per 1% interest rate change | Usually small except on long-dated |

Long options are **long gamma, long vega, short theta**: you benefit from movement and rising IV, you bleed time. Short options are the mirror.

## Common multi-leg strategies

| Strategy | Construction | When to use |
|---|---|---|
| **Vertical spread** | Buy strike A, sell strike B (same expiry) | Defined risk + reward; directional view |
| **Iron condor** | Sell put spread + sell call spread | Range-bound thesis; collect premium |
| **Calendar spread** | Sell near-month, buy further-month (same strike) | Long vega in the back month, short theta in the front |
| **Straddle** | Buy call + buy put (same strike, expiry) | Big move expected, direction unclear |
| **Strangle** | Buy OTM call + buy OTM put | Cheaper than straddle; needs bigger move |
| **Covered call** | Own 100 shares + sell call | Income on existing position |
| **Cash-secured put** | Sell put + hold cash to buy if assigned | Acquire stock at discount; capture premium |

## Decision rules for beginners

- **First trade**: paper-trade long calls and puts to feel how Greeks behave.
- **Sizing**: never risk more than 0.5–2% of portfolio on a single options trade.
- **Liquidity check**: OI > 500 minimum, bid-ask spread < 5% of mid.
- **Earnings**: avoid buying premium right before — vol crush usually wins.
- **Avoid naked anything** until you've held a position through an earnings report and understand the P&L swings.

## Key risk warning

A short call has **theoretically unlimited loss** — if the stock spikes, you must deliver shares at the strike no matter how high the market goes. Naked options sellers must understand this before placing the trade. See playbooks for sizing rules.

## Famous options blowups

| Event | What happened |
|---|---|
| Niederhoffer 1997 | Short SPX puts → Asian crisis → fund wiped out |
| LTCM 1998 | Short vol via dividend swaps → leveraged blowup |
| GameStop Jan 2021 | Short calls → gamma squeeze → Melvin loses 53% |
| Volmageddon Feb 2018 | Short XIV (inverse VIX ETF) → 96% loss in one day |

The pattern: short premium without proper sizing or hedging eventually meets a tail event.

## Where to start

If you're new:
1. **Paper-trade** long calls and puts for at least a month.
2. Read the OCC's *Characteristics & Risks of Standardized Options* — boring but mandatory.
3. Trade one structure at a time until each Greek's behavior is intuitive.
4. Keep a journal — what you expected vs. what happened.

# Transaction-Cost Modelling

The single most common reason a beautiful backtest dies in live trading is that costs were modelled as zero, or as a flat commission per share. Real costs have three components — **spread, market impact, and fees** — and the relative weight of each depends on the strategy's turnover, holding period, and average trade size.

This entry breaks each down, gives realistic numbers for common scenarios, and explains how to estimate capacity (the AUM at which the strategy stops working).

## Cost components

### 1. Bid-ask spread

The simplest component. If you buy at the ask and sell at the bid, you pay the full spread per round-trip. For liquid US equities (mega-cap names), this is ~1 bp; for liquid ETFs ~1 bp; for mid-cap stocks 5–20 bps; for small-cap stocks 30–100+ bps; for less liquid corporate bonds and emerging market FX, hundreds of bps.

Always model spread as the half-spread on each leg (entry pays half, exit pays half).

```
Spread cost (per round trip, in bps) = 2 × (ask − bid) / mid × 10000 / 2
                                     = (ask − bid) / mid × 10000
```

### 2. Market impact

When you trade size, your own order pushes the price against you. A 100-share market order in AAPL has negligible impact; a $100M institutional order moves the market by 10–50 bps before completion.

The standard model — Almgren-Chriss / square-root law — says impact scales with the square root of trade size:

```
Impact (bps) ≈ σ × √(Q / V)
```

Where σ is daily volatility, Q is your trade size in shares, and V is the day's expected volume. For a stock with σ = 200 bps daily vol and trading 1% of daily volume, impact ≈ 200 × √0.01 = 20 bps.

Crucial implication: **impact is non-linear in size**. Doubling your trade size more than doubles your cost as a % of the trade (you trade through deeper book layers). This is what limits strategy capacity.

### 3. Fees, taxes, financing

- **Commissions.** Retail US: $0 at major brokers. Institutional: ~$0.001/share, or ~5 bps for FX. Smaller markets: significantly higher.
- **SEC fees.** ~$0.27 per $10,000 sold (US equities). Trivial individually, matters at high turnover.
- **Exchange / clearing fees.** Mostly passed through; a few bps of notional for futures and options.
- **Borrow / short rebate.** For short positions in hard-to-borrow names, borrow fees can reach 50–200% annualised. For easy-to-borrow names, ~25 bps annualised.
- **Financing.** Long leveraged positions pay margin interest (Fed funds + 0–300 bps depending on broker). Short positions earn a rebate on the cash from the short sale (Fed funds minus the borrow fee).
- **Taxes.** Short-term capital gains in the US: ordinary income rates (top 37%). Long-term (>1 year hold): 0–20%. High-turnover strategies are extremely tax-inefficient in taxable accounts. Wash-sale rules limit harvesting losses within 30 days.

## Putting it together — a worked example

A US mid-cap momentum strategy. Average holding period: 30 days. Turnover: ~12× per year. Universe: stocks with avg daily volume > $10M.

Per-round-trip costs:

| Component | Cost (bps) |
|---|---|
| Spread (half each side, 6 bps avg) | 12 |
| Impact (10 bps avg per side, 100k positions in 5M ADV stocks) | 20 |
| Commission ($0.003 × 10000 shares / $300k notional) | 1 |
| SEC + exchange fees | 1 |
| **Total per round trip** | **34** |

Annual cost = 34 bps × 12 round trips = **4.1 %**.

If the gross strategy Sharpe is 1.0 with 10 % volatility and 10 % return, the **net** return is 5.9 %, Sharpe ≈ 0.6. Cost has cut the realised Sharpe by 40 %.

If the strategy size triples (trading 30 % of daily volume per position), impact roughly doubles, total cost climbs to 6+ %, and the live Sharpe drops below 0.4 — the strategy is barely worth running net of fees.

## Capacity — what AUM kills the strategy

Capacity is the AUM at which transaction costs eat the entire alpha. It scales with **how liquid the universe is** and **how often the strategy trades**.

Rough orders of magnitude (US equity long-short, 100 bps gross alpha):

| Universe | Avg ADV | Capacity |
|---|---|---|
| S&P 100 mega-cap | $5B | $100B+ |
| Russell 1000 | $200M | $5–20B |
| Russell 2000 | $20M | $200M–1B |
| Russell Microcap | $2M | $20–100M |

Capacity is fundamentally why Medallion stays small (signals exploit short-horizon micro-inefficiencies that capacity-cap at single-digit billions) and why AQR can run $100B+ (factor strategies on liquid universes with weekly rebalancing). The same alpha at higher AUM is a lower Sharpe.

## What modelling actually looks like

For a backtest to be credible, model these in this order:

1. **Spread**: per-asset, time-varying if possible. Use TAQ data for US equities; for non-US or smaller markets, use a conservative constant scaled to liquidity tier.
2. **Impact**: square-root model with the strategy's empirical per-trade size as % of ADV. The constant σ × √(Q/V) has a coefficient typically 0.1–0.5; pick the higher end for stressed regimes.
3. **Commission + fees**: fixed per-share or per-notional, depending on broker.
4. **Borrow + financing**: track per-position for shorts; use Fed funds for longs.
5. **Crash / liquidity stress**: stress all of the above by 3–5× for a "what if markets are stressed when I'm trying to exit?" path.

Run the full strategy three times: at 1× modelled costs, 2× modelled costs, and "broker's flat fee" pretend-no-costs. If the strategy is positive at 2× and dramatically positive at 1×, it's robust. If it's negative at 2× costs, the live version will almost certainly be negative.

## Strategies most sensitive to costs

In rough order:

- **HFT / market-making.** Spread *is* the strategy; if you don't have rebates, you don't have a strategy.
- **Stat-arb / pairs trading.** High turnover, modest per-trade edge, capacity-limited.
- **Daily momentum / mean-reversion.** Sensitive to slippage on signal turnover.
- **Factor portfolios (monthly).** Modest sensitivity; costs are 50–100 bps per year vs gross alpha of 2–5 %.
- **Long-term value / quality (yearly).** Costs are tiny relative to alpha; spread + commission < 30 bps per year.

If your strategy doesn't yield economic alpha after honest cost modelling, the answer is **always** "don't trade it" — never "find a broker who doesn't charge me costs." The market makers charging the costs will be on the other side of your trades.

## See also

- [[backtesting-discipline|Backtesting discipline]] — the broader methodology cost modelling fits into
- [[risk-adjusted-returns|Risk-adjusted return measures]]
- [[factor-investing|Factor investing]] — gross factor premiums frequently halve after realistic costs

## External references

- Almgren & Chriss (2001). "Optimal Execution of Portfolio Transactions." *Journal of Risk*.
- Kissell (2014). *The Science of Algorithmic Trading and Portfolio Management*. Academic Press.
- Lehalle & Laruelle (2018). *Market Microstructure in Practice* (2nd ed). World Scientific.
- Frazzini, Israel, & Moskowitz (2014). "Trading Costs of Asset Pricing Anomalies." *AQR Capital Management*.

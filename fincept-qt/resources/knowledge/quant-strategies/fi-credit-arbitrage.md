# Credit Arbitrage — CDS Basis and Swap-Spread

Two of the most-cited fixed-income arbitrage strategies are **CDS basis arbitrage** (exploiting the gap between a bond's credit spread and the cost of credit default insurance on it) and **swap-spread arbitrage** (a relative-value trade between an interest-rate swap and a duration-matched Treasury). Both look like riskless arbitrages on paper. Both have produced spectacular profits and spectacular losses depending on whether you survived the funding-liquidity events of 2008.

Kakushadze & Serur (2018) §5.14–§5.15 cover them in five paragraphs. This entry expands the mechanics, the real-world frictions (funding, counterparty, repo), and the post-2008 changes that fundamentally altered both trades.

## Strategy A — CDS Basis Arbitrage

### The setup

A **credit default swap** (CDS) is a contract where the buyer pays a periodic premium (the **CDS spread**, in basis points per year) to the seller, in exchange for compensation if the reference entity defaults.

Theoretically, buying a corporate bond and simultaneously buying CDS on that bond converts the position into a synthetic risk-free instrument. The bond pays you its full yield. The CDS premium offsets the credit risk. What's left is the **risk-free rate**.

So in equilibrium:

```
Bond yield = Risk-free rate + Bond spread
            ≈ Risk-free rate + CDS spread
```

The difference between the CDS spread and the bond spread is the **CDS basis**:

```
CDS basis  =  CDS spread − Bond spread             (Eq. 5.44)
```

In theory, the basis should be zero. In practice, it isn't.

### The trade

- **Negative basis** (CDS < bond spread → bond is cheap relative to CDS).
  - Buy the bond.
  - Buy CDS protection.
  - Pocket: bond yield − risk-free rate − CDS premium = the negative basis.
  - This is a (theoretically) risk-free arbitrage.

- **Positive basis** (CDS > bond spread → bond is expensive relative to CDS).
  - Short the bond.
  - Sell CDS protection.
  - This trade requires the ability to short the bond — much harder than the long-basis trade in practice.

### Why does the basis exist?

The basis is *not* arbitrage in the textbook sense. It exists because of several real frictions:

1. **Funding cost asymmetry.** Buying a bond requires capital (or repo financing). Buying CDS protection does not — it's an unfunded credit exposure. The negative basis compensates for the bond holder's funding cost.
2. **Counterparty risk.** Selling CDS exposes you to the buyer's default risk on the premium; buying CDS exposes you to the seller's default risk on the protection. Post-2008, ISDA standardisation and central clearing reduced but did not eliminate this.
3. **Liquidity premium.** Bonds are less liquid than CDS for many issuers. Wider negative basis is the price of bearing bond liquidity risk.
4. **Cheapest-to-deliver option.** Physically settled CDS contracts allow the protection buyer to choose which bond to deliver upon credit event; the cheapest one is delivered, which the protection seller knows and prices in. This gives the CDS a small **positive** basis structurally.

### 2008 and the basis blowout

In late 2008, the negative basis on investment-grade bonds **widened to −200 bps or more** — meaning the synthetic risk-free instrument (bond + protection) yielded 2% over LIBOR. The basis trade looked spectacular.

But the trade *also* required:

- **Repo funding** to finance the bond purchase — which dried up entirely.
- **Margin posting** as the basis widened further before reverting.
- **Counterparty capacity** at protection sellers — which evaporated.

Many funds running the basis trade in 2008 were forced sellers as their funding lines were pulled, locking in losses just before the basis normalised in 2009. **The textbook "arbitrage" wiped out funds**, exactly because the trade was funding-intensive and the funding crisis was the cause of the dislocation.

Choudhry (2007) and Bai & Collin-Dufresne (2013) document the basis evolution in detail.

### Post-2008 landscape

- The basis is much narrower in normal times (single digits to ~30 bps for most names).
- Central clearing of CDS via ICE and CME reduced counterparty risk and made the trade more capital-efficient.
- Regulatory changes (Basel III) raised the cost of holding bonds, structurally widening the negative basis.
- The trade is now mostly the domain of relative-value hedge funds with stable financing.

## Strategy B — Swap-Spread Arbitrage

### The setup

Two ostensibly very similar instruments:

- A **fixed-floating interest rate swap**: pay LIBOR, receive a fixed rate `r_swap`. No principal exchange.
- A **Treasury bond** with a yield `Y_Treasury`.

If you receive fixed on the swap and short the Treasury (financed at repo rate `r`), the cash flows are:

```
Period: receive r_swap from swap counterparty
Period: pay LIBOR L(t) on swap
Period: pay Y_Treasury to short-bond buyer
Period: receive r (repo financing)

Net per period:
C(t)  =  +[C₁ − C₂(t)]                              (Eq. 5.45)

where  C₁  =  r_swap − Y_Treasury                   (Eq. 5.46)
       C₂(t)  =  L(t) − r(t)                        (Eq. 5.47)
```

`C₁` is the **swap spread** — the gap between the swap rate and the Treasury yield. This is roughly constant for a given trade.

`C₂(t)` is the **LIBOR-repo spread** — how much above the Treasury repo rate banks have to pay to fund themselves. This **varies over time** with the credit conditions of the banking system.

The trade is essentially: **profit if `(swap spread) > (LIBOR-repo spread)`**, i.e., if `C₁ > C₂(t)` on average.

### What is this trade actually betting on?

This is a **bet on LIBOR vs. repo spreads narrowing or staying low**. In normal times, LIBOR is roughly 10–25 bps above repo (LIBOR-OIS or LIBOR-GC spreads are the standard measures). The swap spread is in the same vicinity. So the trade earns the *difference* between the two, which is small but mean-reverting.

### When it works

- **Quiet credit markets.** LIBOR-repo spreads compressed; swap spreads compressed less.
- **Strong bank balance sheets.** Funding costs are low; LIBOR-Treasury is narrow.

### When it breaks

- **Banking crises.** 2008 and 2020. LIBOR-OIS spread blew out to 300+ bps; swap spreads moved but not as much. The trade lost dramatically.
- **Repo squeezes.** When specific Treasury collateral becomes scarce (the "special repo" phenomenon), the repo rate `r(t)` falls relative to LIBOR, blowing out `C₂(t)`.
- **LIBOR transition (2022–2023).** With LIBOR being replaced by SOFR, the trade structure itself had to be re-specified. Live swap-spread arbs migrated to SOFR-Treasury spreads, which behave differently from LIBOR-Treasury.

### LTCM and the 1998 cousin

The famous LTCM trade in 1998 was **not exactly** this swap-spread trade, but a closely-related US-Italian swap-spread convergence trade. Same structural risk: a wedge between LIBOR-equivalent rates and government-bond rates that *should* be small but can blow out under funding pressure. LTCM was forced to liquidate when its repo funding was withdrawn and the convergence trade widened against them. Duarte, Longstaff, Yu (2006) "Risk and Return in Fixed-Income Arbitrage: Nickels in Front of a Steamroller" is the canonical post-mortem.

## What makes both of these "arbitrage" misleading

In strict equilibrium theory, both trades are arbitrages: identifiable mispricings between equivalent claims. In practice, both are **earning a premium for bearing funding risk**.

The expected payoff is positive in normal times. The realised payoff has a long left tail — when funding markets seize, both trades lose simultaneously and badly. **You are short a tail event in the funding-liquidity market**.

This is the unifying theme of all "fixed-income arbitrage" strategies — what Duarte, Longstaff, Yu (2006) called "nickels in front of a steamroller." The strategies make money slowly and steadily, then occasionally give back years of return in weeks.

## Risk management essentials

- **Stable financing is a prerequisite.** Without a multi-year prime-broker relationship and committed repo lines, these trades cannot survive a funding shock. Retail and small-fund implementations are essentially uninvestable.
- **Mark-to-market risk dwarfs default risk.** The basis trade hedges default but is exposed to spread widening on a mark-to-market basis. A 100bps further widening on a $100M position is a $1M+ unrealised loss that must be financed.
- **Counterparty diversification.** Don't have all your CDS protection or your swap exposure with one counterparty. Bilateral risk is enormous in extremes.
- **Position-sizing relative to funding capacity.** Size such that a 2x-historical-worst-case widening still leaves you with funding headroom.

## Common mistakes

- **Treating the trade as risk-free in backtests.** It pays slowly and then bites. Backtests that miss the 2008 episode look fantastic.
- **Confusing CDS-bond basis with CDS-CDS basis.** Different references (e.g., 5y CDS vs 10y CDS on the same name) trade their own basis; that's a curve trade, not a basis trade.
- **Ignoring the cheapest-to-deliver option in CDS.** Physical-settled CDS has a structural positive basis component you can't trade away.
- **Mismatched durations in swap-spread trade.** Match modified durations between the swap and the Treasury or you're taking a level-of-rates bet, not a spread bet.

## Where to do this in the terminal

- **Bond screen** — corporate bond yields with overlay of corresponding 5y CDS spread; the bid-ask of the basis displayed live.
- **Backtesting** — fixed-income arbitrage backtest module that includes simulated funding-cost shocks based on historical LIBOR-OIS / SOFR-IORB widening.
- **AI Quant Lab** — basis-trade templates with funding-cost assumptions adjustable.

## See also

- [[fi-curve-spreads|Yield Curve Spreads]] — relative-value curve trades within Treasuries
- [[fi-butterflies|Bond Butterflies]] — curvature trades
- [[fi-bond-factors|Bond Factor Investing]] — broader systematic credit strategies
- [[strategy-patterns|Strategy Patterns]] — where credit arb fits

## External references

- Choudhry, M. (2007). "Trading the CDS Basis: Illustrating Positive and Negative Basis Arbitrage Trades." *Journal of Trading* 2(1), 79–94.
- Bai, J., Collin-Dufresne, P. (2013). "The CDS-Bond Basis." Working paper, SSRN 2024531.
- Choudhry, M. (2004). "The Credit Default Swap Basis: Analysing the Relationship Between Cash and Synthetic Credit Markets." *Journal of Derivatives Use, Trading and Regulation* 10(1), 8–26.
- Duarte, J., Longstaff, F., Yu, F. (2006). "Risk and Return in Fixed-Income Arbitrage: Nickels in Front of a Steamroller?" *Review of Financial Studies* 20(3), 769–811.
- Duffie, D., Singleton, K. (1997). "An Econometric Model of the Term Structure of Interest Rate Swap Yields." *Journal of Finance* 52(4), 1287–1321.
- Feldhütter, P., Lando, D. (2008). "Decomposing Swap Spreads." *Journal of Financial Economics* 88(2), 375–405.
- Collin-Dufresne, P., Solnik, B. (2001). "On the Term Structure of Default Premia in the Swap and LIBOR Markets." *Journal of Finance* 56(3), 1095–1115.
- Du, W., Tepper, A., Verdelhan, A. (2018). "Deviations from Covered Interest Rate Parity." *Journal of Finance* 73(3), 915–957.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §5.14–5.15. https://doi.org/10.1007/978-3-030-02792-6

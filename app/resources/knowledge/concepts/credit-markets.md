# Credit Markets

The credit market is where corporations and governments borrow money by issuing debt. It is substantially larger than the equity market by outstanding value, and it often leads equity markets as a leading indicator of financial stress. The level and direction of credit spreads — the premium borrowers pay over the risk-free rate — is one of the most useful real-time signals available to investors across all asset classes.

## The credit spectrum

All debt lies on a spectrum from risk-free to highly speculative. The defining dimension is **default probability** — the likelihood that the borrower fails to pay interest or principal.

| Category | Rating (S&P / Moody's) | Typical spread over Treasuries | Characteristics |
|---|---|---|---|
| **Sovereign (US)** | AAA / Aaa | 0 bps (benchmark) | Risk-free reference rate |
| **Agency / GSE** | AA+ | 20–50 bps | Fannie, Freddie; quasi-government |
| **Investment Grade (IG)** | BBB- / Baa3 and above | 50–200 bps | Large corporates; low default risk |
| **Crossover / BBB** | BBB+ to BBB- | 100–250 bps | Lowest IG tier; fallen angel risk |
| **High Yield (HY)** | BB+ / Ba1 and below | 250–700+ bps | "Junk bonds"; meaningful default risk |
| **Distressed** | CCC and below | 1000+ bps | Near-default or in restructuring |

The boundary between Investment Grade and High Yield is important. Many institutional investors (pension funds, insurance companies) are prohibited by mandate from holding sub-IG bonds. When a company is downgraded from BBB- to BB+ ("fallen angel"), forced sellers flood the market, pushing spreads sharply wider.

## Credit spreads as a market signal

The **option-adjusted spread (OAS)** strips out embedded optionality (call features, puts) to give a clean picture of credit risk premium. The most-watched aggregates:

- **IG OAS** (Bloomberg US Corporate Bond Index): investment-grade corporate spread to Treasuries
- **HY OAS** (Bloomberg US HY Index): high-yield spread; more volatile, more leading
- **EM Spreads** (EMBI): emerging market sovereign spreads to US Treasuries

**What spread levels signal**:

| Spread regime | IG OAS | HY OAS | What it means |
|---|---|---|---|
| Tight (risk-on) | < 100 bps | < 300 bps | Complacency; credit is freely available; late-cycle warning if sustained |
| Normal | 100–150 bps | 300–450 bps | Healthy environment |
| Widening (caution) | 150–250 bps | 450–650 bps | Risk appetite declining; economic concerns |
| Stress | 250–400 bps | 650–900 bps | Financial tightening; recession risk elevated |
| Crisis | > 400 bps | > 1000 bps | Systemic stress; 2008 IG hit 600 bps, HY ~2000 bps |

## Credit default swaps (CDS)

A **credit default swap** is a bilateral contract where one party (protection buyer) pays a regular fee (the "spread" or "premium") and receives a large payment if a specified reference entity defaults. It's economically similar to insurance on a bond.

```
Protection buyer pays: 100 bps/year × notional
Protection seller receives: 100 bps/year × notional
If default: seller pays (1 - recovery rate) × notional to buyer
```

CDS markets are useful because:

1. They are **tradeable instruments** — you can bet on credit deterioration without owning or shorting bonds
2. The CDS spread often **leads bond spreads** in anticipating stress (more liquid, easier to short)
3. The **5-year CDS spread** is the most-quoted standard; you'll see it for sovereigns and large corporates

**CDS indices** (CDX in North America, iTraxx in Europe) are liquid baskets of CDS on index members. CDX IG and CDX HY are major institutional hedging and macro expression tools.

## The primary market: how companies borrow

When a company wants to issue a bond, the process is:

1. **Mandate**: company hires an investment bank (or syndicate of banks) to manage the deal
2. **Rating**: S&P, Moody's, Fitch assign a rating based on financial analysis and covenants
3. **Roadshow / bookbuild**: banks market the deal to institutional investors; gauge demand
4. **Pricing**: final coupon and spread over Treasuries set based on order book
5. **Allocation**: bonds distributed to investors (mostly institutional); retail gets very little primary access
6. **Secondary trading**: bonds trade OTC (over the counter) between dealers and institutions

Unlike equities, most bonds don't have centralized exchange trading. Liquidity is **dealer-provided** — institutional investors call dealers for bids/offers. This makes bond markets structurally less liquid than equity markets, especially in stress when dealers pull back.

## High yield mechanics

High yield bonds carry meaningful default probability, so the analysis is more intense:

**Covenant analysis**: Bond indentures define what the issuer can and cannot do — leverage limits, restrictions on asset sales, dividend payments. Covenant erosion ("cov-lite" deals) was widespread in the 2015–2021 era; weaker covenants mean less protection for bondholders.

**Recovery rates**: When a company defaults, creditors recover some fraction of their investment in restructuring. Historical average is ~40% for senior secured, ~20–30% for senior unsecured, near zero for subordinated/equity.

**Yield to call / yield to worst**: HY bonds almost always have call features — the issuer can repay early. The relevant yield metric is the **yield to worst (YTW)**: the lowest yield you'll receive given all call scenarios.

**Fallen angels and rising stars**: Major market events occur when companies cross the IG/HY boundary. Fallen angel issuers face forced selling. Rising star upgrades from HY to IG trigger forced buying. Both create persistent mispricings that credit hedge funds exploit.

## Credit as an equity signal

Credit markets often lead equity markets at turning points. Mechanisms:

- Credit investors are often better informed about near-term cash flow and refinancing risk
- Rising HY spreads signal higher cost of capital and tighter conditions before it appears in equity earnings
- CDS widening on a specific company is sometimes the first sign of financial distress before equity reacts

The historical pattern: credit spreads widen first, equities follow 2–6 weeks later in deteriorating environments. In stress events (2008, 2020), the sequence can compress to days.

**Practical rule**: if the S&P 500 is rallying but HY spreads are widening, question the equity rally — the credit market doesn't believe it.

## Leveraged loans and CLOs

The leveraged loan market is the bank equivalent of the high-yield bond market — floating-rate debt to sub-investment grade companies. Leveraged loans are packaged into **CLOs** (Collateralized Loan Obligations), which tranche the risk and sell it to different investor types.

CLO tranches:
- AAA: first to be paid; lowest yield; usually held by banks/insurers
- AA through BB: intermediate risk
- Equity tranche: residual; highest risk/return; often held by CLO managers

The health of CLO issuance is a real-time indicator of leveraged credit appetite.

## In finterm

The Economics screen shows the IG and HY OAS series. Watch the spread level and its rate of change together — a spread that is elevated and still widening is more dangerous than one that is wide but stabilizing. The Markets screen shows sector-level credit spreads for deeper analysis.

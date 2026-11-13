# Silicon Valley Bank — March 2023

A 40-year-old, $200B-asset bank — the **15th largest in the US** — failed in **36 hours** on March 9–10, 2023. Customers withdrew $42B (a quarter of all deposits) in a single day before regulators stepped in.

The case is the canonical study of **interest-rate duration risk meeting concentrated deposit risk** — and how social media accelerates bank runs into hours rather than days.

## The setup (2020–2022)

SVB's customer base was the **US tech and venture-capital ecosystem**:
- Heavily concentrated in startups and VC funds
- Average deposit per account: very high (well above the $250K FDIC cap)
- Deposits ballooned from $62B (Q4 2019) to $189B (Q1 2022) as VC funding boomed

What did SVB do with all that money? They bought **long-duration Treasuries and mortgage-backed securities** at the prevailing low yields:

```
SVB's Q4 2021 securities portfolio:
  ~$120B in HTM (held-to-maturity) bonds
  Avg duration: ~6 years
  Avg yield purchased: ~1.5% (2020-2021 yields were near generational lows)
```

This was a **massive duration bet** — perfectly fine if rates stayed low forever.

## The Fed's hike cycle

From March 2022 to March 2023, the Fed raised the funds rate from **0.25% → 4.75%** — the fastest hike cycle in 40 years.

What that did to SVB's bond portfolio:

```
Estimated MTM loss on the HTM portfolio:
  Duration × yield change ≈ 6 × 4% = ~24% loss
  $120B × 24% ≈ $29B unrealized loss

SVB's tangible equity: ~$13B
$29B loss > entire equity base
```

In normal accounting, **HTM bonds don't have to mark-to-market** — losses don't show up in earnings. So SVB looked solvent on paper. **Economically, it was insolvent at any moment after late 2022.**

## The trigger

By early 2023:
- VC funding had collapsed; startups were burning cash
- SVB customers were drawing down deposits faster than expected
- SVB needed to **sell some of its HTM bonds** to fund withdrawals
- Selling crystallizes the unrealized loss into a real loss

**March 8, 2023**: SVB announced it had sold $21B of bonds at a $1.8B realized loss and needed to raise $2.25B in equity.

This was the spark.

## The run (36 hours)

- **March 8 evening**: News spreads on Twitter / VC group chats / Slack
- **March 9 morning**: Founders Fund, Coatue, Y Combinator, others tell portfolio companies to pull money
- **March 9 close**: $42B in withdrawals attempted in one day (25% of deposits)
- **March 10 morning**: California regulators close SVB; FDIC takes over
- Total time from announcement to closure: **~36 hours**

This was unprecedented speed. **Continental Illinois (1984)** took ~10 days to fail. **Wachovia (2008)** took weeks. SVB happened over a Twitter thread.

## The aftermath

- **March 12**: Fed/Treasury/FDIC announce all SVB deposits guaranteed (above the $250k cap)
- **March 12**: Fed launches **BTFP (Bank Term Funding Program)** — banks can borrow against bonds at par (not market value)
- **Signature Bank** (NYC, crypto-heavy) closed same day
- **First Republic** (San Francisco, similar customer profile) failed weeks later
- **Credit Suisse** (separate issues but stress-spread) acquired by UBS

US regional bank stocks fell 30–60% over the following weeks.

## What SVB did wrong

1. **Mismatched asset duration to liability duration.** Demand deposits behave like overnight liabilities; long bonds behave like 5–10 year assets. Classic banking sin.
2. **Didn't hedge interest rate risk.** A $5B notional swap could have substantially mitigated; SVB had close to none.
3. **Concentrated customer base.** Tech/VC clients are correlated — if VC funding dries up, *all* deposits drop simultaneously.
4. **Slow communication.** They announced the bond sale and capital raise simultaneously, in a way that scared depositors.
5. **No risk officer for ~8 months** in 2022 — a fact later disclosed.

## Concepts illustrated

- [[duration|Duration]] — the textbook example of duration risk
- [[convexity|Convexity]] — long bonds + low coupons = highest convexity (and largest losses)
- [[liquidity|Liquidity]] — disappeared instantly when needed; the run was a liquidity event, not just solvency
- [[yield-curve|Yield Curve]] — the inverted curve was the macro background

## Lessons

1. **Asset-liability matching matters.** Banking 101.
2. **Held-to-Maturity accounting hides duration risk** until you need to sell.
3. **Customer concentration is risk.** A diversified deposit base is more resilient than a "specialty" base.
4. **Bank runs in 2023 happen at network speed.** Communication channels make traditional run dynamics obsolete.
5. **Regulators react after the fact.** BTFP existed within 72 hours of SVB's failure — but only because the systemic risk was so large.

## What changed

- **Basel III + US regional bank stress tests** are being revisited
- **Interest-rate risk reporting** is being tightened
- **Deposit-stickiness assumptions** in stress tests are being lowered
- **Real-time deposit monitoring** is now standard at most regional banks

## Read more

- Federal Reserve Review of SVB Supervision (April 2023)
- Matt Levine's *Money Stuff* coverage in March 2023 — the best contemporaneous analysis

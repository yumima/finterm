# Bonds — A Primer

A **bond** is a loan: the issuer borrows money and promises to pay back periodic interest (coupons) and the face amount (principal) at a specified date (maturity).

```
You buy bond:    Issuer hands you face $1,000 worth of debt
You receive:     Coupons (e.g., 4% × $1,000 = $40/year, often split semi-annually)
At maturity:     $1,000 principal returned
```

## What you actually own

A bond is a contractual claim. The issuer must:
- Pay interest on the schedule
- Return principal at maturity
- Or default — at which point you become an unsecured / secured creditor in bankruptcy

## The bond market is bigger than the stock market

| Market | Approx size (2024) |
|---|---|
| Global bond market | $130T |
| Global equity market | $115T |
| US Treasury alone | $34T |

Bonds set the discount rate for everything else. **You can't analyze stocks without understanding bonds.**

## Types of bonds

| Type | Issuer | Risk | Tax (US) |
|---|---|---|---|
| **Treasury** | US government | "Risk-free" (default) | Federal taxable, state-exempt |
| **Treasury (TIPS)** | US government | Inflation-protected | Federal taxable; phantom income |
| **Agency** | Fannie Mae, Freddie Mac, etc. | Implicit gov't backing | Mostly federal taxable |
| **Municipal** | State/city governments | Credit-dependent | Federal exempt; some state |
| **Investment-grade corporate** | Major companies (BBB+) | Low default | Fully taxable |
| **High-yield ("junk")** | Below-IG companies | Real default risk | Fully taxable |
| **Foreign sovereign** | Non-US governments | Country risk + FX | Varies |
| **EM corporate** | Companies in EM | Default + FX + political | Varies |

## How a bond is priced

```
Bond Price = Σ [Coupon / (1+y)^t]  +  Face / (1+y)^T

Solve for y = YTM (yield to maturity)
```

Price moves **inversely to yield**: yields up → prices down. Bigger move for longer-duration bonds.

See [[duration]] for the precise sensitivity, [[convexity]] for the second-order correction.

## Three risks in every bond

1. **Credit risk** — will the issuer pay? Quantified by [[credit-spread]]
2. **Duration risk** — will rates change before I get my principal back? Quantified by [[duration]]
3. **Reinvestment risk** — will I be able to reinvest coupons at YTM?

For Treasuries: only duration and reinvestment matter (default risk ~0).
For corporates: all three.
For HY: credit dominates.

## The yield curve as macroeconomic signal

[[yield-curve|Yield curve shape]] reflects market expectations:
- **Steep**: growth + Fed cuts ahead
- **Flat**: late cycle / transition
- **Inverted**: recession risk / Fed too tight

Yields are 60–80% noise + 20–40% signal. The signal is in the persistent shifts.

## Bond strategies

| Strategy | What it does |
|---|---|
| **Buy and hold to maturity** | Locks in YTM; ignores price swings |
| **Bond laddering** | Multiple maturities (1y, 2y, 3y…); reduces reinvestment + duration risk |
| **Barbell** | Mix short + long; nothing in middle |
| **Bullet** | Concentrate at single maturity (matched to a liability) |
| **Active duration management** | Shift duration based on rate view |
| **Credit rotation** | Move IG → HY in cycle troughs, reverse at peaks |
| **Leveraged carry** | Borrow short, lend long; classic bank profit + duration risk |

## Where retail investors usually fail

- **Buying long bonds at multi-decade-low yields** (2020 → got crushed in 2022)
- **Treating bond ETFs as cash equivalents** — 7–10% drawdowns possible
- **Ignoring inflation** — nominal yield isn't your real yield
- **Chasing yield in HY at cycle tights** — paying too much for too little spread

## Decision rules

- **Cash needed within 1y** → T-bills only
- **Liability matching** (target retirement, college tuition) → match duration to liability
- **Inflation hedge** → TIPS or short-duration nominal
- **Long-term retirement portfolio** → modest long-duration exposure for diversification (it sometimes works)
- **HY** → max 5% of portfolio for retail; treat as equity-like, not safe

## Bond ETFs to know

| Ticker | Exposure |
|---|---|
| BIL | 1–3 month T-bills |
| SHY | 1–3 year Treasuries |
| IEF | 7–10 year Treasuries |
| TLT | 20+ year Treasuries (high duration) |
| LQD | Investment-grade corporates |
| HYG / JNK | High-yield corporates |
| MUB | Tax-exempt municipals |
| TIP / SCHP | TIPS |
| BND / AGG | Total US bond market |
| EMB | EM USD-denominated sovereigns |

## In finterm

Gov Data tracks the Treasury yield curve. Economics tracks credit spreads. For position-level bond analytics, use the Portfolio risk panel for duration / convexity.

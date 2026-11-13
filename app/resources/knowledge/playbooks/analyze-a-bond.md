# How to Analyze a Corporate Bond

Corporate bonds offer a different risk/return profile than equities — fixed income, seniority in bankruptcy, and a ceiling on upside. But analyzing a bond correctly requires understanding yield, duration, credit quality, and relative value simultaneously. This playbook walks you through assessing whether a specific corporate bond is worth owning.

## When to Use This

Use this when evaluating a specific corporate bond for purchase, when assessing the credit risk of a company's debt (relevant even for equity investors), or when comparing bonds across issuers or maturities.

---

## Step-by-Step Process

### Step 1: Establish the Bond's Basic Terms

Before analysis, confirm:
- **Face (par) value**: typically $1,000
- **Coupon rate**: the annual interest payment as % of face
- **Maturity date**: when you get your principal back
- **Current price**: bonds trade at par (100), above par (premium), or below par (discount)
- **Callable provisions**: can the issuer redeem early? When? At what price?

### Step 2: Calculate (or Read) the Yield to Maturity

Yield to maturity (YTM) is the annualized return you earn if you hold the bond to maturity and receive all coupon and principal payments. It accounts for the purchase price, coupon, and time to maturity.

If the bond trades at a discount to par, YTM > coupon rate. If at a premium, YTM < coupon rate.

For callable bonds, also calculate **yield to worst (YTW)** — the lowest yield you'd earn across all possible call scenarios. YTW is the relevant number for callable bonds.

### Step 3: Calculate the Credit Spread

The credit spread is the yield of the corporate bond *minus* the yield of a comparable Treasury bond (same or similar maturity). This is the pure compensation for credit risk.

```
Credit spread = corporate bond YTM − comparable Treasury yield
```

A 10-year investment-grade corporate might yield 5.3% while the 10-year Treasury yields 4.5% — credit spread of 80bps.

**Context matters.** Compare the current spread to:
- **The issuer's historical spread**: is it wide (cheap) or tight (expensive) relative to its own history?
- **Sector peers**: is this issuer's spread in line with comparable companies?
- **Index averages**: IG index average spread is typically 80–130bps; HY average is 300–500bps in calm markets

### Step 4: Assess Credit Quality

The bond's credit risk comes down to one question: will the issuer pay coupon and principal as promised?

**Quantitative checks:**
- **Interest coverage ratio**: EBIT / interest expense. Below 2x is concerning; above 4x is comfortable
- **Debt / EBITDA**: leverage ratio. IG companies typically 1–3x; HY companies 4–7x
- **Free cash flow coverage**: can operating cash flow service the debt? Look at FCF / total debt service
- **Debt maturity schedule**: is there a large maturity "wall" in the next 2–3 years that requires refinancing?

**Qualitative checks:**
- Business model durability: does this company's competitive position look stable over the bond's life?
- Covenant package: secured vs. unsecured; covenant-lite vs. full covenants
- Capital structure seniority: senior secured > senior unsecured > subordinated > equity

**Credit rating** (Moody's, S&P, Fitch) provides an independent assessment, but ratings are lagging indicators. Markets often move before rating agencies act. For fallen angels (IG to HY downgrades), the price move usually precedes the formal downgrade.

### Step 5: Assess Duration Risk

Duration measures the bond's sensitivity to interest rate changes.

```
Approximate price change ≈ −(duration) × (yield change)
```

A 7-year duration bond loses approximately 7% in price for a 100bps rise in yields. This is *separate* from credit risk — it applies even to Treasuries.

**Rule**: Longer duration = more interest rate risk. Shorter duration = more credit risk relative to total return (less time for issuer to recover if financial stress develops).

For corporate bonds, total risk = duration risk + credit spread risk. Rising rates AND widening spreads can both cause losses.

### Step 6: Determine Relative Value

The final step: is this bond worth owning relative to alternatives?

- **Vs. the same issuer's equity**: if the bond yields 7% and the equity has an expected return of 8% with much higher volatility, the bond's risk-adjusted return may be superior
- **Vs. comparable bonds**: compare spread-per-unit-of-leverage across similar issuers
- **Vs. the risk-free rate**: what's the incremental return for taking on this credit risk? Is it sufficient?

---

## Common Mistakes

- **Focusing only on yield, not spread.** A 7% bond in a 6.5% Treasury environment (50bps spread) is very different from a 7% bond in a 4% Treasury environment (300bps spread). Same yield, very different credit compensation.
- **Ignoring call provisions.** Callable bonds often get called when it benefits the issuer (when rates fall) — which is exactly when you want to hold them. YTW reflects this risk; yield-to-maturity does not.
- **Treating credit ratings as current analysis.** Ratings lag. Read the issuer's financials directly.
- **Not stress-testing the maturity wall.** Companies that can service debt with current cash flows may face a cliff if they have $2B maturing in 18 months in a tight credit market.

---

## What Success Looks Like

You can state the bond's YTW, credit spread, interest coverage, and leverage ratio in 60 seconds. You can explain why the spread is wide or tight relative to peers. You know where it sits in the capital structure and what recovery rates might look like in a distressed scenario. You've decided whether the credit risk and duration risk are compensated by the spread.

# High-Yield Bond ("Junk Bond")

A corporate bond rated **below investment grade** — BB+ (S&P) or Ba1 (Moody's) and lower. Higher promised yield compensates for higher default risk and liquidity premium.

## Rating ladder

| S&P | Moody's | Bucket | Annual default rate (long-run) |
|---|---|---|---|
| AAA | Aaa | Investment grade | ~0.02% |
| AA | Aa | Investment grade | ~0.05% |
| A | A | Investment grade | ~0.10% |
| BBB | Baa | Investment grade | ~0.30% |
| **BB** | **Ba** | **High yield (HY)** | ~1.5% |
| **B** | **B** | High yield | ~6% |
| **CCC** | **Caa** | High yield (distressed) | ~25% |
| C / D | C / Default | Default | n/a |

Investment grade ends at BBB-/Baa3; below that is "junk."

## How HY differs from IG

| Aspect | Investment Grade | High Yield |
|---|---|---|
| Yield premium over Treasuries | 100–200 bps | 400–600+ bps |
| Default rate (avg) | 0.1–0.3% | 3–5% |
| Recovery rate on default | 60–80 cents | 30–50 cents |
| Issuer leverage (debt/EBITDA) | <3× | 4–7× typical |
| Equity-correlation in stress | low (0.2) | high (0.6) |
| Covenant strength | strong (historically) | weakening; "cov-lite" common |
| Issue size | $500M+ typical | $300M+ |

## Worked example — yield decomposition

```
Single-B HY bond:
  Coupon: 8.5%
  Treasury 5y: 4.5%
  Credit spread: 400 bps

Where does the yield go?
  Risk-free rate:        4.5%
  Expected default loss: ~1.5–2.5%   (default rate × loss given default)
  Liquidity premium:     ~0.5%
  Spread vol premium:    ~0.5%
  "Pure compensation":   ~0.5–1%
```

So of 8.5% nominal yield, maybe 5.5–7% is "earned" after default losses — depending on cycle and timing.

## HY in cycles

- **Tight spreads (cycle peaks)**: HY yields ~400 bps over Treasuries; defaults are minimal; everyone reaches for yield. **Late-cycle complacency.**
- **Wide spreads (recessions)**: HY blows out to 1,000–2,000 bps; defaults spike to 10–15%; sellers panic.

The pattern repeats: **buy when spreads wide and panic dominates**; trim when spreads tight and inflows are heavy.

## Diversification illusion

HY is often pitched as "diversifying" vs equities. In normal times that's true (correlation ~0.4). **In real crises (2008, 2020), HY-equity correlation jumps to ~0.7+**. The diversification benefit you signed up for evaporates when you most need it.

## Common HY products

| Vehicle | Notes |
|---|---|
| Individual HY bonds | High minimum size, illiquid; institutional only |
| HY mutual funds | Diversified; daily liquidity (which is a problem in stress) |
| HYG / JNK ETFs | Liquid; basket-pricing distortion in stress |
| BDCs (Business Dev Cos) | Equity-like exposure to private credit |
| Bank loan funds | Floating rate; less duration risk |
| CLO equity / debt | Structured; complex |

## Pitfalls

- **Reaching for yield** in late cycle — spreads are tight precisely because risk is mispriced
- **Treating HY as fixed income** — under stress it acts like equity
- **Liquidity mismatch**: HY bond ETFs trade daily but underlying bonds may go days without a print
- **Cov-lite** loans give bondholders little protection at default
- **Distressed buying without restructuring expertise** — recovery is uncertain and can take years

## Decision rules

- **HY < 5% portfolio for retail** unless you have specific expertise
- **Hold via diversified fund or ETF**, not individual bonds
- **HY spread > 800 bps** → consider adding (but slowly; spreads can keep widening)
- **HY spread < 350 bps** → consider trimming; late-cycle complacency
- **Default-cycle phase** matters: early default phase = sell, peak default = hold, post-default = buy

## In finterm

Economics screen tracks the HY spread (BAMLH0A0HYM2 / similar). Combine with [[yield-curve]] inversion and unemployment for a multi-factor recession risk score.

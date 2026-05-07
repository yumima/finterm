The **overnight reverse repo (ON RRP)** is a facility operated by the Federal Reserve that allows eligible counterparties — money market funds, banks, GSEs — to lend cash to the Fed overnight in exchange for Treasury securities as collateral, earning a set interest rate.

From the Fed's perspective it is a repo (borrowing); from the counterparty's perspective it is a reverse repo (lending). The ON RRP rate functions as a hard floor on short-term money market rates.

## Mechanics

```
Money market fund lends $100M cash to Fed overnight
Fed provides $100M in Treasuries as collateral
Next day: Fed returns $100M + interest at ON RRP rate
Fund receives Treasuries back
```

The Fed sets the ON RRP rate — typically 5 bps below IORB (Interest on Reserve Balances). This creates a rate corridor:

```
ON RRP rate (floor) ≤ EFFR ≤ IORB rate (ceiling)
```

## Why usage surged 2021–2023

When the Fed flooded the system with QE reserves and Treasury issued fewer bills, money market funds had too much cash and too few safe assets. ON RRP usage reached a peak of $2.5T in December 2022.

```
Peak ON RRP usage: $2.553T (Dec 30, 2022)
Providers: ~100 eligible money market funds
Rate: 4.30% (matching Fed Funds upper bound)
```

As Treasury bill issuance increased in 2023, money funds shifted from ON RRP to T-bills, and usage fell sharply toward $100–200B.

## Policy signal

ON RRP usage is a useful liquidity barometer:

| ON RRP Trend | Interpretation |
|---|---|
| Rising rapidly | System flush with excess cash; easy financial conditions |
| High plateau | Structural cash surplus; money market rates well-anchored |
| Falling quickly | Cash draining to bills/loans; reserves normalizing |
| Near zero | Potential reserve scarcity ahead; watch repo rates |

## Difference from regular repo

| Feature | Repo (standard) | ON RRP (Fed facility) |
|---|---|---|
| Counterparty | Any bank/dealer | Eligible: MMFs, banks, GSEs |
| Rate | Market-determined | Fed-set (policy tool) |
| Purpose | Dealer financing | Fed plumbing / rate floor |
| Collateral | Various | U.S. Treasuries only |

## Pitfalls

- ON RRP usage is not inherently good or bad; it reflects the plumbing of the financial system.
- A rapid decline in ON RRP while the Fed is doing QT can signal reserves are getting scarce — a warning for repo market stress.
- The rate differential between ON RRP and T-bills drives rotation between the two instruments, causing Treasury auction demand fluctuations.

See also: [[federal-funds-rate|Federal Funds Rate]], [[quantitative-tightening|Quantitative Tightening]], [[sofr|SOFR]], [[treasury-securities|Treasury Securities]].

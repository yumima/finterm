The **ex-dividend date** (ex-date) is the cutoff date established by a company to determine which shareholders are entitled to receive an upcoming dividend — to receive the dividend, you must own the shares *before* the ex-date.

If you buy shares on or after the ex-dividend date, you are not entitled to the upcoming dividend. The shares begin trading "without" the dividend value, and the stock price typically drops by approximately the dividend amount at the open on the ex-date.

## Key dates in the dividend timeline

```
Declaration Date → Record Date → Ex-Dividend Date → Payment Date

1. Declaration Date:  Board announces dividend amount
2. Ex-Dividend Date:  First day you can buy without receiving dividend
                      (typically 1 business day before record date under T+1 settlement)
3. Record Date:       Shareholders on record that day receive dividend
4. Payment Date:      Dividend deposited to accounts (typically 2–4 weeks after record)
```

## Worked example

```
AAPL dividend announcement:
  Declaration: Apr 25, 2026 ($0.25/share quarterly dividend)
  Ex-date:     May 8, 2026
  Record date: May 9, 2026
  Payment:     May 22, 2026

Buyer on May 7:  Receives $0.25/share ✓
Buyer on May 8:  Does NOT receive $0.25/share ✗

Stock price behavior:
  Close May 7:  $185.00
  Open May 8:   ~$184.75 (drops by ~$0.25 dividend amount)
```

## Why the ex-date price drop occurs

The dividend is a distribution of company assets. On the ex-date:
- Buyers no longer receive the dividend → the stock is worth dividend amount less.
- In theory, price declines by the exact dividend amount.
- In practice, taxes, market conditions, and other factors cause the drop to differ slightly from the exact dividend.

## Ex-date in options

Options traders must account for ex-dates carefully:
- Short calls may be exercised early (American options) the day before the ex-date if intrinsic value > time value.
- Put prices increase relative to calls on ex-dates (puts benefit from dividend, calls lose relative value).
- Put-call parity adjusts for expected dividends.

## Dividend capture strategy

Some investors attempt to "capture" dividends by buying before the ex-date and selling after:

```
Buy at $185.00 (day before ex-date)
Receive $0.25 dividend
Stock opens at $184.75 on ex-date
Sell at $184.75

Net result: −$0.25 (stock loss) + $0.25 (dividend) = $0 before commissions and taxes
```

Dividend capture is generally not profitable because the price drop offsets the dividend. Tax inefficiency makes it worse.

## Ex-date vs. record date under T+1

Under T+1 settlement (adopted by U.S. markets in 2024):
- Trades settle 1 business day after execution.
- To be on record as of the record date, you must buy by the ex-date (the day before).

Under the old T+2 system, the ex-date was 2 days before the record date.

## Pitfalls

- Confusing ex-date with record date or payment date — only the ex-date matters for determining who gets the dividend.
- Assuming the stock will recover quickly to pre-ex-date levels; sometimes the post-ex-date price becomes the new baseline.
- High-yield stocks with quarterly dividends have frequent ex-dates; understanding the schedule is important for options trading and tax planning.

See also: [[dividend-yield|Dividend Yield]], [[buyback-yield|Buyback Yield]], [[eps|EPS]], [[put-call-parity|Put-Call Parity]].

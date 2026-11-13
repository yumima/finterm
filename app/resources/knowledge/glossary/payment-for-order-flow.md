**Payment for order flow (PFOF)** is the practice of a retail broker routing customer orders to specific wholesale market makers in exchange for monetary compensation, rather than routing orders to exchanges based on best execution criteria alone.

PFOF became the dominant revenue model for zero-commission retail brokers (Robinhood, Schwab, TD Ameritrade). The controversy centers on whether the arrangement serves retail customers' best interests or primarily the broker's.

## How it works

```
Retail investor → places market order to buy 100 shares of AAPL
Broker → routes order to Citadel Securities (market maker)
Citadel → fills order at $185.015 (versus NBBO ask of $185.02)
             → price improvement: $0.005/share = $0.50 on 100 shares
Citadel → pays broker $0.002/share = $0.20 in PFOF
```

The customer gets a slight price improvement. Citadel earns from the order flow information. The broker earns PFOF revenue while charging $0 commission.

## Revenue scale

Robinhood earned ~$974M in PFOF in 2021. Wholesale market makers paid billions across all retail brokers for priority access to retail order flow, which is considered "uninformed" (retail traders don't have inside information) and thus safe and profitable to trade against.

## The case for PFOF

- Retail investors receive genuine price improvement vs. the NBBO (typically $0.001–$0.01/share).
- PFOF funds zero-commission trading, democratizing access to markets.
- Wholesale market makers internalize orders — faster execution than exchange routing.

## The case against PFOF

- Retail order flow is kept away from lit exchanges, reducing price discovery quality for everyone.
- Price improvement is small; the market maker extracts more value from order information (option gamma from directional flow, latency arbitrage) than it returns.
- Creates an inherent conflict of interest — brokers maximize PFOF revenue, not execution quality.

## International context

PFOF is banned or restricted in the UK, EU, Canada, and Australia. The SEC has proposed but not finalized restrictions on PFOF in the U.S. (2022 proposal).

## Measuring execution quality

Rule 605 and 606 reports (required by the SEC) provide execution quality statistics:
- Effective spread (compared to quoted spread)
- Price improvement frequency
- Speed of execution

Sophisticated retail traders can compare their actual execution to NBBO midpoint to assess whether their broker is giving them a good deal.

## Pitfalls

- Zero-commission trading financed by PFOF is not truly "free" — the cost is embedded in slightly worse execution.
- The trade-off is favorable for small market orders but less clear for larger orders or limit orders where execution price matters more.
- PFOF creates incentives to encourage excessive trading (Robinhood's gamification controversy) since more trades = more PFOF revenue.

See also: [[nbbo|NBBO]], [[market-maker|Market Maker]], [[dark-pool|Dark Pool]], [[bid-ask-spread|Bid-Ask Spread]], [[liquidity|Liquidity]].

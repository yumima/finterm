A **dark pool** is a private electronic trading venue where large institutional orders are matched without pre-trade transparency — orders are "dark" because they don't appear in the public order book until after execution.

Dark pools were created to allow institutional investors (pension funds, mutual funds) to trade large blocks of stock without tipping off the market about their intentions, thereby avoiding adverse price impact.

## How dark pools work

```
Institution wants to sell 500,000 shares of AAPL
→ Placing on public exchange reveals intent: price drops before order fills
→ Instead: routes to dark pool

Dark pool matches seller with another institutional buyer anonymously
→ Trade executes at or near NBBO midpoint ($185.01 for example)
→ Trade reported to tape AFTER execution (post-trade transparency)
```

The "lit" public market never sees the order. Only the completed trade is reported.

## Types of dark pools

| Type | Operator | Characteristics |
|---|---|---|
| Broker-dealer internalization | Goldman SIGMA X, Morgan Stanley MS POOL | Matches client flow against broker inventory or other clients |
| Agency/exchange-owned | BATS TRF, NYSE Arcalight | Purely match client orders |
| Independent consortium | IEX (lit but with speed bump) | Independent ATS with protections |

## Volume and market share

Dark pool volume accounts for approximately 35–40% of total U.S. equity volume. Major dark pools:
- UBS ATS
- Credit Suisse CrossFinder (now UBS)
- Goldman Sachs SIGMA X2
- Morgan Stanley MS Pool
- Liquidnet (block trading focused)

## Benefits

- **Reduced market impact**: Large orders don't signal intent to HFT algorithms.
- **Price improvement**: Often execute at midpoint (between bid and ask), better than either side.
- **Reduced information leakage**: Identity and size remain confidential until post-trade.

## Criticisms and risks

- **Price discovery damage**: Moving volume off-exchange means lit exchange prices reflect less total trading interest.
- **Conflict of interest**: Broker dark pools may favor the broker's proprietary positions.
- **Front-running risk**: Within a broker's dark pool, there's potential for information exploitation.
- **Fragmentation**: 50+ dark pools in the U.S. create complexity and uncertainty about best execution.

## Regulatory controversy

SEC Rule ATS (Alternative Trading System) and FINRA Rule 4552 require reporting. Several dark pools were fined for violations:
- Barclays ($70M, 2016): misrepresented dark pool protections.
- Credit Suisse ($84M, 2016): gave advantages to high-frequency traders.
- ITG ($20M, 2015): ran a secret prop trading desk using client order flow.

## Pitfalls

- Dark pools provide best execution for large blocks but may not benefit small retail investors.
- "Toxic flow" (HFT firms routing to dark pools) can convert supposedly safe dark trading into information-loss scenarios.
- Not all midpoint executions in dark pools are genuine price improvements — they may simply delay price discovery until the lit market catches up.

See also: [[nbbo|NBBO]], [[market-maker|Market Maker]], [[payment-for-order-flow|Payment for Order Flow]], [[order-book|Order Book]], [[short-interest|Short Interest]].

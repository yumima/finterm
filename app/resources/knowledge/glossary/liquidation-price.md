The **liquidation price** is the asset price at which a leveraged position is automatically closed by the exchange because the trader's margin (collateral) is no longer sufficient to cover potential losses.

When a price approaches the liquidation level, the exchange's liquidation engine takes over and closes the position to prevent the account balance from going negative — protecting the exchange from counterparty losses.

## Formula

**Long position liquidation price:**

```
Liquidation Price (Long) = Entry Price × (1 − 1/Leverage + Maintenance Margin Rate)
```

**Short position liquidation price:**

```
Liquidation Price (Short) = Entry Price × (1 + 1/Leverage − Maintenance Margin Rate)
```

## Worked example

```
BTC entry: $65,000
Leverage: 10×
Maintenance margin: 0.5%

Long liquidation:  65,000 × (1 − 1/10 + 0.005) = 65,000 × 0.905 = $58,825
Short liquidation: 65,000 × (1 + 1/10 − 0.005) = 65,000 × 1.095 = $71,175

At 10× leverage long:
  BTC drops 9.6% → liquidation
  BTC would need to fall from $65k to $58.8k
```

## Cascade liquidations

In volatile markets, liquidations trigger sell orders, which push prices lower, triggering more liquidations — a cascade. This dynamic amplifies volatility in crypto markets:

```
BTC drops 5% → triggers levered longs → forced selling
→ BTC drops another 3% → triggers more longs → cascade
→ "Liquidation cascade" total: -15% in minutes
```

Major liquidation events are reported in real-time by data providers (Coinglass, CryptoQuant).

## Risk management around liquidation

| Leverage | Move to Liquidation | Risk |
|---|---|---|
| 2× | 50% adverse move | Low — appropriate for volatile assets |
| 5× | 20% adverse move | Moderate |
| 10× | 10% adverse move | High for crypto |
| 20× | 5% adverse move | Very high — crypto can move 5% in minutes |
| 100× | 1% adverse move | Extreme — near-certain eventual liquidation |

## Margin types

- **Cross margin**: entire account balance backs the position; harder to liquidate but full account at risk.
- **Isolated margin**: only the margin allocated to that position is at risk; liquidation doesn't affect rest of account.

## Pitfalls

- The exchange liquidates at the *mark price* (derived from multiple exchanges), not necessarily the last traded price — mark price smoothing prevents manipulation-triggered liquidations.
- Partial liquidation (at some exchanges) occurs before full liquidation to reduce position size and margin call risk.
- Funding rate costs can erode margin and bring the liquidation price closer even without adverse price movement.
- Stop-losses are not the same as liquidation price — always set a stop-loss well above (for longs) the liquidation price.

See also: [[funding-rate|Funding Rate]], [[margin|Margin]], [[margin-call|Margin Call]], [[leverage|Leverage]], [[volatility|Volatility]].

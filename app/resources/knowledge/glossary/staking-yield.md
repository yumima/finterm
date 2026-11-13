**Staking yield** is the annualized return earned by holding and "staking" a proof-of-stake cryptocurrency — locking tokens to participate in network validation and receiving newly issued tokens and/or transaction fees as reward.

Staking replaces mining in proof-of-stake (PoS) networks. Instead of computational power, validators put up collateral (staked tokens) to earn the right to propose and attest to new blocks.

## How staking works

```
User locks ETH → Becomes validator (or delegates to one)
→ Validates transactions / proposes blocks
→ Earns: new ETH issuance + transaction tips
→ Risk: "slashing" (penalty) for misbehavior or downtime
```

Minimum stake on Ethereum: 32 ETH to run a solo validator.
Liquid staking (Lido, Rocket Pool): any amount, receive a staking derivative token (stETH, rETH).

## Ethereum staking yield calculation

```
Annual yield ≈ (Total ETH issuance per year) / (Total ETH staked)

Approximate values (2024–2025):
Total ETH staked:   ~30 million ETH
ETH issuance rate:  ~0.3–0.5 million ETH/year (variable)
Base staking yield: ~3–4% annualized

Plus execution layer tips (EIP-1559):
During high activity periods: additional 0.5–2% yield
Total yield range: 3–6% depending on network activity
```

## Real yield vs nominal yield

Raw staking yield is **nominal** — denominated in the staked asset. To compare to traditional yields:

```
Real USD staking yield = Staking yield − ETH price change (+ if ETH appreciates)
```

If staking ETH earns 4% APY in ETH terms, but ETH rises 50%, total USD return is ~56%.
If ETH falls 30%, total USD return is ~−27% despite "4% yield."

## Liquid staking derivatives

| Protocol | Token | Yield Type |
|---|---|---|
| Lido | stETH | Rebasing (balance increases) |
| Rocket Pool | rETH | Accumulating (price increases) |
| Coinbase | cbETH | Non-rebasing wrapper |

Liquid staking tokens can be used in DeFi while still earning staking rewards — the staked ETH is "unlocked."

## Other PoS chains' staking yields

| Chain | Approximate Staking Yield |
|---|---|
| Solana (SOL) | 6–8% |
| Cosmos (ATOM) | 15–20% (high inflation) |
| Cardano (ADA) | 3–5% |
| Polkadot (DOT) | 12–15% |

High nominal yields often mean high token inflation — real yield (after inflation) may be negative.

## Pitfalls

- High staking yields in obscure protocols are usually funded by token inflation, not real economic activity — the yield is diluted by the same token issuance that pays it.
- Slashing risk: Ethereum validators can lose up to their full 32 ETH stake for certain offenses (double signing).
- Liquid staking concentrates power — Lido controls ~30%+ of staked ETH, creating systemic/centralization risk.
- Withdrawal queues exist on Ethereum — unstaking takes days to weeks depending on validator queue length.

See also: [[hash-rate|Hash Rate]], [[gas-fee|Gas Fee]], [[funding-rate|Funding Rate]], [[mvrv-ratio|MVRV Ratio]].

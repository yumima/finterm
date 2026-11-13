A **gas fee** is the transaction cost paid to validators on the Ethereum network (and similar EVM-compatible blockchains) for computing power required to process and confirm transactions or execute smart contracts.

"Gas" is the unit measuring the computational work needed for an operation. Every action on Ethereum — sending ETH, swapping tokens, minting NFTs — has a fixed gas cost in computation units, multiplied by the current gas price to determine the fee in ETH.

## Formula

```
Transaction fee = Gas Units Used × Gas Price (in gwei)
1 gwei = 0.000000001 ETH = 10^-9 ETH
```

Post EIP-1559 (August 2021), fees have two components:

```
Total Fee = Gas Used × (Base Fee + Priority Fee)
Base Fee:      Burned (removed from supply, deflationary)
Priority Fee:  Paid to validator (tip for faster inclusion)
```

## Worked example

```
Simple ETH transfer: 21,000 gas units
Base fee: 20 gwei
Priority tip: 2 gwei

Fee = 21,000 × (20 + 2) gwei = 462,000 gwei = 0.000462 ETH

At ETH = $3,000:  0.000462 × $3,000 = $1.39 per simple transfer

Complex DeFi swap: ~100,000–200,000 gas units
Fee: $7–$30 at 20 gwei base fee
```

## Gas price determinants

Gas prices are set by auction — users bid higher priority fees to get included faster in congested blocks. The base fee adjusts automatically based on block fullness:

- Block > 50% full → base fee rises next block (up to +12.5%)
- Block < 50% full → base fee falls next block

## Gas fee levels and congestion

| Base Fee | Network State | Activity |
|---|---|---|
| < 5 gwei | Very low | Low activity |
| 5–20 gwei | Normal | Typical daily use |
| 20–50 gwei | Busy | Active DeFi/NFT periods |
| 50–100 gwei | High | Major events (NFT launches, DeFi liquidations) |
| > 100 gwei | Congested | Mania events (CryptoKitties, NFT rush) |

Peak historical base fees exceeded 500 gwei during the 2021 NFT boom.

## Layer 2 and gas optimization

Ethereum Layer 2 solutions (Arbitrum, Optimism, Base, zkSync) dramatically reduce gas fees by batching transactions off-chain and submitting compressed proofs to Ethereum mainnet:

```
Ethereum mainnet:  $5–$50 per swap
Arbitrum/Base:     $0.05–$0.50 per swap (10–100× cheaper)
```

## EIP-1559 and ETH supply

The base fee burn makes Ethereum deflationary when network activity is high. During peak periods, ETH issuance can be more than offset by fee burning, creating negative net supply.

## Pitfalls

- Gas fees are paid in ETH even when transacting in other tokens — you must hold ETH to pay fees.
- Failed transactions on Ethereum still consume gas (and fees) for the computation attempted.
- Gas limits set too low result in transaction failure; set too high is wasteful but not harmful (unused gas is refunded).
- Gas costs on L2s depend partly on Ethereum mainnet gas prices (calldata costs), so L2 fees are not fully independent.

See also: [[staking-yield|Staking Yield]], [[funding-rate|Funding Rate]], [[hash-rate|Hash Rate]].

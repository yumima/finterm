**Hash rate** is the total computational power being used to mine and process transactions on a proof-of-work blockchain network, measured in hashes per second.

A higher hash rate means more computational resources are competing to find the next block — making the network more secure, harder to attack, and (for miners) more competitive.

## Units

| Unit | Value | Context |
|---|---|---|
| H/s | 1 hash/second | Individual CPU |
| MH/s | 10^6 | Early GPU mining |
| GH/s | 10^9 | Consumer ASIC |
| TH/s | 10^12 | Enterprise ASIC |
| EH/s | 10^18 | Network-level (Bitcoin) |

Bitcoin's network hash rate in 2024–2025 ranges between 500–700 EH/s.

## Why hash rate matters

**Security**: A "51% attack" — where an attacker controls the majority of hash rate to rewrite transaction history — becomes exponentially more expensive as total hash rate grows.

```
Cost to 51% attack Bitcoin ≈ $20–30B+ in hardware + ongoing electricity
(at 600 EH/s and $0.05/kWh electricity)
```

**Mining economics**: Higher network hash rate means more competition. Each miner's *expected* BTC revenue per unit of hash rate declines as more miners join.

```
Miner revenue ∝ Own hash rate / Total network hash rate
```

## Hash rate and difficulty adjustment

Bitcoin automatically adjusts mining difficulty every 2,016 blocks (~2 weeks) to maintain a ~10-minute block time:

```
New difficulty = Old difficulty × (Actual time for 2016 blocks / Target time)
```

If hash rate surges, difficulty rises. If miners leave (e.g., high energy prices, falling BTC price), difficulty falls, making mining easier for remaining participants.

## Hash rate as an on-chain indicator

- **Rising hash rate**: bullish long-term — new capital is deploying into mining infrastructure, which requires confidence in future BTC price.
- **Falling hash rate**: bearish or neutral — miners shutting down, often due to low price or rising costs; can also precede "miner capitulation" selling events.
- **Miner capitulation**: when BTC price falls below production cost, miners sell BTC reserves and sometimes shut down, historically marking market bottoms.

## Hashprice

**Hashprice** ($USD per TH/s per day) combines hash rate and BTC price into a profitability metric for miners:

```
Hashprice = (Block Reward × BTC Price × 86,400) / (Network Hash Rate × Block Time)
```

When hashprice falls below a miner's cost basis, they operate at a loss.

## Pitfalls

- Hash rate reported in real-time is an estimate, not a direct measurement — it is inferred from block frequency.
- A sudden drop in reported hash rate may just be a statistical artifact of variance in block times, not an actual decline in miners.
- Hash rate growth doesn't scale linearly with network security beyond a certain point — the marginal security improvement of each additional EH/s decreases.

See also: [[staking-yield|Staking Yield]], [[mvrv-ratio|MVRV Ratio]], [[funding-rate|Funding Rate]].

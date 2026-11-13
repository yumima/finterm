A **circuit breaker** is a regulatory mechanism that temporarily halts trading in a security or across an entire market when prices move beyond a predefined threshold — designed to prevent panic-driven free-falls and give investors time to process information.

Circuit breakers were introduced after the 1987 Black Monday crash and have been refined since. They function as "cooling off" periods during extreme market stress.

## Market-wide circuit breakers (S&P 500 based)

The SEC's Market Wide Circuit Breaker (MWCB) is triggered based on the S&P 500's decline from the prior day's close:

| Decline Level | Halt Duration | Time of Day |
|---|---|---|
| Level 1: −7% | 15-minute halt | If before 3:25 PM ET |
| Level 2: −13% | 15-minute halt | If before 3:25 PM ET |
| Level 3: −20% | Trading halted for rest of day | Any time |

If Level 1 or 2 is reached at or after 3:25 PM, trading continues without a halt.

## Single-stock circuit breakers (Limit Up-Limit Down)

The Limit Up-Limit Down (LULD) mechanism prevents individual stocks from trading outside a percentage band:

| Security Type | Price Band (each direction) |
|---|---|
| S&P 500 / Russell 1000 components | 5% for prices ≥$3 (10% for prices $0.75–$3) |
| Other NMS securities | 10% for prices ≥$3 (20% for prices $0.75–$3) |
| ETFs | 5% |

When a stock moves to the band limit, a 5-minute pause allows quotes to refresh within limits.

## Historical circuit breaker activations

| Date | Event | Trigger |
|---|---|---|
| Mar 9, 2020 | COVID fear | −7% (Level 1); first S&P MWCB since 1997 |
| Mar 12, 2020 | COVID continued | Level 1 again |
| Mar 16, 2020 | COVID black Monday | −7% and −13% on same day |
| Oct 27, 1997 | Asian crisis | −7% and −13% (first time system tested) |

## Individual securities

Single stocks can be halted for reasons beyond LULD:
- **News pending halts**: Trading suspended ahead of material announcement.
- **Regulatory halts**: SEC or FINRA-ordered pause.
- **Volatility halts**: LULD band breach.

## Options market circuit breakers

Options exchanges have their own volatility interruptions. During extreme market moves, options market makers may widen bid-ask spreads dramatically rather than halt, effectively making the market illiquid without a formal halt.

## International circuit breakers

Most major exchanges have equivalent systems:
- **South Korea**: Stock halts after 10% decline, triggered famously during 2020.
- **China (CSI 300)**: Circuit breaker introduced 2016; suspended after repeatedly triggering.
- **EU**: Individual stock halts vary by exchange.

China's circuit breaker was withdrawn after it paradoxically accelerated selling as investors rushed to exit before trading halted.

## Pitfalls

- Circuit breakers can create "front-running" behavior as investors try to sell just before a halt.
- China's experience shows poorly calibrated thresholds can accelerate the panic they're meant to prevent.
- Level 3 (−20%) halt is a nuclear option — markets closing entirely adds uncertainty about reopening conditions.
- Individual stock halts can strand positions in options strategies where the hedge becomes impossible to execute.

See also: [[vix|VIX]], [[volatility|Volatility]], [[tail-risk|Tail Risk]], [[liquidity|Liquidity]], [[market-maker|Market Maker]].

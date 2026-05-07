# Terra/Luna Collapse — May 2022

Between May 7 and May 13, 2022, the Terra blockchain ecosystem — worth approximately **$60 billion at its peak** — collapsed to near zero. The stablecoin TerraUSD (UST) lost its $1 peg and spiraled to $0.10. Its sister token LUNA fell from $116 to less than $0.0001 in ten days. An estimated **$40 billion in value was destroyed in 72 hours**. It was the largest sudden destruction of crypto wealth in history to that point.

## Background

Terra was the brainchild of Do Kwon and Daniel Shin, co-founders of Terraform Labs, based in South Korea. The Terra ecosystem ran on the LUNA token and featured an algorithmic stablecoin, UST, that maintained its $1 peg not through dollar reserves but through an on-chain mint-and-burn mechanism.

**The algorithmic peg mechanic:**
- If UST traded below $1: users could burn 1 UST to receive $1 worth of newly minted LUNA. This removes UST from supply (pushing price up).
- If UST traded above $1: users could burn $1 worth of LUNA to mint 1 new UST. This adds UST supply (pushing price down).
- The system relied entirely on arbitrageurs having confidence in LUNA's value — and on demand for UST growing faster than the mechanism could be tested.

To bootstrap demand for UST, Terraform Labs ran **Anchor Protocol**, which offered a ~20% APY on UST deposits. This yield — astronomically high for a stablecoin — attracted billions in deposits but was funded by Terraform's own reserves, not real economic activity. At its peak, Anchor held **$14 billion** in UST deposits.

By early 2022, the Anchor yield was clearly unsustainable. The Anchor reserve was depleting. Do Kwon proposed gradually reducing the rate. The community pushed back — but markets had already begun pricing the risk.

## What Happened

**May 7, 2022**: Large withdrawals from Anchor Protocol begin — approximately **$2 billion UST** leaves in 72 hours. Separately, a large holder begins selling hundreds of millions of dollars of UST on Curve Finance (the main DeFi liquidity pool for UST). Whether this was coordinated or opportunistic is disputed; some have speculated a deliberate attack.

**UST begins to depeg** — trading at $0.98, then $0.97.

**May 9**: The Luna Foundation Guard (LFG), a reserve body set up by Do Kwon, deploys its Bitcoin reserves to defend the peg — approximately **$3.5 billion in BTC**. It doesn't work. UST continues falling.

The death spiral activates:
1. UST falls below $1
2. Arbitrageurs burn UST for LUNA — this is working as designed
3. But the scale of UST being burned mints **enormous quantities of new LUNA**
4. LUNA supply hyperinflates: from 350 million tokens to **6.5 trillion tokens** in days
5. LUNA price collapses under the supply pressure
6. When LUNA is worthless, burning UST for LUNA provides no floor — the arb breaks down
7. UST has no backing; confidence collapses entirely

**May 12**: LUNA is effectively worth $0. UST is at $0.20 and still falling. The Terra blockchain itself is halted twice as validators can't keep up with the spam.

**Do Kwon's response** was widely criticized — slow, inconsistent, and ultimately unable to explain how the reserve could defend a peg when LUNA itself was the reserve. His Twitter posts during the collapse ranged from reassuring to dismissive.

Total losses: UST holders lost approximately $18 billion. LUNA holders lost approximately $30+ billion. LFG's BTC reserves were fully depleted; approximately 80,000 BTC was sold into a declining market, contributing to a broader crypto selloff that wiped another $300 billion from the total crypto market cap.

## Why It Happened

The architecture had a fatal flaw: it was circular. UST was backed by LUNA's value; LUNA derived its value partly from the demand for UST. This is a **reflexive system**, not a stable one. Under normal conditions (growing demand), the circularity is reinforcing. Under stress (declining demand), it is catastrophic.

Anchor Protocol's 20% yield created **artificial demand** for UST. That demand was the only reason billions of dollars sat in the ecosystem. When trust in the yield's sustainability eroded, the demand vanished. There was nothing underneath it.

The Luna Foundation Guard's Bitcoin reserve was never large enough. Defending a $18 billion stablecoin peg with $3.5 billion in BTC — itself falling during the crisis — was insufficient by design.

## Aftermath

- Do Kwon was arrested in Montenegro in March 2023. He was extradited to the U.S. in 2024 and faced fraud charges.
- The SEC charged Terraform Labs with securities fraud.
- Contagion spread to Three Arrows Capital (3AC), a major crypto hedge fund heavily exposed to LUNA/UST, which collapsed in June 2022.
- 3AC's collapse triggered Voyager Digital and Celsius Network to freeze withdrawals, eventually filing for bankruptcy.
- Bitcoin fell from ~$38,000 at the start of May to ~$26,000 by month end, and ~$16,000 by November 2022.
- Regulators globally accelerated stablecoin legislation, citing Terra as exhibit A.

## Lessons Learned

1. **Algorithmic stablecoins with circular backing are not stablecoins.** A dollar peg backed by a token whose value depends on demand for the dollar peg is a tautology. When confidence breaks, both sides collapse together.

2. **Unsustainably high yields are a warning sign, not a feature.** 20% APY on a "stable" asset signals either subsidy (Ponzi-adjacent) or risk that isn't being priced. If a yield looks too good, ask where it comes from.

3. **Size matters for peg defense.** A stablecoin reserve must be overcollateralized to weather selling pressure. A reserve equal to 20 cents on the dollar cannot stop a bank run.

4. **Reflexive systems are inherently fragile.** Many crypto tokenomics rely on circular reinforcement. These work during expansion and implode during contraction — the same dynamic as the yen carry trade but faster.

5. **Contagion in crypto is tightly interconnected.** Terra's collapse didn't stay in Terra. It moved through 3AC, through Celsius, through Voyager, through the entire DeFi ecosystem.

## Related Concepts

- [[stablecoin|Stablecoin]] — how different peg mechanisms work
- [[liquidity|Liquidity]] — how DeFi pools drain
- [[reflexivity|Reflexivity]] — when price affects fundamentals
- [[margin-call|Margin Call]] — 3AC's subsequent collapse
- [[bank-run|Bank Run]] — the Anchor Protocol withdrawal dynamic

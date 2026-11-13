"""Revenue Synergy Analysis"""
from typing import Dict, Any, List, Optional
from dataclasses import dataclass
from enum import Enum
import numpy as np

class RevenueSynergyType(Enum):
    CROSS_SELL = "cross_sell"
    MARKET_EXPANSION = "market_expansion"
    PRODUCT_BUNDLING = "product_bundling"
    PRICING_POWER = "pricing_power"
    DISTRIBUTION_ACCESS = "distribution_access"
    CHANNEL_OPTIMIZATION = "channel_optimization"
    BRAND_LEVERAGE = "brand_leverage"

@dataclass
class SynergyOpportunity:
    synergy_type: RevenueSynergyType
    description: str
    addressable_revenue: float
    conversion_rate: float
    ramp_years: int
    steady_state_year: int

class RevenueSynergyAnalyzer:
    """Analyze and quantify revenue synergies in M&A transactions"""

    def __init__(self, tax_rate: float = 0.25,
                 synergy_discount_rate: float = 0.15):
        self.tax_rate = tax_rate
        self.synergy_discount_rate = synergy_discount_rate

    def calculate_cross_sell_synergy(self, acquirer_customers: int,
                                     target_customers: int,
                                     acquirer_arpu: float,
                                     target_arpu: float,
                                     cross_sell_rate: float,
                                     ramp_years: int = 3) -> Dict[str, Any]:
        """Calculate cross-selling revenue synergy"""

        acquirer_to_target = target_customers * cross_sell_rate * acquirer_arpu
        target_to_acquirer = acquirer_customers * cross_sell_rate * target_arpu

        total_annual_synergy = acquirer_to_target + target_to_acquirer

        yearly_synergies = []
        for year in range(1, ramp_years + 4):
            if year <= ramp_years:
                realization = year / ramp_years
            else:
                realization = 1.0

            synergy = total_annual_synergy * realization
            pv = synergy / ((1 + self.synergy_discount_rate) ** year)

            yearly_synergies.append({
                'year': year,
                'realization_pct': realization * 100,
                'revenue_synergy': synergy,
                'after_tax': synergy * (1 - self.tax_rate),
                'present_value': pv * (1 - self.tax_rate)
            })

        total_pv = sum(y['present_value'] for y in yearly_synergies)

        return {
            'synergy_type': 'cross_sell',
            'acquirer_customers': acquirer_customers,
            'target_customers': target_customers,
            'cross_sell_rate': cross_sell_rate * 100,
            'acquirer_to_target_synergy': acquirer_to_target,
            'target_to_acquirer_synergy': target_to_acquirer,
            'total_annual_synergy': total_annual_synergy,
            'ramp_years': ramp_years,
            'yearly_projections': yearly_synergies,
            'total_pv': total_pv,
            'tax_rate': self.tax_rate * 100
        }

    def calculate_market_expansion_synergy(self, target_revenue_new_markets: float,
                                          acquirer_product_penetration: float,
                                          market_share_gain: float,
                                          ramp_years: int = 4) -> Dict[str, Any]:
        """Calculate geographic/market expansion synergy"""

        addressable_revenue = target_revenue_new_markets * acquirer_product_penetration
        annual_synergy = addressable_revenue * market_share_gain

        yearly_synergies = []
        for year in range(1, ramp_years + 4):
            if year <= ramp_years:
                realization = (year / ramp_years) ** 0.8
            else:
                realization = 1.0

            synergy = annual_synergy * realization
            pv = synergy / ((1 + self.synergy_discount_rate) ** year)

            yearly_synergies.append({
                'year': year,
                'realization_pct': realization * 100,
                'revenue_synergy': synergy,
                'after_tax': synergy * (1 - self.tax_rate),
                'present_value': pv * (1 - self.tax_rate)
            })

        total_pv = sum(y['present_value'] for y in yearly_synergies)

        return {
            'synergy_type': 'market_expansion',
            'target_revenue_new_markets': target_revenue_new_markets,
            'acquirer_product_penetration': acquirer_product_penetration * 100,
            'market_share_gain': market_share_gain * 100,
            'addressable_revenue': addressable_revenue,
            'annual_synergy_steady_state': annual_synergy,
            'ramp_years': ramp_years,
            'yearly_projections': yearly_synergies,
            'total_pv': total_pv
        }

    def calculate_product_bundling_synergy(self, standalone_products: List[Dict[str, float]],
                                          bundle_price_premium: float,
                                          bundle_adoption_rate: float,
                                          churn_reduction: float,
                                          ramp_years: int = 2) -> Dict[str, Any]:
        """Calculate product bundling synergy"""

        total_standalone_revenue = sum(p['revenue'] for p in standalone_products)

        bundle_revenue_lift = total_standalone_revenue * bundle_price_premium * bundle_adoption_rate

        retention_value = total_standalone_revenue * bundle_adoption_rate * churn_reduction

        total_annual_synergy = bundle_revenue_lift + retention_value

        yearly_synergies = []
        for year in range(1, ramp_years + 4):
            if year <= ramp_years:
                realization = year / ramp_years
            else:
                realization = 1.0

            synergy = total_annual_synergy * realization
            pv = synergy / ((1 + self.synergy_discount_rate) ** year)

            yearly_synergies.append({
                'year': year,
                'realization_pct': realization * 100,
                'revenue_synergy': synergy,
                'bundle_lift_component': bundle_revenue_lift * realization,
                'retention_component': retention_value * realization,
                'present_value': pv * (1 - self.tax_rate)
            })

        total_pv = sum(y['present_value'] for y in yearly_synergies)

        return {
            'synergy_type': 'product_bundling',
            'standalone_products': standalone_products,
            'total_standalone_revenue': total_standalone_revenue,
            'bundle_price_premium': bundle_price_premium * 100,
            'bundle_adoption_rate': bundle_adoption_rate * 100,
            'bundle_revenue_lift': bundle_revenue_lift,
            'retention_value': retention_value,
            'total_annual_synergy': total_annual_synergy,
            'yearly_projections': yearly_synergies,
            'total_pv': total_pv
        }

    def calculate_pricing_power_synergy(self, combined_revenue: float,
                                       price_increase: float,
                                       volume_elasticity: float,
                                       market_share: float,
                                       ramp_years: int = 3) -> Dict[str, Any]:
        """Calculate pricing power synergy from reduced competition"""

        volume_impact = -1 * price_increase * volume_elasticity

        net_revenue_impact = combined_revenue * (price_increase + volume_impact)

        concentration_risk_factor = 1 - (market_share * 0.5)
        risk_adjusted_synergy = net_revenue_impact * concentration_risk_factor

        yearly_synergies = []
        for year in range(1, ramp_years + 4):
            if year <= ramp_years:
                realization = year / ramp_years
            else:
                realization = 1.0

            synergy = risk_adjusted_synergy * realization
            pv = synergy / ((1 + self.synergy_discount_rate) ** year)

            yearly_synergies.append({
                'year': year,
                'realization_pct': realization * 100,
                'revenue_synergy': synergy,
                'present_value': pv * (1 - self.tax_rate)
            })

        total_pv = sum(y['present_value'] for y in yearly_synergies)

        return {
            'synergy_type': 'pricing_power',
            'combined_revenue': combined_revenue,
            'price_increase': price_increase * 100,
            'volume_elasticity': volume_elasticity,
            'volume_impact': volume_impact * 100,
            'gross_revenue_impact': net_revenue_impact,
            'concentration_risk_factor': concentration_risk_factor,
            'risk_adjusted_synergy': risk_adjusted_synergy,
            'yearly_projections': yearly_synergies,
            'total_pv': total_pv
        }

    def calculate_distribution_synergy(self, target_revenue: float,
                                      acquirer_distribution_advantage: float,
                                      incremental_reach: float,
                                      ramp_years: int = 3) -> Dict[str, Any]:
        """Calculate distribution channel synergy"""

        addressable_revenue = target_revenue * incremental_reach

        revenue_uplift = addressable_revenue * acquirer_distribution_advantage

        yearly_synergies = []
        for year in range(1, ramp_years + 4):
            if year <= ramp_years:
                realization = year / ramp_years
            else:
                realization = 1.0

            synergy = revenue_uplift * realization
            pv = synergy / ((1 + self.synergy_discount_rate) ** year)

            yearly_synergies.append({
                'year': year,
                'realization_pct': realization * 100,
                'revenue_synergy': synergy,
                'present_value': pv * (1 - self.tax_rate)
            })

        total_pv = sum(y['present_value'] for y in yearly_synergies)

        return {
            'synergy_type': 'distribution',
            'target_revenue': target_revenue,
            'acquirer_distribution_advantage': acquirer_distribution_advantage * 100,
            'incremental_reach': incremental_reach * 100,
            'addressable_revenue': addressable_revenue,
            'annual_revenue_uplift': revenue_uplift,
            'yearly_projections': yearly_synergies,
            'total_pv': total_pv
        }

    def comprehensive_revenue_synergy(self, opportunities: List[SynergyOpportunity]) -> Dict[str, Any]:
        """Aggregate multiple revenue synergy opportunities"""

        synergy_breakdown = []
        total_pv = 0

        for opp in opportunities:
            annual_synergy = opp.addressable_revenue * opp.conversion_rate

            yearly_projections = []
            for year in range(1, 7):
                if year <= opp.ramp_years:
                    realization = year / opp.ramp_years
                elif year < opp.steady_state_year:
                    realization = 1.0
                else:
                    realization = 1.0

                synergy = annual_synergy * realization
                pv = synergy / ((1 + self.synergy_discount_rate) ** year)

                yearly_projections.append({
                    'year': year,
                    'synergy': synergy,
                    'present_value': pv * (1 - self.tax_rate)
                })

            opp_pv = sum(y['present_value'] for y in yearly_projections)
            total_pv += opp_pv

            synergy_breakdown.append({
                'synergy_type': opp.synergy_type.value,
                'description': opp.description,
                'addressable_revenue': opp.addressable_revenue,
                'conversion_rate': opp.conversion_rate * 100,
                'annual_synergy_steady_state': annual_synergy,
                'ramp_years': opp.ramp_years,
                'present_value': opp_pv,
                'yearly_projections': yearly_projections
            })

        return {
            'total_revenue_synergy_pv': total_pv,
            'synergy_breakdown': synergy_breakdown,
            'number_of_opportunities': len(opportunities),
            'discount_rate': self.synergy_discount_rate * 100,
            'tax_rate': self.tax_rate * 100
        }

    def synergy_risk_adjustment(self, base_synergy_pv: float,
                               execution_risk: str = 'medium',
                               timing_risk: str = 'medium') -> Dict[str, Any]:
        """Apply risk adjustments to revenue synergy estimates"""

        execution_discounts = {
            'low': 0.10,
            'medium': 0.25,
            'high': 0.40,
            'very_high': 0.60
        }

        timing_discounts = {
            'low': 0.05,
            'medium': 0.15,
            'high': 0.25
        }

        exec_discount = execution_discounts.get(execution_risk, 0.25)
        timing_discount = timing_discounts.get(timing_risk, 0.15)

        combined_discount = exec_discount + timing_discount - (exec_discount * timing_discount)

        risk_adjusted_pv = base_synergy_pv * (1 - combined_discount)

        return {
            'base_synergy_pv': base_synergy_pv,
            'execution_risk': execution_risk,
            'timing_risk': timing_risk,
            'execution_discount': exec_discount * 100,
            'timing_discount': timing_discount * 100,
            'combined_discount': combined_discount * 100,
            'risk_adjusted_synergy_pv': risk_adjusted_pv,
            'haircut_amount': base_synergy_pv - risk_adjusted_pv
        }

# ── JSON contract (MAAnalyticsService "calculate") ───────────────────────────
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from corporateFinance._cli import is_json_call, run_json, num, opt_num, text, has, pct  # noqa: E402
from corporateFinance.synergies.synergy_valuation import (  # noqa: E402
    project_synergies, projection_inputs)


def _run_rate_revenue_synergy(p: Dict[str, Any]) -> Dict[str, Any]:
    """Steady-state annual revenue synergy for the chosen method; every driver
    is a caller input."""
    kind = text(p, 'type', choices=('simple', 'cross_sell', 'market_expansion', 'pricing_power')) \
        if has(p, 'type') else 'simple'
    if kind == 'simple':
        base = num(p, 'combined_revenue', min=0)
        frac = num(p, 'synergy_pct', label='synergy_pct (decimal share of combined revenue)', min=0, max=1)
        return {'type': kind, 'run_rate': base * frac, 'combined_revenue': base, 'synergy_pct': pct(frac)}
    if kind == 'cross_sell':
        acq_c = num(p, 'acquirer_customers', min=0)
        tgt_c = num(p, 'target_customers', min=0)
        acq_arpu = num(p, 'acquirer_arpu', min=0)
        tgt_arpu = num(p, 'target_arpu', min=0)
        rate = num(p, 'cross_sell_rate', label='cross_sell_rate (decimal)', min=0, max=1)
        a2t = tgt_c * rate * acq_arpu   # acquirer products sold to target customers
        t2a = acq_c * rate * tgt_arpu   # target products sold to acquirer customers
        return {'type': kind, 'run_rate': a2t + t2a,
                'acquirer_to_target_synergy': a2t, 'target_to_acquirer_synergy': t2a,
                'cross_sell_rate_pct': pct(rate)}
    if kind == 'market_expansion':
        rev = num(p, 'target_revenue_new_markets', min=0)
        pen = num(p, 'acquirer_product_penetration', label='acquirer_product_penetration (decimal)', min=0, max=1)
        gain = num(p, 'market_share_gain', label='market_share_gain (decimal)', min=0, max=1)
        addressable = rev * pen
        return {'type': kind, 'run_rate': addressable * gain, 'addressable_revenue': addressable}
    # pricing_power: exact revenue effect of a price rise with a constant
    # volume elasticity: R * ((1 + dp) * (1 - e * dp) - 1)
    base = num(p, 'combined_revenue', min=0)
    dp = num(p, 'price_increase', label='price_increase (decimal)', min=0, max=1)
    e = num(p, 'volume_elasticity', label='volume_elasticity (volume % lost per 1% price)', min=0)
    vol = -e * dp
    return {'type': kind, 'run_rate': base * ((1 + dp) * (1 + vol) - 1),
            'combined_revenue': base, 'price_increase_pct': pct(dp), 'volume_change_pct': pct(vol)}


def json_calculate(p: Dict[str, Any]) -> Dict[str, Any]:
    """Revenue synergy run-rate + DCF. Requires revenue_synergy_margin,
    tax_rate, discount_rate, ramp_years, projection_years; optional
    terminal_growth, integration_cost, integration_cost_years."""
    rr = _run_rate_revenue_synergy(p)
    margin = num(p, 'revenue_synergy_margin',
                 label='revenue_synergy_margin (decimal EBITDA margin on synergy revenue)', min=0, max=1)
    t = projection_inputs(p)
    val = project_synergies(rr['run_rate'], margin, 0.0, opt_num(p, 'integration_cost', 0.0, min=0), **t)
    out = {'synergy_type': rr['type'], 'run_rate_revenue_synergy': rr['run_rate']}
    out.update({k: v for k, v in rr.items() if k not in ('type', 'run_rate')})
    out.update({k: v for k, v in val.items() if k not in ('run_rate_cost_synergy',)})
    return out

def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({"calculate": json_calculate})


if __name__ == '__main__':
    main()

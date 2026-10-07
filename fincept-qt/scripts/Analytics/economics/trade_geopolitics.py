
"""Economic Trade Geopolitics Module
=============================

Trade and geopolitical risk analysis

===== DATA SOURCES REQUIRED =====
INPUT:
  - Macroeconomic time series data from official sources
  - Central bank policy statements and interest rate data
  - International trade and balance of payments statistics
  - Market indicators and sentiment measures
  - Demographic and structural economic data

OUTPUT:
  - Economic trend analysis and forecasts
  - Policy impact assessment and scenario modeling
  - Market cycle identification and timing analysis
  - Cross-country economic comparisons and rankings
  - Investment recommendations based on economic outlook

PARAMETERS:
  - forecast_horizon: Economic forecast horizon (default: 12 months)
  - confidence_level: Confidence level for predictions (default: 0.90)
  - base_currency: Base currency for analysis (default: 'USD')
  - seasonal_adjustment: Seasonal adjustment method (default: true)
  - lookback_period: Historical analysis period (default: 10 years)


"""

import math
from decimal import Decimal
from typing import Dict, List, Any, Optional, Tuple

try:
    from .core import EconomicsBase, ValidationError
except ImportError:  # run as a script (python trade_geopolitics.py ...): sibling import
    from core import EconomicsBase, ValidationError


# ── Tariff price / volume framework ─────────────────────────────────────────
#
# Partial-equilibrium import market, ad valorem tariff t (fraction):
#   pass-through pt in [0, 1] is the elasticity of the domestic import price
#   with respect to (1 + t):  P_d1/P_d0 = ((1+t1)/(1+t0))^pt
#   the rest falls on the foreign border price: P_w1/P_w0 = ((1+t1)/(1+t0))^(pt-1)
#   (pt = 1: small country, full pass-through, border price unchanged).
#   Import demand is iso-elastic with elasticity -e (e > 0):
#       Q1/Q0 = (P_d1/P_d0)^(-e)            (exact for constant elasticity)
#   the linear approximation -e * dP/P is reported alongside.
#   Border import value V = P_w Q; tariff revenue R = t V.
#   Consumer surplus change on the import demand curve (area left of the
#   curve between the two domestic prices), D0 = V0 (1 + t0):
#       dCS = D0 (r^(1-e) - 1) / (e - 1)      (e != 1)
#       dCS = D0 ln(1/r)                      (e == 1)
#   National welfare change = dCS + dR (terms-of-trade effects enter through
#   the border price inside R and D).

def _iso_cs_change(d0: float, r: float, e: float) -> float:
    """Consumer-surplus gain when the domestic price moves from P0 to r*P0
    on Q = Q0 (P/P0)^-e, with D0 = P0*Q0. Positive when r < 1."""
    if abs(e - 1.0) < 1e-12:
        return d0 * math.log(1.0 / r)
    return d0 * (r ** (1.0 - e) - 1.0) / (e - 1.0)


def tariff_change_effects(t0: float, t1: float, elasticity: float, pass_through: float,
                          import_value: Optional[float] = None) -> Dict[str, Any]:
    """Effects of moving an ad valorem tariff from t0 to t1 (fractions).
    `elasticity` is the magnitude of the import demand elasticity (> 0),
    `pass_through` in [0, 1]; `import_value` (border value at t0) scales the
    amounts and is optional (percent effects need no value)."""
    if t0 < 0 or t1 < 0:
        raise ValidationError("tariff rates must be >= 0")
    if elasticity <= 0:
        raise ValidationError("import demand elasticity must be > 0 (magnitude)")
    if not 0 <= pass_through <= 1:
        raise ValidationError("pass-through must be between 0 and 100%")
    k = (1.0 + t1) / (1.0 + t0)
    r = k ** pass_through                 # domestic import price ratio
    b = k ** (pass_through - 1.0)         # border price ratio
    q = r ** (-elasticity)                # import volume ratio
    out = {
        'domestic_import_price_change_pct': (r - 1.0) * 100,
        'border_price_change_pct': (b - 1.0) * 100,
        'import_volume_change_pct': (q - 1.0) * 100,
        'import_volume_change_linear_approx_pct': -elasticity * (r - 1.0) * 100,
        'border_import_value_change_pct': (q * b - 1.0) * 100,
    }
    if import_value is not None:
        v0 = float(import_value)
        if v0 < 0:
            raise ValidationError("import value must be >= 0")
        v1 = v0 * q * b
        r0, r1 = t0 * v0, t1 * v1
        d_cs = _iso_cs_change(v0 * (1.0 + t0), r, elasticity)
        out.update({
            'import_value_before': v0,
            'import_value_after': v1,
            'tariff_revenue_before': r0,
            'tariff_revenue_after': r1,
            'tariff_revenue_change': r1 - r0,
            'consumer_surplus_change': d_cs,
            'net_welfare_change': d_cs + (r1 - r0),
        })
    return out


class TradeAnalyzer(EconomicsBase):
    """International trade analysis and policy assessment"""

    def analyze_trade_benefits_costs(self, trade_data: Dict[str, Any]) -> Dict[str, Any]:
        """Analyze benefits and costs of international trade"""
        return {
            'trade_benefits': {
                'efficiency_gains': {
                    'comparative_advantage': 'Countries specialize in relative strengths',
                    'resource_allocation': 'More efficient global resource use',
                    'scale_economies': 'Larger markets enable economies of scale',
                    'quantitative_benefit': self._calculate_trade_gains(trade_data)
                },
                'consumer_benefits': {
                    'variety': 'Greater product variety and choice',
                    'lower_prices': 'Increased competition reduces prices',
                    'quality_improvement': 'Competition drives quality improvements',
                    'consumer_surplus_gain': self._estimate_consumer_surplus_gain(trade_data)
                },
                'growth_benefits': {
                    'technology_transfer': 'Access to foreign technology and knowledge',
                    'productivity_spillovers': 'Learning from foreign competition',
                    'investment_flows': 'Foreign direct investment attraction',
                    'innovation_incentives': 'Competition spurs innovation'
                }
            },
            'trade_costs': {
                'adjustment_costs': {
                    'job_displacement': 'Workers in import-competing industries lose jobs',
                    'regional_impacts': 'Concentrated effects in specific regions',
                    'skill_premiums': 'Wage gaps between skilled/unskilled workers',
                    'adjustment_period': 'Time and cost of worker reallocation'
                },
                'distributional_effects': {
                    'income_inequality': 'May worsen within-country inequality',
                    'factor_returns': 'Changes in wages, profits, land rents',
                    'sectoral_shifts': 'Decline of import-competing sectors',
                    'compensation_needs': 'Required support for affected workers'
                },
                'vulnerability_risks': {
                    'import_dependence': 'Reliance on foreign suppliers',
                    'economic_security': 'Potential supply chain disruptions',
                    'policy_autonomy': 'Constraints on domestic policy flexibility'
                }
            },
            'net_welfare_assessment': self._assess_net_welfare_impact(trade_data)
        }

    def analyze_trade_restrictions(self, restriction_data: Dict[str, Any]) -> Dict[str, Any]:
        """Analyze different types of trade restrictions and their impacts"""
        return {
            'tariffs': {
                'mechanism': 'Tax on imports',
                'economic_effects': self._analyze_tariff_effects(restriction_data.get('tariff_rate'), restriction_data),
                'revenue_generation': 'Provides government revenue',
                'protection_level': 'Proportional to tariff rate',
                'welfare_impact': 'Net welfare loss (deadweight loss)'
            },
            'quotas': {
                'mechanism': 'Quantity limit on imports',
                'economic_effects': self._analyze_quota_effects(restriction_data.get('quota_volume')),
                'revenue_generation': 'No government revenue (quota rents to importers)',
                'protection_level': 'Fixed quantity protection',
                'welfare_impact': 'Similar to tariffs but different rent distribution'
            },
            'export_subsidies': {
                'mechanism': 'Government payments to exporters',
                'economic_effects': self._analyze_subsidy_effects(restriction_data.get('subsidy_rate')),
                'revenue_generation': 'Costs government revenue',
                'protection_level': 'Supports domestic producers',
                'welfare_impact': 'Welfare loss in subsidizing country'
            },
            'non_tariff_barriers': {
                'types': ['Technical standards', 'Sanitary measures', 'Administrative procedures'],
                'effects': 'Hidden protection, often more restrictive than tariffs',
                'measurement_difficulty': 'Hard to quantify economic impact',
                'welfare_impact': 'Potentially large welfare costs'
            },
            'restriction_comparison': self._compare_trade_restrictions(),
            'optimal_policy_recommendation': self._recommend_trade_policy(restriction_data)
        }

    def analyze_trading_blocs(self, bloc_data: Dict[str, Any]) -> Dict[str, Any]:
        """Analyze trading blocs, common markets, and economic unions"""
        integration_types = {
            'free_trade_area': {
                'definition': 'Eliminate tariffs among members, keep individual external tariffs',
                'examples': ['NAFTA/USMCA', 'ASEAN FTA'],
                'advantages': ['Trade creation', 'Market access', 'Political cooperation'],
                'disadvantages': ['Trade diversion', 'Rules of origin complexity'],
                'economic_impact': self._assess_fta_impact(bloc_data)
            },
            'customs_union': {
                'definition': 'Free trade area plus common external tariff',
                'examples': ['EU Customs Union', 'Mercosur'],
                'advantages': ['Eliminates trade deflection', 'Stronger negotiating power'],
                'disadvantages': ['Loss of tariff autonomy', 'Complex revenue sharing'],
                'economic_impact': self._assess_customs_union_impact(bloc_data)
            },
            'common_market': {
                'definition': 'Customs union plus free movement of factors',
                'examples': ['EU Single Market', 'ECOWAS'],
                'advantages': ['Factor mobility benefits', 'Efficiency gains', 'Scale economies'],
                'disadvantages': ['Adjustment pressures', 'Migration concerns', 'Policy coordination needs'],
                'economic_impact': self._assess_common_market_impact(bloc_data)
            },
            'economic_union': {
                'definition': 'Common market plus unified economic policies',
                'examples': ['European Union', 'Proposed ASEAN Economic Community'],
                'advantages': ['Maximum integration benefits', 'Policy coherence', 'Stability'],
                'disadvantages': ['Sovereignty loss', 'Complex governance', 'Asymmetric effects'],
                'economic_impact': self._assess_economic_union_impact(bloc_data)
            }
        }

        type_key = {'fta': 'free_trade_area'}.get(bloc_data.get('integration_type'),
                                                    bloc_data.get('integration_type'))
        summary: Dict[str, Any] = {'integration_type': type_key}
        if type_key is not None and type_key not in integration_types:
            raise ValidationError(f"Unknown integration_type: {bloc_data.get('integration_type')}")
        if bloc_data.get('trade_creation') is not None and bloc_data.get('trade_diversion') is not None:
            tc = float(bloc_data['trade_creation'])
            td = float(bloc_data['trade_diversion'])
            summary.update({
                'trade_creation': tc,
                'trade_diversion': td,
                'net_trade_creation': tc - td,
                'diversion_share_pct': td / (tc + td) * 100 if (tc + td) > 0 else None,
                'note': ('Volumes, not welfare: diverted trade costs the gap between the non-member and member '
                         'supply prices, created trade gains the gap to domestic cost'),
            })
        return {
            'selected': summary,
            'integration_levels': integration_types,
            'motivations_for_integration': self._analyze_integration_motivations(),
            'success_factors': self._identify_integration_success_factors(),
            'trade_creation_vs_diversion': self._analyze_trade_creation_diversion(bloc_data)
        }

    def assess_trade_barrier_removal(self, d: Dict[str, Any]) -> Dict[str, Any]:
        """Quantify a tariff cut. Required: current_tariff_pct (ad valorem
        level today), tariff_cut_pct (share of the current tariff removed,
        0-100: 50 turns a 20% tariff into 10%, 100 = full removal),
        import_demand_elasticity (magnitude), pass_through_pct (0-100),
        import_value (border value at the current tariff). Optional: gdp (same
        unit as import_value) to express the welfare change as % of GDP."""
        req = ['current_tariff_pct', 'tariff_cut_pct', 'import_demand_elasticity',
               'pass_through_pct', 'import_value']
        missing = [k for k in req if d.get(k) is None or d.get(k) == '']
        if missing:
            raise ValidationError('Missing required input(s): ' + ', '.join(missing))
        t0 = float(d['current_tariff_pct'])
        cut = float(d['tariff_cut_pct'])
        if not 0 < cut <= 100:
            raise ValidationError('tariff_cut_pct must be in (0, 100] — the share of the current tariff removed')
        t1 = t0 * (1.0 - cut / 100.0)
        res = tariff_change_effects(t0 / 100.0, t1 / 100.0, float(d['import_demand_elasticity']),
                                    float(d['pass_through_pct']) / 100.0, float(d['import_value']))
        gdp = d.get('gdp')
        if gdp is not None and float(gdp) > 0:
            res['net_welfare_change_pct_gdp'] = res['net_welfare_change'] / float(gdp) * 100
        res['new_tariff_pct'] = t1
        res['tariff_cut_pct_points'] = t0 - t1
        res['assumptions'] = {
            'current_tariff_pct': t0,
            'tariff_cut_pct': cut,
            'import_demand_elasticity': float(d['import_demand_elasticity']),
            'pass_through_pct': float(d['pass_through_pct']),
            'import_value': float(d['import_value']),
            'gdp': float(gdp) if gdp is not None else None,
        }
        res['model'] = ('Partial equilibrium, iso-elastic import demand; domestic price ratio = '
                        '((1+t1)/(1+t0))^pass_through; welfare = consumer surplus change + tariff revenue change')
        return res

    def _calculate_trade_gains(self, data: Dict[str, Any]) -> Any:
        """Calculate quantitative trade gains.

        Requires the caller's trade_volume_gdp and efficiency_gain_percent (an
        explicit assumption, echoed in the result so it is never mistaken for a
        measurement); either missing -> None.
        """
        if data.get('trade_volume_gdp') is None or data.get('efficiency_gain_percent') is None:
            return None
        trade_volume = self.to_decimal(data['trade_volume_gdp'])
        eff_pct = self.to_decimal(data['efficiency_gain_percent'])
        return {
            'value_pct_gdp': trade_volume * eff_pct / self.to_decimal(100),
            'assumed_efficiency_gain_percent': eff_pct,
            'basis': 'trade_volume_gdp x assumed efficiency gain (illustrative assumption, not measured)'
        }

    def _estimate_consumer_surplus_gain(self, data: Dict[str, Any]) -> Any:
        """Estimate consumer surplus gains from trade (simplified triangle
        approximation). Both inputs are required; missing -> None."""
        if data.get('price_reduction_percent') is None or data.get('traded_goods_consumption') is None:
            return None
        price_reduction = self.to_decimal(data['price_reduction_percent'])
        consumption_share = self.to_decimal(data['traded_goods_consumption'])
        return price_reduction * consumption_share / self.to_decimal(200)  # Simplified calculation

    def _analyze_tariff_effects(self, tariff_rate: Any, data: Dict[str, Any] = None) -> Dict[str, Any]:
        """Effects of imposing `tariff_rate` (percent) from free trade, using
        tariff_change_effects(). Import demand elasticity and pass-through are
        required inputs (no assumed values); without them no numbers."""
        if tariff_rate is None:
            return {'status': 'Tariff rate not provided - no quantitative estimate'}
        data = data or {}
        if data.get('import_demand_elasticity') is None or data.get('pass_through_pct') is None:
            return {'status': 'Provide import demand elasticity and pass-through to quantify tariff effects'}
        res = tariff_change_effects(0.0, float(tariff_rate) / 100.0,
                                    float(data['import_demand_elasticity']),
                                    float(data['pass_through_pct']) / 100.0,
                                    data.get('import_value'))
        res['basis'] = (f"Tariff 0% -> {float(tariff_rate):g}%, import demand elasticity "
                        f"{float(data['import_demand_elasticity']):g}, pass-through "
                        f"{float(data['pass_through_pct']):g}% (iso-elastic import demand)")
        return res

    def _analyze_quota_effects(self, quota_volume: float) -> Dict[str, Any]:
        """Analyze economic effects of import quotas"""
        return {
            'price_effect': 'Domestic price rises to clear market at quota level',
            'quantity_certainty': 'Import volume fixed regardless of demand changes',
            'rent_distribution': 'Quota rents accrue to license holders',
            'supply_response': 'Domestic producers expand to fill demand gap'
        }

    def _analyze_subsidy_effects(self, subsidy_rate: Any) -> Dict[str, Any]:
        """Analyze economic effects of export subsidies (multipliers are
        stated assumptions; missing rate yields no numbers)"""
        if subsidy_rate is None:
            return {'status': 'Subsidy rate not provided - no quantitative estimate'}
        rate = self.to_decimal(subsidy_rate)
        return {
            'export_increase': f"Exports rise by approximately {rate * self.to_decimal(1.2)}% (assumed export elasticity 1.2)",
            'domestic_price_rise': f"Domestic price increases by {rate * self.to_decimal(0.5)}% (assumed 50% pass-through)",
            'fiscal_cost': f"Government cost {rate}% of export value",
            'foreign_welfare': 'Foreign consumers benefit from lower prices'
        }

    def _compare_trade_restrictions(self) -> Dict[str, str]:
        """Compare different trade restriction types"""
        return {
            'transparency': 'Tariffs > Quotas > Non-tariff barriers',
            'revenue_generation': 'Tariffs > Export subsidies (cost) > Quotas (no revenue)',
            'welfare_impact': 'All create deadweight losses, magnitude varies',
            'administrative_burden': 'Non-tariff barriers > Quotas > Tariffs',
            'flexibility': 'Tariffs > Export subsidies > Quotas'
        }

    def _recommend_trade_policy(self, data: Dict[str, Any]) -> str:
        """Recommend optimal trade policy"""
        development_level = data.get('development_level', 'middle')
        industry_maturity = data.get('industry_maturity', 'mature')

        if development_level == 'developing' and industry_maturity == 'infant':
            return 'Temporary protection may be justified for infant industries'
        elif development_level == 'developed':
            return 'Free trade generally optimal for developed economies'
        else:
            return 'Gradual liberalization with adjustment assistance'

    def _assess_fta_impact(self, data: Dict[str, Any]) -> str:
        """Assess free trade agreement impact"""
        if data.get('trade_creation') is None or data.get('trade_diversion') is None:
            return 'Trade creation/diversion estimates not provided - cannot assess net effect'
        trade_creation = self.to_decimal(data['trade_creation'])
        trade_diversion = self.to_decimal(data['trade_diversion'])

        if trade_creation > trade_diversion:
            return 'Net welfare gain from trade creation effects'
        else:
            return 'Potential welfare loss from trade diversion'

    def _assess_customs_union_impact(self, data: Dict[str, Any]) -> str:
        """Assess customs union impact"""
        return 'Generally more beneficial than FTA due to common external tariff'

    def _assess_common_market_impact(self, data: Dict[str, Any]) -> str:
        """Assess common market impact"""
        return 'Significant benefits from factor mobility, but requires strong institutions'

    def _assess_economic_union_impact(self, data: Dict[str, Any]) -> str:
        """Assess economic union impact"""
        return 'Maximum benefits but requires political integration and sovereignty transfer'

    def _analyze_integration_motivations(self) -> List[str]:
        """Analyze motivations for regional integration"""
        return [
            'Economic: Market access, scale economies, efficiency gains',
            'Political: Peace, cooperation, international influence',
            'Strategic: Counterbalance to other blocs, bargaining power',
            'Development: Technology transfer, investment attraction'
        ]

    def _identify_integration_success_factors(self) -> List[str]:
        """Identify factors for successful regional integration"""
        return [
            'Geographic proximity and cultural similarity',
            'Similar development levels and economic structures',
            'Political commitment and institutional capacity',
            'Complementary rather than competing economies',
            'Mechanism for handling adjustment costs'
        ]

    def _analyze_trade_creation_diversion(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Analyze trade creation vs trade diversion effects"""
        return {
            'trade_creation': {
                'definition': 'New trade due to elimination of barriers among members',
                'welfare_effect': 'Positive - increases efficiency',
                'mechanism': 'Efficient producers replace inefficient domestic production'
            },
            'trade_diversion': {
                'definition': 'Trade shifts from efficient non-members to less efficient members',
                'welfare_effect': 'Negative - reduces efficiency',
                'mechanism': 'Preferential access distorts comparative advantage'
            },
            'net_effect': 'Depends on relative magnitude of creation vs diversion'
        }

    def _estimate_fdi_increase(self, data: Dict[str, Any]) -> str:
        """Estimate FDI increase from liberalization"""
        liberalization_scope = data.get('liberalization_scope', 'moderate')

        increases = {
            'limited': '20-40% increase over 5 years',
            'moderate': '50-100% increase over 5 years',
            'comprehensive': '100-200% increase over 5 years'
        }

        return increases.get(liberalization_scope, '50-100% increase over 5 years')

    def _estimate_productivity_gains(self, data: Dict[str, Any]) -> str:
        """Estimate productivity gains from liberalization"""
        return '2-5% productivity gain over 5-10 years'

    def _estimate_employment_effects(self, data: Dict[str, Any]) -> str:
        """Estimate employment effects of liberalization"""
        return 'Short-term adjustment costs, long-term employment gains'

    def _analyze_skill_premium_effects(self, data: Dict[str, Any]) -> str:
        """Analyze effects on skill premiums"""
        return 'Skill premium may increase initially, then stabilize with education/training'

    def _estimate_gdp_impact(self, data: Dict[str, Any]) -> str:
        """Estimate GDP impact of trade liberalization"""
        return '1-3% permanent GDP level increase, spread over 10-15 years'

    def _analyze_sectoral_growth_effects(self, data: Dict[str, Any]) -> Dict[str, str]:
        """Analyze sectoral growth effects"""
        return {
            'export_sectors': 'Strong growth, increased investment',
            'import_competing_sectors': 'Decline, but may become more efficient',
            'service_sectors': 'Generally benefit from lower input costs',
            'technology_sectors': 'Benefit from knowledge spillovers'
        }

    def _assess_regional_impact_variation(self, data: Dict[str, Any]) -> str:
        """Assess regional variation in impacts"""
        return 'Urban areas and regions with comparative advantage benefit most'

    def _recommend_liberalization_policies(self, data: Dict[str, Any]) -> List[str]:
        """Recommend supporting policies for liberalization"""
        return [
            'Trade adjustment assistance for displaced workers',
            'Education and training programs for skill upgrading',
            'Infrastructure investment to support new trade patterns',
            'Competition policy to ensure domestic market efficiency',
            'Social safety net to manage transition costs'
        ]

    def _assess_net_welfare_impact(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Assess net welfare impact of trade"""
        return {
            'aggregate_welfare': 'Generally positive but distribution matters',
            'time_dimension': 'Short-term costs, long-term benefits',
            'policy_implications': 'Need complementary policies for inclusive growth',
            'measurement_challenges': 'Difficult to quantify all benefits and costs'
        }

    def calculate(self, analysis_type: str = 'benefits_costs', **kwargs) -> Dict[str, Any]:
        """Main trade analysis dispatcher"""
        analyses = {
            'benefits_costs': lambda: self.analyze_trade_benefits_costs(kwargs.get('trade_data', {})),
            'restrictions': lambda: self.analyze_trade_restrictions(kwargs.get('restriction_data', {})),
            'trading_blocs': lambda: self.analyze_trading_blocs(kwargs.get('bloc_data', {})),
            'barrier_removal': lambda: self.assess_trade_barrier_removal(kwargs.get('liberalization_data', {}))
        }

        if analysis_type not in analyses:
            raise ValidationError(f"Unknown analysis type: {analysis_type}")

        result = analyses[analysis_type]()
        result['metadata'] = self.get_metadata()
        return result


class GeopoliticalRiskAnalyzer(EconomicsBase):
    """Geopolitical risk assessment and investment implications"""

    def analyze_geopolitics_framework(self) -> Dict[str, Any]:
        """Analyze geopolitics from cooperation vs competition perspective"""
        return {
            'cooperation_perspective': {
                'drivers': ['Economic interdependence', 'Shared challenges', 'Institutional frameworks'],
                'mechanisms': ['Trade agreements', 'International organizations', 'Diplomatic engagement'],
                'benefits': ['Peace dividend', 'Economic gains', 'Global public goods'],
                'examples': ['EU integration', 'WTO system', 'Climate cooperation']
            },
            'competition_perspective': {
                'drivers': ['National interests', 'Power struggles', 'Resource competition'],
                'mechanisms': ['Military buildup', 'Economic sanctions', 'Technology competition'],
                'risks': ['Conflict escalation', 'Economic fragmentation', 'Arms races'],
                'examples': ['US-China rivalry', 'Russia-West tensions', 'Cyber warfare']
            },
            'cooperation_competition_spectrum': {
                'coopetition': 'Simultaneous cooperation and competition',
                'issue_specificity': 'Cooperation on some issues, competition on others',
                'temporal_variation': 'Shifting between cooperation and competition over time',
                'stakeholder_differences': 'Different actors may prefer different approaches'
            },
            'current_global_trends': self._assess_current_geopolitical_trends()
        }

    def analyze_geopolitics_globalization(self) -> Dict[str, Any]:
        """Analyze relationship between geopolitics and globalization"""
        return {
            'globalization_drivers': {
                'economic': 'Trade, investment, financial integration',
                'technological': 'Communication, transportation, digital connectivity',
                'political': 'International governance, regulatory convergence',
                'cultural': 'Information flow, cultural exchange, migration'
            },
            'geopolitical_constraints': {
                'sovereignty_concerns': 'National autonomy vs global integration',
                'security_considerations': 'Economic interdependence vs strategic autonomy',
                'distributional_effects': 'Winners and losers from globalization',
                'cultural_resistance': 'Preserving national identity and values'
            },
            'interaction_dynamics': {
                'reinforcing_effects': 'Economic integration can reduce conflict incentives',
                'tension_creation': 'Globalization can threaten traditional power structures',
                'policy_responses': 'Governments balance integration with national interests',
                'cyclical_patterns': 'Periods of integration followed by fragmentation'
            },
            'current_deglobalization_trends': self._analyze_deglobalization_trends()
        }

    def analyze_international_organizations(self) -> Dict[str, Any]:
        """Analyze functions and objectives of key international organizations"""
        return {
            'world_bank': {
                'primary_objective': 'Reduce poverty and promote shared prosperity',
                'functions': [
                    'Development financing and technical assistance',
                    'Policy advice and capacity building',
                    'Knowledge sharing and research',
                    'Crisis response and post-conflict reconstruction'
                ],
                'lending_instruments': ['IBRD loans', 'IDA grants', 'Private sector lending'],
                'governance': '189 member countries, voting power based on capital contributions',
                'effectiveness_assessment': 'Mixed results, criticism for conditionality and governance'
            },
            'international_monetary_fund': {
                'primary_objective': 'Ensure stability of international monetary system',
                'functions': [
                    'Surveillance of global economy and exchange rates',
                    'Financial assistance to countries in balance of payments difficulties',
                    'Technical assistance and capacity development',
                    'Standard setting and policy coordination'
                ],
                'lending_facilities': ['Stand-by arrangements', 'Extended fund facility', 'Emergency assistance'],
                'governance': '190 member countries, quota-based voting system',
                'effectiveness_assessment': 'Critical role in crisis response, debates over conditionality'
            },
            'world_trade_organization': {
                'primary_objective': 'Promote free and fair trade globally',
                'functions': [
                    'Trade rule making and negotiation',
                    'Dispute settlement between members',
                    'Trade policy monitoring and transparency',
                    'Technical assistance and capacity building'
                ],
                'key_principles': ['Non-discrimination', 'Market access', 'Fair competition', 'Development'],
                'governance': '164 members, consensus-based decision making',
                'effectiveness_assessment': 'Success in trade liberalization, challenges with dispute resolution'
            },
            'organizational_interactions': self._analyze_organizational_coordination(),
            'reform_needs': self._assess_reform_requirements()
        }

    def assess_geopolitical_risk(self, risk_data: Dict[str, Any]) -> Dict[str, Any]:
        """Assess geopolitical risk levels and components"""
        return {
            'risk_categories': {
                'interstate_conflict': {
                    'probability': self._assess_conflict_probability(risk_data),
                    'impact': 'High - disrupts trade, increases defense spending',
                    'indicators': ['Military buildups', 'Territorial disputes', 'Alliance shifts'],
                    'current_hotspots': ['Taiwan Strait', 'Ukraine', 'Middle East', 'South China Sea']
                },
                'domestic_instability': {
                    'probability': self._assess_instability_probability(risk_data),
                    'impact': 'Medium to High - affects governance and economic policy',
                    'indicators': ['Political polarization', 'Social unrest', 'Economic inequality'],
                    'monitoring_metrics': ['Polity IV scores', 'Fragile States Index', 'Social cohesion indices']
                },
                'economic_warfare': {
                    'probability': 'Medium - already occurring',
                    'impact': 'High - trade disruption, technology decoupling',
                    'manifestations': ['Trade wars', 'Technology sanctions', 'Financial restrictions'],
                    'current_examples': ['US-China tech competition', 'Russia sanctions', 'Supply chain nationalism']
                },
                'cyber_threats': {
                    'probability': 'High - ongoing',
                    'impact': 'Medium to High - infrastructure and financial system risks',
                    'evolution': 'Rapidly increasing sophistication and frequency',
                    'mitigation_challenges': 'Attribution difficulties, cross-border nature'
                }
            },
            'risk_assessment_methodology': self._describe_risk_methodology(),
            'early_warning_indicators': self._identify_early_warning_signs(),
            'risk_mitigation_strategies': self._recommend_risk_mitigation()
        }

    def analyze_geopolitical_tools(self) -> Dict[str, Any]:
        """Analyze tools of geopolitics and their economic impact"""
        return {
            'economic_tools': {
                'trade_policy': {
                    'instruments': ['Tariffs', 'Quotas', 'Trade agreements', 'Export controls'],
                    'effectiveness': 'High for economic coercion, limited for security goals',
                    'economic_impact': 'Efficiency losses, distributional effects',
                    'examples': ['China trade war', 'Iran sanctions', 'Brexit negotiations']
                },
                'financial_sanctions': {
                    'instruments': ['Asset freezes', 'Banking restrictions', 'Capital market access'],
                    'effectiveness': 'High when multilateral, moderate when unilateral',
                    'economic_impact': 'Disrupts financial flows, increases transaction costs',
                    'examples': ['Russia SWIFT exclusion', 'Iran banking sanctions', 'North Korea restrictions']
                },
                'investment_controls': {
                    'instruments': ['FDI screening', 'Technology transfer restrictions',
                                    'Sovereign wealth fund limits'],
                    'effectiveness': 'Moderate for strategic sectors',
                    'economic_impact': 'Reduces capital flows, technology diffusion',
                    'examples': ['CFIUS reviews', 'EU FDI screening', 'Technology export controls']
                }
            },
            'diplomatic_tools': {
                'multilateral_engagement': 'International organizations and forums',
                'bilateral_relations': 'Direct government-to-government engagement',
                'public_diplomacy': 'Cultural and informational influence',
                'summit_diplomacy': 'High-level leader engagement'
            },
            'military_tools': {
                'defense_spending': 'Military buildup and alliance strengthening',
                'military_presence': 'Forward deployment and bases',
                'arms_sales': 'Defense cooperation and influence building',
                'security_assistance': 'Training and capacity building'
            },
            'tool_effectiveness_comparison': self._compare_geopolitical_tools()
        }

    def assess_investment_implications(self, geopolitical_data: Dict[str, Any]) -> Dict[str, Any]:
        """Assess investment implications of geopolitical risk"""
        return {
            'asset_class_impacts': {
                'equities': {
                    'safe_haven_flows': 'Flight to quality during crises',
                    'sector_differentiation': 'Defense up, trade-dependent sectors down',
                    'regional_variation': 'Emerging markets more vulnerable',
                    'volatility_impact': 'Increased uncertainty and volatility'
                },
                'fixed_income': {
                    'government_bonds': 'Safe haven demand for developed market bonds',
                    'corporate_bonds': 'Credit spreads widen, especially for affected regions',
                    'emerging_market_debt': 'Capital flight and spread widening',
                    'inflation_expectations': 'Supply disruptions may increase inflation'
                },
                'currencies': {
                    'reserve_currencies': 'Dollar, euro, yen benefit from safe haven flows',
                    'commodity_currencies': 'Impact depends on commodity exposure',
                    'emerging_market_currencies': 'Generally weaken during geopolitical stress',
                    'crypto_currencies': 'Mixed reactions, some safe haven demand'
                },
                'commodities': {
                    'energy': 'Supply disruption premium for oil and gas',
                    'precious_metals': 'Traditional safe haven demand for gold',
                    'agriculture': 'Supply chain disruptions affect food prices',
                    'industrial_metals': 'Demand reduction from economic slowdown'
                }
            },
            'sector_analysis': self._analyze_sector_impacts(geopolitical_data),
            'geographic_considerations': self._assess_geographic_impacts(geopolitical_data),
            'investment_strategies': self._recommend_investment_strategies(geopolitical_data),
            'risk_monitoring_framework': self._develop_monitoring_framework()
        }

    def _assess_current_geopolitical_trends(self) -> List[str]:
        """Assess current global geopolitical trends"""
        return [
            'Rise of China and shifting global power balance',
            'Renewed great power competition between US, China, and Russia',
            'Fragmentation of global governance and institutions',
            'Technology competition and digital sovereignty concerns',
            'Climate change as a security issue and cooperation challenge',
            'Democratic backsliding and authoritarian resilience'
        ]

    def _analyze_deglobalization_trends(self) -> Dict[str, str]:
        """Analyze current deglobalization trends"""
        return {
            'trade_slowdown': 'Growth in global trade relative to GDP has slowed',
            'supply_chain_reshoring': 'Companies reducing dependence on distant suppliers',
            'technology_decoupling': 'Separate technology ecosystems emerging',
            'financial_fragmentation': 'Reduced cross-border capital flows',
            'immigration_restrictions': 'Tighter controls on human mobility',
            'policy_implications': 'Governments balancing efficiency with resilience'
        }

    def _analyze_organizational_coordination(self) -> Dict[str, str]:
        """Analyze coordination between international organizations"""
        return {
            'world_bank_imf': 'Close coordination on development and financial stability',
            'wto_relationship': 'Limited formal links but complementary mandates',
            'regional_organizations': 'Growing importance of regional institutions',
            'coordination_challenges': 'Overlapping mandates and competing priorities'
        }

    def _assess_reform_requirements(self) -> List[str]:
        """Assess reform needs for international organizations"""
        return [
            'IMF: Quota reform to reflect changing global economy',
            'World Bank: Climate focus and private sector engagement',
            'WTO: Dispute settlement reform and digital trade rules',
            'UN Security Council: Representation reform for emerging powers',
            'All: Enhanced coordination and reduced overlap'
        ]

    def _assess_conflict_probability(self, data: Dict[str, Any]) -> str:
        """Assess probability of interstate conflict"""
        tension_level = data.get('tension_index')
        if tension_level is None:
            return 'Unavailable - tension_index not provided'

        if tension_level > 0.8:
            return 'High risk of conflict escalation'
        elif tension_level > 0.6:
            return 'Moderate risk, close monitoring needed'
        else:
            return 'Low to moderate risk'

    def _assess_instability_probability(self, data: Dict[str, Any]) -> str:
        """Assess probability of domestic instability"""
        governance_score = data.get('governance_index')
        if governance_score is None:
            return 'Unavailable - governance_index not provided'

        if governance_score < 0.3:
            return 'High instability risk'
        elif governance_score < 0.6:
            return 'Moderate instability risk'
        else:
            return 'Low instability risk'

    def _describe_risk_methodology(self) -> Dict[str, str]:
        """Describe geopolitical risk assessment methodology"""
        return {
            'quantitative_indicators': 'Economic data, governance indices, conflict databases',
            'qualitative_assessment': 'Expert analysis, scenario planning, historical analogies',
            'early_warning_systems': 'Real-time monitoring of key indicators',
            'scenario_analysis': 'Multiple future scenarios with probability weights',
            'stress_testing': 'Impact assessment under extreme scenarios'
        }

    def _identify_early_warning_signs(self) -> List[str]:
        """Identify early warning indicators of geopolitical stress"""
        return [
            'Diplomatic relations: Embassy closures, ambassador recalls',
            'Military indicators: Troop movements, exercise frequency, defense spending',
            'Economic signals: Trade restrictions, investment controls, sanctions threats',
            'Political rhetoric: Leadership statements, media coverage, public opinion',
            'Market indicators: Risk premiums, capital flows, currency movements'
        ]

    def _recommend_risk_mitigation(self) -> List[str]:
        """Recommend geopolitical risk mitigation strategies"""
        return [
            'Diversification: Geographic and supply chain diversification',
            'Scenario planning: Regular stress testing and contingency planning',
            'Political risk insurance: Coverage for expropriation and conflict',
            'Local partnerships: Joint ventures and local content requirements',
            'Flexible operations: Ability to quickly adjust to changing conditions'
        ]

    def _compare_geopolitical_tools(self) -> Dict[str, str]:
        """Compare effectiveness of different geopolitical tools"""
        return {
            'economic_tools': 'High immediate impact but may create long-term costs',
            'diplomatic_tools': 'Low immediate impact but sustainable and relationship-preserving',
            'military_tools': 'High coercive power but risks escalation and high costs',
            'optimal_strategy': 'Combination of tools tailored to specific objectives and constraints'
        }

    def _analyze_sector_impacts(self, data: Dict[str, Any]) -> Dict[str, str]:
        """Analyze sectoral impacts of geopolitical risk"""
        return {
            'defense_aerospace': 'Generally benefits from increased defense spending',
            'energy': 'Mixed impact depending on supply chain exposure',
            'technology': 'Vulnerable to export controls and technology restrictions',
            'financials': 'Exposed to sanctions and capital flow restrictions',
            'materials': 'Supply chain disruptions and commodity price volatility',
            'consumer_discretionary': 'Reduced confidence affects spending patterns'
        }

    def _assess_geographic_impacts(self, data: Dict[str, Any]) -> Dict[str, str]:
        """Assess geographic variation in geopolitical impacts"""
        return {
            'developed_markets': 'Relative safety but not immune to global shocks',
            'emerging_markets': 'Higher vulnerability to capital flow reversals',
            'frontier_markets': 'Extreme sensitivity to risk sentiment changes',
            'regional_variation': 'Proximity to conflict zones increases impact',
            'economic_integration': 'Highly integrated economies more affected'
        }

    def _recommend_investment_strategies(self, data: Dict[str, Any]) -> List[str]:
        """Recommend investment strategies for geopolitical risk environment"""
        return [
            'Tactical allocation: Adjust portfolio weights based on risk assessment',
            'Safe haven assets: Maintain allocation to defensive assets',
            'Sector rotation: Favor sectors that benefit from geopolitical trends',
            'Currency hedging: Protect against adverse currency movements',
            'Volatility management: Use derivatives to manage downside risk',
            'ESG integration: Consider governance and sustainability factors'
        ]

    def _develop_monitoring_framework(self) -> Dict[str, List[str]]:
        """Develop framework for monitoring geopolitical risks"""
        return {
            'daily_monitoring': ['News flow', 'Market reactions', 'Policy statements'],
            'weekly_assessment': ['Economic data', 'Diplomatic developments', 'Military activities'],
            'monthly_review': ['Risk indicator updates', 'Scenario probability updates', 'Portfolio adjustments'],
            'quarterly_analysis': ['Comprehensive risk assessment', 'Strategy review', 'Stress testing'],
            'annual_planning': ['Long-term scenario development', 'Strategic asset allocation',
                                'Risk budget allocation']
        }

    def calculate(self, analysis_type: str = 'risk_assessment', **kwargs) -> Dict[str, Any]:
        """Main geopolitical analysis dispatcher"""
        analyses = {
            'framework': self.analyze_geopolitics_framework,
            'globalization': self.analyze_geopolitics_globalization,
            'organizations': self.analyze_international_organizations,
            'risk_assessment': lambda: self.assess_geopolitical_risk(kwargs.get('risk_data', {})),
            'tools': self.analyze_geopolitical_tools,
            'investment_implications': lambda: self.assess_investment_implications(kwargs.get('geopolitical_data', {}))
        }

        if analysis_type not in analyses:
            raise ValidationError(f"Unknown analysis type: {analysis_type}")

        result = analyses[analysis_type]()
        result['metadata'] = self.get_metadata()
        return result


class TradingBlocAnalyzer(EconomicsBase):
    """Specialized analysis of trading blocs and economic integration"""

    def analyze_bloc_performance(self, bloc_data: Dict[str, Any]) -> Dict[str, Any]:
        """Analyze performance and effectiveness of trading blocs"""
        return {
            'trade_creation_measurement': {
                'intra_bloc_trade_growth': self._calculate_intra_bloc_growth(bloc_data),
                'trade_intensity_index': self._calculate_trade_intensity(bloc_data),
                'revealed_comparative_advantage': 'Analysis of changing trade patterns',
                'welfare_impact_estimate': self._estimate_welfare_impact(bloc_data)
            },
            'integration_depth_assessment': {
                'tariff_elimination': bloc_data.get('tariff_elimination_percent', 'N/A'),
                'non_tariff_barriers': bloc_data.get('ntb_reduction_score', 'N/A'),
                'services_liberalization': bloc_data.get('services_openness_index', 'N/A'),
                'factor_mobility': bloc_data.get('factor_mobility_score', 'N/A'),
                'policy_coordination': bloc_data.get('policy_coordination_index', 'N/A')
            },
            'economic_convergence': {
                'income_convergence': 'Analysis of per capita income gaps',
                'inflation_convergence': 'Monetary policy coordination effects',
                'business_cycle_synchronization': 'Economic cycle alignment assessment',
                'structural_convergence': 'Industry structure and productivity alignment'
            },
            'challenges_and_obstacles': self._identify_integration_challenges(bloc_data),
            'success_factors': self._assess_integration_success_factors(bloc_data)
        }

    def _calculate_intra_bloc_growth(self, data: Dict[str, Any]) -> str:
        """Calculate intra-bloc trade growth"""
        if not data.get('baseline_intra_trade') or data.get('current_intra_trade') is None:
            return 'Unavailable - baseline_intra_trade and current_intra_trade not provided'
        baseline_trade = self.to_decimal(data['baseline_intra_trade'])
        current_trade = self.to_decimal(data['current_intra_trade'])

        growth_rate = ((current_trade - baseline_trade) / baseline_trade) * self.to_decimal(100)
        return f"Intra-bloc trade grew by {growth_rate:.1f}% since formation"

    def _calculate_trade_intensity(self, data: Dict[str, Any]) -> str:
        """Calculate trade intensity index"""
        return "Trade intensity index measures whether bloc members trade more with each other than expected"

    def _estimate_welfare_impact(self, data: Dict[str, Any]) -> str:
        """Estimate welfare impact of trading bloc"""
        trade_creation = data.get('trade_creation_estimate', 'positive')
        trade_diversion = data.get('trade_diversion_estimate', 'moderate')

        if trade_creation == 'positive' and trade_diversion == 'low':
            return 'Net positive welfare impact'
        elif trade_creation == 'positive' and trade_diversion == 'moderate':
            return 'Likely positive welfare impact'
        else:
            return 'Mixed welfare impact, requires detailed analysis'

    def _identify_integration_challenges(self, data: Dict[str, Any]) -> List[str]:
        """Identify challenges to deeper integration"""
        return [
            'Asymmetric development levels among members',
            'Different regulatory frameworks and standards',
            'Political sovereignty concerns',
            'Unequal distribution of integration benefits',
            'External pressure from non-member countries',
            'Coordination costs and administrative burden'
        ]

    def _assess_integration_success_factors(self, data: Dict[str, Any]) -> Dict[str, str]:
        """Assess factors contributing to integration success"""
        return {
            'political_commitment': 'Strong leadership commitment to integration goals',
            'institutional_framework': 'Effective governance and dispute resolution mechanisms',
            'economic_complementarity': 'Complementary rather than competing economic structures',
            'adjustment_mechanisms': 'Policies to help losers from integration',
            'external_support': 'Technical and financial assistance for integration process'
        }

    def calculate(self, **kwargs) -> Dict[str, Any]:
        """Calculate trading bloc analysis"""
        result = self.analyze_bloc_performance(kwargs.get('bloc_data', {}))
        result['metadata'] = self.get_metadata()
        return result


if __name__ == "__main__":
    import sys
    import json

    def to_serializable(obj):
        """Recursively convert Decimal and other non-serializable types."""
        from decimal import Decimal
        if isinstance(obj, Decimal):
            return float(obj)
        if isinstance(obj, dict):
            return {k: to_serializable(v) for k, v in obj.items()}
        if isinstance(obj, (list, tuple)):
            return [to_serializable(i) for i in obj]
        return obj

    if len(sys.argv) < 2:
        print(json.dumps({"error": "Usage: trade_geopolitics.py <benefits_costs|restrictions|trading_blocs|barrier_removal> '<params JSON>'"}))
        sys.exit(1)
    analysis_type = sys.argv[1]
    try:
        params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
        analyzer = TradeAnalyzer()
        if analysis_type == "benefits_costs":
            result = analyzer.analyze_trade_benefits_costs(params)
        elif analysis_type == "restrictions":
            result = analyzer.analyze_trade_restrictions(params)
        elif analysis_type == "trading_blocs":
            result = analyzer.analyze_trading_blocs(params)
        elif analysis_type == "barrier_removal":
            result = analyzer.assess_trade_barrier_removal(params)
        else:
            raise ValidationError(f"Unknown analysis type: {analysis_type}")
    except Exception as e:  # noqa: BLE001
        print(json.dumps({"error": str(e)}))
        sys.exit(1)

    def _finite(o):
        if isinstance(o, float) and not math.isfinite(o):
            return None
        if isinstance(o, dict):
            return {k: _finite(v) for k, v in o.items()}
        if isinstance(o, list):
            return [_finite(v) for v in o]
        return o

    print(json.dumps(_finite(to_serializable(result)), indent=2))

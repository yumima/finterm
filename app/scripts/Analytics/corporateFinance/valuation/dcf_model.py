"""Discounted Cash Flow (DCF) Valuation Model"""
from typing import Dict, Any, List, Optional
import sys
import numpy as np

class DCFModel:
    """Comprehensive DCF valuation model"""

    def __init__(self, company_name: str):
        self.company_name = company_name

    def calculate_wacc(self, risk_free_rate: float,
                      market_risk_premium: float,
                      beta: float,
                      cost_of_debt: float,
                      tax_rate: float,
                      market_value_equity: float,
                      market_value_debt: float,
                      country_risk_premium: float = 0.0,
                      size_premium: float = 0.0) -> Dict[str, Any]:
        """Calculate Weighted Average Cost of Capital

        Args:
            risk_free_rate: Risk-free rate (e.g., 10-year Treasury)
            market_risk_premium: Equity risk premium
            beta: Company beta
            cost_of_debt: Pre-tax cost of debt
            tax_rate: Corporate tax rate
            market_value_equity: Market value of equity
            market_value_debt: Market value of debt
            country_risk_premium: Additional premium for country risk (default: 0)
            size_premium: Small cap premium (default: 0)
        """

        # Input validation
        if not (0 <= risk_free_rate <= 0.20):
            raise ValueError(f"Risk-free rate {risk_free_rate:.2%} outside valid range (0-20%)")
        if not (0 <= market_risk_premium <= 0.20):
            raise ValueError(f"Market risk premium {market_risk_premium:.2%} outside valid range (0-20%)")
        if not (0.1 <= beta <= 3.0):
            raise ValueError(f"Beta {beta} outside valid range (0.1-3.0)")
        if not (0 <= cost_of_debt <= 0.30):
            raise ValueError(f"Cost of debt {cost_of_debt:.2%} outside valid range (0-30%)")
        if not (0 <= tax_rate <= 0.50):
            raise ValueError(f"Tax rate {tax_rate:.2%} outside valid range (0-50%)")
        if market_value_equity < 0:
            raise ValueError("Market value of equity cannot be negative")
        if market_value_debt < 0:
            raise ValueError("Market value of debt cannot be negative")
        if not (0 <= country_risk_premium <= 0.15):
            raise ValueError(f"Country risk premium {country_risk_premium:.2%} outside valid range (0-15%)")
        if not (0 <= size_premium <= 0.10):
            raise ValueError(f"Size premium {size_premium:.2%} outside valid range (0-10%)")

        # Cost of equity = Rf + β(MRP) + Country Risk + Size Premium
        cost_of_equity = risk_free_rate + (beta * market_risk_premium) + country_risk_premium + size_premium

        after_tax_cost_of_debt = cost_of_debt * (1 - tax_rate)

        total_value = market_value_equity + market_value_debt
        equity_weight = market_value_equity / total_value if total_value > 0 else 0
        debt_weight = market_value_debt / total_value if total_value > 0 else 0

        wacc = (equity_weight * cost_of_equity) + (debt_weight * after_tax_cost_of_debt)

        return {
            'wacc': wacc,
            'wacc_pct': wacc * 100,
            'cost_of_equity': cost_of_equity * 100,
            'cost_of_equity_pretax': cost_of_equity,
            'cost_of_debt_pretax': cost_of_debt * 100,
            'cost_of_debt_aftertax': after_tax_cost_of_debt * 100,
            'equity_weight': equity_weight * 100,
            'debt_weight': debt_weight * 100,
            'inputs': {
                'risk_free_rate': risk_free_rate * 100,
                'market_risk_premium': market_risk_premium * 100,
                'beta': beta,
                'tax_rate': tax_rate * 100,
                'country_risk_premium': country_risk_premium * 100,
                'size_premium': size_premium * 100
            }
        }

    def unlever_beta(self, levered_beta: float, tax_rate: float,
                     debt_to_equity: float) -> float:
        """Unlever beta to remove debt impact

        Args:
            levered_beta: Current levered beta
            tax_rate: Corporate tax rate
            debt_to_equity: Debt/Equity ratio

        Returns:
            Unlevered beta
        """
        unlevered_beta = levered_beta / (1 + (1 - tax_rate) * debt_to_equity)
        return unlevered_beta

    def relever_beta(self, unlevered_beta: float, tax_rate: float,
                     target_debt_to_equity: float) -> float:
        """Relever beta to target capital structure

        Args:
            unlevered_beta: Unlevered (asset) beta
            tax_rate: Corporate tax rate
            target_debt_to_equity: Target Debt/Equity ratio

        Returns:
            Relevered beta
        """
        levered_beta = unlevered_beta * (1 + (1 - tax_rate) * target_debt_to_equity)
        return levered_beta

    def calculate_free_cash_flow(self, ebit: float,
                                 tax_rate: float,
                                 depreciation: float,
                                 capex: float,
                                 change_in_nwc: float,
                                 stock_based_comp: float = 0.0,
                                 maintenance_capex: Optional[float] = None,
                                 growth_capex: Optional[float] = None) -> Dict[str, Any]:
        """Calculate Free Cash Flow to Firm

        Args:
            ebit: Earnings before interest and tax
            tax_rate: Corporate tax rate
            depreciation: Depreciation & amortization
            capex: Total capital expenditures (if maintenance/growth not separated)
            change_in_nwc: Change in net working capital
            stock_based_comp: Stock-based compensation to add back (default: 0)
            maintenance_capex: CapEx to maintain operations (optional)
            growth_capex: CapEx for growth (optional)
        """

        # Use split CapEx if provided, otherwise use total
        if maintenance_capex is not None and growth_capex is not None:
            total_capex = maintenance_capex + growth_capex
        else:
            # No maintenance/growth split was supplied: report the total only
            # (an assumed split would be invented data).
            total_capex = capex

        # NOPAT = EBIT × (1 - Tax)
        nopat = ebit * (1 - tax_rate)

        # Add back stock-based comp (non-cash expense)
        nopat_adjusted = nopat + stock_based_comp

        # FCF = NOPAT + D&A - CapEx - ΔNWC
        fcf = nopat_adjusted + depreciation - total_capex - change_in_nwc

        return {
            'ebit': ebit,
            'tax': ebit * tax_rate,
            'nopat': nopat,
            'add_stock_based_comp': stock_based_comp,
            'nopat_adjusted': nopat_adjusted,
            'add_depreciation': depreciation,
            'less_capex': total_capex,
            'capex_breakdown': {
                'maintenance': maintenance_capex,
                'growth': growth_capex
            },
            'less_change_in_nwc': change_in_nwc,
            'free_cash_flow': fcf
        }

    def project_cash_flows(self, base_year_fcf: float,
                          growth_rates: List[float]) -> List[Dict[str, Any]]:
        """Project future free cash flows"""

        projections = []
        current_fcf = base_year_fcf

        for year, growth_rate in enumerate(growth_rates, 1):
            current_fcf = current_fcf * (1 + growth_rate)

            projections.append({
                'year': year,
                'growth_rate': growth_rate * 100,
                'fcf': current_fcf
            })

        return projections

    def calculate_terminal_value(self, final_year_fcf: float,
                                terminal_growth_rate: float,
                                wacc: float,
                                method: str = 'perpetuity',
                                exit_multiple: Optional[float] = None,
                                exit_metric: Optional[float] = None) -> Dict[str, Any]:
        """Calculate terminal value using perpetuity growth or exit multiple

        Args:
            final_year_fcf: Final year free cash flow
            terminal_growth_rate: Perpetual growth rate (for perpetuity method)
            wacc: Weighted average cost of capital
            method: 'perpetuity' or 'exit_multiple'
            exit_multiple: EV/EBITDA multiple for exit (optional, for exit_multiple method)
            exit_metric: Final year EBITDA (optional, for exit_multiple method)
        """

        # Input validation
        if method == 'perpetuity' and not (-0.05 <= terminal_growth_rate <= 0.05):
            raise ValueError(f"Terminal growth rate {terminal_growth_rate:.2%} should be within -5%..5% (long-run nominal GDP)")

        if method == 'perpetuity':
            if wacc <= terminal_growth_rate:
                raise ValueError(f"WACC ({wacc:.2%}) must be greater than terminal growth rate ({terminal_growth_rate:.2%})")

            terminal_value = (final_year_fcf * (1 + terminal_growth_rate)) / (wacc - terminal_growth_rate)

            return {
                'method': 'perpetuity_growth',
                'terminal_value': terminal_value,
                'terminal_growth_rate': terminal_growth_rate * 100,
                'terminal_year_fcf': final_year_fcf,
                'wacc': wacc * 100
            }

        elif method == 'exit_multiple':
            if exit_multiple is None or exit_metric is None:
                raise ValueError("exit_multiple and exit_metric required for exit_multiple method")

            if exit_multiple <= 0:
                raise ValueError(f"Exit multiple must be positive (got {exit_multiple}x)")

            terminal_value = exit_multiple * exit_metric

            return {
                'method': 'exit_multiple',
                'terminal_value': terminal_value,
                'exit_multiple': exit_multiple,
                'exit_metric': exit_metric,
                'implied_exit_enterprise_value': terminal_value
            }

        else:
            raise ValueError(f"Unknown method: {method}. Use 'perpetuity' or 'exit_multiple'")

    def calculate_enterprise_value(self, fcf_projections: List[float],
                                  terminal_value: float,
                                  wacc: float,
                                  mid_year_convention: bool = False,
                                  terminal_method: str = 'perpetuity') -> Dict[str, Any]:
        """Calculate enterprise value from DCF

        Args:
            fcf_projections: List of projected free cash flows
            terminal_value: Terminal value
            wacc: Weighted average cost of capital
            mid_year_convention: If True, assumes cash flows occur mid-year (default: False)
            terminal_method: 'perpetuity' or 'exit_multiple'. Under the mid-year
                convention a perpetuity-growth TV (a stream of mid-year flows) is
                discounted N-0.5 years, but an exit-multiple TV (a sale at the
                end of year N on trailing EBITDA) is discounted the full N years.
        """

        pv_fcf_list = []
        pv_fcf_total = 0

        # Mid-year adjustment: discount by year - 0.5 instead of year
        for year, fcf in enumerate(fcf_projections, 1):
            if mid_year_convention:
                discount_factor = (1 + wacc) ** (year - 0.5)
            else:
                discount_factor = (1 + wacc) ** year

            pv = fcf / discount_factor
            pv_fcf_total += pv

            pv_fcf_list.append({
                'year': year,
                'fcf': fcf,
                'discount_factor': discount_factor,
                'present_value': pv
            })

        terminal_year = len(fcf_projections)

        if mid_year_convention and terminal_method == 'perpetuity':
            terminal_discount_factor = (1 + wacc) ** (terminal_year - 0.5)
        else:
            terminal_discount_factor = (1 + wacc) ** terminal_year

        pv_terminal_value = terminal_value / terminal_discount_factor

        enterprise_value = pv_fcf_total + pv_terminal_value

        return {
            'pv_of_fcf': pv_fcf_total,
            'pv_of_terminal_value': pv_terminal_value,
            'enterprise_value': enterprise_value,
            'terminal_value_contribution': (pv_terminal_value / enterprise_value * 100) if enterprise_value > 0 else None,
            'fcf_details': pv_fcf_list,
            'wacc_used': wacc * 100,
            'mid_year_convention': mid_year_convention
        }

    def calculate_equity_value(self, enterprise_value: float,
                              cash: float,
                              debt: float,
                              minority_interest: float = 0,
                              preferred_stock: float = 0,
                              excess_cash: Optional[float] = None) -> Dict[str, Any]:
        """Convert enterprise value to equity value

        Args:
            enterprise_value: Enterprise value from DCF
            cash: Total cash and equivalents
            debt: Total debt
            minority_interest: Minority interest to subtract
            preferred_stock: Preferred stock to subtract
            excess_cash: Cash above operating needs (optional, if not specified uses total cash)
        """

        # If excess cash not specified, use total cash
        # Operating cash typically = 2% of revenue (rule of thumb)
        cash_to_add = excess_cash if excess_cash is not None else cash

        equity_value = enterprise_value + cash_to_add - debt - minority_interest - preferred_stock

        return {
            'enterprise_value': enterprise_value,
            'add_cash': cash_to_add,
            'total_cash': cash,
            'excess_cash_used': excess_cash is not None,
            'less_debt': debt,
            'less_minority_interest': minority_interest,
            'less_preferred_stock': preferred_stock,
            'equity_value': equity_value
        }

    def calculate_price_per_share(self, equity_value: float,
                                 shares_outstanding: float,
                                 diluted_shares: Optional[float] = None) -> Dict[str, Any]:
        """Calculate price per share"""

        shares = diluted_shares if diluted_shares else shares_outstanding

        price_per_share = equity_value / shares if shares and shares > 0 else None

        return {
            'equity_value': equity_value,
            'shares_outstanding_basic': shares_outstanding,
            'shares_outstanding_diluted': diluted_shares,
            'shares_used': shares,
            'price_per_share': price_per_share
        }

    def comprehensive_dcf(self, wacc_inputs: Dict[str, float],
                         fcf_inputs: Dict[str, float],
                         growth_rates: List[float],
                         terminal_growth_rate: float,
                         balance_sheet: Dict[str, float],
                         shares_outstanding: float) -> Dict[str, Any]:
        """Complete DCF valuation from inputs to price per share"""

        wacc_result = self.calculate_wacc(**wacc_inputs)
        wacc = wacc_result['wacc']

        base_fcf_calc = self.calculate_free_cash_flow(**fcf_inputs)
        base_fcf = base_fcf_calc['free_cash_flow']

        fcf_projections = self.project_cash_flows(base_fcf, growth_rates)
        fcf_values = [p['fcf'] for p in fcf_projections]

        terminal_value_result = self.calculate_terminal_value(
            fcf_values[-1],
            terminal_growth_rate,
            wacc
        )

        ev_result = self.calculate_enterprise_value(
            fcf_values,
            terminal_value_result['terminal_value'],
            wacc
        )

        equity_result = self.calculate_equity_value(
            ev_result['enterprise_value'],
            balance_sheet['cash'],
            balance_sheet['debt'],
            balance_sheet.get('minority_interest', 0),
            balance_sheet.get('preferred_stock', 0)
        )

        price_result = self.calculate_price_per_share(
            equity_result['equity_value'],
            shares_outstanding,
            balance_sheet.get('diluted_shares')
        )

        return {
            'company_name': self.company_name,
            'wacc': wacc_result,
            'base_year_fcf': base_fcf_calc,
            'fcf_projections': fcf_projections,
            'terminal_value': terminal_value_result,
            'enterprise_value': ev_result,
            'equity_value': equity_result,
            'valuation_per_share': price_result,
            'summary': {
                'enterprise_value': ev_result['enterprise_value'],
                'equity_value': equity_result['equity_value'],
                'price_per_share': price_result['price_per_share'],
                'wacc': wacc * 100,
                'terminal_growth': terminal_growth_rate * 100
            }
        }

    def sensitivity_analysis(self, base_fcf: float,
                           growth_rates: List[float],
                           terminal_growth_scenarios: List[float],
                           wacc_scenarios: List[float],
                           balance_sheet: Dict[str, float],
                           shares_outstanding: float) -> Dict[str, Any]:
        """Two-way sensitivity analysis (WACC vs Terminal Growth)"""

        sensitivity_matrix = []

        for terminal_growth in terminal_growth_scenarios:
            for wacc in wacc_scenarios:
                if wacc <= terminal_growth:
                    continue

                fcf_projections_calc = self.project_cash_flows(base_fcf, growth_rates)
                fcf_values = [p['fcf'] for p in fcf_projections_calc]

                terminal_value_calc = self.calculate_terminal_value(
                    fcf_values[-1],
                    terminal_growth,
                    wacc
                )

                ev_calc = self.calculate_enterprise_value(
                    fcf_values,
                    terminal_value_calc['terminal_value'],
                    wacc
                )

                equity_calc = self.calculate_equity_value(
                    ev_calc['enterprise_value'],
                    balance_sheet['cash'],
                    balance_sheet['debt'],
                    balance_sheet.get('minority_interest', 0),
                    balance_sheet.get('preferred_stock', 0)
                )

                price_calc = self.calculate_price_per_share(
                    equity_calc['equity_value'],
                    shares_outstanding
                )

                sensitivity_matrix.append({
                    'wacc': wacc * 100,
                    'terminal_growth': terminal_growth * 100,
                    'price_per_share': price_calc['price_per_share'],
                    'equity_value': equity_calc['equity_value']
                })

        return {
            'sensitivity_matrix': sensitivity_matrix,
            'wacc_range': [min(wacc_scenarios) * 100, max(wacc_scenarios) * 100],
            'terminal_growth_range': [min(terminal_growth_scenarios) * 100, max(terminal_growth_scenarios) * 100]
        }

    def scenario_analysis(self, base_inputs: Dict[str, Any],
                         bear_adjustments: Optional[Dict[str, Any]] = None,
                         bull_adjustments: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Run bear/base/bull scenario analysis

        Args:
            base_inputs: Base case DCF inputs (wacc_inputs, fcf_inputs, etc.)
            bear_adjustments: Bear case adjustments (e.g., {'growth_rates': [0.02, 0.02, ...]})
            bull_adjustments: Bull case adjustments
        """

        # Default adjustments if not provided
        if bear_adjustments is None:
            bear_adjustments = {
                'growth_multiplier': 0.5,  # 50% of base growth
                'wacc_adjustment': 0.02,   # +2% to WACC
                'terminal_growth_adjustment': -0.01  # -1% terminal growth
            }

        if bull_adjustments is None:
            bull_adjustments = {
                'growth_multiplier': 1.5,   # 150% of base growth
                'wacc_adjustment': -0.01,   # -1% to WACC
                'terminal_growth_adjustment': 0.005  # +0.5% terminal growth
            }

        scenarios = {}

        # Base case
        base_result = self.comprehensive_dcf(**base_inputs)
        scenarios['base'] = {
            'price_per_share': base_result['summary']['price_per_share'],
            'equity_value': base_result['summary']['equity_value'],
            'enterprise_value': base_result['summary']['enterprise_value'],
            'full_results': base_result
        }

        # Bear case
        bear_inputs = self._adjust_inputs(base_inputs, bear_adjustments, scenario_type='bear')
        bear_result = self.comprehensive_dcf(**bear_inputs)
        scenarios['bear'] = {
            'price_per_share': bear_result['summary']['price_per_share'],
            'equity_value': bear_result['summary']['equity_value'],
            'enterprise_value': bear_result['summary']['enterprise_value'],
            'full_results': bear_result
        }

        # Bull case
        bull_inputs = self._adjust_inputs(base_inputs, bull_adjustments, scenario_type='bull')
        bull_result = self.comprehensive_dcf(**bull_inputs)
        scenarios['bull'] = {
            'price_per_share': bull_result['summary']['price_per_share'],
            'equity_value': bull_result['summary']['equity_value'],
            'enterprise_value': bull_result['summary']['enterprise_value'],
            'full_results': bull_result
        }

        return {
            'scenarios': scenarios,
            'price_range': {
                'low': scenarios['bear']['price_per_share'],
                'base': scenarios['base']['price_per_share'],
                'high': scenarios['bull']['price_per_share']
            },
            'valuation_range': {
                'low': scenarios['bear']['equity_value'],
                'base': scenarios['base']['equity_value'],
                'high': scenarios['bull']['equity_value']
            }
        }

    def _adjust_inputs(self, base_inputs: Dict[str, Any],
                      adjustments: Dict[str, Any],
                      scenario_type: str) -> Dict[str, Any]:
        """Helper to adjust inputs for scenario analysis"""
        import copy
        inputs = copy.deepcopy(base_inputs)

        # Adjust growth rates
        if 'growth_rates' in adjustments:
            inputs['growth_rates'] = adjustments['growth_rates']
        elif 'growth_multiplier' in adjustments:
            multiplier = adjustments['growth_multiplier']
            inputs['growth_rates'] = [max(0.0, min(g * multiplier, 0.30)) for g in inputs['growth_rates']]

        # Adjust WACC
        if 'wacc_adjustment' in adjustments:
            # Adjust beta to change WACC
            current_beta = inputs['wacc_inputs']['beta']
            # Approximate: +1% WACC ≈ +0.15 beta (assuming 7% MRP)
            mrp = inputs['wacc_inputs']['market_risk_premium']
            beta_change = adjustments['wacc_adjustment'] / mrp if mrp > 0 else 0
            inputs['wacc_inputs']['beta'] = max(0.1, min(current_beta + beta_change, 3.0))

        # Adjust terminal growth
        if 'terminal_growth_adjustment' in adjustments:
            current_tg = inputs['terminal_growth_rate']
            inputs['terminal_growth_rate'] = max(0.0, min(current_tg + adjustments['terminal_growth_adjustment'], 0.05))

        return inputs

# ── JSON contract (MAAnalyticsService / ma_dcf MCP tool) ─────────────────────

def _dcf_inputs(p: Dict[str, Any]) -> Dict[str, Any]:
    """Resolve the DCF inputs from one params object. Every assumption is a
    required input; nothing is defaulted.

    WACC: either `wacc` directly, or its components risk_free_rate, beta,
          market_risk_premium, cost_of_debt, tax_rate, market_cap (equity
          market value) and debt (market value of debt).
    FCF:  either `fcf_projections` (list of explicit FCFF per year) or a base
          year build -- ebit, tax_rate, d_and_a, capex, change_in_nwc -- grown
          by `growth_rates` (list, decimal per projection year).
    Terminal: terminal_method 'perpetuity' (needs terminal_growth) or
          'exit_multiple' (needs exit_multiple and terminal_ebitda, or a base
          `ebitda` grown at the same growth_rates).
    Bridge: cash, debt (or net_debt), optional minority_interest /
          preferred_stock (0 = none), shares_outstanding.
    """
    from corporateFinance._cli import num, opt_num, num_list, has, InputError

    out: Dict[str, Any] = {}
    model = DCFModel(str(p.get('company_name') or 'Target'))

    # WACC
    if has(p, 'wacc'):
        out['wacc'] = num(p, 'wacc', gt=0, max=1)
        out['wacc_detail'] = {'source': 'input'}
    else:
        tax = num(p, 'tax_rate', min=0, max=0.6)
        w = model.calculate_wacc(
            risk_free_rate=num(p, 'risk_free_rate', min=0, max=0.2),
            market_risk_premium=num(p, 'market_risk_premium', label='market_risk_premium (equity risk premium)', min=0, max=0.2),
            beta=num(p, 'beta', min=0.1, max=3.0),
            cost_of_debt=num(p, 'cost_of_debt', min=0, max=0.3),
            tax_rate=tax,
            market_value_equity=num(p, 'market_cap', label='market_cap (market value of equity)', min=0),
            market_value_debt=num(p, 'debt', min=0),
            country_risk_premium=opt_num(p, 'country_risk_premium', 0.0, min=0, max=0.15),
            size_premium=opt_num(p, 'size_premium', 0.0, min=0, max=0.10),
        )
        if p.get('market_cap', 0) + p.get('debt', 0) <= 0:
            raise InputError("market_cap + debt must be positive to weight the WACC")
        out['wacc'] = w['wacc']
        out['wacc_detail'] = {
            'source': 'capm',
            'cost_of_equity_pct': w['cost_of_equity'],
            'cost_of_debt_after_tax_pct': w['cost_of_debt_aftertax'],
            'equity_weight_pct': w['equity_weight'],
            'debt_weight_pct': w['debt_weight'],
        }

    # Free cash flows
    if has(p, 'fcf_projections'):
        fcfs = num_list(p, 'fcf_projections')
        out['fcf_rows'] = [{'year': i + 1, 'fcf': f} for i, f in enumerate(fcfs)]
        out['base_fcf'] = None
        growth = None
    else:
        growth = num_list(p, 'growth_rates', label='growth_rates (FCF growth per projection year)')
        if any(g <= -1 for g in growth):
            raise InputError("growth_rates must be > -100%")
        base = model.calculate_free_cash_flow(
            ebit=num(p, 'ebit'),
            tax_rate=num(p, 'tax_rate', min=0, max=0.6),
            depreciation=num(p, 'd_and_a', label='d_and_a (depreciation & amortization)', min=0),
            capex=num(p, 'capex', min=0),
            change_in_nwc=num(p, 'change_in_nwc', label='change_in_nwc (increase in net working capital)'),
        )
        out['base_fcf'] = base['free_cash_flow']
        proj = model.project_cash_flows(base['free_cash_flow'], growth)
        fcfs = [r['fcf'] for r in proj]
        out['fcf_rows'] = [{'year': r['year'], 'growth_pct': r['growth_rate'], 'fcf': r['fcf']} for r in proj]
        out['base_fcf_detail'] = {
            'ebit': base['ebit'], 'taxes': base['tax'], 'nopat': base['nopat'],
            'add_d_and_a': base['add_depreciation'], 'less_capex': base['less_capex'],
            'less_change_in_nwc': base['less_change_in_nwc'], 'base_year_fcf': base['free_cash_flow'],
        }
    out['fcfs'] = fcfs

    # Terminal value
    method = str(p.get('terminal_method') or '').strip().lower()
    if method in ('perpetuity', 'gordon', 'perpetuity_growth'):
        method = 'perpetuity'
        out['terminal_growth'] = num(p, 'terminal_growth', min=-0.05, max=0.05)
    elif method in ('exit_multiple', 'multiple'):
        method = 'exit_multiple'
        out['exit_multiple'] = num(p, 'exit_multiple', gt=0)
        if has(p, 'terminal_ebitda'):
            out['terminal_ebitda'] = num(p, 'terminal_ebitda')
            out['terminal_ebitda_basis'] = 'input'
        else:
            if growth is None:
                raise InputError("exit_multiple method needs terminal_ebitda when fcf_projections are given")
            e = num(p, 'ebitda', label='ebitda (base-year EBITDA, or give terminal_ebitda)')
            for g in growth:
                e *= (1 + g)
            out['terminal_ebitda'] = e
            out['terminal_ebitda_basis'] = 'base EBITDA grown at growth_rates'
    else:
        raise InputError("Missing required input: terminal_method ('perpetuity' or 'exit_multiple')")
    out['terminal_method'] = method

    mid = p.get('mid_year_convention')
    if mid is None:
        raise InputError("Missing required input: mid_year_convention (true/false)")
    out['mid_year'] = bool(mid)

    # Bridge
    if has(p, 'net_debt') and not has(p, 'cash'):
        out['net_debt'] = num(p, 'net_debt')
    else:
        out['cash'] = num(p, 'cash', min=0)
        out['debt'] = num(p, 'debt', min=0)
        out['net_debt'] = out['debt'] - out['cash']
    out['minority_interest'] = opt_num(p, 'minority_interest', 0.0, min=0)
    out['preferred_stock'] = opt_num(p, 'preferred_stock', 0.0, min=0)
    out['shares'] = num(p, 'shares_outstanding', gt=0) if has(p, 'shares_outstanding') else num(p, 'shares', label='shares_outstanding', gt=0)
    out['model'] = model
    return out


def _dcf_value(i: Dict[str, Any], wacc: float, terminal_growth: Optional[float] = None) -> Dict[str, Any]:
    model: DCFModel = i['model']
    fcfs = i['fcfs']
    if i['terminal_method'] == 'perpetuity':
        g = i['terminal_growth'] if terminal_growth is None else terminal_growth
        tv = model.calculate_terminal_value(fcfs[-1], g, wacc, method='perpetuity')
    else:
        tv = model.calculate_terminal_value(fcfs[-1], 0.0, wacc, method='exit_multiple',
                                            exit_multiple=i['exit_multiple'], exit_metric=i['terminal_ebitda'])
    ev = model.calculate_enterprise_value(fcfs, tv['terminal_value'], wacc,
                                          mid_year_convention=i['mid_year'], terminal_method=i['terminal_method'])
    equity = ev['enterprise_value'] - i['net_debt'] - i['minority_interest'] - i['preferred_stock']
    return {'tv': tv, 'ev': ev, 'equity_value': equity, 'price_per_share': equity / i['shares']}


def _json_calculate(p: Dict[str, Any]) -> Dict[str, Any]:
    i = _dcf_inputs(p)
    wacc = i['wacc']
    r = _dcf_value(i, wacc)
    ev = r['ev']
    rows = []
    for row, d in zip(i['fcf_rows'], ev['fcf_details']):
        rows.append({**row, 'discount_factor': d['discount_factor'], 'present_value': d['present_value']})
    data = {
        'enterprise_value': ev['enterprise_value'],
        'equity_value': r['equity_value'],
        'value_per_share': r['price_per_share'],
        'wacc_pct': wacc * 100,
        'terminal_method': i['terminal_method'],
        'terminal_value': r['tv']['terminal_value'],
        'pv_fcf': ev['pv_of_fcf'],
        'pv_terminal_value': ev['pv_of_terminal_value'],
        'terminal_value_share_pct': ev['terminal_value_contribution'],
        'mid_year_convention': i['mid_year'],
        'projections': rows,
        'equity_bridge': {
            'enterprise_value': ev['enterprise_value'],
            'less_net_debt': i['net_debt'],
            'less_minority_interest': i['minority_interest'],
            'less_preferred_stock': i['preferred_stock'],
            'equity_value': r['equity_value'],
            'shares_outstanding': i['shares'],
            'value_per_share': r['price_per_share'],
        },
        'wacc_build': i['wacc_detail'],
    }
    if i['terminal_method'] == 'perpetuity':
        data['terminal_growth_pct'] = i['terminal_growth'] * 100
        # Implied exit multiple is informative only when EBITDA was supplied.
    else:
        data['exit_multiple_x'] = i['exit_multiple']
        data['terminal_ebitda'] = i['terminal_ebitda']
        data['terminal_ebitda_basis'] = i['terminal_ebitda_basis']
        # Implied perpetuity growth: TV = FCF_N (1+g)/(wacc-g)  =>  g = (TV*wacc - FCF_N)/(TV + FCF_N)
        tvv, f = r['tv']['terminal_value'], i['fcfs'][-1]
        data['implied_terminal_growth_pct'] = (tvv * wacc - f) / (tvv + f) * 100 if (tvv + f) else None
    if i.get('base_fcf_detail'):
        data['base_year_fcf'] = i['base_fcf_detail']
    return data


def _json_sensitivity(p: Dict[str, Any]) -> Dict[str, Any]:
    """Grid of value per share over WACC x terminal growth (perpetuity) or
    WACC alone (exit multiple). Base inputs are the same as `calculate`
    (optionally nested under base_params)."""
    from corporateFinance._cli import num_list, InputError
    base = p.get('base_params') if isinstance(p.get('base_params'), dict) else p
    i = _dcf_inputs(base)
    waccs = num_list(p, 'wacc_range', label='wacc_range (list of WACC values)')
    if i['terminal_method'] == 'perpetuity':
        tgs = num_list(p, 'tgr_range', label='tgr_range (list of terminal growth values)')
    else:
        tgs = [None]
    grid = []
    for g in tgs:
        row = {'terminal_growth_pct': g * 100 if g is not None else None}
        for w in waccs:
            key = f"WACC {w * 100:.2f}%"
            if g is not None and w <= g:
                row[key] = None
                continue
            row[key] = _dcf_value(i, w, g)['price_per_share']
        grid.append(row)
    return {
        'terminal_method': i['terminal_method'],
        'base_wacc_pct': i['wacc'] * 100,
        'value_per_share_grid': grid,
        'note': 'Rows: terminal growth; columns: WACC; cells: value per share. Null where WACC <= g.',
    }


JSON_COMMANDS = {
    'calculate': _json_calculate,
    'dcf': _json_calculate,
    'sensitivity': _json_sensitivity,
}


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json(JSON_COMMANDS)


if __name__ == '__main__':
    main()

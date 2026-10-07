"""Monte Carlo Simulation for M&A Valuation"""
from typing import Dict, Any, List, Optional, Callable
import numpy as np

def _irr(cash_flows: List[float]) -> Optional[float]:
    """IRR via polynomial roots (np.irr was removed in numpy 1.20, so the
    old call always raised and every simulation was silently dropped).
    Returns None when no real rate exists."""
    # NPV = sum cf_t * x^t with x = 1/(1+r); np.roots wants highest degree first.
    roots = np.roots(list(reversed(cash_flows)))
    rates = [1.0 / x.real - 1.0 for x in roots
             if abs(x.imag) < 1e-10 and x.real > 0]
    if not rates:
        return None
    return min(rates, key=abs)


class MonteCarloValuation:
    """Monte Carlo simulation for deal valuation under uncertainty"""

    def __init__(self, num_simulations: int = 10000, random_seed: Optional[int] = None):
        self.num_simulations = num_simulations
        if random_seed:
            np.random.seed(random_seed)

    def simulate_synergies(self, base_revenue_synergy: float,
                          base_cost_synergy: float,
                          revenue_std_dev_pct: float,
                          cost_std_dev_pct: float,
                          correlation: float = 0.3) -> Dict[str, Any]:
        """Simulate synergy outcomes with correlated uncertainties"""

        revenue_std = base_revenue_synergy * revenue_std_dev_pct
        cost_std = base_cost_synergy * cost_std_dev_pct

        mean = [base_revenue_synergy, base_cost_synergy]
        cov = [
            [revenue_std**2, correlation * revenue_std * cost_std],
            [correlation * revenue_std * cost_std, cost_std**2]
        ]

        simulated_synergies = np.random.multivariate_normal(mean, cov, self.num_simulations)

        revenue_synergies = simulated_synergies[:, 0]
        cost_synergies = simulated_synergies[:, 1]
        total_synergies = revenue_synergies + cost_synergies

        revenue_synergies = np.maximum(revenue_synergies, 0)
        cost_synergies = np.maximum(cost_synergies, 0)
        total_synergies = revenue_synergies + cost_synergies

        return {
            'revenue_synergies': {
                'mean': float(np.mean(revenue_synergies)),
                'median': float(np.median(revenue_synergies)),
                'std': float(np.std(revenue_synergies)),
                'percentile_5': float(np.percentile(revenue_synergies, 5)),
                'percentile_25': float(np.percentile(revenue_synergies, 25)),
                'percentile_75': float(np.percentile(revenue_synergies, 75)),
                'percentile_95': float(np.percentile(revenue_synergies, 95))
            },
            'cost_synergies': {
                'mean': float(np.mean(cost_synergies)),
                'median': float(np.median(cost_synergies)),
                'std': float(np.std(cost_synergies)),
                'percentile_5': float(np.percentile(cost_synergies, 5)),
                'percentile_25': float(np.percentile(cost_synergies, 25)),
                'percentile_75': float(np.percentile(cost_synergies, 75)),
                'percentile_95': float(np.percentile(cost_synergies, 95))
            },
            'total_synergies': {
                'mean': float(np.mean(total_synergies)),
                'median': float(np.median(total_synergies)),
                'std': float(np.std(total_synergies)),
                'percentile_5': float(np.percentile(total_synergies, 5)),
                'percentile_25': float(np.percentile(total_synergies, 25)),
                'percentile_75': float(np.percentile(total_synergies, 75)),
                'percentile_95': float(np.percentile(total_synergies, 95))
            },
            'probability_positive': float(np.sum(total_synergies > 0) / self.num_simulations * 100),
            'correlation': correlation,
            'num_simulations': self.num_simulations
        }

    def simulate_dcf_valuation(self, base_fcf: List[float],
                               fcf_growth_mean: float,
                               fcf_growth_std: float,
                               terminal_growth_mean: float,
                               terminal_growth_std: float,
                               wacc_mean: float,
                               wacc_std: float) -> Dict[str, Any]:
        """Simulate DCF valuation with uncertain inputs"""

        valuations = []

        for _ in range(self.num_simulations):
            growth_rates = np.random.normal(fcf_growth_mean, fcf_growth_std, len(base_fcf))
            fcf_projection = [base_fcf[0] * np.prod(1 + growth_rates[:i+1]) for i in range(len(base_fcf))]

            terminal_growth = np.random.normal(terminal_growth_mean, terminal_growth_std)
            terminal_growth = np.clip(terminal_growth, 0, 0.06)

            wacc = np.random.normal(wacc_mean, wacc_std)
            wacc = np.clip(wacc, 0.05, 0.20)

            terminal_fcf = fcf_projection[-1] * (1 + terminal_growth)
            terminal_value = terminal_fcf / (wacc - terminal_growth)

            pv_fcf = sum(fcf / ((1 + wacc) ** (i + 1)) for i, fcf in enumerate(fcf_projection))
            pv_terminal = terminal_value / ((1 + wacc) ** len(base_fcf))

            valuation = pv_fcf + pv_terminal
            valuations.append(valuation)

        valuations = np.array(valuations)

        return {
            'valuation_statistics': {
                'mean': float(np.mean(valuations)),
                'median': float(np.median(valuations)),
                'std': float(np.std(valuations)),
                'min': float(np.min(valuations)),
                'max': float(np.max(valuations)),
                'percentile_5': float(np.percentile(valuations, 5)),
                'percentile_10': float(np.percentile(valuations, 10)),
                'percentile_25': float(np.percentile(valuations, 25)),
                'percentile_75': float(np.percentile(valuations, 75)),
                'percentile_90': float(np.percentile(valuations, 90)),
                'percentile_95': float(np.percentile(valuations, 95))
            },
            'input_assumptions': {
                'fcf_growth_mean': fcf_growth_mean * 100,
                'fcf_growth_std': fcf_growth_std * 100,
                'terminal_growth_mean': terminal_growth_mean * 100,
                'terminal_growth_std': terminal_growth_std * 100,
                'wacc_mean': wacc_mean * 100,
                'wacc_std': wacc_std * 100
            },
            'num_simulations': self.num_simulations
        }

    def simulate_deal_returns(self, purchase_price: float,
                             synergy_mean: float,
                             synergy_std: float,
                             integration_cost_mean: float,
                             integration_cost_std: float,
                             years_to_realize: int = 3,
                             discount_rate: float = 0.10) -> Dict[str, Any]:
        """Simulate deal returns considering synergies and integration costs"""

        npvs = []
        irrs = []

        for _ in range(self.num_simulations):
            annual_synergy = np.random.normal(synergy_mean, synergy_std)
            annual_synergy = max(0, annual_synergy)

            integration_cost = np.random.normal(integration_cost_mean, integration_cost_std)
            integration_cost = max(0, integration_cost)

            cash_flows = [-purchase_price - integration_cost]

            for year in range(1, years_to_realize + 6):
                if year <= years_to_realize:
                    realized_synergy = annual_synergy * (year / years_to_realize)
                else:
                    realized_synergy = annual_synergy

                cash_flows.append(realized_synergy)

            npv = sum(cf / ((1 + discount_rate) ** i) for i, cf in enumerate(cash_flows))
            npvs.append(npv)

            irr = _irr(cash_flows)
            if irr is not None and -1 < irr < 2:
                irrs.append(irr)

        npvs = np.array(npvs)
        irrs = np.array(irrs)

        probability_positive_npv = float(np.sum(npvs > 0) / self.num_simulations * 100)

        return {
            'npv_statistics': {
                'mean': float(np.mean(npvs)),
                'median': float(np.median(npvs)),
                'std': float(np.std(npvs)),
                'percentile_5': float(np.percentile(npvs, 5)),
                'percentile_25': float(np.percentile(npvs, 25)),
                'percentile_75': float(np.percentile(npvs, 75)),
                'percentile_95': float(np.percentile(npvs, 95))
            },
            'irr_statistics': {
                'mean': float(np.mean(irrs)) * 100 if len(irrs) > 0 else None,
                'median': float(np.median(irrs)) * 100 if len(irrs) > 0 else None,
                'std': float(np.std(irrs)) * 100 if len(irrs) > 0 else None
            },
            'probability_positive_npv': probability_positive_npv,
            'probability_negative_npv': 100 - probability_positive_npv,
            'value_at_risk_5pct': float(np.percentile(npvs, 5)),
            'expected_shortfall_5pct': float(np.mean(npvs[npvs <= np.percentile(npvs, 5)])),
            'purchase_price': purchase_price,
            'num_simulations': self.num_simulations
        }

    def simulate_accretion_dilution(self, acquirer_eps: float,
                                   target_eps: float,
                                   purchase_price_mean: float,
                                   purchase_price_std: float,
                                   synergy_mean: float,
                                   synergy_std: float,
                                   acquirer_shares: float,
                                   target_shares: float,
                                   payment_stock_pct: float = 0.5) -> Dict[str, Any]:
        """Simulate EPS accretion/dilution"""

        accretion_pcts = []

        for _ in range(self.num_simulations):
            purchase_price = np.random.normal(purchase_price_mean, purchase_price_std)
            purchase_price = max(purchase_price_mean * 0.5, purchase_price)

            synergy = np.random.normal(synergy_mean, synergy_std)
            synergy = max(0, synergy)

            stock_consideration = purchase_price * payment_stock_pct
            acquirer_price = purchase_price / target_shares

            new_shares = (stock_consideration / acquirer_price) * (target_shares / acquirer_shares)

            combined_earnings = (acquirer_eps * acquirer_shares) + (target_eps * target_shares) + synergy
            pro_forma_shares = acquirer_shares + new_shares

            pro_forma_eps = combined_earnings / pro_forma_shares

            accretion_pct = ((pro_forma_eps - acquirer_eps) / acquirer_eps) * 100
            accretion_pcts.append(accretion_pct)

        accretion_pcts = np.array(accretion_pcts)

        probability_accretive = float(np.sum(accretion_pcts > 0) / self.num_simulations * 100)

        return {
            'accretion_statistics': {
                'mean': float(np.mean(accretion_pcts)),
                'median': float(np.median(accretion_pcts)),
                'std': float(np.std(accretion_pcts)),
                'percentile_5': float(np.percentile(accretion_pcts, 5)),
                'percentile_25': float(np.percentile(accretion_pcts, 25)),
                'percentile_75': float(np.percentile(accretion_pcts, 75)),
                'percentile_95': float(np.percentile(accretion_pcts, 95))
            },
            'probability_accretive': probability_accretive,
            'probability_dilutive': 100 - probability_accretive,
            'expected_accretion_pct': float(np.mean(accretion_pcts)),
            'downside_scenario_5pct': float(np.percentile(accretion_pcts, 5)),
            'upside_scenario_95pct': float(np.percentile(accretion_pcts, 95)),
            'num_simulations': self.num_simulations
        }

    def value_at_risk_analysis(self, deal_value: float,
                              value_distribution: np.ndarray,
                              confidence_level: float = 0.95) -> Dict[str, Any]:
        """Calculate Value at Risk metrics"""

        var_level = 1 - confidence_level
        var_value = float(np.percentile(value_distribution, var_level * 100))

        tail_values = value_distribution[value_distribution <= var_value]
        expected_shortfall = float(np.mean(tail_values)) if len(tail_values) > 0 else var_value

        return {
            'confidence_level': confidence_level * 100,
            'value_at_risk': var_value,
            'expected_shortfall': expected_shortfall,
            'potential_loss': deal_value - var_value,
            'potential_loss_pct': ((deal_value - var_value) / deal_value * 100) if deal_value > 0 else 0
        }

# ── JSON contract (MAAnalyticsService "run" / ma_monte_carlo MCP tool) ───────
#
#   monte_carlo_valuation.py run '<params JSON>'
#
# Revenue-driven DCF enterprise-value simulation. Every input is required
# except `seed`; rates are decimals (0.10 == 10 %).
#   base_revenue        current (year-0) revenue, amount
#   rev_growth_mean     mean annual revenue growth      (decimal)
#   rev_growth_std      std dev of annual growth        (decimal, >= 0)
#   margin_mean         mean unlevered FCF margin = FCF / revenue (decimal)
#   margin_std          std dev of FCF margin           (decimal, >= 0)
#   discount_rate       WACC used to discount FCF       (decimal)
#   terminal_growth     Gordon terminal growth          (decimal, < discount_rate)
#   projection_years    explicit forecast years         (integer, 1-30)
#   simulations         number of paths                 (integer, 100-200000)
#   seed                optional RNG seed for a reproducible run
#
# Each path draws an independent normal growth rate and FCF margin for every
# projection year: Rev_t = Rev_{t-1} (1+g_t); FCF_t = Rev_t m_t;
# TV = FCF_N (1+tg) / (r - tg); EV = sum FCF_t/(1+r)^t + TV/(1+r)^N
# (end-of-year discounting). With both std devs at 0 every path equals the
# deterministic DCF, which is reported alongside for reference.

import sys
from pathlib import Path as _Path

sys.path.insert(0, str(_Path(__file__).resolve().parent.parent.parent))
from corporateFinance._cli import InputError, has, is_json_call, num, run_json  # noqa: E402


def _alias(p, key, *aliases):
    """Copy the first present alias into `key` (keys only, no value defaults)."""
    if not has(p, key):
        for a in aliases:
            if has(p, a):
                p[key] = p[a]
                break
    return p


def simulate_revenue_dcf(base_revenue, rev_growth_mean, rev_growth_std, margin_mean, margin_std,
                         discount_rate, terminal_growth, projection_years, simulations, seed=None):
    rng = np.random.default_rng(seed)
    n, N = int(simulations), int(projection_years)
    growth = rng.normal(rev_growth_mean, rev_growth_std, size=(n, N))
    margin = rng.normal(margin_mean, margin_std, size=(n, N))
    revenue = base_revenue * np.cumprod(1.0 + growth, axis=1)
    fcf = revenue * margin
    disc = (1.0 + discount_rate) ** -np.arange(1, N + 1)
    pv_fcf = fcf @ disc
    tv = fcf[:, -1] * (1.0 + terminal_growth) / (discount_rate - terminal_growth)
    pv_tv = tv * disc[-1]
    ev = pv_fcf + pv_tv

    # Deterministic DCF at the mean inputs (what a zero-volatility run returns).
    rev_det = base_revenue * (1.0 + rev_growth_mean) ** np.arange(1, N + 1)
    fcf_det = rev_det * margin_mean
    tv_det = fcf_det[-1] * (1.0 + terminal_growth) / (discount_rate - terminal_growth)
    ev_det = float(fcf_det @ disc + tv_det * disc[-1])

    pct = [1, 5, 10, 25, 50, 75, 90, 95, 99]
    pv = np.percentile(ev, pct)
    with np.errstate(divide='ignore', invalid='ignore'):
        tv_share = np.where(ev != 0, pv_tv / ev, np.nan)
    return {
        'method': f'Monte Carlo simulation ({n:,} paths, {N}-year revenue-driven DCF)',
        'is_simulation': True,
        'mean_ev': float(ev.mean()),
        'median_ev': float(np.median(ev)),
        'std_ev': float(ev.std(ddof=1)) if n > 1 else None,
        'p5_ev': float(pv[1]),
        'p95_ev': float(pv[7]),
        'min_ev': float(ev.min()),
        'max_ev': float(ev.max()),
        'deterministic_ev': ev_det,
        'prob_ev_below_deterministic_pct': float(np.mean(ev < ev_det) * 100),
        'prob_negative_ev_pct': float(np.mean(ev < 0) * 100),
        'mean_terminal_value_share_pct': float(np.nanmean(tv_share) * 100),
        'simulations': n,
        'seed': seed,
        'percentiles': [{'percentile': p, 'enterprise_value': float(v)} for p, v in zip(pct, pv)],
        'assumptions': {
            'base_revenue': base_revenue,
            'rev_growth_mean_pct': rev_growth_mean * 100,
            'rev_growth_std_pct': rev_growth_std * 100,
            'fcf_margin_mean_pct': margin_mean * 100,
            'fcf_margin_std_pct': margin_std * 100,
            'discount_rate_pct': discount_rate * 100,
            'terminal_growth_pct': terminal_growth * 100,
            'projection_years': N,
        },
    }


def cmd_run(p):
    p = dict(p)
    _alias(p, 'rev_growth_mean', 'revenue_growth_mean', 'growth_mean')
    _alias(p, 'rev_growth_std', 'revenue_growth_std', 'growth_std')
    _alias(p, 'discount_rate', 'wacc', 'wacc_mean')
    _alias(p, 'simulations', 'num_simulations')
    if not has(p, 'base_revenue') and (has(p, 'base_valuation') or has(p, 'base_value')):
        raise InputError("base_revenue is required: the simulation projects revenue x FCF margin; "
                         "a base valuation cannot stand in for revenue")
    base_revenue = num(p, 'base_revenue', label='base_revenue', gt=0)
    g_mu = num(p, 'rev_growth_mean', label='rev_growth_mean (decimal)', min=-0.99, max=5)
    g_sd = num(p, 'rev_growth_std', label='rev_growth_std (decimal)', min=0, max=5)
    m_mu = num(p, 'margin_mean', label='margin_mean (FCF margin, decimal)', min=-5, max=1)
    m_sd = num(p, 'margin_std', label='margin_std (decimal)', min=0, max=5)
    r = num(p, 'discount_rate', label='discount_rate (decimal)', gt=0, max=1)
    tg = num(p, 'terminal_growth', label='terminal_growth (decimal)', min=-0.5)
    if tg >= r:
        raise InputError(f"terminal_growth ({tg:.4f}) must be below discount_rate ({r:.4f})")
    years = num(p, 'projection_years', label='projection_years', min=1, max=30, integer=True)
    sims = num(p, 'simulations', label='simulations', min=100, max=200000, integer=True)
    seed = num(p, 'seed', integer=True, min=0) if has(p, 'seed') else None
    return simulate_revenue_dcf(base_revenue, g_mu, g_sd, m_mu, m_sd, r, tg, years, sims, seed)


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'run': cmd_run, 'monte_carlo': cmd_run})


if __name__ == '__main__':
    main()

"""Complete LBO Model"""
import sys
from pathlib import Path
from typing import Dict, Any, List, Optional

# Add Analytics path for absolute imports
analytics_path = Path(__file__).parent.parent.parent
sys.path.insert(0, str(analytics_path))

# Use absolute imports instead of relative imports
from corporateFinance.lbo.returns_calculator import ReturnsCalculator

# ── JSON contract (MAAnalyticsService / ma_lbo_* MCP tools) ──────────────────

def _lbo_inputs(p: Dict[str, Any]) -> Dict[str, Any]:
    from corporateFinance._cli import num, opt_num, num_list, has, InputError
    from corporateFinance.lbo.debt_schedule import tranches_from_params
    i: Dict[str, Any] = {}
    i['entry_ebitda'] = num(p, 'ebitda', label='ebitda (LTM EBITDA at entry)', gt=0)
    i['entry_multiple'] = num(p, 'entry_multiple', gt=0)
    i['exit_multiple'] = num(p, 'exit_multiple', gt=0)
    i['years'] = num(p, 'holding_period', min=1, max=15, integer=True)
    i['revenue'] = num(p, 'revenue', label='revenue (LTM revenue at entry)', gt=0)
    if has(p, 'revenue_growth') and isinstance(p['revenue_growth'], (list, str)):
        g = num_list(p, 'revenue_growth')
        if len(g) < i['years']:
            raise InputError(f"revenue_growth needs {i['years']} values (one per year)")
        i['growth'] = g[:i['years']]
    else:
        i['growth'] = [num(p, 'revenue_growth', min=-0.99)] * i['years']
    i['margin'] = num(p, 'ebitda_margin', label='ebitda_margin (projected)', min=-1, max=1)
    i['da_pct'] = num(p, 'd_and_a_pct', label='d_and_a_pct (D&A % of revenue)', min=0, max=1)
    i['capex_pct'] = num(p, 'capex_pct', label='capex_pct (capex % of revenue)', min=0, max=1)
    i['nwc_pct'] = num(p, 'nwc_pct', label='nwc_pct (NWC % of incremental revenue)', min=-1, max=1)
    i['tax_rate'] = num(p, 'tax_rate', min=0, max=0.6)
    i['sweep_pct'] = num(p, 'sweep_pct', min=0, max=1)
    i['transaction_fees'] = opt_num(p, 'transaction_fees', 0.0, min=0)
    i['financing_fees'] = opt_num(p, 'financing_fees', 0.0, min=0)
    i['tranches'] = tranches_from_params(p)
    return i


def _lbo_run(i: Dict[str, Any], entry_multiple: Optional[float] = None,
             exit_multiple: Optional[float] = None) -> Dict[str, Any]:
    from corporateFinance._cli import InputError
    from corporateFinance.lbo.debt_schedule import lbo_waterfall
    em = i['entry_multiple'] if entry_multiple is None else entry_multiple
    xm = i['exit_multiple'] if exit_multiple is None else exit_multiple
    n = i['years']
    entry_ev = i['entry_ebitda'] * em
    debt = sum(t['amount'] for t in i['tranches'])
    uses = entry_ev + i['transaction_fees'] + i['financing_fees']
    equity = uses - debt
    if equity <= 0:
        raise InputError(f"Debt ({debt:,.0f}) covers all uses ({uses:,.0f}): sponsor equity would be <= 0")

    rev, prev = [], i['revenue']
    for g in i['growth']:
        prev = prev * (1 + g)
        rev.append(prev)
    ebitda = [r * i['margin'] for r in rev]
    da = [r * i['da_pct'] for r in rev]
    capex = [r * i['capex_pct'] for r in rev]
    nwc = [(rev[k] - (rev[k - 1] if k else i['revenue'])) * i['nwc_pct'] for k in range(n)]
    w = lbo_waterfall(ebitda, da, capex, nwc, i['tax_rate'], i['tranches'], i['sweep_pct'])

    exit_ev = ebitda[-1] * xm
    exit_equity = exit_ev - w['final_debt'] + w['final_cash']
    irr = ReturnsCalculator.calculate_irr([-equity] + [0.0] * (n - 1) + [exit_equity])
    return {
        'entry_ev': entry_ev, 'debt': debt, 'uses': uses, 'equity': equity,
        'revenue': rev, 'ebitda': ebitda, 'waterfall': w,
        'exit_ev': exit_ev, 'exit_equity': exit_equity,
        'moic': exit_equity / equity, 'irr': irr,
    }


def _json_build(p: Dict[str, Any]) -> Dict[str, Any]:
    i = _lbo_inputs(p)
    r = _lbo_run(i)
    w = r['waterfall']
    sources = [{'item': t['name'] + ' debt', 'amount': t['amount'],
                'share_pct': t['amount'] / r['uses'] * 100} for t in i['tranches']]
    sources.append({'item': 'sponsor equity', 'amount': r['equity'], 'share_pct': r['equity'] / r['uses'] * 100})
    uses = [{'item': 'purchase enterprise value', 'amount': r['entry_ev'], 'share_pct': r['entry_ev'] / r['uses'] * 100},
            {'item': 'transaction fees', 'amount': i['transaction_fees'], 'share_pct': i['transaction_fees'] / r['uses'] * 100},
            {'item': 'financing fees', 'amount': i['financing_fees'], 'share_pct': i['financing_fees'] / r['uses'] * 100}]
    projections = []
    for k, row in enumerate(w['rows']):
        projections.append({'year': k + 1, 'revenue': r['revenue'][k], **row})
    return {
        'irr_pct': r['irr'] * 100 if r['irr'] is not None else None,
        'moic_x': r['moic'],
        'entry_enterprise_value': r['entry_ev'],
        'sponsor_equity': r['equity'],
        'total_debt_at_entry': r['debt'],
        'entry_leverage_x': r['debt'] / i['entry_ebitda'],
        'exit_enterprise_value': r['exit_ev'],
        'exit_debt': w['final_debt'],
        'exit_cash': w['final_cash'],
        'exit_equity_value': r['exit_equity'],
        'holding_period_years': i['years'],
        'sources_total': sum(s_['amount'] for s_ in sources),
        'uses_total': r['uses'],
        'funding_shortfall_years': ', '.join(map(str, w['shortfall_years'])) or 'none',
        'sources': sources,
        'uses': uses,
        'projections': projections,
        'method': 'Purchase on a cash-free/debt-free basis (EV = entry multiple x LTM EBITDA); sponsor equity is '
                  'the plug so sources = uses; exit equity = exit multiple x final-year EBITDA - debt + cash; '
                  'no interim distributions.',
    }


def _json_sensitivity(p: Dict[str, Any]) -> Dict[str, Any]:
    """IRR / MOIC grid over entry multiple x exit multiple around the base case."""
    from corporateFinance._cli import num, InputError
    i = _lbo_inputs(p)
    entry_base = num(p, 'entry_base', gt=0) if p.get('entry_base') is not None else i['entry_multiple']
    exit_base = num(p, 'exit_base', gt=0) if p.get('exit_base') is not None else i['exit_multiple']
    rng = num(p, 'range', label='range (+/- multiple turns)', gt=0)
    steps = num(p, 'steps', min=2, max=15, integer=True)
    def axis(base):
        return [base - rng + 2 * rng * k / (steps - 1) for k in range(steps)]
    entries = [m for m in axis(entry_base) if m > 0]
    exits = [m for m in axis(exit_base) if m > 0]
    # Label precision follows the step so adjacent exit columns never share a
    # key (e.g. 9.95x vs 10.0x at a 0.05 step).
    step = 2 * rng / (steps - 1)
    decimals = 1
    while decimals < 6 and round(step, decimals) != round(step, decimals + 2):
        decimals += 1
    irr_grid, moic_grid = [], []
    for em in entries:
        irr_row = {'entry_multiple_x': em}
        moic_row = {'entry_multiple_x': em}
        for xm in exits:
            col = f"exit {xm:.{decimals}f}x"
            try:
                r = _lbo_run(i, em, xm)
                irr_row[col + ' IRR %'] = r['irr'] * 100 if r['irr'] is not None else None
                moic_row[col + ' MOIC'] = r['moic']
            except InputError:
                irr_row[col + ' IRR %'] = None
                moic_row[col + ' MOIC'] = None
        irr_grid.append(irr_row)
        moic_grid.append(moic_row)
    base = _lbo_run(i, entry_base, exit_base)
    return {
        'base_irr_pct': base['irr'] * 100 if base['irr'] is not None else None,
        'base_moic_x': base['moic'],
        'irr_grid': irr_grid,
        'moic_grid': moic_grid,
        'note': 'Rows: entry multiple; columns: exit multiple. Debt amounts held fixed; sponsor equity re-plugs '
                'with entry price. Null where debt would cover all uses.',
    }


JSON_COMMANDS = {'build': _json_build, 'sensitivity': _json_sensitivity}


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

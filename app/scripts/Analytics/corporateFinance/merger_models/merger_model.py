"""Complete Merger Model - Accretion/Dilution Analysis"""
import sys
from pathlib import Path
from typing import Dict, Any, Optional

# Add Analytics path for absolute imports
analytics_path = Path(__file__).parent.parent.parent
sys.path.insert(0, str(analytics_path))

# Use absolute imports instead of relative imports

# ── JSON contract (MAAnalyticsService / ma_accretion_dilution, ma_merger_model,
#    ma_pro_forma) ─────────────────────────────────────────────────────────────

def _ad_inputs(p: Dict[str, Any]) -> Dict[str, Any]:
    """Accretion/dilution inputs. Acquirer / target may be nested objects
    (UI) or flat acquirer_* / target_* keys (MCP). Every financing assumption
    is required when the part of the deal it prices exists."""
    from corporateFinance._cli import num, opt_num, has, InputError
    a = p.get('acquirer') if isinstance(p.get('acquirer'), dict) else {}
    t = p.get('target') if isinstance(p.get('target'), dict) else {}
    def pick(src, key, flat):
        return src.get(key) if src.get(key) is not None else p.get(flat)
    q = {
        'acq_ni': pick(a, 'net_income', 'acquirer_net_income'),
        'acq_shares': pick(a, 'shares', 'acquirer_shares'),
        'acq_price': pick(a, 'share_price', 'acquirer_share_price'),
        'acq_revenue': pick(a, 'revenue', 'acquirer_revenue'),
        'tgt_ni': pick(t, 'net_income', 'target_net_income'),
        'tgt_revenue': pick(t, 'revenue', 'target_revenue'),
    }
    i: Dict[str, Any] = {}
    i['acq_shares'] = num(q, 'acq_shares', label='acquirer shares outstanding (diluted)', gt=0)
    if q['acq_ni'] is None and has(p, 'acquirer_eps'):
        q['acq_ni'] = num(p, 'acquirer_eps') * i['acq_shares']
    i['acq_ni'] = num(q, 'acq_ni', label='acquirer net income')
    i['tgt_ni'] = num(q, 'tgt_ni', label='target net income')
    i['acq_revenue'] = q['acq_revenue']
    i['tgt_revenue'] = q['tgt_revenue']
    i['price'] = num(p, 'deal_value', label='deal_value (equity purchase price for the target)', gt=0)
    i['cash_pct'] = num(p, 'cash_pct', label='cash_pct (cash share of consideration)', min=0, max=1)
    cash_consid = i['price'] * i['cash_pct']
    stock_consid = i['price'] - cash_consid
    i['cash_consid'], i['stock_consid'] = cash_consid, stock_consid
    i['tax_rate'] = num(p, 'tax_rate', min=0, max=0.6)
    if stock_consid > 0:
        i['acq_price'] = num(q, 'acq_price', label='acquirer share price (for the stock issued)', gt=0)
    else:
        i['acq_price'] = opt_num(q, 'acq_price', gt=0)
    i['cash_used'] = 0.0
    i['new_debt'] = 0.0
    if cash_consid > 0:
        i['cash_used'] = num(p, 'cash_from_balance_sheet', label='cash_from_balance_sheet (acquirer cash used)', min=0)
        if i['cash_used'] > cash_consid:
            raise InputError("cash_from_balance_sheet exceeds the cash consideration")
        i['new_debt'] = cash_consid - i['cash_used']
    i['debt_rate'] = num(p, 'debt_rate', label='debt_rate (pre-tax rate on new acquisition debt)', min=0, max=1) if i['new_debt'] > 0 else 0.0
    i['cash_yield'] = num(p, 'cash_yield', label='cash_yield (pre-tax interest forgone on cash used)', min=0, max=1) if i['cash_used'] > 0 else 0.0
    # Optional items whose absence means exactly zero.
    i['synergies'] = opt_num(p, 'synergies', 0.0)                    # pre-tax run-rate
    i['synergy_phase'] = opt_num(p, 'synergy_phase_in', 1.0, min=0, max=1)  # share realized this year
    i['integration_costs'] = opt_num(p, 'integration_costs', 0.0, min=0)    # pre-tax, this year
    i['new_amortization'] = opt_num(p, 'new_amortization', 0.0, min=0)     # pre-tax D&A on step-ups
    if has(p, 'synergies_after_tax'):   # MCP spelling: after-tax synergies
        i['synergies'] = num(p, 'synergies_after_tax') / (1 - i['tax_rate']) if i['tax_rate'] < 1 else 0.0
    return i


def _ad_compute(i: Dict[str, Any], acq_ni: float, tgt_ni: float, phase: float) -> Dict[str, Any]:
    t = i['tax_rate']
    interest = i['new_debt'] * i['debt_rate']
    forgone = i['cash_used'] * i['cash_yield']
    syn = i['synergies'] * phase
    pretax_adj = syn - i['integration_costs'] - i['new_amortization'] - interest - forgone
    new_shares = i['stock_consid'] / i['acq_price'] if i['stock_consid'] > 0 else 0.0
    pf_shares = i['acq_shares'] + new_shares
    pf_ni = acq_ni + tgt_ni + pretax_adj * (1 - t)
    eps_sa = acq_ni / i['acq_shares']
    eps_pf = pf_ni / pf_shares
    accretion = (eps_pf / eps_sa - 1) if eps_sa > 0 else None
    # Pre-tax synergies (realized this year) for EPS neutrality.
    other = i['integration_costs'] + i['new_amortization'] + interest + forgone
    breakeven = ((eps_sa * pf_shares - acq_ni - tgt_ni) / (1 - t) + other) if t < 1 else None
    return {'interest': interest, 'forgone': forgone, 'syn': syn, 'pretax_adj': pretax_adj,
            'new_shares': new_shares, 'pf_shares': pf_shares, 'pf_ni': pf_ni,
            'eps_sa': eps_sa, 'eps_pf': eps_pf, 'accretion': accretion,
            'breakeven': max(0.0, breakeven) if breakeven is not None else None}


def _json_accretion_dilution(p: Dict[str, Any]) -> Dict[str, Any]:
    i = _ad_inputs(p)
    r = _ad_compute(i, i['acq_ni'], i['tgt_ni'], i['synergy_phase'])
    t = i['tax_rate']
    out = {
        'standalone_eps': r['eps_sa'],
        'pro_forma_eps': r['eps_pf'],
        'accretion_dilution_pct': r['accretion'] * 100 if r['accretion'] is not None else None,
        'status': ('accretive' if r['eps_pf'] > r['eps_sa'] else 'dilutive' if r['eps_pf'] < r['eps_sa'] else 'neutral'),
        'eps_change': r['eps_pf'] - r['eps_sa'],
        'pro_forma_net_income': r['pf_ni'],
        'new_shares_issued': r['new_shares'],
        'pro_forma_shares': r['pf_shares'],
        'target_holders_ownership_pct': r['new_shares'] / r['pf_shares'] * 100,
        'breakeven_pretax_synergies': r['breakeven'],
        'purchase_pe_x': i['price'] / i['tgt_ni'] if i['tgt_ni'] > 0 else None,
        'net_income_bridge': [
            {'item': 'acquirer net income', 'amount': i['acq_ni']},
            {'item': 'target net income', 'amount': i['tgt_ni']},
            {'item': 'synergies realized (after tax)', 'amount': r['syn'] * (1 - t)},
            {'item': 'integration costs (after tax)', 'amount': -i['integration_costs'] * (1 - t)},
            {'item': 'new D&A / amortization (after tax)', 'amount': -i['new_amortization'] * (1 - t)},
            {'item': 'interest on new debt (after tax)', 'amount': -r['interest'] * (1 - t)},
            {'item': 'interest forgone on cash (after tax)', 'amount': -r['forgone'] * (1 - t)},
            {'item': 'pro forma net income', 'amount': r['pf_ni']},
        ],
        'consideration': {
            'deal_value': i['price'], 'cash_consideration': i['cash_consid'],
            'stock_consideration': i['stock_consid'], 'acquirer_cash_used': i['cash_used'],
            'new_debt': i['new_debt'],
        },
    }
    if i['acq_price']:
        out['acquirer_pe_x'] = i['acq_price'] * i['acq_shares'] / i['acq_ni'] if i['acq_ni'] > 0 else None
    return out


def _json_pro_forma(p: Dict[str, Any]) -> Dict[str, Any]:
    """Multi-year accretion/dilution: standalone net incomes grow at
    acquirer_ni_growth / target_ni_growth, synergies phase in per
    synergy_phase_in_by_year (list, fraction of run-rate per year), and
    integration_costs_by_year (list, pre-tax). Acquisition debt is held
    constant (no paydown modelled)."""
    from corporateFinance._cli import num, num_list, has, InputError
    i = _ad_inputs(p)
    n = num(p, 'years', min=1, max=10, integer=True)
    ga = num(p, 'acquirer_ni_growth', label='acquirer_ni_growth (standalone net income growth)', min=-0.99)
    gt = num(p, 'target_ni_growth', label='target_ni_growth (standalone net income growth)', min=-0.99)
    phases = num_list(p, 'synergy_phase_in_by_year', min_len=n) if has(p, 'synergy_phase_in_by_year') else None
    if i['synergies'] and phases is None:
        raise InputError("Missing required input: synergy_phase_in_by_year (one fraction per year)")
    integ = num_list(p, 'integration_costs_by_year', min_len=n) if has(p, 'integration_costs_by_year') else [0.0] * n
    rg = p.get('revenue_growth')
    rows = []
    for y in range(1, n + 1):
        acq_ni = i['acq_ni'] * (1 + ga) ** y
        tgt_ni = i['tgt_ni'] * (1 + gt) ** y
        i['integration_costs'] = integ[y - 1]
        r = _ad_compute(i, acq_ni, tgt_ni, phases[y - 1] if phases else 0.0)
        row = {'year': y, 'acquirer_net_income': acq_ni, 'target_net_income': tgt_ni,
               'pro_forma_net_income': r['pf_ni'], 'standalone_eps': r['eps_sa'], 'pro_forma_eps': r['eps_pf'],
               'accretion_dilution_pct': r['accretion'] * 100 if r['accretion'] is not None else None}
        if rg is not None and i['acq_revenue'] is not None and i['tgt_revenue'] is not None:
            row['combined_revenue'] = (float(i['acq_revenue']) + float(i['tgt_revenue'])) * (1 + float(rg)) ** y
        rows.append(row)
    return {
        'years': n,
        'year1_accretion_dilution_pct': rows[0]['accretion_dilution_pct'],
        'final_year_accretion_dilution_pct': rows[-1]['accretion_dilution_pct'],
        'projections': rows,
        'method': 'Standalone net incomes grown at the stated rates; after-tax synergies (phased), integration '
                  'costs, new amortization and financing costs added; acquisition debt held constant.',
    }


JSON_COMMANDS = {
    'accretion_dilution': _json_accretion_dilution,
    'build': _json_accretion_dilution,
    'pro_forma': _json_pro_forma,
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

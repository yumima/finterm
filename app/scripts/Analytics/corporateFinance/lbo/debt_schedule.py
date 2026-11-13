"""Debt Schedule and Amortization Calculator"""
import sys
from pathlib import Path
from typing import Dict, Any, List

# Add Analytics path for absolute imports
analytics_path = Path(__file__).parent.parent.parent
sys.path.insert(0, str(analytics_path))


# ── Debt waterfall used by the JSON contract (LBO model / debt schedule) ─────

def lbo_waterfall(ebitda: List[float], d_and_a: List[float], capex: List[float],
                  nwc_change: List[float], tax_rate: float,
                  tranches: List[Dict[str, Any]], sweep_pct: float,
                  opening_cash: float = 0.0) -> Dict[str, Any]:
    """Year-by-year levered cash flow and debt paydown.

    tranches: [{name, amount, rate, amort_pct, sweepable}] in repayment
    priority order. amort_pct is the mandatory amortization per year as a
    fraction of the ORIGINAL principal.

    Per year:
      interest_i  = rate_i x opening balance_i   (beginning balance; no circularity)
      taxes       = max(0, (EBITDA - D&A - interest) x tax_rate)   (no NOL carryforward)
      levered FCF = EBITDA - interest - taxes - capex - change in NWC
      mandatory_i = min(amort_pct_i x original_i, opening_i)
      sweep pool  = sweep_pct x max(0, FCF - mandatory), applied to sweepable
                    tranches in order, capped at their balances
      cash        = opening cash + FCF - mandatory - sweep
    A negative cash balance is reported as a funding shortfall (a revolver
    draw is not modelled).
    """
    years = len(ebitda)
    bal = {t['name']: float(t['amount']) for t in tranches}
    orig = dict(bal)
    cash = float(opening_cash)
    rows: List[Dict[str, Any]] = []
    total_interest = 0.0
    shortfall_years: List[int] = []
    for y in range(years):
        opening_debt = sum(bal.values())
        interest_by = {t['name']: bal[t['name']] * t['rate'] for t in tranches}
        interest = sum(interest_by.values())
        taxable = ebitda[y] - d_and_a[y] - interest
        taxes = max(0.0, taxable * tax_rate)
        fcf = ebitda[y] - interest - taxes - capex[y] - nwc_change[y]
        mandatory_by = {t['name']: min(t.get('amort_pct', 0.0) * orig[t['name']], bal[t['name']]) for t in tranches}
        mandatory = sum(mandatory_by.values())
        for n, m in mandatory_by.items():
            bal[n] -= m
        pool = sweep_pct * max(0.0, fcf - mandatory)
        sweep = 0.0
        for t in tranches:
            if not t.get('sweepable') or pool <= 0:
                continue
            pay = min(pool, bal[t['name']])
            bal[t['name']] -= pay
            pool -= pay
            sweep += pay
        cash += fcf - mandatory - sweep
        if cash < -1e-6:
            shortfall_years.append(y + 1)
        closing_debt = sum(bal.values())
        total_interest += interest
        row = {
            'year': y + 1,
            'ebitda': ebitda[y],
            'interest': interest,
            'taxes': taxes,
            'capex': capex[y],
            'nwc_change': nwc_change[y],
            'levered_fcf': fcf,
            'mandatory_amort': mandatory,
            'sweep': sweep,
            'debt_opening': opening_debt,
            'debt_closing': closing_debt,
            'cash_closing': cash,
            'net_debt_x': (closing_debt - cash) / ebitda[y] if ebitda[y] > 0 else None,
            'interest_coverage_x': ebitda[y] / interest if interest > 0 else None,
        }
        for t in tranches:
            row[f"{t['name']}_closing"] = bal[t['name']]
        rows.append(row)
    initial = sum(orig.values())
    final = sum(bal.values())
    return {
        'rows': rows,
        'initial_debt': initial,
        'final_debt': final,
        'final_cash': cash,
        'total_interest': total_interest,
        'total_paydown': initial - final,
        'paydown_pct': (initial - final) / initial * 100 if initial > 0 else None,
        'shortfall_years': shortfall_years,
    }


def tranches_from_params(p: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Revolver (if drawn) -> senior -> subordinated, the usual repayment
    priority. Sweep applies to the revolver and senior debt; subordinated
    notes are non-call bullets. Rates and senior amortization are required
    whenever the tranche is present."""
    from corporateFinance._cli import num, opt_num
    out = []
    rev = opt_num(p, 'revolver', 0.0, min=0)
    if rev > 0:
        out.append({'name': 'revolver', 'amount': rev, 'rate': num(p, 'revolver_rate', min=0, max=1),
                    'amort_pct': 0.0, 'sweepable': True})
    sr = num(p, 'senior_debt', min=0)
    if sr > 0:
        out.append({'name': 'senior', 'amount': sr, 'rate': num(p, 'senior_rate', min=0, max=1),
                    'amort_pct': num(p, 'senior_amort_pct', label='senior_amort_pct (mandatory amortization, fraction of original per year)', min=0, max=1),
                    'sweepable': True})
    sub = opt_num(p, 'sub_debt', 0.0, min=0)
    if sub > 0:
        out.append({'name': 'subordinated', 'amount': sub, 'rate': num(p, 'sub_rate', min=0, max=1),
                    'amort_pct': 0.0, 'sweepable': False})
    return out


def _json_analyze(p: Dict[str, Any]) -> Dict[str, Any]:
    """Debt schedule from an EBITDA path: ebitda (year-1), ebitda_growth,
    d_and_a, capex, nwc_change (annual, flat), tax_rate, years, sweep_pct,
    tranches (see tranches_from_params)."""
    from corporateFinance._cli import num, InputError
    years = num(p, 'years', min=1, max=30, integer=True)
    e1 = num(p, 'ebitda', label='ebitda (year-1 EBITDA)')
    g = num(p, 'ebitda_growth', min=-0.99)
    ebitda = [e1 * (1 + g) ** y for y in range(years)]
    da = [num(p, 'd_and_a', min=0)] * years
    cx = [num(p, 'capex', min=0)] * years
    nwc = [num(p, 'nwc_change', label='nwc_change (annual increase in NWC)')] * years
    tranches = tranches_from_params(p)
    if not tranches:
        raise InputError("Enter at least one debt tranche")
    w = lbo_waterfall(ebitda, da, cx, nwc, num(p, 'tax_rate', min=0, max=0.6), tranches,
                      num(p, 'sweep_pct', min=0, max=1))
    return {
        'initial_debt': w['initial_debt'],
        'final_debt': w['final_debt'],
        'total_paydown': w['total_paydown'],
        'paydown_pct': w['paydown_pct'],
        'total_interest': w['total_interest'],
        'final_cash': w['final_cash'],
        'funding_shortfall_years': ', '.join(map(str, w['shortfall_years'])) or 'none',
        'schedule': w['rows'],
        'method': 'Interest on opening balances; taxes on EBITDA - D&A - interest (no NOLs); '
                  'sweep % of FCF after mandatory amortization, revolver then senior.',
    }


JSON_COMMANDS = {'analyze': _json_analyze, 'debt_schedule': _json_analyze}


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

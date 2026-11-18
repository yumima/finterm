"""Comprehensive Startup Valuation Summary"""
import sys
from pathlib import Path
from typing import Dict, Any

# Add Analytics path for absolute imports
analytics_path = Path(__file__).parent.parent.parent
sys.path.insert(0, str(analytics_path))


def comprehensive_json(p: Dict[str, Any]) -> Dict[str, Any]:
    """JSON contract: any subset (>= 1) of
    {berkus: {...}, scorecard: {...}, vc: {...}, first_chicago: {...}, risk_factor: {...}}
    where each object is exactly the payload of that method's own `calculate`.
    Returns each method's value plus the range / mean / median across methods.
    No method weighting is invented: a weighted value is reported only when the
    caller supplies `weights` ({method: weight}).
    """
    from corporateFinance._cli import InputError
    from corporateFinance.startup_valuation.berkus_method import berkus_json
    from corporateFinance.startup_valuation.scorecard_method import scorecard_json
    from corporateFinance.startup_valuation.vc_method import vc_json
    from corporateFinance.startup_valuation.first_chicago_method import first_chicago_json
    from corporateFinance.startup_valuation.risk_factor_summation import risk_factor_json

    runners = [
        ('berkus', 'Berkus', berkus_json, 'pre_money_valuation'),
        ('scorecard', 'Scorecard', scorecard_json, 'pre_money_valuation'),
        ('vc', 'VC Method', vc_json, 'pre_money_valuation'),
        ('first_chicago', 'First Chicago', first_chicago_json, 'valuation'),
        ('risk_factor', 'Risk Factor Summation', risk_factor_json, 'pre_money_valuation'),
    ]
    rows, errors, details = [], [], {}
    for key, label, fn, value_key in runners:
        sub = p.get(key)
        if sub is None:
            continue
        if not isinstance(sub, dict):
            raise InputError(f"{key} must be an object")
        try:
            res = fn(sub)
        except Exception as e:  # one method's bad input should not hide the others
            errors.append({'method': label, 'error': str(e)})
            continue
        details[key] = res
        rows.append({'method': label, 'valuation': res.get(value_key)})
    if not rows and not errors:
        raise InputError("Provide inputs for at least one method: berkus, scorecard, vc, first_chicago, risk_factor")
    vals = sorted(r['valuation'] for r in rows if r['valuation'] is not None)
    if not vals:
        raise InputError("; ".join(f"{e['method']}: {e['error']}" for e in errors) or "No method produced a value")
    n = len(vals)
    median = vals[n // 2] if n % 2 else (vals[n // 2 - 1] + vals[n // 2]) / 2
    weighted = None
    if isinstance(p.get('weights'), dict):
        keyed = {k: details[k] for k in details}
        num_, den = 0.0, 0.0
        for key, label, fn, value_key in runners:
            w = p['weights'].get(key)
            if w is None or key not in keyed or keyed[key].get(value_key) is None:
                continue
            num_ += float(w) * keyed[key][value_key]
            den += float(w)
        weighted = num_ / den if den > 0 else None
    return {
        'methods_used': n,
        'low': vals[0],
        'high': vals[-1],
        'mean': sum(vals) / n,
        'median': median,
        'weighted_valuation': weighted,
        'valuations': rows,
        'errors': errors,
        'detail': details,
    }


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'comprehensive': comprehensive_json})


if __name__ == '__main__':
    main()

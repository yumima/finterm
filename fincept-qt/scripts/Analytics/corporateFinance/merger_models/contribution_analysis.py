"""Contribution Analysis for M&A"""
import sys
from pathlib import Path
from typing import Dict, Any

# Add Analytics path for absolute imports
analytics_path = Path(__file__).parent.parent.parent
sys.path.insert(0, str(analytics_path))

# Use absolute imports instead of relative imports
from corporateFinance.merger_models.pro_forma_builder import CompanyFinancials

class ContributionAnalyzer:
    """Analyze contributions from each party to combined entity"""

    def __init__(self, acquirer: CompanyFinancials, target: CompanyFinancials):
        self.acquirer = acquirer
        self.target = target

    def analyze_contributions(self, acquirer_shares: float,
                            new_shares_issued: float) -> Dict[str, Any]:
        """Analyze financial and ownership contributions.

        A metric missing for either party (None) yields None contributions,
        never 0% -- the caller renders those as unavailable.
        """

        total_shares = acquirer_shares + new_shares_issued
        acquirer_ownership = acquirer_shares / total_shares * 100
        target_ownership = new_shares_issued / total_shares * 100

        def split(field: str) -> Dict[str, Any]:
            a = getattr(self.acquirer, field)
            t = getattr(self.target, field)
            if a is None or t is None:
                return {'acquirer_pct': None, 'target_pct': None}
            combined = a + t
            if not combined:
                return {'acquirer_pct': None, 'target_pct': None}
            return {'acquirer_pct': a / combined * 100, 'target_pct': t / combined * 100}

        return {
            'ownership': {
                'acquirer_pct': acquirer_ownership,
                'target_pct': target_ownership
            },
            'revenue_contribution': split('revenue'),
            'ebitda_contribution': split('ebitda'),
            'net_income_contribution': split('net_income'),
            'assets_contribution': split('total_assets')
        }


def _build_financials(data: Dict[str, Any]) -> CompanyFinancials:
    """Build CompanyFinancials from caller data. Fields the caller did not
    supply stay None (no invented COGS/SG&A/D&A ratios)."""
    fields = CompanyFinancials.__dataclass_fields__
    return CompanyFinancials(**{k: data.get(k) for k in fields})


def _target_ownership_fraction(ownership: Any) -> float:
    """Target's fraction of the combined entity, from either a float split or
    a dict with acquirer_shares/target_shares. Both must be supplied."""
    if isinstance(ownership, dict):
        acq_shares = ownership.get('acquirer_shares')
        tgt_shares = ownership.get('target_shares')
        if acq_shares is None or tgt_shares is None:
            raise ValueError("acquirer_shares and target_shares are both required")
        if acq_shares + tgt_shares <= 0:
            raise ValueError("acquirer_shares + target_shares must be positive")
        return tgt_shares / (acq_shares + tgt_shares)
    if ownership is None:
        raise ValueError("ownership_split is required")
    split = float(ownership)
    if not 0 <= split < 1:
        raise ValueError("ownership_split must be in [0, 1)")
    return split


def _run(acquirer_data: Dict[str, Any], target_data: Dict[str, Any], ownership: Any) -> Dict[str, Any]:
    ownership_split = _target_ownership_fraction(ownership)
    acquirer = _build_financials(acquirer_data)
    target = _build_financials(target_data)
    # Ownership % is scale-invariant: express shares per unit of acquirer
    # shares rather than assuming an absolute share count.
    acquirer_units = 1.0
    new_units = ownership_split / (1 - ownership_split)
    analyzer = ContributionAnalyzer(acquirer, target)
    return analyzer.analyze_contributions(acquirer_units, new_units)


def main():
    """CLI entry point - outputs JSON for C++ integration"""
    import json

    if len(sys.argv) < 2:
        result = {"success": False, "error": "No command specified"}
        print(json.dumps(result))
        sys.exit(1)

    command = sys.argv[1]

    try:
        if command == "contribution":
            if len(sys.argv) < 5:
                raise ValueError("Acquirer data, target data, and ownership split required")

            acquirer_data = json.loads(sys.argv[2])
            target_data = json.loads(sys.argv[3])
            # ownership_split can be a float or a JSON dict with acquirer_shares/target_shares
            ownership_arg = sys.argv[4]
            try:
                ownership = float(ownership_arg)
            except ValueError:
                ownership = json.loads(ownership_arg)

            result = {"success": True, "data": _run(acquirer_data, target_data, ownership)}
            print(json.dumps(result))

        elif command == "analyze":
            # Single JSON params object: {acquirer, target, ownership_split}
            if len(sys.argv) < 3:
                raise ValueError("Params JSON required")
            params = json.loads(sys.argv[2])
            if not isinstance(params.get('acquirer'), dict) or not isinstance(params.get('target'), dict):
                raise ValueError("acquirer and target financials are required")
            result = {"success": True,
                      "data": _run(params['acquirer'], params['target'], params.get('ownership_split'))}
            print(json.dumps(result))

        else:
            result = {"success": False, "error": f"Unknown command: {command}"}
            print(json.dumps(result))
            sys.exit(1)

    except Exception as e:
        result = {"success": False, "error": str(e)}
        print(json.dumps(result))
        sys.exit(1)

if __name__ == '__main__':
    main()

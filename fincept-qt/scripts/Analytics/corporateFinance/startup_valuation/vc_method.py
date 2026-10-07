"""Venture Capital (VC) Method of Valuation"""
from pathlib import Path
from typing import Dict, Any, Optional
import sys

_ANALYTICS = str(Path(__file__).resolve().parent.parent.parent)
if _ANALYTICS not in sys.path:
    sys.path.insert(0, _ANALYTICS)

class VCMethod:
    """Venture Capital Method for startup valuation"""

    def __init__(self):
        self.typical_ror_by_stage = {
            'seed': 0.60,
            'series_a': 0.50,
            'series_b': 0.40,
            'series_c': 0.30,
            'late_stage': 0.25
        }

        self.typical_exit_multiples = {
            'revenue': {'saas': 8.0, 'ecommerce': 1.5, 'marketplace': 3.0, 'fintech': 5.0},
            'ebitda': {'default': 12.0},
            'pe': {'default': 20.0}
        }

    def calculate_terminal_value(self, exit_year_metric: float,
                                 exit_multiple: float) -> float:
        """Calculate terminal/exit value"""
        return exit_year_metric * exit_multiple

    def calculate_present_value(self, terminal_value: float,
                               required_ror: float, years_to_exit: int) -> float:
        """Discount terminal value to present"""
        return terminal_value / ((1 + required_ror) ** years_to_exit)

    def calculate_post_money_valuation(self, terminal_value: float,
                                      required_ror: float, years_to_exit: int) -> float:
        """Calculate post-money valuation (before financing)"""
        return self.calculate_present_value(terminal_value, required_ror, years_to_exit)

    def calculate_pre_money_valuation(self, post_money: float,
                                     investment_amount: float) -> float:
        """Calculate pre-money valuation"""
        return post_money - investment_amount

    def calculate_ownership_percentage(self, investment_amount: float,
                                      post_money: float) -> float:
        """Calculate investor ownership percentage"""
        return (investment_amount / post_money) * 100

    def comprehensive_valuation(self, exit_year_metric: float,
                                exit_multiple: float,
                                years_to_exit: int,
                                investment_amount: float,
                                stage: str = 'series_a',
                                custom_ror: Optional[float] = None) -> Dict[str, Any]:
        """
        Complete VC method valuation

        Args:
            exit_year_metric: Projected revenue/EBITDA at exit
            exit_multiple: Exit valuation multiple
            years_to_exit: Years until liquidity event
            investment_amount: Amount being invested
            stage: Funding stage
            custom_ror: Custom required rate of return

        Returns:
            Complete valuation analysis
        """

        required_ror = custom_ror or self.typical_ror_by_stage.get(stage, 0.40)

        terminal_value = self.calculate_terminal_value(exit_year_metric, exit_multiple)

        post_money = self.calculate_post_money_valuation(terminal_value, required_ror, years_to_exit)

        pre_money = self.calculate_pre_money_valuation(post_money, investment_amount)

        ownership_pct = self.calculate_ownership_percentage(investment_amount, post_money)

        investor_exit_value = terminal_value * (ownership_pct / 100)

        investor_return = investor_exit_value / investment_amount

        return {
            'method': 'VC Method',
            'inputs': {
                'exit_year_metric': exit_year_metric,
                'exit_multiple': exit_multiple,
                'years_to_exit': years_to_exit,
                'investment_amount': investment_amount,
                'required_ror': required_ror * 100,
                'stage': stage
            },
            'terminal_value': terminal_value,
            'post_money_valuation': post_money,
            'pre_money_valuation': pre_money,
            'investor_ownership_pct': ownership_pct,
            'investor_exit_value': investor_exit_value,
            'investor_return_multiple': investor_return,
            'investor_irr': required_ror * 100
        }

    def reverse_engineer_valuation(self, investment_amount: float,
                                   target_ownership_pct: float) -> Dict[str, Any]:
        """Calculate implied valuation from desired ownership"""

        post_money = investment_amount / (target_ownership_pct / 100)
        pre_money = post_money - investment_amount

        return {
            'investment_amount': investment_amount,
            'target_ownership_pct': target_ownership_pct,
            'implied_post_money': post_money,
            'implied_pre_money': pre_money
        }

    def scenario_analysis(self, base_case: Dict[str, float],
                         bear_case: Dict[str, float],
                         bull_case: Dict[str, float],
                         investment_amount: float,
                         stage: str = 'series_a') -> Dict[str, Any]:
        """Run scenario analysis with three cases"""

        scenarios = {}

        for case_name, case_data in [('bear', bear_case), ('base', base_case), ('bull', bull_case)]:
            valuation = self.comprehensive_valuation(
                exit_year_metric=case_data['exit_metric'],
                exit_multiple=case_data['exit_multiple'],
                years_to_exit=case_data['years_to_exit'],
                investment_amount=investment_amount,
                stage=stage
            )

            scenarios[case_name] = valuation

        return {
            'scenarios': scenarios,
            'valuation_range': {
                'low': scenarios['bear']['pre_money_valuation'],
                'base': scenarios['base']['pre_money_valuation'],
                'high': scenarios['bull']['pre_money_valuation']
            }
        }

    def calculate_required_exit_multiple(self, current_valuation: float,
                                        investment_amount: float,
                                        target_return: float,
                                        years_to_exit: int,
                                        exit_year_metric: float) -> float:
        """Calculate exit multiple needed for target return"""

        post_money = current_valuation + investment_amount
        ownership_pct = investment_amount / post_money

        required_exit_value = investment_amount * target_return

        required_company_value = required_exit_value / ownership_pct

        required_multiple = required_company_value / exit_year_metric

        return required_multiple

def _alias(p: Dict[str, Any], key: str, *aliases: str) -> Dict[str, Any]:
    if p.get(key) is None:
        for a in aliases:
            if p.get(a) is not None:
                p[key] = p[a]
                break
    return p


def vc_json(p: Dict[str, Any]) -> Dict[str, Any]:
    """JSON contract:
    {exit_metric, exit_multiple, years, investment, target_return, retention_ratio?}

    terminal value  TV   = exit_metric x exit_multiple
    final ownership      = investment x (1+r)^n / TV
    current ownership    = final ownership / retention   (retention = share of
                           today's stake left after future dilution; 1 = none)
    post-money           = investment / current ownership = TV x retention / (1+r)^n
    pre-money            = post-money - investment
    """
    from corporateFinance._cli import InputError, num, opt_num, pct
    p = dict(p)
    _alias(p, 'exit_metric', 'projected_revenue', 'exit_year_metric')
    _alias(p, 'years', 'years_to_exit')
    _alias(p, 'investment', 'investment_needed', 'investment_amount')
    _alias(p, 'target_return', 'target_ror', 'required_return')
    metric = num(p, 'exit_metric', label='Exit-year metric ($)', gt=0)
    multiple = num(p, 'exit_multiple', label='Exit multiple', gt=0)
    years = num(p, 'years', label='Years to exit', gt=0)
    investment = num(p, 'investment', label='Investment ($)', gt=0)
    r = num(p, 'target_return', label='Target annual return (decimal)', gt=-1)
    retention = opt_num(p, 'retention_ratio', 1.0, label='Retention ratio', gt=0, max=1)

    tv = metric * multiple
    growth = (1 + r) ** years
    final_own = investment * growth / tv
    current_own = final_own / retention
    post = investment / current_own
    pre = post - investment
    return {
        'method': 'VC Method',
        'terminal_value': tv,
        'post_money_valuation': post,
        'pre_money_valuation': pre,
        'required_ownership_pct': pct(current_own),
        'ownership_at_exit_pct': pct(final_own),
        'investor_exit_proceeds': tv * final_own,
        'investor_moic_x': growth,
        'target_return_pct': pct(r),
        'retention_ratio': retention,
        'feasible': bool(current_own < 1.0 and pre > 0),
        'note': None if (current_own < 1.0 and pre > 0) else
                'Investment exceeds the discounted exit value: the target return is not achievable at these terms',
    }


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'calculate': vc_json})


if __name__ == '__main__':
    main()

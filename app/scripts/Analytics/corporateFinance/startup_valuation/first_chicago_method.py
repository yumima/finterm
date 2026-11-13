"""First Chicago Method - Scenario-Based Valuation"""
from pathlib import Path
from typing import Dict, Any, List, Optional
from dataclasses import dataclass
import sys

_ANALYTICS = str(Path(__file__).resolve().parent.parent.parent)
if _ANALYTICS not in sys.path:
    sys.path.insert(0, _ANALYTICS)

@dataclass
class Scenario:
    """Valuation scenario"""
    name: str
    probability: float
    exit_year: int
    exit_value: float
    description: str

class FirstChicagoMethod:
    """First Chicago Method for scenario-based startup valuation"""

    def __init__(self):
        self.discount_rate = 0.40

    def create_scenario(self, name: str, probability: float,
                       exit_year: int, exit_value: float,
                       description: str = "") -> Scenario:
        """Create valuation scenario"""

        if not 0 <= probability <= 1:
            raise ValueError("Probability must be between 0 and 1")

        return Scenario(name, probability, exit_year, exit_value, description)

    def discount_to_present(self, future_value: float, years: int,
                          discount_rate: Optional[float] = None) -> float:
        """Discount future value to present"""

        rate = self.discount_rate if discount_rate is None else discount_rate
        return future_value / ((1 + rate) ** years)

    def calculate_expected_value(self, scenarios: List[Scenario],
                                discount_rate: Optional[float] = None) -> Dict[str, Any]:
        """Calculate probability-weighted expected value"""

        rate = self.discount_rate if discount_rate is None else discount_rate

        total_probability = sum(s.probability for s in scenarios)
        if abs(total_probability - 1.0) > 0.01:
            raise ValueError(f"Probabilities must sum to 1.0, got {total_probability}")

        scenario_details = []
        weighted_pv_sum = 0

        for scenario in scenarios:
            pv = self.discount_to_present(scenario.exit_value, scenario.exit_year, rate)
            weighted_pv = pv * scenario.probability

            weighted_pv_sum += weighted_pv

            scenario_details.append({
                'name': scenario.name,
                'probability': scenario.probability * 100,
                'exit_year': scenario.exit_year,
                'exit_value': scenario.exit_value,
                'present_value': pv,
                'weighted_present_value': weighted_pv,
                'description': scenario.description
            })

        return {
            'method': 'First Chicago Method',
            'scenarios': scenario_details,
            'discount_rate': rate * 100,
            'expected_present_value': weighted_pv_sum,
            'valuation': weighted_pv_sum
        }

    def three_scenario_valuation(self, best_case: Dict[str, Any],
                                 base_case: Dict[str, Any],
                                 worst_case: Dict[str, Any],
                                 probabilities: Optional[Dict[str, float]] = None) -> Dict[str, Any]:
        """
        Standard three-scenario valuation

        Args:
            best_case: {'exit_value': float, 'exit_year': int, 'description': str}
            base_case: {...}
            worst_case: {...}
            probabilities: Optional custom probabilities (default: 20%, 50%, 30%)

        Returns:
            Expected valuation
        """

        if probabilities is None:
            probabilities = {'best': 0.20, 'base': 0.50, 'worst': 0.30}

        scenarios = [
            self.create_scenario(
                'Best Case',
                probabilities['best'],
                best_case['exit_year'],
                best_case['exit_value'],
                best_case.get('description', '')
            ),
            self.create_scenario(
                'Base Case',
                probabilities['base'],
                base_case['exit_year'],
                base_case['exit_value'],
                base_case.get('description', '')
            ),
            self.create_scenario(
                'Worst Case',
                probabilities['worst'],
                worst_case['exit_year'],
                worst_case['exit_value'],
                worst_case.get('description', '')
            )
        ]

        return self.calculate_expected_value(scenarios)

    def sensitivity_to_probabilities(self, scenarios: List[Scenario],
                                    variable_scenario_index: int) -> Dict[str, Any]:
        """Analyze sensitivity to scenario probabilities"""

        results = []

        for prob in [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7]:
            adjusted_scenarios = scenarios.copy()

            remaining_prob = 1.0 - prob
            other_scenarios = [i for i in range(len(scenarios)) if i != variable_scenario_index]

            original_other_total = sum(scenarios[i].probability for i in other_scenarios)

            adjusted_scenarios[variable_scenario_index] = Scenario(
                scenarios[variable_scenario_index].name,
                prob,
                scenarios[variable_scenario_index].exit_year,
                scenarios[variable_scenario_index].exit_value,
                scenarios[variable_scenario_index].description
            )

            for i in other_scenarios:
                new_prob = (scenarios[i].probability / original_other_total) * remaining_prob
                adjusted_scenarios[i] = Scenario(
                    scenarios[i].name,
                    new_prob,
                    scenarios[i].exit_year,
                    scenarios[i].exit_value,
                    scenarios[i].description
                )

            valuation = self.calculate_expected_value(adjusted_scenarios)

            results.append({
                'variable_probability': prob * 100,
                'valuation': valuation['expected_present_value']
            })

        return {
            'variable_scenario': scenarios[variable_scenario_index].name,
            'sensitivity_data': results
        }

    def calculate_breakeven_probability(self, scenarios: List[Scenario],
                                       current_investment: float,
                                       success_scenario_index: int = 0) -> float:
        """Calculate probability needed for scenario to break even on investment"""

        success_scenario = scenarios[success_scenario_index]
        other_scenarios = [s for i, s in enumerate(scenarios) if i != success_scenario_index]

        success_pv = self.discount_to_present(success_scenario.exit_value, success_scenario.exit_year)

        other_weighted_pv = sum(
            self.discount_to_present(s.exit_value, s.exit_year) * s.probability
            for s in other_scenarios
        )

        other_total_prob = sum(s.probability for s in other_scenarios)

        if success_pv <= 0:
            return 1.0

        breakeven_prob = (current_investment - other_weighted_pv) / success_pv

        return max(0, min(1, breakeven_prob))

def first_chicago_json(p: Dict[str, Any]) -> Dict[str, Any]:
    """JSON contract:
    {scenarios: [{name?, probability, exit_value, exit_year?}], discount_rate, years?}
    (or flat: success_value/base_value/failure_value + success_prob/base_prob).
    A scenario without exit_year uses the top-level `years`.
    Value = sum(p_i x exit_value_i / (1+r)^t_i); probabilities must sum to 1.
    """
    from corporateFinance._cli import InputError, num, opt_num, pct
    r = num(p, 'discount_rate', label='Discount rate (decimal)', gt=-1)
    default_years = opt_num(p, 'years', None, label='Years to exit', gt=0)

    raw = p.get('scenarios')
    if raw is None and p.get('success_value') is not None:
        sp = num(p, 'success_prob', label='Success probability', min=0, max=1)
        bp = num(p, 'base_prob', label='Base probability', min=0, max=1)
        raw = [
            {'name': 'Success', 'probability': sp, 'exit_value': num(p, 'success_value', min=0)},
            {'name': 'Base', 'probability': bp, 'exit_value': num(p, 'base_value', min=0)},
            {'name': 'Failure', 'probability': 1.0 - sp - bp, 'exit_value': num(p, 'failure_value', min=0)},
        ]
    if not isinstance(raw, list) or not raw:
        raise InputError("Missing required input: scenarios")

    default_names = ['Bull', 'Base', 'Bear'] if len(raw) == 3 else []
    rows = []
    total_prob = 0.0
    value = 0.0
    for i, sc in enumerate(raw):
        if not isinstance(sc, dict):
            raise InputError("each scenario must be an object")
        name = sc.get('name') or (default_names[i] if i < len(default_names) else f"Scenario {i + 1}")
        prob = num(sc, 'probability', label=f"{name} probability", min=0, max=1)
        exit_value = num(sc, 'exit_value', label=f"{name} exit value", min=0)
        t = sc.get('exit_year', sc.get('years'))
        if t is None:
            t = default_years
        if t is None:
            raise InputError(f"Missing required input: years to exit for {name}")
        t = float(t)
        if t <= 0:
            raise InputError(f"{name} years to exit must be > 0")
        pv = exit_value / (1 + r) ** t
        total_prob += prob
        value += prob * pv
        rows.append({'scenario': name, 'probability_pct': pct(prob), 'exit_value': exit_value,
                     'years': t, 'present_value': pv, 'weighted_value': prob * pv})
    if abs(total_prob - 1.0) > 1e-3:
        raise InputError(f"Scenario probabilities must sum to 100% (got {total_prob * 100:.2f}%)")
    return {
        'method': 'First Chicago Method',
        'valuation': value,
        'discount_rate_pct': pct(r),
        'probability_weighted_exit_value': sum(row['exit_value'] * row['probability_pct'] / 100 for row in rows),
        'scenarios': rows,
    }


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'calculate': first_chicago_json})


if __name__ == '__main__':
    main()
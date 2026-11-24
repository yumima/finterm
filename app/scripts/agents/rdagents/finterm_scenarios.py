"""
finterm_scenarios.py — rdagent's Qlib scenarios, with what the user asked for
at the end of the description every step of a loop reads.

worker.py names these to rdagent (its `scen` settings are class paths) and puts
the request in FINTERM_RD_GOAL.
"""

from __future__ import annotations

import os

from rdagent.scenarios.qlib.experiment.factor_experiment import QlibFactorScenario
from rdagent.scenarios.qlib.experiment.model_experiment import QlibModelScenario
from rdagent.scenarios.qlib.experiment.quant_experiment import QlibQuantScenario


class _Asked:
    def get_scenario_all_desc(self, *args, **kwargs) -> str:
        text = super().get_scenario_all_desc(*args, **kwargs)
        goal = os.environ.get("FINTERM_RD_GOAL", "").strip()
        return f"{text}\nWhat the user asked for:\n{goal}\n" if goal else text


class FactorScenario(_Asked, QlibFactorScenario):
    pass


class ModelScenario(_Asked, QlibModelScenario):
    pass


class QuantScenario(_Asked, QlibQuantScenario):
    pass

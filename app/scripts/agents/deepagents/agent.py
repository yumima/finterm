"""
agent.py — the Deep Agent: a planner with specialist sub-agents, over the same
finterm tools the app's other agents have.

The tools are the app's own (quotes, candles, fundamentals, news, portfolios…):
read-only ones, reached over its loopback bridge (AgentService's terminal
tools), each call authorised by the session token in the endpoint.

The agent's files are scratch space in its own state. It has no path to the
disk and no shell: what it reads (news, filings) is written by strangers.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
from typing import Any

from subagents import get_subagents_for_type

_FINAGENT = Path(__file__).resolve().parent.parent / "finagent_core"


def _finagent(rel: str):
    """A module of finagent_core by its file: scripts/agents must not go on
    sys.path, where this folder's name would hide the deepagents library."""
    path = _FINAGENT / rel
    spec = importlib.util.spec_from_file_location(f"_finagent_{path.stem}", path)
    mod = importlib.util.module_from_spec(spec)
    import sys
    sys.modules[spec.name] = mod  # dataclasses look their module up
    spec.loader.exec_module(mod)
    return mod


grounding = _finagent("number_grounding.py")

_SYSTEM_PROMPT = """You are finterm's Deep Agent: you plan a financial analysis, delegate its parts to \
your specialist sub-agents, and write the answer.

- Get every fact from the tools. What the tools do not give you, you do not know: say so.
- Say how recent the data is, and tell facts from judgement.
- State the risks of any recommendation.
- Put the complete answer in your last message, not only in a file.
- Be specific about what you need when you delegate.

"""


class ToolLog(list):
    """Every answer the tools gave, sub-agents' calls included: what the
    figures in the reply are checked against."""


def build_tools(config: dict[str, Any], log: ToolLog) -> list[Any]:
    """finterm's tools as LangChain tools, or none when the app offers none."""
    endpoint = config.get("terminal_mcp_endpoint") or ""
    defs = config.get("terminal_tools") or []
    if not endpoint or not defs:
        return []
    from langchain_core.tools import StructuredTool

    toolkit = _finagent("tools/terminal_toolkit.py").TerminalToolkit(endpoint, defs)

    def make(d: dict[str, Any]):
        name = d["name"]

        def call(**kwargs: Any) -> str:
            out = toolkit._call_tool(name, kwargs)
            log.append(out)
            return out

        return StructuredTool.from_function(
            func=call, name=name, description=d.get("description") or name,
            args_schema=d.get("inputSchema") or {"type": "object", "properties": {}})

    return [make(d) for d in defs if d.get("name")]


def create_agent(model: Any, agent_type: str, tools: list[Any]) -> Any:
    from deepagents import create_deep_agent
    from deepagents.backends.state import StateBackend

    return create_deep_agent(
        model=model,
        tools=tools,
        system_prompt=_SYSTEM_PROMPT + grounding.DIRECTIVE,
        subagents=[{**s, "system_prompt": s["system_prompt"] + "\n\n" + grounding.DIRECTIVE}
                   for s in get_subagents_for_type(agent_type)],
        backend=StateBackend,
        name=f"finterm-{agent_type}-agent",
    )

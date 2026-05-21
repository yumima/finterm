"""finagent_core — finterm's agent framework, two-runtime split.

The package is laid out so the *new* two-runtime path (Track 3)
imports cleanly without pulling the legacy agno-based stack:

    from finagent_core.runtimes.anthropic_runtime import AnthropicRuntime
    from finagent_core.runtimes.skills_loader      import list_available_skills

…etc. — none of those touch agno.

The legacy CoreAgent / AgentFactory / SuperAgent / ExecutionPlanner
surface still works, but it's loaded *lazily* via PEP 562 module-
level ``__getattr__``.  Accessing any of those symbols (or
importing them via ``from finagent_core import CoreAgent``) triggers
the agno cascade on demand; never at package-init time.

Why this split.  Track 12 of plans/ai-stack-free-local.md migrates
alpha_arena off direct agno usage onto the two runtimes.  Until
that lands, agno remains an optional dependency: present in
production but missing in the eval / test venv.  Eager imports
broke the entire harness; the lazy form keeps both worlds working
during the transition.

Usage examples remain unchanged:

    from finagent_core import CoreAgent, CoreAgentBuilder
    # CoreAgent loaded on first attribute access; ImportError surfaces
    # if agno isn't installed, with the same traceback as before.
"""
from __future__ import annotations

import importlib
from typing import Any

__version__ = "2.1.0"

# Map of public name → (module path under finagent_core, attribute).
# Anything listed here is exposed as a module-level attribute via
# __getattr__ and loaded on first access.
_LAZY: dict[str, tuple[str, str]] = {
    # Core
    "CoreAgent": ("core_agent", "CoreAgent"),
    "CoreAgentBuilder": ("core_agent", "CoreAgentBuilder"),
    # Registries
    "ToolsRegistry": ("registries", "ToolsRegistry"),
    "ModelsRegistry": ("registries", "ModelsRegistry"),
    "VectorDBRegistry": ("registries", "VectorDBRegistry"),
    "EmbedderRegistry": ("registries", "EmbedderRegistry"),
    # Modules
    "MemoryModule": ("modules", "MemoryModule"),
    "TeamModule": ("modules", "TeamModule"),
    "WorkflowModule": ("modules", "WorkflowModule"),
    "KnowledgeModule": ("modules", "KnowledgeModule"),
    "ReasoningModule": ("modules", "ReasoningModule"),
    "SessionModule": ("modules", "SessionModule"),
    "SessionManager": ("modules", "SessionManager"),
    # Builders
    "TeamBuilder": ("modules.team_module", "TeamBuilder"),
    "WorkflowBuilder": ("modules.workflow_module", "WorkflowBuilder"),
    "KnowledgeBuilder": ("modules.knowledge_module", "KnowledgeBuilder"),
    "ReasoningBuilder": ("modules.reasoning_module", "ReasoningBuilder"),
    # Legacy
    "AgentFactory": ("agent_factory", "AgentFactory"),
    "ConfigLoader": ("config_loader", "ConfigLoader"),
    # Dynamic agent loading (v2.1)
    "AgentLoader": ("agent_loader", "AgentLoader"),
    "AgentCard": ("agent_loader", "AgentCard"),
    "AgentRegistry": ("agent_loader", "AgentRegistry"),
    "get_loader": ("agent_loader", "get_loader"),
    "discover_agents": ("agent_loader", "discover_agents"),
    "create_agent": ("agent_loader", "create_agent"),
    "list_agents": ("agent_loader", "list_agents"),
    "list_categories": ("agent_loader", "list_categories"),
    # SuperAgent routing (v2.1)
    "SuperAgent": ("super_agent", "SuperAgent"),
    "QueryIntent": ("super_agent", "QueryIntent"),
    "RouteConfig": ("super_agent", "RouteConfig"),
    "RoutingResult": ("super_agent", "RoutingResult"),
    "IntentClassifier": ("super_agent", "IntentClassifier"),
    "route_query": ("super_agent", "route_query"),
    "execute_query": ("super_agent", "execute_query"),
    # Execution planner (v2.1)
    "ExecutionPlanner": ("execution_planner", "ExecutionPlanner"),
    "ExecutionPlan": ("execution_planner", "ExecutionPlan"),
    "PlanStep": ("execution_planner", "PlanStep"),
    "PlanBuilder": ("execution_planner", "PlanBuilder"),
    "StepStatus": ("execution_planner", "StepStatus"),
    "StepType": ("execution_planner", "StepType"),
    "create_stock_analysis_plan": ("execution_planner", "create_stock_analysis_plan"),
    "execute_plan": ("execution_planner", "execute_plan"),
    # Repository pattern (v2.1)
    "Repository": ("repositories", "Repository"),
    "AgentSession": ("repositories", "AgentSession"),
    "AgentMemory": ("repositories", "AgentMemory"),
    "TradeDecision": ("repositories", "TradeDecision"),
    "PerformanceSnapshot": ("repositories", "PerformanceSnapshot"),
    "AgentSessionRepository": ("repositories", "AgentSessionRepository"),
    "AgentMemoryRepository": ("repositories", "AgentMemoryRepository"),
    "TradeDecisionRepository": ("repositories", "TradeDecisionRepository"),
    "PerformanceSnapshotRepository": ("repositories", "PerformanceSnapshotRepository"),
    "RepositoryFactory": ("repositories", "RepositoryFactory"),
    # Paper-trading bridge (v2.1)
    "PaperTradingBridge": ("paper_trading_bridge", "PaperTradingBridge"),
    "Portfolio": ("paper_trading_bridge", "Portfolio"),
    "Order": ("paper_trading_bridge", "Order"),
    "Position": ("paper_trading_bridge", "Position"),
    "Trade": ("paper_trading_bridge", "Trade"),
    "Stats": ("paper_trading_bridge", "Stats"),
    "get_bridge": ("paper_trading_bridge", "get_bridge"),
    "execute_trade": ("paper_trading_bridge", "execute_trade"),
    "get_portfolio_value": ("paper_trading_bridge", "get_portfolio_value"),
    "get_positions_summary": ("paper_trading_bridge", "get_positions_summary"),
}

__all__ = sorted(_LAZY.keys()) + ["__version__", "get_system_info", "list_all_tools", "list_all_models"]


def __getattr__(name: str) -> Any:
    """Lazy-load legacy symbols.  Called on first attribute access.

    Anything not in _LAZY raises AttributeError so the caller's
    misspelling surfaces normally instead of triggering an import
    storm.
    """
    target = _LAZY.get(name)
    if target is None:
        raise AttributeError(f"module 'finagent_core' has no attribute {name!r}")
    module_path, attr_name = target
    module = importlib.import_module(f".{module_path}", __name__)
    value = getattr(module, attr_name)
    globals()[name] = value  # cache on the module namespace
    return value


def __dir__() -> list[str]:
    return __all__


# Convenience helpers — these touch the registries lazily, so they
# still require agno when actually invoked, but importing the package
# alone doesn't trigger any cascade.

def get_system_info() -> dict:
    """Information about the finagent_core system."""
    ModelsRegistry = __getattr__("ModelsRegistry")
    ToolsRegistry = __getattr__("ToolsRegistry")
    VectorDBRegistry = __getattr__("VectorDBRegistry")
    EmbedderRegistry = __getattr__("EmbedderRegistry")
    ReasoningModule = __getattr__("ReasoningModule")
    return {
        "version": __version__,
        "model_providers": ModelsRegistry.list_providers(),
        "tool_categories": ToolsRegistry.list_categories(),
        "vectordbs": VectorDBRegistry.list_vectordbs(),
        "embedders": EmbedderRegistry.list_providers(),
        "reasoning_strategies": ReasoningModule.list_strategies(),
    }


def list_all_tools() -> dict:
    """All available tools by category."""
    return __getattr__("ToolsRegistry").list_tools()


def list_all_models() -> dict:
    """All model providers with their available models."""
    ModelsRegistry = __getattr__("ModelsRegistry")
    return {p: ModelsRegistry.get_provider_info(p) for p in ModelsRegistry.list_providers()}

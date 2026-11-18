"""
Agno trading tools.

What is left of the upstream "Agno Trading Agent System" is the one part
anything uses: tools/technical_indicators.py, which Alpha Arena reads for
its agents' technical features (core/features_pipeline.py) and hands to
an Agno agent as tools when an agent card enables them (core/base_agent.py).

This file imports nothing on purpose. It used to import modules that were
never committed (core.team_orchestrator, agents.*), so every
`import agno_trading.<anything>` raised ImportError, and Alpha Arena
silently ran with empty technical features.
"""

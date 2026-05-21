"""Agent evaluation harness.

See plans/testing-strategy.md for the testing approach this package
implements.

- fixture.py: YAML fixture loader + schema
- trace.py: unified trace data structure + fixture-driven assertions
- runtime_adapter.py: runtime selection (local | anthropic) — stubs
  until Tracks 2 and 3 wire the real runtimes
- parity.py: cross-runtime parity comparator
- runner.py: CLI entry point (python -m evals.runner ...)
- conformance/: OpenAI + MCP conformance test skeletons
- e2e/: end-to-end fixtures, one per directory
- tests/: unit tests for the harness itself
"""

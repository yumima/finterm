"""Runtime adapters that drive specific agent backends.

- anthropic_runtime.py: Claude via the claude-agent-sdk (Apache 2.0).
  Pay-per-token Anthropic API; requires the `claude` CLI on PATH.

Future:
- local_runtime.py (Track 2): minimal OpenAI-compatible agent loop
  for the sibling local-AI engine.

Both feed the same evals harness via evals.runtime_adapter.
"""

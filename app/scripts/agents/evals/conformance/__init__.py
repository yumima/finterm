"""Conformance test suites.

- openai/: OpenAI Chat Completions / Embeddings / Audio
  Transcriptions conformance against the local AI engine.
  Populated by Track 2 (local runtime) as the engine matures.
- mcp/: MCP spec conformance against finterm's internal
  McpProvider.  Populated by Track 5 (full MCP spec on internal
  servers).

Each subdirectory starts with a single test_conformance.py skeleton
in Track 0; the suites grow into multiple files (per endpoint /
per capability) as Tracks 2 and 5 land.
"""

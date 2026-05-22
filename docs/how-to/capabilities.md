# AI capability matrix

Generated from [`fincept-qt/resources/ai/capabilities.json`](../../fincept-qt/resources/ai/capabilities.json) by `fincept-qt/scripts/gen_capabilities_doc.py`.  Don't edit this file directly — change the JSON, re-run the generator, commit both.

Glyphs:
- **✓** supported
- **◐** partial
- **…** planned
- **—** unsupported

| Capability | Anthropic | Local | What it does |
|---|---|---|---|
| **Token-by-token streaming** | ✓ | ✓ | Stream response tokens to the chat surface as they arrive. |
| **MCP tool calling** | ✓ | ✓ | Agents call MCP tools (internal int__* + external <server>__*) mid-turn. |
| **MCP resources** | ✓ | ✓ | Typed read-only refs (finterm://portfolio/snapshot etc.) the agent reads without spending a tool call. |
| **MCP prompts** | ✓ | ✓ | Templated prompts surfaced in the slash menu. |
| **MCP sampling primitive** | ✓ | ✓ | Tools request an LLM completion via ctx.sample(). |
| **MCP elicitation primitive** | ✓ | ✓ | Tools ask the user a structured mid-call question. |
| **SKILL.md skill loading** | ✓ | — | Agents load SKILL.md methodology files into context. |
| **Prompt caching** | ✓ | ◐ | Long system prompts cached for input-token savings on subsequent turns. |
| **Persistent memory** | ✓ | ✓ | Agents store + recall durable state across turns / sessions. |
| **Files API (PDF / images)** | — | — | Upload PDFs / images / CSVs by handle, reference across turns. |
| **Per-claim citations** | — | — | Agent output carries source citations alongside text. |
| **Computer use (screen control)** | … | — | Agent controls a virtual desktop / browser. |
| **Sub-agents** | … | — | Coordinator agent spawns child agents with their own context. |
| **Prompt-injection guard** | ✓ | ✓ | External text wrapped with <untrusted> markers; system prompt tells the model not to follow. |
| **Per-tool kill-switch** | ✓ | ✓ | User can disable any tool system-wide; refusal wins over per-agent allowlists. |
| **Audit-grade trace log** | ✓ | ✓ | Every agent dispatch lands in agent_traces with prompts, latency, tokens, cost, status. |
| **Per-agent daily USD cap** | ✓ | ◐ | Budget gate refuses dispatch before any LLM tokens are consumed. |
| **Cron-shaped scheduler** | ✓ | ✓ | @daily / @every entries fire scheduled agent runs. |
| **Slash command dispatch** | ✓ | ✓ | /comps, /dcf, /earnings, … route to named agents with resolved args. |
| **OAuth 2.0 + DCR** | ✓ | ✓ | Hosted MCP servers authenticate via OAuth. client_credentials and authorization_code grants both supported; RFC 8414 discovery, RFC 7591 dynamic client registration, RFC 7636 PKCE on authorization_code. |
| **Inline completion** | ◐ | ◐ | Ghost-text suggestions in the chat composer + memo editors (think IDE autocomplete).  Debounced + LRU-cached; user accepts with Tab, dismisses with any other key. |
| **Text-to-speech** | ◐ | ◐ | Read agent output aloud via a local TTS engine. |

## Notes

### SKILL.md skill loading

- *Local*: Would require an SKILL.md → system-prompt injector on the local side; deferred.

### Prompt caching

- *Local*: Server-dependent (Ollama 0.5+ KV cache, vLLM prefix cache); finterm doesn't introspect cache hits.

### Persistent memory

- *Anthropic*: SDK-native memory tool.
- *Local*: finterm MemoryTools (memory_upsert/search/list/delete) backed by SQLite; sqlite-vec semantic search defers to Engine M1.

### Files API (PDF / images)

- *Anthropic*: Not exposed in claude-agent-sdk 0.2.83; revisit on SDK upgrade.
- *Local*: Not part of OpenAI-compat spec; per-server vision support varies.

### Per-claim citations

- *Anthropic*: Not exposed in claude-agent-sdk 0.2.83.
- *Local*: Not in OpenAI-compat spec.

### Computer use (screen control)

- *Anthropic*: SDK exposes the primitive on Claude 3.5+; finterm doesn't wire the UI surface yet.

### Sub-agents

- *Anthropic*: SDK supports it; finterm's named-agent layer doesn't expose the team primitive yet.

### Per-agent daily USD cap

- *Local*: Budget enforcement works on both, but cost-attribution accuracy depends on the runtime reporting cost_usd back. Local servers usually don't.

### OAuth 2.0 + DCR

- authorization_code flow uses a localhost callback server (default port 47823) + the user's browser; client_id must be set in SecureStorage out-of-band (DCR for browser-flow clients is rare and not auto-wired).

### Inline completion

- Off by default — `inline_completion.enabled = true` in Settings turns it on.  Latency is API-bound until Engine M1 lands a fast local model; the UX is honest about the wait.  No streaming on the suggestion itself (whole completion arrives at once).

### Text-to-speech

- Same TtsService drives either runtime — runtime choice doesn't matter for output.  User installs Piper (`pip install piper-tts`) and downloads a voice .onnx file, then points `tts.model_path` at it via Settings.


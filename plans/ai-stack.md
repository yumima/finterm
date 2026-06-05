# finterm — AI / MCP / Agentic Stack

Single source of truth for finterm's AI architecture. Current
state only; this file isn't a changelog — git history is. Update
this doc whenever the architecture shifts, not when individual
features ship.

---

## 1. Goals + non-goals

**Goal.** Make finterm's AI / MCP / agentic surface *correct,
complete, and elegant in class* against what AI providers
(primarily Anthropic) ship today — leverage what's there, don't
re-invent it.

**Primary user.** Individual investor / retail quant. Bloomberg-
adjacent feature set, individual price. US-market favored;
broker integration is "connect to Fidelity-style external" not
"bundle a clearing stack".

**What we ship.**

- A complete agentic chat surface (slash commands, named agents,
  scheduled runs).
- Two first-class runtimes — local OpenAI-compat and Anthropic
  via the Claude Agent SDK.
- 32+ internal MCP tool families covering market data, portfolio,
  quant lab, paper trading, financial-datasets REST, etc.
- HTTP + stdio MCP transports with a 9-entry marketplace seed.
- Safety + observability: trace log, per-tool kill-switch,
  prompt-injection guard, per-agent USD budget.
- Voice (local Whisper default; Deepgram opt-in).

**What we don't ship.**

- A new model. We integrate, we don't train.
- A new SaaS. The Anthropic profile pays per-token to Anthropic;
  the local profile talks to the user's own machine. finterm has
  no backend.
- Multi-tenant or enterprise admin. One user per install.
- Bundled clearing. Trading is paper today; broker connect is
  "external link" not "we route orders".

---

## 2. Architecture overview

```
┌──────────────────────────────────────────────────────────────────────┐
│   finterm (Qt6 / C++)                                                │
│                                                                       │
│   ┌──────────────┐   ┌────────────────────────────────────────────┐  │
│   │  AI Chat     │   │  Settings → AI System + Scheduler           │  │
│   │  (composer + │   │  Settings → LLM Config + MCP Servers + …    │  │
│   │   slash      │   │  AI Workbench (⬡ WORK toolbar button)       │  │
│   │   dispatch)  │   └────────────────────────────────────────────┘  │
│   └──────┬───────┘                                                    │
│          │  run_agent(query, config)                                  │
│   ┌──────▼─────────────────────────────────────────────────────────┐ │
│   │  AgentService                                                  │ │
│   │    • budget check (BudgetService)                              │ │
│   │    • trace create  (AgentTraceRepository)                      │ │
│   │    • runtime dispatch → Python via PythonRunner                │ │
│   │    • trace finish on result                                    │ │
│   └──────┬─────────────────────────────────────────────────────────┘ │
│          │                                                            │
│   ┌──────▼──────┐  ┌──────────────┐  ┌─────────────────────┐         │
│   │ McpService  │  │ AgentScheduler│  │ SlashCommandService │         │
│   │ (catalog +  │  │ (cron tick    │  │ (/comps AAPL →      │         │
│   │  dispatch + │  │  agent_       │  │  agent + skill +    │         │
│   │  killswitch)│  │  schedule)    │  │  args)              │         │
│   └─────────────┘  └──────────────┘  └─────────────────────┘         │
└──────────────┬─────────────────────────────────────────────────────┘
               │  IPC (stdin/stdout JSON)
┌──────────────▼─────────────────────────────────────────────────────┐
│   Python — finagent_core / alpha_arena                              │
│                                                                       │
│   finagent_core.runtimes.anthropic_runtime  — claude-agent-sdk      │
│   finagent_core.runtimes.local_runtime      — OpenAI-compat (urllib)│
│   finagent_core.runtimes.mcp_bridge         — McpService → SDK MCP  │
│   finagent_core.runtimes.skills_loader      — SKILL.md discovery    │
│   finagent_core.runtimes.stream_handler     — typed event sink      │
│                                                                       │
│   alpha_arena.core.BaseAgent — auto-routes to one of the two        │
│       runtimes per `provider` field; agno path is opt-in legacy.    │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.1 Two-runtime split

Every agent dispatch picks one runtime:

| Runtime | When | What |
|---|---|---|
| **anthropic** | `provider == "anthropic"` | claude-agent-sdk (Apache-2.0) wrapping the `claude` CLI. Native MCP, native skills, server-side tool use, prompt caching, optional thinking. |
| **local** | `provider in {openai, ollama, groq, gemini, deepseek, openrouter}` | Plain OpenAI-compat chat completions via `urllib`. Default base URL `http://localhost:11434/v1` (Ollama); env overrides via `FINCEPT_LOCAL_{BASE_URL,MODEL,API_KEY}`. |
| **agno** *(opt-in legacy)* | `advanced_config["runtime"] = "agno"` explicit | Pre-Track-12 path. Exists as an escape hatch for alpha-arena agents that haven't migrated. New code should not target this. |

No silent failover. When the active runtime is down the chat
surface raises a clear error and links to the relevant docs —
the user picks the next move, not the framework.

### 2.2 Profile routing

`LlmProfileRepository` resolves a `(role, entity)` query →
runtime + model + settings. Roles: `ai_chat`, `agent`,
`agent_default`, `team`, `team_default`, `team_coordinator`.
Resolution: entity-specific → type default → global default.

A profile points at either `runtime: local` (with
`local_llm_base_url`, model name, params) or `runtime: anthropic`
(with API key reference in SecureStorage, model name, params).

---

## 3. MCP layer

### 3.1 Internal tools (32 families)

Internal tools live in `fincept-qt/src/mcp/tools/*` and register
through `McpInit::initialize_all_tools` into `McpProvider`. Wire-
form name is `int__<tool_name>`. Coverage today:

- **Market data.** MarketsTools, MarketDataTools, ExchangeTools,
  EquityResearchTools.
- **News + corporate.** NewsTools, EdgarTools, FinancialDatasetsTools.
- **Portfolio.** PortfolioTools, WatchlistTools.
- **Trading.** PaperTradingTools, CryptoTradingTools (destructive
  ops gated via MCP elicitation).
- **Quant.** AiQuantLabTools (factor models / backtests / attribution),
  QuantNarratorTools (LLM commentary).
- **Memory.** MemoryTools (Track 9 stub: SQLite keyword search;
  sqlite-vec upgrade is a follow-up. Anthropic profile uses the
  SDK's native memory tool instead).
- **Agents framework.** AgentsTools (CRUD), AgenticMemoryTools,
  AiChatTools, DataHubTools, DataSourcesTools, FileManagerTools.
- **Macro + geo.** GovDataTools, GeopoliticsTools, MaritimeTools,
  GeoTools, MAAnalyticsTools, DBnomicsTools, SecurityAuditTools.
- **Workflow.** WorkflowsTools, NavigationTools, MetaTools,
  SurfaceAnalyticsTools.
- **Other.** AltInvestmentsTools, ForumTools, KnowledgeTools.

Resources (read-only refs without spending a tool call):
`finterm://portfolio/snapshot`, `finterm://watchlist/all`,
`finterm://news/digest`, `finterm://notes/active_thesis`.

Prompts (templated, surface via SDK's slash menu):
`daily_brief`.

ToolContext primitives:
`on_progress`, `is_cancelled`, `on_log`, `on_sample`,
`on_elicit`.

### 3.2 External servers

Two transports:

- **stdio** — `McpClient` (existing). Used by the marketplace
  seed list and `financial-datasets/mcp-server`.
- **HTTP** — `McpHttpClient`. Static bearer / API-key headers
  via `Authorization` or a configurable header. OAuth 2.0 + DCR
  is reserved as `auth_scheme="oauth"` and rejected by
  `McpHttpClient` until implemented.

Marketplace seed (9 servers, all `enabled=0` by default):

| Server | Transport | Key |
|---|---|---|
| `mcp-server-fetch` | stdio | — |
| `mcp-server-filesystem` | stdio | — |
| `mcp-server-sqlite` | stdio | — |
| `mcp-server-time` | stdio | — |
| `mcp-server-sequentialthinking` | stdio | — |
| `mcp-server-brave-search` | stdio | Brave key |
| `mcp-server-playwright` | stdio | — |
| `yfinance-mcp` | stdio | — |
| `financial-datasets/mcp-server` | stdio | Free-tier key |

### 3.3 Namespacing + scoping

Wire-form: `<server_id>__<tool_name>`. Internal server id is
`int`. Per-agent `allow_tools` glob patterns filter
`list_tools_for(agent_id)` *before* either runtime sees the
catalogue. The system-wide kill-switch (`tool_killswitch` table)
wins over every per-agent allow.

Tool-result caching: `McpService` retains a 5s TTL cache keyed
by call-hash. Both runtimes hit the cache transparently;
per-call cache-control can disable it for non-idempotent reads.

---

## 4. Agents + skills

### 4.1 Named agents

Ten identities seeded in v028. Each row in `agent_configs`
carries (system_prompt, skills[], allow_tools[]). All
allow_tools globs are read-side only — no agent gets destructive
surfaces.

| Agent | Skills | Notes |
|---|---|---|
| `pitch_agent` | comps-analysis, dcf-model, ic-memo, idea-generation, initiating-coverage | Long/short pitch builder |
| `meeting_prep` | morning-note, client-review, ic-memo | Client / IC / management prep |
| `market_researcher` | sector-overview, idea-generation, thesis-tracker, catalyst-calendar | Theme + sector research |
| `earnings_reviewer` | earnings-analysis, earnings-preview, morning-note | KPI walk, guide-change |
| `model_builder` | 3-statement-model, dcf-model, lbo-model, model-update | Builds models |
| `valuation_reviewer` | comps-analysis, dcf-model, model-update | Critique-as-a-service |
| `gl_reconciler` | portfolio-monitoring | Custody-vs-GL break detector |
| `month_end_closer` | portfolio-monitoring, portfolio-rebalance | NAV + sign-off pack |
| `statement_auditor` | earnings-analysis | 10-K/10-Q audit |
| `kyc_screener` | — | Counterparty screening |
| `quant_critic` | earnings-analysis, model-update | Track 11 — backtest narrator |

Every agent's `system_prompt` is prefixed (v031) with a
directive teaching the model to treat `<untrusted>…</untrusted>`
content as data, not instructions (Track 14 #40 / R23).

### 4.2 Skills library

`fincept-qt/scripts/agents/finagent_core/skills/<vertical>/<name>/SKILL.md`.
Anthropic SKILL.md grammar verbatim — no fork.

Three shipped today (finterm-original methodology, not upstream-
vendored):
- `equity-research/morning-note/SKILL.md`
- `equity-research/earnings-analysis/SKILL.md`
- `investment-banking/comps-analysis/SKILL.md`

Each declares a `data-source-priority` block listing internal
tools first (`int__fd_*`, `int__get_news*`, `int__get_filing*`,
`int__get_quote*`, …) and external MCPs as fall-through. When
upstream `anthropics/financial-services` is vendored, the same
pattern applies — see `skills/README.md` for the vendoring
checklist.

`skills_loader` discovers SKILL.md files at runtime and hands
the available names to the Anthropic SDK via
`ClaudeAgentOptions(skills=[…])`.

### 4.3 Slash command dispatch

`SlashCommandService` resolves `/comps AAPL --period=quarterly`
to `(agent_id, skill, args)`. In-code registry of 13 commands
mirroring §7.3 of the original design doc.

| Command | Agent | Skill |
|---|---|---|
| `/comps <ticker>` | pitch_agent | comps-analysis |
| `/dcf <ticker>` | model_builder | dcf-model |
| `/lbo <ticker>` | model_builder | lbo-model |
| `/earnings <ticker> <quarter>` | earnings_reviewer | earnings-analysis |
| `/earnings-preview <ticker>` | earnings_reviewer | earnings-preview |
| `/initiate <ticker>` | pitch_agent | initiating-coverage |
| `/sector <sector>` | market_researcher | sector-overview |
| `/ic-memo <ticker>` | pitch_agent | ic-memo |
| `/morning-note` | market_researcher | morning-note |
| `/catalysts <ticker>` | market_researcher | catalyst-calendar |
| `/thesis <ticker>` | market_researcher | thesis-tracker |
| `/rebalance` | month_end_closer | portfolio-rebalance |
| `/client-review` | meeting_prep | client-review |

AiChatScreen intercepts `/`-prefixed input and dispatches
through `AgentService::run_agent(query, config)`. Slash
exchanges persist to `ChatRepository` (survive session reload)
but stay out of LLM history — they're a parallel dispatch
channel.

`/help` is built-in; lists every registered command.

### 4.4 Scheduler

`AgentScheduler` ticks once a minute and dispatches every
`agent_schedule` entry whose cron condition is met.

Grammar (`parse_cron` is a pure function):

- `@daily HH:MM` — anacron-style: fires when local time is
  past HH:MM and we haven't already fired today. Catches the
  morning-brief case when the app was off at the scheduled
  minute.
- `@every Nm` — every N minutes (1..1440).
- `@every Nh` — every N hours (1..168).

CRUD UI lives in **Settings → Scheduler** *and* **Workbench →
System** (same widget instantiated in both surfaces). Run-now
forces an immediate tick.

---

## 5. Safety + observability

| Concern | Mechanism |
|---|---|
| **Audit trail** | Every `AgentService::run_agent` writes one `agent_traces` row at dispatch + finishes it at the result callback. Captures runtime, source, query, latency, tokens (when reported), cost (when reported), status, error. |
| **Per-tool kill-switch** | `tool_killswitch` table; `McpService::execute_tool` refuses dispatch for any disabled wire-form name. Fails open on SQL error to avoid blocking everything on a transient hiccup. UI under **Settings → AI System** and **Workbench → System**. |
| **Prompt-injection guard** | `mcp::UntrustedContent::wrap()` brackets externally-sourced text with `<untrusted>…</untrusted>` markers and entity-encodes `<`/`>` so attackers can't close the tag early. Every agent's system prompt carries a v031 directive instructing the model to treat marked content as data. Applied to NewsTools (headline + summary) and ForumTools (post / comment / profile fields). |
| **Per-agent budget** | `BudgetService::check_dispatch(agent_id)` runs ahead of every dispatch. Reads `agent_configs.config_json.budget.max_usd_per_day`; sums `cost_usd` from same-day traces; refuses dispatch when the cap is hit. Missing / non-positive cap = enforcement off. |
| **Auth gate** | Destructive tools (`paper.place_order`, settings mutators, …) declare `is_destructive = true`. `McpProvider::call_tool` routes through `ctx.elicit` then falls back to the registered `AuthChecker`. |
| **Eval result store** | Append-only JSON Lines at `~/.fincept/eval_runs.jsonl` (override via `FINCEPT_EVAL_RESULTS`). `result_store.record_run()` wired into the runner's run / run-all commands. Survives the trace-schema evolution window without migrations. |

### 5.1 Acceptance checklist

`fincept-qt/tests/ui/acceptance_checklist.md` covers:

- **Flavour A** — Local-only: A1 probe, A2 chat (no outbound),
  A3 banner on server-down (no silent failover), A4 `/comps`
  end-to-end.
- **Flavour B** — Anthropic: B1 key + profile, B2 chat (no
  localhost probe), B3 auth-error surface (no silent failover),
  B4 `/comps` with citations, B5 cache-hit visible.
- **Always-on** — C1 marketplace install, C2 financial-datasets
  install, C3 voice (Whisper default), C4 named agent ‹/comps›,
  C5 scheduled morning note, C6 kill-switch + elicitation
  refusal, C7 trace detail view.
- **Safety spot-checks** — S1 dispatch lands in `agent_traces`,
  S2 budget refusal pre-python, S3 prompt-injection ignored, S4
  kill-switch refusal surfaces as ToolResult::fail.

Re-run on every release that touches `services/agents/`, `mcp/`,
`ai_chat/`, or `screens/settings/`.

---

## 6. UI surface

### 6.1 AI Workbench

Single screen with left nav (Track 13 spec). Reachable via the
"⬡ WORK" toolbar button. Mutually exclusive with "⬡ CHAT" mode —
entering one clears the other's flag.

Sections:

- **Chat** — placeholder pointing to the dedicated Chat screen.
- **Agents** — placeholder pointing to Settings → Profiles (the
  full editor with allow_tools globs etc.).
- **Teams** — placeholder ("coming soon"; Anthropic sub-agent
  + local planner are the primitives).
- **Workflows** — placeholder pointing to the Workflows screen.
- **Tools** — placeholder pointing to Settings → MCP Servers
  → Tools tab (catalog).
- **Servers** — placeholder pointing to Settings → MCP Servers.
- **Profiles** — placeholder pointing to Settings → LLM Config.
- **System** — fully populated: embeds `AiSystemSection`
  (traces / spend / kill-switch) above `SchedulerSection`
  (agent_schedule CRUD).

Embedding the full chat_mode / agent_config / mcp_servers
widgets into Workbench panels is the natural follow-up — done
one screen at a time so each port's lifecycle assumptions stay
under test.

### 6.2 Settings sections

- **LLM Config** — provider dropdown (anthropic + ollama only;
  others as dormant adapters), per-profile model + base URL.
- **MCP Servers** — marketplace list, per-server transport
  config, tool catalog browser.
- **AI System** — copies of traces / spend / kill-switch (shares
  widget with Workbench → System).
- **Scheduler** — copies of agent_schedule CRUD (shares widget
  with Workbench → System).
- **Voice** — Whisper (default) + Deepgram (opt-in). Google SR
  removed.

### 6.3 Chat composer

Standard chat with three additions:

- `/`-prefix detected pre-dispatch; resolves via
  `SlashCommandService` and routes through `AgentService` instead
  of the LLM.
- `/help` is built-in.
- Slash exchanges persist to `ChatRepository` but stay out of
  LLM conversation history.

---

## 7. Storage

### 7.1 SQLite schema (selected tables)

| Table | Purpose | Migration |
|---|---|---|
| `llm_configs` | Per-provider config rows (keys via SecureStorage) | v022 |
| `llm_profiles` | (role, entity) → runtime + model + settings | v023 |
| `mcp_servers` | External MCP server registry | v024, v025 |
| `agent_configs` | Named agent identities (system_prompt + skills + allow_tools) | v003, v026, v028, v031 |
| `agent_schedule` | Cron entries for AgentScheduler | v027 |
| `agent_traces` | One row per `run_agent` dispatch (Track 14 #38) | v029 |
| `tool_killswitch` | Wire-form names disabled by the user (Track 14 #39) | v030 |
| `memory_entries` | MemoryTools backing store (scope, key, content, metadata) | v032 |

All repositories go through `Database::execute(sql, params)` —
the mutex-guarded path. `raw_db()` is reserved for migration
code that runs single-threaded at startup.

### 7.2 SecureStorage

- DPAPI (Windows) / Keychain (macOS) / libsecret (Linux) /
  XOR-key fallback.
- API keys live here under `mcp.auth.<server_id>` (MCP HTTP),
  `mcp.tools.<tool_set>` (per-tool keys e.g.
  `mcp.tools.financial-datasets`), and `llm.configs.<provider>`
  / `llm.models.<id>` / `llm.profiles.<id>` for LLM credentials.
- `LlmSecureKeys` defines the naming convention; v022 NULLed
  plaintext columns so every read goes through SecureStorage.

### 7.3 Eval result store

`~/.fincept/eval_runs.jsonl` (override via
`FINCEPT_EVAL_RESULTS`). One JSON line per `(fixture × runtime)`
execution: started_at, runtime, fixture_path, status,
failed_assertions, trace summary (turn_id, model, iterations,
latency_ms, output_preview, tool_call_count).

JSON Lines (not SQLite) while the trace schema is still in
flux. Moves to SQLite when the Workbench needs indexed queries.

---

## 8. Voice

- **STT default:** local Whisper via
  `scripts/voice/whisper_stt.py` (`faster-whisper`, model
  `medium`; `large-v3` opt-in).
- **STT optional:** Deepgram (paid, opt-in).
- **Wake trigger:** `clap_detector.py`.
- **TTS:** gap. Anthropic ships no TTS; a future track may add
  local TTS via Piper / Kokoro.

Google SR removed. `SpeechService` uses `WhisperSttProvider`.

---

## 9. Track status

State of every track in the original design. Each row is the
current truth; older history lives in `git log`.

| # | Track | State | Notes |
|---|---|---|---|
| 0 | Testing scaffold | ✅ | `scripts/agents/evals/` |
| 1 | Foundation cleanup | ✅ | KNOWN_PROVIDERS trimmed; Google SR removed |
| 1A-C | Framing + capabilities | ✅ | Capability matrix (JSON + generator + `CapabilityRegistry`); README four-pillar value prop; nightly SDK eval + vendor-skill diff CI |
| 2 | Local runtime | ✅ | `run_text` + `run_with_tools` + SSE streaming (`run_text_stream`, `run_with_tools_stream`) + `probe` |
| 3 | Anthropic runtime | ✅ | `anthropic_runtime.run_turn` + `run_text` |
| 3A-B | Composer + entity refs | ✅ | Runtime toggle pill on chat composer; `@`-mention resolver (resources + tickers); session-reload chip restore |
| 4 | MCP HTTP transport + marketplace | ✅ | OAuth 2.0 + DCR — both `client_credentials` and `authorization_code` (PKCE + RFC 8414 discovery + auto-401 retry); server-initiated request dispatch end-to-end |
| 5 | Full MCP spec + artefacts | ✅ | resources / prompts / sampling / elicitation / progress / logging / auth; `chat_artefacts` foundation + `emit_artefact` tool + Artefacts panel + Lineage view (explicit `supersedes_id` chain, v039) + suggested follow-ups |
| 6 | `FinancialDatasetsTools` | ✅ | 6 REST tools |
| 6A-C | Voice + Forum + Workbench port | ✅ | Local Piper TTS (incl. Settings → Voice TTS + chat 🔊 button); user-configurable forum URL; Workbench tools/chat/agents/workflows/servers/profiles/teams panels all real |
| 7 | Skills + slash + agent identities | ✅ | 10 agents (v028) + slash service + 3 starter SKILL.md files |
| 7A-C | Task tray + feedback loop | ✅ | In-flight + recent task tray; mark-wrong / mark-right capture; SKILL.md propose-fix flow (Track 7C) with type-to-confirm overwrite |
| 8 | Per-agent tool scoping + namespacing | ✅ | `int__` prefix + `allow_tools` glob patterns |
| 8 (inline) | Inline completion scaffold | ✅ scaffold | `InlineCompletionController` capability-gated; Settings checkbox + LRU cache + slash-prefix guard.  Latency API-bound until Engine M1 lands a fast local model |
| 9 | Memory + vector store | ✅ | `MemoryTools` semantic search via local-engine embeddings (`EmbeddingsClient` → hearth `/v1/embeddings`) + float32 BLOB column (v040) + C++ cosine KNN, keyword LIKE fallback when the engine is down or nothing clears the relevance floor. sqlite-vec deliberately declined — a loadable extension fights cross-platform packaging (Qt's bundled SQLite often omits load-extension on Win/macOS; per-OS signed .so/.dylib/.dll). BLOB+cosine is ample at terminal scale and forward-compatible: a `vec0` table can layer over the same BLOBs if scale ever demands it. |
| 10 | Scheduler + hooks | ⚠ partial | Core + UI ✅; SDK hook registration on Anthropic profile defers |
| 11 | Quant narrator | ✅ | `QuantNarratorTools` + `quant_critic` agent |
| 12 | Alpha-arena migration | ✅ | Default flipped to two-runtime path; agno stays as opt-in legacy |
| 13 | AI Workbench | ✅ | WorkbenchScreen scaffold + every panel populated (real catalog tables for Chat/Agents/Workflows/Tools/Servers/Profiles + full Teams create/edit/delete + System) |
| 14 | Evals + obs + audit + safety + UX | ✅ | #37 result store, #38 traces (incl. v036 tool-call timeline), #39 kill-switch, #40 prompt-injection, #41 budgets (incl. per-agent USD cap), #42 acceptance checklist |
| 15 | Community surface | ⚠ partial | Forum URL configurable; Reddit RSS / Discord deep-link still off the critical path |
| 96 | Tool-call timeline | ✅ | v036 `agent_traces.tool_calls_json`; renders in trace drill-down for non-streaming + team paths (streaming proper requires StreamingCoreAgent to expose RunOutput — deferred) |
| 98 | Multi-agent teams | ✅ | v037 schema + v038 mode rename; `TeamRepository`; Workbench Teams panel with full CRUD + agno mode dropdown (coordinate / route / collaborate); `/team <name> <query>` slash dispatch with response delivery via filtered `agent_stream_done` |
| 100 | Vendor-skill drift review | ✅ | `SkillDiffsSection` — runs `vendor_skills_diff.py` async, displays drift inbox, atomic overwrite with path-traversal guards |

---

## 10. Open work

Categorised by why each item isn't done yet.  Anything that says
"BLOCKED ON" cannot be finished from this repo alone.

### Blocked on Engine M1 (sibling repo)

- **Engine M1 — `local-ai-engine` packaging.** Curated bundle of
  Ollama / vLLM / faster-whisper / sqlite-vec behind one
  OpenAI-compatible service.  finterm's local-runtime adapter
  already works against any OpenAI-compat server today; this is
  the opinionated install path.  See `plans/local-ai-engine.md`.
- ~~**sqlite-vec semantic search in MemoryTools.**~~ ✅ Done a
  different way (v040): `memory_search` now embeds via the local
  engine (`EmbeddingsClient` → hearth `/v1/embeddings`) and does
  C++ cosine over a float32 BLOB column, with a keyword fallback.
  sqlite-vec was declined for cross-platform reasons (see Track 9);
  the BLOB column stays forward-compatible with a future `vec0`
  table. No longer blocked on the engine shipping an extension.
- **Fast-local inline completion.** Scaffold shipped (Track 8 /
  Settings → AI System checkbox); the controller works against
  any configured runtime.  Quality story (sub-300ms ghost text)
  needs Engine M1's fast local model — over Anthropic/OpenAI
  round-trips the UX feels slow, which is honest behaviour but
  not the headline feel.

### Blocked on Anthropic SDK

- **Files API + per-claim citations.** `claude-agent-sdk` 0.2.83
  doesn't expose either; revisit on SDK upgrade.
- **In-flight token budget (abort mid-stream).** The C++ gate
  ("refuse if already overspent today") is in place.  Aborting a
  call mid-response needs runtime-side enforcement inside the SDK
  + local runtime — separate work.
- **Anthropic-runtime tool-call timeline.** Track 96 surfaces
  tool calls from agno's `RunOutput.tools` (local runtime); the
  SDK's tool loop isn't exposed equivalently.  Trace dialog shows
  the table for local/agno turns and falls back to "(no tool
  log)" for Anthropic-direct dispatches.

### Workable in finterm — scoped follow-ups

- **StreamingCoreAgent tool-log surfacing.**  The non-streaming
  `run` path attaches `RunOutput.tools` to the result via
  `_attach_tool_calls()` (Track 96).  The streaming path yields
  chunks rather than returning a RunOutput; surfacing tool calls
  there would need StreamingCoreAgent to expose the final
  RunOutput at end-of-stream.  Today the streaming fallback
  (when StreamingCoreAgent itself errors) does get the tool log
  via `core_agent.run()`'s response.
- **Per-artefact `supersedes_id` for multi-artefact turns.**
  Today `latest_for_request` links only the most-recent artefact
  back to the predecessor.  An agent that emits N artefacts in
  one turn leaves N-1 unlinked.  Proper per-artefact tracking
  needs the `emit_artefact` tool to accept an optional
  `predecessor_id` arg that the dispatch config seeds.
- **LlmService `RequestConfig` refactor.**  Documented as
  accepted design choice for now (band-aid mutex held across
  full request).  Revisit if a future surface starts firing
  multiple concurrent chats routinely.
- **Reddit RSS / Discord deep-link forum surfaces.**  Forum URL
  config shipped (Track 6B); the rich client surfaces are still
  filed.  Off the AI critical path.

---

## 11. References

External libraries / docs that this stack leans on:

- **claude-agent-sdk (Python, Apache 2.0)** — Anthropic's
  agentic loop. Wraps the `claude` CLI.
- **agno** — legacy agent framework (alpha_arena's pre-Track-12
  path).
- **MCP** — Model Context Protocol; tools / resources / prompts
  / sampling / elicitation / progress / cancellation / logging.
- **financialdatasets.ai** — REST source for fundamentals,
  filings, insider trades, institutional holdings (free tier).
- **anthropics/financial-services** — open methodology layer
  (Apache 2.0). Skills + slash commands + agent identities;
  finterm vendors a starter subset.
- **faster-whisper + webrtcvad** — local STT.
- **Qt6 6.8** — Qt Multimedia FFmpeg backend, ADS docking.

Companion docs in this repo:

- `plans/local-ai-engine.md` — sibling project's vision /
  requirements (separate repo concern).
- `plans/testing-strategy.md` — three-layer test plan (unit +
  integration + UI).
- `fincept-qt/tests/ui/acceptance_checklist.md` — manual
  sign-off list for AI releases.

# AI Stack — Design and Plan

**Status:** Current
**Date:** 2026-05-19
**Owner:** yumima
**Canonical tree:** `/home/yma/fin/finterm/fincept-qt/`
**Related:** `docs/datahub-phases/phase-09-ai-mcp-agents.md`, `docs/agents/datahub-guide.md`

---

## 1. Goal

Two first-class paths, picked per-user via the existing profile system:

- **Local path** — point at a sibling LLM-services project running on
  `localhost`, OpenAI-compatible. Zero outbound calls, zero commercial
  keys.
- **External path** — paste any commercial or free-tier provider key
  (OpenAI, Anthropic, Gemini, Groq, Cerebras, Mistral, OpenRouter,
  DeepSeek, etc.). The existing multi-provider `LlmService` already
  handles all of these; UX makes the choice obvious.

Neither path is the default architecturally. On a brand-new install
with *nothing* configured, the local path is suggested because it
needs no keys; users override by pasting any external key.

Three install flavours work as peers: local-only, external-only, mixed
(profile system routes per role).

### Non-goals

- Replacing the multi-provider `LlmService` — it already handles both
  local and external; we configure it, not rewrite it.
- Forcing users onto one path or the other; finterm must not silently
  failover between local and external in either direction.
- Self-hosting embeddings model training or fine-tuning.
- Installing, supervising, or updating local LLM runtimes (Ollama,
  vLLM, llama.cpp) — that lives in a sibling project.
- Joining Anthropic's hosted Managed Agents API (`/v1/agents`) — two
  paths, not three.
- Building a new MCP server *inside* finterm to serve finterm to
  others. Internal tools live in `McpProvider` and stay there.
- Replacing QuantLib / Qlib / VectorBT / Agno math engines — AI sits
  next to them, not under them.

---

## 2. Current state

### 2.1 LLM layer — `src/ai_chat/`

- `LlmService` is a singleton multi-provider client over
  `QNetworkAccessManager`, with SSE streaming and OpenAI-style tool
  calling. Eleven providers: OpenAI, Anthropic, Gemini, Groq, DeepSeek,
  Kimi, MiniMax, xAI, OpenRouter, Ollama, Fincept (hosted, see 2.6).
- Tool-calling is on by default; per-request disable for surfaces like
  the floating chat bubble where unintended navigation is hostile.
  Follow-up loop handles OpenAI-shaped tool calls plus a text-parse
  fallback for providers/models that emit malformed JSON.
- `ModelCatalog` caps per-model output tokens — 40+ models across 12
  providers, last verified 2026-04-28; defaults to 16k when
  unpublished.

### 2.2 Profiles — `src/storage/repositories/`

- `LlmProfileRepository` stores named portable configs and resolves
  them per context: `ai_chat`, `agent`, `agent_default`, `team`,
  `team_default`, `team_coordinator`. Resolution order is
  entity-specific → type default → global default → legacy provider.
- `LlmConfigRepository` is the legacy single-active-provider store,
  superseded but still read.

### 2.3 MCP layer — `src/mcp/`

- `McpClient` is **stdio-only** JSON-RPC 2.0; per-server subprocess,
  worker thread, 30s default timeout, last-500-line log capture.
- `McpProvider` is the **internal** tool registry (in-process, no
  subprocess). Sync + async handlers, auth hook for destructive ops,
  per-tool enable/disable.
- `McpManager` runs lifecycle for external servers: health checks,
  auto-start, exponential-backoff restart (max 3).
- `McpService` is the unified entry point; merges internal + external
  tools with a 5s tool-cache TTL and formats them for OpenAI function
  calling. Everything in finterm calls `McpService`, not the layers
  below it.
- `McpInit.cpp` registers **31 internal tool families** across
  navigation, markets, watchlist, portfolio, AI chat, Python, system,
  settings, Edgar, M&A, alt investments, data sources, forum,
  geopolitics, gov data, DBnomics, equity research, crypto trading,
  paper trading, agentic memory, meta, quant lab, surface analytics,
  file manager, report builder, notes. 38 .cpp tool modules under
  `src/mcp/tools/` (count higher than family count because some
  families split across partials, e.g. `AgentsTools_Discovery`,
  `AgentsTools_Execution`, `AgentsTools_Repos`).

### 2.4 Agents — `src/services/agents/` + `scripts/agents/`

Three independent agent runtimes coexist today. **This is the single
biggest source of complexity in the AI surface.**

- `AgentService` (C++ singleton) delegates to Python through two
  transports: `PythonRunner` for lightweight calls, a custom
  `QProcess` + stdin for large payloads. It re-emits results as Qt
  signals (`stream_token`, `stream_thinking`, `stream_done`).
- `scripts/agents/finagent_core/main.py` is the actual dispatcher:
  `agent_loader.py` discovers, `agent_factory.py` instantiates,
  `core_agent.py` runs the tool-calling loop, `execution_planner.py`
  does plan-before-execute, `paper_trading_bridge.py` exposes the
  paper-trade surface.
- `scripts/agents/deepagents/` is a LangGraph-style runtime with its
  own provider integration (Anthropic, OpenAI, Groq, DeepSeek). Tool
  calling native on the first three; rest fall back to prompt-loop
  text parsing.
- `scripts/agents/rdagents/` is **autonomous research** —
  FactorRDLoop / ModelRDLoop / QuantRDLoop run LLM-driven hypothesis
  generation, accessed through `AIQuantLabService::rd_agent_*` and
  exposed to other agents via `mcp_server.py` + `mcp_tools.py`.
- `services/alpha_arena/` is **fully wired** for LLM trading — Agno
  framework, OpenAI + Anthropic agents, scored leaderboard.
- `scripts/agents/GeopoliticsAgents/`, `TraderInvestorsAgent/`,
  `hedgeFundAgents/renaissance*` are config trees and execution
  shims; no first-class agent loop ties them in.

### 2.5 STT — `src/services/stt/` + `scripts/voice/`

- Strategy pattern with two backends: Google SR (default, outbound,
  scheduled for removal) and Deepgram (paid, opt-in via
  `deepgram_stt.py`). `clap_detector.py` is the trigger.
- Wired into the floating `AiChatBubble` mic button and the chat-mode
  composer.

### 2.6 UI surfaces — `src/screens/`

Five distinct screens plus a floating bubble:

| Screen | Path | What the user sees |
|---|---|---|
| **Chat Mode** | `chat_mode/` | Multi-session AI chat. `ChatSessionPanel` list, `ChatMessagePanel` render, `ChatAgentPanel` for picking an agent to drive the turn, `TerminalToolBridge` routes tool calls through `McpService`. Streams tokens + tool-call cards. |
| **Agent Config** | `agent_config/` | Browse/build agents and teams. `AgentsViewPanel`, `CreateAgentPanel`, `AgentChatPanel`, `ToolsViewPanel`, `TeamsViewPanel`, `WorkflowsViewPanel`, `PlannerViewPanel`, `SystemViewPanel`. |
| **MCP Servers** | `mcp_servers/` | Add / start / stop / remove external MCP servers, view logs, health status. Backed by `mcp_servers` SQLite table. |
| **Settings → LLM Config** | `settings/LlmConfigSection.*` | Two tabs. **Providers** — keys, model picker, fetch list, test, globals (temperature, max-tokens, system prompt, tools-enabled). **Profiles** — named profiles bound to context types. |
| **AI Quant Lab** | `ai_quant_lab/AIQuantLabScreen.*` | 24-module grid launcher. RD-Agent appears as a module (not as a chat surface). |
| **Floating chat bubble** | `AiChatBubble` (overlay) | Quick-ask anywhere; mic button triggers STT; tool calling deliberately disabled here. |

### 2.7 Key storage, outbound endpoints

- LLM keys are currently in the plaintext `llm_configs.api_key`
  column. Schema is already on `SecureStorage` for Databento and
  Deepgram keys — LLM keys are the holdout.
- `SecureStorage` backs onto Windows DPAPI, macOS Keychain,
  libsecret on Linux; degrades to XOR-obfuscated QSettings when
  libsecret is absent (file header is honest about this).
- A fresh install with no key configured falls back to the **Fincept
  hosted endpoint** at `fincept.in/research/llm/async`. Until Track 1
  cleanup lands, finterm makes an outbound call to a non-user
  endpoint when a user asks anything before configuring a key.

### 2.8 Data in the agent's reach

| Category | Outside today | In-box today |
|---|---|---|
| LLM inference | 10 external providers + `fincept.in` | Ollama (localhost via existing provider entry) |
| Embeddings | Provider-side (Gemini free, OpenAI, Anthropic) | None natively — `vectordb_registry.py` exists, unconfigured |
| STT | Google SR (outbound), Deepgram (paid) | None — Whisper local is the plan |
| TTS | None | None |
| MCP servers | Anything stdio the user installs | The 31 internal families |
| Agent reasoning | Provider LLMs through `finagent_core` / `deepagents` | RD-Agent loop bodies live local, each step calls a provider LLM |
| Tool-call data | Per-tool: Yahoo, Alpha Vantage, FMP, CoinGecko, etc. | Edgar, fiscaldata, EIA, CFTC, akshare (CN), DBnomics, ACLED, etc. |

### 2.9 Quant subsystems — AI status per service

| Subsystem | AI today | UI surface |
|---|---|---|
| `services/ai_quant_lab/` | **Partial.** 24 modules; only the RD-Agent / Deep-Agent module (`agents/rdagents/`) is LLM-backed. 23 others are pure Qlib / RL / statsmodels. | `AIQuantLabScreen` — 24-module grid, no chat. |
| `services/algo_trading/` | **None.** Rule-based indicator engine + scanner + live runner. Backtests return JSON; no narration. | `AlgoTradingScreen` — manual condition builder. |
| `services/alpha_arena/` | **Fully wired.** Agno-backed LLM agents (OpenAI + Anthropic) per-cycle buy/sell decisions; leaderboard scoring. | `AlphaArenaScreen` — model selector, cycle runner, leaderboard. |
| `services/backtesting/` | **None.** Six providers (VectorBT, Backtesting.py, FastTrade, Zipline, BT, Fincept). Pure metrics out. | `BacktestingScreen` — provider/strategy picker, results table. |
| `services/quantlib/` | **None.** 590-endpoint REST client against a localhost QuantLib server. Pricing, Greeks, VaR, vol surface — all numerical. | `QuantLibScreen` — endpoint navigator + param builder. |

---

## 3. Investor needs vs current AI coverage

What an investor or analyst expects the terminal's AI to do, and
where finterm sits. **Covered** = ships and works end-to-end.
**Partial** = primitives exist but no user-facing flow ties them
together. **Missing** = no path at all.

### 3.1 Buy-side analyst

| Workflow | Status | Notes |
|---|---|---|
| Initiate coverage on a name | Missing | Edgar/news/markets tools exist; nothing pulls them together. |
| Comparable-company analysis (`/comps`) | Missing | No skill, no command. FMP and yfinance can supply data; no methodology layer. |
| DCF / LBO model build | Missing | Excel-style modelling is also out of scope for a Qt terminal; output substrate would be ReportBuilder. |
| Earnings preview (consensus, what to listen for) | Missing | News tool can fetch coverage; no agent pre-digests. |
| Earnings post (call summary, model update, note draft) | Missing | No transcript ingestion. News tool only. |
| Screen for ideas (factor / thematic) | Partial | `MarketsTools` and equity-research tools can screen on metrics; no LLM-driven thematic screen. |
| Catalyst calendar / thesis tracking | Missing | Notes service exists; no agent watches the world against an active thesis. |
| Sector overview / industry map | Partial | DBnomics, gov data, news available; no agent assembles the picture. |

### 3.2 Portfolio manager

| Workflow | Status | Notes |
|---|---|---|
| Portfolio review with narrative commentary | Missing | Portfolio screen renders numbers; no agent writes the explanation. |
| Daily morning meeting note | Missing | News + markets + watchlist tools are present; no scheduled assembly. |
| Factor attribution / risk-bucket explanation | Partial | QuantLib has the math; nothing narrates. |
| Rebalance proposal with rationale | Missing | Paper trading tools execute; nothing proposes. |
| Position-level alerts with action recommendations | Partial | Alerts surface exists; LLM does not enrich them. |

### 3.3 Quant / systematic

| Workflow | Status | Notes |
|---|---|---|
| Factor / model hypothesis generation | Covered | RD-Agent loops. The only fully wired AI workflow in quant. |
| Backtest narration ("why did this go wrong") | Missing | Six engines return JSON metrics; no agent reads them. |
| Parameter-sweep proposal | Missing | Walk-forward / optimize endpoints exist; no agent picks ranges. |
| Regime detection narrative | Missing | — |
| Live anomaly explanation | Missing | — |
| Alpha-arena competitive LLM trading | Covered | Agno-backed agents, OpenAI + Anthropic, scored leaderboard. |

### 3.4 Crypto / DeFi

| Workflow | Status | Notes |
|---|---|---|
| Pool / yield comparison narrative | Missing | `CryptoTradingTools` can pull data; nothing narrates. |
| Wallet-position commentary | Missing | — |
| On-chain anomaly explanation | Missing | — |

### 3.5 Compliance / ops

| Workflow | Status | Notes |
|---|---|---|
| KYC document parsing | Missing | — |
| Insider window tracking | Partial | Edgar Form 4 access is there; no policy layer. |
| Trade-suitability check | Missing | — |

### 3.6 Cross-cutting capabilities the AI lacks

- **No skills / methodology layer.** Agents have a system prompt and
  tools but no equivalent of `/comps`, `/dcf`, `/earnings`,
  `/ic-memo` slash-commands or domain SKILL.md guides.
- **No transcript corpus.** Earnings calls, conference calls, fed
  speakers — none indexed or callable.
- **No embeddings + vector store wired end-to-end.** `vectordb_registry.py`
  exists; nothing populates a corpus.
- **No scheduled agent runs.** Morning notes, catalyst alerts, KPI
  watches — all need a background scheduler the agent surface lacks.
- **No agent-readable history of finterm activity.** What screens did
  the user open, what positions did they look at, what's in the
  notebook? `AgenticMemoryTools` exists but is small and underused.

---

## 4. External AI assets we adopt

Two repos under `/home/yma/fin/`. They sit at different layers and
compose cleanly.

### 4.1 `financial-datasets` — data layer

Three public repos under `github.com/financial-datasets`:

- **`mcp-server`** (Python, MIT). FastMCP-based **stdio** MCP server
  wrapping the financialdatasets.ai REST API. Wraps **10 tools**:
  `get_income_statements`, `get_balance_sheets`,
  `get_cash_flow_statements`, `get_current_stock_price`,
  `get_historical_stock_prices`, `get_company_news`,
  `get_available_crypto_tickers`, `get_crypto_prices`,
  `get_historical_crypto_prices`, `get_current_crypto_price`.
  Stdio transport — added to `mcp_servers` table directly; no
  `mcp-remote` bridge needed.
- **`web-crawler`**. General crawler. Not directly AI-shaped.
- **`llm-evaluations`**. Eval harness for LLMs on financial research
  tasks. Useful as a model-selection blueprint and the seed for
  finterm's own eval bench.

**Coverage vs finterm's existing data tools.** The MCP wraps a narrow
slice of the underlying REST API:

| Dataset | MCP exposes? | REST API has it? | Finterm equivalent? |
|---|---|---|---|
| Income / balance / cash flow | yes | yes | FMP, Edgar XBRL |
| Stock prices (historical + snapshot) | yes | yes | yfinance, Alpha Vantage, FMP |
| Company news (ticker-tagged) | yes | yes | News service |
| Crypto prices + tickers | yes | yes | CoinGecko, Kraken WS, akshare |
| SEC filings (10-K / 10-Q / 8-K, section-level Item 1A / 7) | no | yes | `sec_data.py` + Edgar tools (no section-level) |
| Insider trades (Form 4) | no | yes | `sec_data.py` (raw) |
| Institutional holdings (13-F) | no | yes | `sec_data.py` (raw) |
| Segmented financials | no | yes | XBRL parseable, not first-class |
| Operational KPIs / guidance / non-GAAP | no | yes | Missing |
| Earnings (estimates, surprises, history) | no | yes | Partial via FMP |

What it adds, beyond raw coverage that finterm mostly has:

1. **Unified normalized line-item schema for financials** —
   `revenue`, `cost_of_revenue`, `operating_income`, … rather than
   raw XBRL tag soup. This is the real win and is what makes
   `/comps`-style workflows tractable.
2. **Hosted-and-maintained.** yfinance is unofficial scrape; FMP free
   rate-caps at 250/day; financial-datasets is a contracted product.
3. **LLM-shaped tool descriptions.** Built for tool calling.
4. **Section-level SEC filing access (REST only, not MCP yet).** The
   REST API exposes 10-K Item 1A (risk factors), Item 7 (MD&A) as
   separate endpoints — *we vendor these via an internal MCP*; see
   Track 6.
5. **Coverage gaps it does not fix.** No options, economics breadth,
   China, energy, Treasury, CFTC, geopolitics — those stay on
   finterm's existing sources. US-only equities, ~27k tickers, 30+
   years history.

### 4.2 `financial-services` — methodology + agent layer

Apache-2.0 plugin marketplace under `github.com/anthropics/financial-services`.
Three things finterm absorbs:

**Skills (markdown methodology) — 60+ files** under
`plugins/vertical-plugins/<vertical>/skills/<name>/SKILL.md`.
Verticals: financial-analysis (core), investment-banking,
equity-research, private-equity, wealth-management, fund-admin,
operations. Representative skills: `comps-analysis`, `dcf-model`,
`lbo-model`, `3-statement-model`, `audit-xls`, `earnings-analysis`,
`earnings-preview`, `initiating-coverage`, `model-update`,
`morning-note`, `sector-overview`, `thesis-tracker`,
`catalyst-calendar`, `idea-generation`, `dd-checklist`, `ic-memo`,
`portfolio-monitoring`, `value-creation-plan`, `client-review`,
`financial-plan`, `portfolio-rebalance`, `tax-loss-harvesting`.

These are **plain markdown**, model-agnostic, explicitly constructed
around MCP-first data-source hierarchies (e.g.
`comps-analysis/SKILL.md` mandates *"FIRST: Check for MCP data
sources … NEVER use web search as a primary data source"*). They
slot into finterm's agent system as system-prompt context for the
relevant turn.

**Slash commands — ~30.** `/comps`, `/dcf`, `/lbo`, `/earnings`,
`/earnings-preview`, `/initiate`, `/sector`, `/screen`, `/ic-memo`,
`/morning-note`, `/thesis`, `/catalysts`, `/cim`, `/teaser`,
`/buyer-list`, `/merger-model`, `/rebalance`, `/tlh`,
`/client-review`. Map to finterm chat slash-commands or right-click
context actions in screens.

**Agent system prompts — 10 named agents.** Pitch Agent, Meeting Prep,
Market Researcher, Earnings Reviewer, Model Builder, Valuation
Reviewer, GL Reconciler, Month-End Closer, Statement Auditor, KYC
Screener. Each is a system prompt + bundled skills. Direct fit for
`AgentService` registrations to populate the Agent Config screen.

Not absorbed:

- `pptx-author` / `xlsx-author` / `audit-xls` Office-host paths.
  Methodology survives; output substrate becomes ReportBuilder.
- 11 partner MCPs in `.mcp.json` (Daloopa, Morningstar, S&P Kensho,
  FactSet, Moody's, MT Newswires, Aiera, LSEG, PitchBook,
  Chronograph, Egnyte) — paid. Future marketplace seed list, not free.
- Managed-agent cookbooks — only matter if finterm adopts a third
  path (Anthropic-hosted `/v1/agents`). Out of scope per §1.
- MS 365 add-in install tooling — irrelevant.

### 4.3 Composition

`financial-datasets` answers *"what's Apple's FCF margin?"*;
`financial-services` answers *"now build me a comps table around it."*

| Gap in finterm | Closed by |
|---|---|
| Agent has no methodology / domain knowledge | financial-services skills + slash commands |
| No out-of-box agent identities | financial-services agent system prompts |
| XBRL line-item normalization burden | financial-datasets MCP |
| US fundamentals reliability | financial-datasets MCP |
| Section-level 10-K access | financial-datasets REST (custom internal MCP, Track 6) |
| Insider trades, 13-F at LLM-shape | financial-datasets REST (custom internal MCP, Track 6) |
| Transcript corpus | Neither — still missing; consider a separate provider |

---

## 5. Target architecture

```
                   ┌──────────────────────────────────────────────┐
                   │              UI surfaces                     │
                   │  Chat Mode · Agent Workbench · MCP Servers   │
                   │      · Settings · AiChatBubble · Screens     │
                   └─────────────────┬────────────────────────────┘
                                     │ (Qt signals + REST-shaped C++ API)
                   ┌─────────────────▼────────────────────────────┐
                   │           LLM Router (LlmService)            │
                   │ profile resolution · streaming · tool-loop · │
                   │   provider-agnostic OpenAI-compatible        │
                   └─────────────────┬────────────────────────────┘
                                     │
                ┌────────────────────┼────────────────────────────┐
                │                    │                            │
       ┌────────▼────────┐  ┌────────▼────────┐         ┌─────────▼─────────┐
       │  Single Agent   │  │  Skills /        │         │   McpService      │
       │     Runtime     │  │  Methodology     │         │  (unified tool    │
       │  (finagent_core)│  │  Library         │         │   namespace)      │
       │  - core_agent   │  │ - SKILL.md files │         │ - internal (31)   │
       │  - planner      │  │ - slash commands │         │ - stdio (N)       │
       │  - team coord   │  │ - agent prompts  │         │ - http (N)        │
       └────────┬────────┘  └────────┬─────────┘         │ - per-agent scope │
                │                    │                   └─────────┬─────────┘
                └────────────────────┴─────────────────────────────┘
                                     │
                   ┌─────────────────▼────────────────────────────┐
                   │              Persistence                     │
                   │ SecureStorage (keys) · SQLite (configs,      │
                   │ profiles, sessions, agentic memory, evals)   │
                   └──────────────────────────────────────────────┘
```

### 5.1 Design decisions

**Routing**

- **D1.** Two first-class paths (local + external), picked by the
  user via the profile system. No silent failover in either direction.
  Fresh-install bias: local path is suggested because it works with
  zero keys.
- **D2.** Local LLM runtime is link-only, owned by a sibling project.
  Finterm holds `local_llm_base_url` and talks OpenAI-compatible HTTP;
  does not install / supervise / update the runtime.

**Agent runtime**

- **D3.** One canonical agent runtime: `finagent_core`. `deepagents`
  is retired; `alpha_arena` calls into `finagent_core` through a
  "competitive" team mode. One provider adapter, one tool-calling
  fallback, one tool namespace.
- **D4.** Skills/methodology as a first-class data layer. Each agent
  identity declares which skills it loads; the runtime composes
  system prompt = `<agent prelude> + <selected SKILL.md bodies>`.
  Skills are markdown; updates land as PRs, no code change.
- **D5.** Slash commands are a thin shell. `/comps` resolves to
  `(agent="equity_research", skill="comps-analysis", args=...)` and
  hits the same runtime. No per-command Python.

**MCP**

- **D6.** Three transports, single namespace. Internal via
  `McpProvider` (in-process). Stdio via `McpClient`. HTTP/SSE added
  to `McpClient` with static-auth support (`Authorization: Bearer …`,
  custom headers; **no OAuth** until a target needs it). All three
  feed `McpService::list_tools()` with source-prefixed names
  (`fd:get_income_statements`, `int:markets.quote`, `ext:fetch.url`).
- **D7.** Per-agent tool scoping. Each agent identity declares
  `allow_tools: ["fd:*", "int:markets.*", "int:portfolio.peek"]`.
  `McpService::list_tools_for(agent_id)` filters before the model
  sees it. Stops the 100-tool soup and the
  "geopolitics-agent-has-PaperTrading.place_order" foot-gun.
- **D8.** Data-source hierarchy is config, not code. Per agent, an
  ordered list: `["fd", "int:edgar", "int:fmp", "int:yfinance"]`.
  Skill text references "your data hierarchy" instead of naming
  providers, so swapping `fd` for `int:fd_rest` (the future internal
  REST wrapper) doesn't touch skills.

**Voice**

- **D9.** Drop Google SR. Once Whisper local lands, STT options are
  Whisper (free, local, default) and Deepgram (paid, opt-in).
  Whisper stays in finterm for v1 (self-contained STT means a fresh
  install has a working mic without depending on the sibling project).
  `SpeechService` exposes a strategy interface so a future HTTP
  backend (POST audio → `{local_llm_base_url}/audio/transcriptions`)
  can slot in.

**Storage and outbound**

- **D10.** SecureStorage migration wipes the plaintext `api_key`
  column. On first run, move each row to the keychain and NULL the
  column (keep the column for schema stability). Reasons: personal
  fork — downgrade isn't a constraint; leaving plaintext defeats the
  migration; zeroing forces every code path through `SecureStorage`,
  eliminating a "forgot to migrate this read" bug class.
- **D11.** Gate the Fincept hosted endpoint. Feature-flag
  `LlmService::fincept_async_request` off by default; hide "Fincept"
  from the provider dropdown. No-key fallback moves from Fincept →
  the local LLM base URL.

**UI**

- **D12.** Unify into "AI Workbench." Collapse `chat_mode`,
  `agent_config`, and `mcp_servers` into a single screen with a left
  nav: Chat, Agents, Teams, Workflows, Tools, Servers, Profiles.
  Floating `AiChatBubble` and screen-context actions stay as fast
  paths. Workbench is *the* surface for configuration.

**Memory and scheduling**

- **D13.** Embeddings + vector store wired end-to-end. Default vector
  DB: sqlite-vec (no external service). Embeddings via the configured
  `local_llm_base_url` or external profile per role. One corpus per
  workspace: notes, watched-ticker filings, transcripts (when
  ingested), recent news. Exposed as `MemoryTools` (replaces the
  narrow `AgenticMemoryTools`).
- **D14.** Background scheduler in `AgentService`. Cron-shaped
  scheduler (Qt timer + SQLite-backed queue). "Morning note at
  6:30am", "catalyst watch every 30 min", "earnings alert on
  schedule" become declarative `(agent_id, skill, args)` tuples —
  same machinery as a chat turn.

**Eval, observability, safety**

- **D15.** Eval harness in-tree. Vendor or interface with
  `financial-datasets/llm-evaluations` as `scripts/agents/evals/`.
  Run nightly on a small fixture set to catch model/provider
  regressions. This is where we measure whether moving the default
  profile from one provider to another is safe.
- **D16.** Observability — per-tool, per-agent, per-provider counters
  (latency, error rate, token cost). Surface in the Workbench's
  System tab. Mirrors what RDAgent already records for its loops.
- **D17.** Safety / kill-switch. Trading-adjacent tools
  (`PaperTradingTools::place_order`, `CryptoTradingTools` paths that
  touch keys) require an auth hook; `McpProvider` already has the
  slot. Make the kill-switch user-visible in the Workbench's Servers
  tab — one toggle disables all destructive tools globally.

---

## 6. Plan — work tracks

Foundation tracks (1–4) come first; they unblock the rest. Content
tracks (5–6) and runtime tracks (7–10) can land in parallel once
foundations ship. Always-on tracks (11–14) close out.

### Track 1 — Foundation cleanup

Pre-flight for everything else. Land before exposing anything to end
users.

1. **SecureStorage migration for LLM keys (D10).** On first run after
   migration, read `llm_configs.api_key`, write each to keychain
   (`SecureStorage`), NULL the column.
2. **Gate the Fincept hosted endpoint (D11).** Feature-flag
   `LlmService::fincept_async_request` off; hide "Fincept" provider.
   Default no-key fallback moves to the local LLM base URL.
3. **Local Whisper STT + drop Google SR (D9).** Add
   `scripts/voice/whisper_stt.py` (`faster-whisper`, `medium`
   default, `large-v3` opt-in). Remove Google SR (outbound,
   unauthenticated). Result: Whisper (default) and Deepgram (opt-in).
4. **Embeddings via OpenAI-compatible endpoint.** Wire
   `scripts/agents/finagent_core/vectordb_registry.py` to call
   `{local_llm_base_url}/embeddings` as the default embeddings
   provider. Keep `text-embedding-004` (Gemini free) as a cloud
   option.

### Track 2 — Local LLM client

5. **Settings → LLM Config "Local LLM service" section.** Editable
   `local_llm_base_url` (suggestion `http://localhost:11434/v1`), a
   Probe button that hits `GET {base_url}/models`, a "Get started →"
   deep link to the sibling project.
6. **Fresh-install fallback flip.** With neither local URL configured
   nor external key pasted, default no-key fallback is the local URL.
   On a configured local profile with the service down, surface a
   "Local LLM service not reachable" banner — **no silent failover**.
7. **Generalise the Ollama provider entry.** Rename to "Local
   (OpenAI-compatible)" and switch to `/v1/...` (works for vLLM,
   llama.cpp, LM Studio, etc.). The OpenAI-compatible path is
   preferred long-term; the sibling project may not be Ollama.
8. **Tool-calling reliability flag.** When the active profile points
   at the local URL, default to the `deepagents/orchestrator.py`
   prompt-loop path (handles malformed tool-call JSON); expose a
   "strict tool calling" opt-in toggle.

### Track 3 — MCP HTTP transport + marketplace seed list

9. **Add HTTP/SSE transport to `McpClient` (D6).** Sibling
   `McpHttpClient` class implementing the same JSON-RPC 2.0 surface
   against streamable HTTP / SSE. New `transport_type` column on
   `mcp_servers` (default `stdio`). Factory in `McpManager` picks the
   transport. Static auth only: `Authorization: Bearer …`,
   `X-API-Key: …`, arbitrary custom headers. Auth value pulled from
   `SecureStorage` at request time.
10. **Marketplace seed list.** Seed the `mcp_servers` table with the
    free / freemium baseline:

    | Server | Transport | Purpose | Key needed? |
    |---|---|---|---|
    | `mcp-server-fetch` | stdio | Generic URL fetch | No |
    | `mcp-server-filesystem` | stdio | Sandboxed file ops | No |
    | `mcp-server-sqlite` | stdio | Local DB queries | No |
    | `mcp-server-time` | stdio | Time / timezone | No |
    | `mcp-server-sequentialthinking` | stdio | Reasoning aid | No |
    | `mcp-server-brave-search` | stdio | Web search | Free Brave key |
    | `mcp-server-playwright` | stdio | Browser automation | No |
    | `yfinance-mcp` (community) | stdio | Free quotes/fundamentals | No |
    | `financial-datasets/mcp-server` | stdio | US fundamentals + news + crypto | Free-tier key |

### Track 4 — Free cloud provider deep-links

11. **Surface a "Free providers" group** in Settings → LLM Config
    with one-line "Get a free key →" deep links for Gemini, Groq,
    OpenRouter, Cerebras, Mistral. No code change in `LlmService.cpp`
    — all five are already in the multi-provider switch.
12. **Seed default profile routing.** If the user pastes a Gemini
    key, embeddings auto-route to `text-embedding-004` (free) and
    chat to `gemini-2.x-flash`. No prompt UI prerequisite.

| Provider | Free tier (verify at wire time) | Recommended role |
|---|---|---|
| Google Gemini (AI Studio) | Generous RPM/RPD on Gemini 2.x Flash / Flash-Lite; free `text-embedding-004` | Agent reasoning, embeddings |
| Groq | High tokens/sec on Llama 3.x, Mixtral, Qwen | "Fast chat" lane |
| Cerebras | Free tier, fast | Alt fast lane |
| Mistral La Plateforme | Free experimental tier | Optional |
| OpenRouter | `:free` tagged models | Catch-all |
| HuggingFace Inference | Rate-limited free | Niche models |
| Anthropic / OpenAI | No real free API tier — signup credits only | Do not plan around |

### Track 5 — Vendor `financial-services` skills + commands + agent prompts

13. **Skills vendoring.** Copy `financial-services` skills (60+
    `SKILL.md` files) under
    `scripts/agents/finagent_core/skills/<vertical>/<name>/SKILL.md`.
    Preserve NOTICE / Apache-2.0 attribution.
14. **Agent identities.** Register the 10 named agents (Pitch,
    Meeting Prep, Market Researcher, Earnings Reviewer, Model
    Builder, Valuation Reviewer, GL Reconciler, Month-End Closer,
    Statement Auditor, KYC Screener) as `finagent_core` agent
    identities, each binding to its skills.
15. **Slash commands.** Resolve `/comps`, `/dcf`, `/earnings`,
    `/ic-memo`, … in chat to `(agent, skill, args)` tuples through
    `finagent_core`. Implement once at the chat-mode level — no
    per-command Python.
16. **Retarget data-source hierarchy.** Patch each `SKILL.md`'s
    "data source priority" block from upstream partner MCPs (Daloopa,
    Kensho, FactSet) to `fd:*` (financial-datasets MCP) and `int:*`
    (finterm internal MCP tools).

### Track 6 — `FinancialDatasetsTools` internal MCP

17. **Build a new `src/mcp/tools/FinancialDatasetsTools.cpp` family**
    that wraps the financialdatasets.ai REST endpoints not exposed by
    upstream's MCP server:

    | Tool | REST endpoint | Why |
    |---|---|---|
    | `get_sec_filing_section` | 10-K / 10-Q / 8-K with `item=1A`, `item=7`, etc. | Section-level filing access; biggest single differentiator |
    | `get_insider_trades` | Form 4 | Pre-normalized vs raw SEC |
    | `get_institutional_holdings` | 13-F | Same |
    | `get_segmented_financials` | Business + geographic | XBRL parseable but not first-class |
    | `get_operational_kpis` | KPIs / guidance / non-GAAP | Missing entirely |
    | `get_earnings` | Estimates, surprises, history | Partial via FMP today |

    Uses the same `FINANCIAL_DATASETS_API_KEY` env var as upstream
    MCP. Tool descriptions tuned for LLM tool calling. Internal —
    served through `McpProvider`, no subprocess.

### Track 7 — Consolidate to one agent runtime

18. **Keep `finagent_core` as canonical.** Migrate `deepagents`
    provider-bridge tricks (text-parse tool-call fallback) into
    `core_agent.py`. Delete `scripts/agents/deepagents/`.
19. **Migrate `alpha_arena` to call into `finagent_core`** through a
    "competitive" team mode. `AlphaArenaService` keeps its public
    C++ surface; internals stop importing `agno` directly.

### Track 8 — Skills system in `core_agent`

20. **System-prompt composition.** Each agent identity declares
    `skills: [...]`; `core_agent.py` composes system prompt =
    `<agent prelude>` + concatenation of selected SKILL.md bodies.
    Backwards-compatible: an agent with no skills still works.
21. **Skill discovery API.** Expose skill list via MCP
    (`agent.list_skills`, `agent.preview_skill`) so the Workbench
    can render the catalogue without a separate Python path.

### Track 9 — Per-agent tool scoping + namespacing

22. **Source-prefixed tool names.** `McpService::list_tools()` returns
    names prefixed by source: `fd:get_income_statements`,
    `int:markets.quote`, `ext:fetch.url`.
23. **Per-agent allowlists.** Each agent declares `allow_tools` (glob
    patterns); `McpService::list_tools_for(agent_id)` filters before
    the model sees the catalogue.

### Track 10 — Embeddings + vector store + `MemoryTools`

24. **sqlite-vec as default vector DB** (no external service).
    Embeddings via the active embeddings profile.
25. **Per-workspace corpus** seeded with: notes, watched-ticker
    filings, transcripts (when ingested), recent news.
26. **`MemoryTools` MCP family** exposing
    `memory.search(query, k)`, `memory.upsert(doc)`,
    `memory.list_collections()`. Replaces `AgenticMemoryTools`
    (which becomes a thin compatibility shim).

### Track 11 — Background scheduler

27. **`AgentService` cron-shaped scheduler.** Qt timer driving a
    SQLite-backed queue. Each entry: `(agent_id, skill, args, cron)`.
    "Morning note at 6:30am", "catalyst watch every 30 min" become
    declarative. Same machinery as a chat turn.
28. **Scheduler UI in Workbench** — list runs, view last output, edit
    cadence, pause/resume.

### Track 12 — Quant narrator

29. **`QuantNarratorTools.cpp`.** Three MCP tools:
    `narrate_backtest_result(run_id)`,
    `propose_param_sweep(run_id)`,
    `narrate_live_trade(trade_id)`. Stateless — takes structured
    numeric input from existing services, asks an LLM (per-profile)
    for English commentary.
30. **`quant_critic` agent identity** in `finagent_core` —
    system prompt composed from `earnings-analysis` +
    `model-update` + quant-specific prelude.

### Track 13 — AI Workbench (UI consolidation)

31. **Single screen with left nav** — Chat, Agents, Teams, Workflows,
    Tools, Servers, Profiles. Replaces `chat_mode`, `agent_config`,
    `mcp_servers` as standalone screens (they become panels under
    Workbench). Floating bubble + screen-context actions unchanged.
32. **Servers tab carries the kill-switch toggle (D17).**

### Track 14 — Evals + observability + kill-switch

33. **`scripts/agents/evals/` directory.** Vendor or interface with
    `financial-datasets/llm-evaluations`. Nightly fixture run, results
    persisted to SQLite, surfaced in Workbench → System.
34. **Per-tool / per-agent / per-provider counters** (latency, error
    rate, token cost). Recorded in SQLite. Surface in System tab.
35. **Trading-adjacent tool auth hook.** Every destructive tool
    (place_order, send_wallet_tx, etc.) gates on the kill-switch
    state; auth callback prompts the user when armed.

---

## 7. Sequencing

```
   Track 1 (cleanup) ──┬─► Track 2 (local client)  ─┐
                       │                             │
                       ├─► Track 3 (MCP HTTP+seed)──┼─► Track 5 (skills) ──┐
                       │                             │                       │
                       └─► Track 4 (free providers)─┘                       │
                                                                            │
                                                Track 6 (fd internal MCP) ──┤
                                                                            │
                                                Track 7 (one runtime) ──────┤
                                                Track 8 (skills system) ────┤
                                                Track 9 (tool scoping) ─────┤
                                                                            ▼
                                                              Track 10 (memory)
                                                              Track 11 (scheduler)
                                                              Track 12 (quant narrator)
                                                                            │
                                                                            ▼
                                                              Track 13 (AI Workbench)
                                                              Track 14 (evals/obs)
```

Concrete order, smallest user-visible wins first:

- **Week 1.** Track 1 (foundation cleanup). Everything else assumes
  these.
- **Week 1.** Track 2 (local LLM client). With the sibling project
  running, user can chat with no API key.
- **Week 1–2.** Track 3 (MCP HTTP transport + marketplace seed).
  Agents gain web/fs/search/finance tools.
- **Week 2.** Track 4 (free-provider deep links).
- **Week 2–3.** Track 5 (skills vendoring). Skills available to
  agents as system-prompt context.
- **Week 3.** Track 6 (`FinancialDatasetsTools` internal MCP).
  Insider trades, 13-F, section-level filings reach the LLM.
- **Week 4.** Track 7 (retire `deepagents`, migrate `alpha_arena`).
- **Week 4.** Track 8 (skills system in `core_agent`).
- **Week 5.** Track 9 (tool scoping + namespacing).
- **Week 5–6.** Track 10 (memory).
- **Week 6.** Track 11 (scheduler).
- **Week 6.** Track 12 (quant narrator).
- **Week 7.** Track 13 (AI Workbench).
- **Week 7–8.** Track 14 (evals + observability + kill-switch).

---

## 8. Risks and mitigations

- **Local-model tool-calling reliability.** 7–14B models sometimes
  produce malformed tool-call JSON.
  *Mitigation:* when the active profile points at the local URL,
  default to the prompt-loop fallback; expose a "strict tool calling"
  opt-in toggle. Re-evaluate per model.
- **Sibling-project unreachable when active profile is local.**
  *Mitigation:* show a banner pointing at the sibling project's setup
  docs and **do not silently fail over to any other provider**. If
  the user wants a fallback to an external provider on local outage,
  that's an explicit profile choice, not implicit behavior. Symmetric
  for external 5xx → local: no silent failback.
- **Endpoint surface drift.** Local runtimes (Ollama, vLLM, llama.cpp,
  LM Studio) all *claim* OpenAI compatibility but diverge on edge
  cases (streaming chunk format, tool-call deltas, embeddings
  response shape).
  *Mitigation:* small compatibility shim per runtime if needed;
  document tested runtimes in the sibling project's README, not here.
- **Free tier drift.** Free quotas / models change without notice.
  *Mitigation:* deep links point at the provider's own pricing/quota
  page, not at hardcoded numbers in our UI.
- **SecureStorage Linux fallback.** When `libsecret` is absent the
  storage degrades to XOR-obfuscated QSettings.
  *Mitigation:* surface a one-time warning in Settings when on the
  fallback; do not block.
- **Fincept endpoint reachability tests.** Any CI / dev tests pointing
  at fincept.in will break once gated.
  *Mitigation:* audit during Track 1; remove or repoint at the local
  stub.
- **`deepagents` retirement may lose features.** LangGraph subagent
  spawning and provider-text-parse fallback ride on `deepagents`.
  *Mitigation:* port the fallback into `core_agent.py` first, verify
  the alpha-arena migration on a non-tool-calling local model, then
  delete.
- **Skill drift.** Vendored `financial-services` skills evolve
  upstream; our retargeted data-source hierarchies will drift.
  *Mitigation:* keep skills under `scripts/agents/finagent_core/skills/`
  as a vendored fork; refresh deliberately, not automatically.
- **eval-harness signal-to-noise.** Cheap nightly evals on a tiny
  fixture set will not catch regressions on long-tail tasks.
  *Mitigation:* start small, expand fixtures when we have a known
  regression we want guarded against.

---

## 9. Acceptance criteria

Three install flavours must all pass.

**Flavour A — Local only** (sibling project running, no commercial
keys configured):

- A1. Settings → LLM Config → confirm/edit `local_llm_base_url` →
  click Probe → list of models from the sibling project appears.
- A2. AI Chat → ask "what's 2+2" → streamed answer from the local
  model; no network calls leaving the box (verify with `ss -tnp`).
- A3. Stop the sibling project → reopen Chat → see "Local LLM service
  not reachable at {url}" banner with a link to the sibling
  project's docs. No silent failover.

**Flavour B — External only** (no sibling project running, one
commercial / free-tier key pasted):

- B1. Paste an Anthropic / OpenAI / Gemini / Groq key → set as active
  profile.
- B2. AI Chat → ask "what's 2+2" → streamed answer from that
  provider. No probe attempts to `localhost`, no "local unreachable"
  banner.
- B3. Revoke the key on the provider's dashboard → reopen Chat → see
  a provider-specific auth error surfaced clearly. No silent
  failover to local.

**Flavour C — Mixed** (both configured):

- C1. Both local URL and an external key present; profile assignments
  say chat → Groq, agent → local Qwen, embeddings → Gemini free.
- C2. Send a chat message → routed to Groq (verify in network panel /
  logs).
- C3. Run an agent → reasoning calls hit local Qwen.
- C4. Trigger a RAG-backed query → embeddings call hits Gemini.

**Always-on (all flavours):**

- D1. MCP Servers tab → seeded list visible → install
  `mcp-server-fetch` → agent can fetch a URL.
- D2. MCP Servers tab → install `financial-datasets/mcp-server` with
  a key → agent can call `get_income_statements("AAPL")`.
- D3. Voice settings → Whisper is the default (Google SR removed);
  Deepgram is opt-in. Mic → chat input populates, no outbound traffic
  in Whisper mode.
- D4. Workbench → Agents → "Equity Researcher" → `/comps AAPL` →
  agent loads `comps-analysis` skill, calls `fd:get_income_statements`
  for AAPL and peers, returns a comparable-company table.
- D5. Workbench → Schedule → "Morning note at 6:30am" → agent runs
  at the scheduled time, output appears in Workbench → Chat → Daily.
- D6. Workbench → System → kill-switch armed → agent attempting
  `paper.place_order` is prompted; user denies; tool returns error
  to the agent loop without trade.

---

## Appendix — Recommended local models

Not finterm's job to install; this is guidance for the sibling
project's README, based on the current dev box (RTX 5070 Ti / 12 GB
VRAM, Intel Ultra 9 285H, 30 GB RAM):

| Role | Suggested model | On-disk | Why |
|---|---|---|---|
| Primary chat + MCP tool calling | Qwen 2.5 14B Instruct (Q4/Q5) | ~9 GB | Best OSS function calling at 14B |
| Fast fallback | Llama 3.1 8B Instruct | ~5 GB | Sub-second TTFT on the 5070 Ti |
| Coding / ReportBuilder | Qwen 2.5 Coder 14B | ~9 GB | Python/report tools |
| Embeddings | nomic-embed-text or bge-m3 | ~280 MB / ~2 GB | Served via `/v1/embeddings` |
| Whisper STT | faster-whisper `medium` or `large-v3` | 1.5–3 GB | OpenAI-compatible `/v1/audio/transcriptions` |

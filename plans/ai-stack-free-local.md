# AI Stack — Architecture and Plan

**Status:** Current
**Date:** 2026-05-20
**Owner:** yumima
**Canonical tree:** `/home/yma/fin/finterm/fincept-qt/`
**Related:** `fincept-qt/docs/datahub-phases/phase-09-ai-mcp-agents.md`, `fincept-qt/docs/agents/datahub-guide.md`, `plans/local-ai-engine.md`, `plans/testing-strategy.md`

---

## 1. Goal

Two first-class agent runtimes, selected per-profile:

- **Local path.** A sibling LLM-services project on `localhost`,
  OpenAI-compatible. Zero outbound calls, zero commercial keys.
  Driven by a minimal in-process agent loop owned by finterm.
- **Anthropic path.** Claude via the Anthropic API, driven by the
  open-source Claude Agent SDK. Pay-per-token; no perpetual free
  API tier.

On a brand-new install with nothing configured, the local path is
suggested because it works with zero keys. Pasting an Anthropic key
switches the active profile.

The architecture is shaped so that adding a third runtime later
(Gemini, OpenAI, Groq, etc.) is one adapter; the rest of the system
— MCP tools, SKILL.md library, storage, UI — is runtime-independent.

### Stance: leverage, do not rebuild

When an AI provider ships a primitive — tool use, prompt caching,
extended thinking, citations, computer use, code execution, memory
tool, hooks, sub-agents, files, batch — finterm uses it through the
provider SDK. We do not build provider-agnostic wrappers around
these primitives. What finterm owns is the financial domain: data,
tools, skills, UI, profile management.

Consequence: the Anthropic path has access to the full Claude
feature surface as it evolves. The local path has a smaller feature
surface by design, but works offline and at zero cost.

### Cost note on the Claude Agent SDK

The SDK is open source (Apache 2.0), free to install. Running it
requires Anthropic API credits — pay-per-token with no perpetual
free tier (signup credits only). Consumer Claude.ai Pro/Max
subscriptions do not include API access. The local path remains the
zero-cost option.

### Non-goals

- Building an MCP **server** so external clients can consume
  finterm. Internal tools live in `McpProvider` and stay there.
- Self-hosting embeddings model training or fine-tuning.
- Installing, supervising, or updating local LLM runtimes (Ollama,
  vLLM, llama.cpp) — those live in the sibling project.
- Anthropic's hosted Managed Agents API (`/v1/agents`). Adopt the
  open-source Agent SDK instead — two runtimes, not three.
- Provider-agnostic abstractions over caching, thinking, citations,
  memory, files, computer use, code execution. Use the SDK on the
  Anthropic profile; degrade gracefully or omit on the local profile.
- Replacing QuantLib / Qlib / VectorBT / Agno math engines — AI sits
  next to them, not under them.

---

## 2. Position vs other AI investment platforms

### 2.1 Where finterm leads

1. **Pluggable LLM, local OR external, by role.** Bloomberg locks
   you to their hosted stack. AlphaSense, Hebbia, Rogo don't expose
   model choice. Anthropic's Managed Agents only run Anthropic
   models. finterm routes chat / agent / embeddings per profile and
   includes a free local path.
2. **MCP-native, three transports, source-prefixed namespace with
   per-agent allowlists.** Bloomberg uses BLPAPI (proprietary,
   closed). Hosted-agent products accept MCP but don't give users a
   marketplace UI with kill-switch and scoping.
3. **Vendored open methodology.** `anthropics/financial-services`
   60+ SKILL.md are model-agnostic, readable, forkable. Bloomberg's
   AI prompting is opaque; AlphaSense's smart-synonym layer is
   closed.
4. **Terminal-surface breadth in one binary.** 31 internal tool
   families spanning Edgar, M&A, alt investments, crypto, paper
   trade, quant lab, geopolitics, gov data, DBnomics. Research-only
   products (Rogo, Brightwave, Endex) don't span this.
5. **Local-first / no-outbound mode.** Compliance environments (PE,
   HF, sovereign desks) cannot adopt hosted AI products at all.
   finterm's local profile is uniquely positioned.
6. **Cost.** $0 (local) or pay-per-token (Anthropic), vs Bloomberg
   ~$30k/yr/seat, AlphaSense ~$15k/yr/seat.
7. **Quant integration AI hosted products can't match.** QuantLib
   (590 endpoints), VectorBT, Backtesting.py, Zipline, RD-Agent
   autonomous research, Alpha Arena — all in-process and callable
   by the agent loop.

### 2.2 Where finterm is short

| Gap | Bloomberg / AlphaSense / Hebbia / etc. | finterm |
|---|---|---|
| Proprietary data moat (BVAL, BIO, ANR, FIGI, real-time L1/L2) | Yes | No — public APIs only |
| Earnings + conference transcript corpus | Yes | No — explicit gap; user-self-serve via local Whisper is the bridge |
| Indexed broker research / IPO prospectus library | Yes (AlphaSense) | No |
| Long-running hosted agent infra w/ checkpointing | Yes (Anthropic Managed Agents) | Via Agent SDK only when Anthropic profile is active |
| Team workspaces / multi-user | Yes | No — single-user desktop |
| Mobile companion | Yes (Bloomberg Anywhere, AlphaSense mobile) | No |
| Fine-tuning on user corpora | Yes (Hebbia, AlphaSense) | No — explicit non-goal |
| TTS / voice broker squawk | Yes (Bloomberg) | No |
| Eval / regression-bench maturity | Yes | Thin (small fixtures, planned) |

### 2.3 Where finterm and the AI providers compose

| Anthropic ships | finterm uses it for |
|---|---|
| `anthropics/financial-services` skills + slash commands + agents | The methodology layer agents have never had |
| Claude Agent SDK | The Anthropic-path runtime; no homemade loop |
| Native MCP client in SDK | Connects the SDK to our internal MCPs and external marketplace |
| Prompt caching | 90% cost reduction on long SKILL.md system prompts |
| Extended thinking | Quality on valuation / quant / multi-step reasoning turns |
| Citations | Per-claim source attribution — the trust unlock |
| Computer use | Broker portal navigation, paywalled-but-public sources |
| Code execution | Ad-hoc Python over finterm data ("plot AAPL drawdown last 5y") |
| Vision + native PDF | Read chart screenshots, 10-K PDFs, broker statements |
| Memory tool | Cross-session learning of goals, theses, preferences |
| Hooks | Event-driven workflows (news threshold, schedule, drift) |
| Subagents | Parallel research workers |
| Batch API | Overnight portfolio refreshes, eval runs at 50% cost |
| Web search server-side tool | No separate MCP install for the Anthropic profile |

---

## 3. Current AI surface and what powers it

| Capability | Behavior | Engine today |
|---|---|---|
| AI Chat (Chat Mode, AiChatBubble) | Multi-session chat, SSE streaming, OpenAI-style tool calls + text-parse fallback | `LlmService` (11-provider switch); fresh install falls back to `fincept.in/research/llm/async` |
| Tool calling | Invokes 31 internal tool families + external stdio MCPs, merged namespace, 5s cache TTL | `McpService` → `McpProvider` (internal) + `McpClient` (stdio JSON-RPC 2.0) |
| Analytical agents | Plan-before-execute tool-calling loop, streams tokens/thinking/done | `AgentService` (C++) → `finagent_core/core_agent.py` + `execution_planner.py` |
| LangGraph-style subagents | Subagent spawn, malformed-JSON tool-call fallback | `deepagents/orchestrator.py` |
| Autonomous quant research | Factor / Model / Quant RD loops | `rdagents/` accessed via `AIQuantLabService::rd_agent_*` |
| LLM trading competition | Per-cycle buy/sell decisions, leaderboard | `services/alpha_arena/` over Agno framework |
| Voice input (STT) | Mic → chat composer | Google SR (default, outbound) + Deepgram (paid); `clap_detector.py` triggers |
| Profile routing | Per-context (chat / agent / team) → provider+model+settings | `LlmProfileRepository` (+ legacy `LlmConfigRepository`) |
| External MCP management | Add / start / stop / log external MCP servers | `McpManager` + `mcp_servers` SQLite table |
| Memory | Narrow K/V-style memory tools | `AgenticMemoryTools` (underused) |
| Embeddings | Not wired end-to-end | `vectordb_registry.py` (unconfigured) |
| LLM key storage | Plaintext column | `llm_configs.api_key` |

Missing as primitives: prompt caching, extended thinking, citations,
vision/PDF, computer use, code-execution sandbox, files API, hooks,
checkpointing, full MCP spec on internal servers, breadth in memory,
OAuth for HTTP MCP.

---

## 4. Target architecture

```
                  ┌─────────────────────────────────────────┐
                  │ UI surfaces                              │
                  │  Workbench · Chat · AiChatBubble ·       │
                  │  Screens (Portfolio, Quant Lab, etc.)    │
                  └────────────────────┬────────────────────┘
                                       │
                  ┌────────────────────▼────────────────────┐
                  │ Profile router (LlmProfileRepository)    │
                  │ chat / agent / agent_default / team /    │
                  │ team_default / team_coordinator          │
                  └────────────────────┬────────────────────┘
                                       │
                  ┌────────────────────┼────────────────────┐
                  ▼                                         ▼
        ┌─────────────────────┐               ┌─────────────────────┐
        │ Local runtime       │               │ Anthropic runtime   │
        │ (minimal loop;      │               │ (Claude Agent SDK)  │
        │  finagent_core slim)│               │                     │
        │                     │               │ - tool use          │
        │ - SSE chat          │               │ - prompt caching    │
        │ - tool loop         │               │ - extended thinking │
        │ - SKILL composer    │               │ - citations         │
        │ - slash dispatch    │               │ - vision / PDF      │
        │                     │               │ - files API         │
        │ Talks OpenAI-       │               │ - memory tool       │
        │ compatible API to   │               │ - computer use      │
        │ `local_llm_base_url`│               │ - code execution    │
        │                     │               │ - web search        │
        │                     │               │ - hooks / subagents │
        │                     │               │ - checkpointing     │
        │                     │               │ - batch API         │
        └──────────┬──────────┘               └──────────┬──────────┘
                   │                                     │
                   └──────────────────┬──────────────────┘
                                      │
              ┌───────────────────────┼───────────────────────────┐
              ▼                       ▼                           ▼
      ┌──────────────┐       ┌───────────────┐         ┌──────────────────┐
      │ Internal MCP │       │ Stdio MCP     │         │ HTTP MCP         │
      │ (McpProvider)│       │ (McpClient    │         │ (McpHttpClient,  │
      │              │       │  per server)  │         │  OAuth+DCR)      │
      │ 31 families  │       │               │         │                  │
      │ + full spec: │       │ financial-    │         │ Future hosted    │
      │  tools       │       │  datasets,    │         │ connectors       │
      │  resources   │       │  yfinance,    │         │ (Gmail, Drive,   │
      │  prompts     │       │  fetch,       │         │  GitHub, …)      │
      │  sampling    │       │  filesystem,  │         │                  │
      │  elicitation │       │  sqlite,      │         │                  │
      │  progress    │       │  time,        │         │                  │
      │  cancel/log  │       │  brave,       │         │                  │
      │              │       │  playwright   │         │                  │
      └──────────────┘       └───────────────┘         └──────────────────┘

       Both runtimes share:
       - SKILL.md library (vendored `financial-services` + custom)
       - Slash command dispatch
       - Profile / settings / SecureStorage
       - SQLite (configs, profiles, sessions, memory, evals, audit)
       - Voice (Whisper local STT) and the sibling local LLM project link
       - Trace + audit log schema
```

### 4.1 What finterm owns vs leverages

| Owned (built and maintained) | Leveraged (adopted from upstream) |
|---|---|
| Internal MCP tool families (31, finance-specific) | Claude Agent SDK (Anthropic profile runtime) |
| `FinancialDatasetsTools` internal MCP family | `anthropics/financial-services` SKILL.md / commands / agent prompts |
| Custom SKILL.md (where upstream doesn't fit) | `financial-datasets/mcp-server` (stdio) |
| Agent identities (system prompts + skill bindings) | Public MCP marketplace servers |
| Local OpenAI-compatible minimal agent loop | MCP spec (full — all capabilities + transports + auth) |
| MCP client (`McpClient` / `McpService`) | `SKILL.md` grammar (verbatim) |
| Profile / SecureStorage / repositories | JSON Schema (tool args, structured outputs) |
| UI (Workbench, Chat, Screens, AiChatBubble) | Sibling local LLM project (runtime, install, models) |
| Voice (Whisper local) | Provider-side primitives on the Anthropic path (caching, thinking, citations, vision/PDF, files, batch, computer use, code execution, web search, hooks, subagents, checkpointing, memory tool) |
| Trace + audit log schema | |

### 4.2 Two runtimes

**Local runtime** — `finagent_core`, slimmed to a minimal
OpenAI-compatible agent loop. Responsibilities:

- Compose system prompt from `<agent prelude>` + selected SKILL.md
  bodies.
- Tool loop: list tools from `McpService::list_tools_for(agent_id)`,
  send to provider, parse response, dispatch tool calls, feed
  results back. Loop until model emits no tool calls or budget
  exhausted.
- Streaming via SSE; tool-call JSON parsed strictly with text-parse
  fallback for malformed local-model output.
- Slash command dispatch: `/comps AAPL` resolves to
  `(agent="equity_research", skill="comps-analysis",
  args={ticker:"AAPL"})` and re-enters the same loop with the
  composed prompt.
- Budgets: max output tokens, max iterations, max tool calls, max
  wall time. Configurable per agent identity.
- Talks to `{local_llm_base_url}/v1/...`.

**Anthropic runtime** — `claude-agent-sdk` (Apache 2.0, OSS).
Responsibilities:

- We register: our internal MCP server (in-proc bridge), external
  MCPs from the `mcp_servers` table, SKILL.md library path, agent
  identity (system prompt + skill bindings + allowed tools).
- SDK handles: agent loop, MCP client wiring, streaming with typed
  deltas, tool use, prompt caching, extended thinking, citations,
  vision/PDF, files, memory tool, computer use, code execution, web
  search, hooks, subagents, checkpointing.
- Slash command dispatch in our chat layer; resolved tuple is handed
  to the SDK as the next user message + agent config.
- Budgets and trace events bridge into our schema via SDK hooks.

Both runtimes share the **same** MCP tool layer and the **same**
SKILL.md files. Adding a future runtime (Gemini, OpenAI) is
implementing one more adapter that loads the same tools and skills.

### 4.3 Shared MCP tool layer (full spec)

Our internal MCP servers (the 31 families + new
`FinancialDatasetsTools`) implement the **full MCP spec**, not only
tools:

- **Tools** — already done.
- **Resources** — typed read-only references (portfolio snapshot,
  watchlist, current news digest, active thesis). The agent
  requests them without spending a tool call.
- **Prompts** — finterm-curated templated prompts surfaced in the
  SDK's slash menu ("Daily Brief", "Stock Deep Dive", "Sector
  Pulse").
- **Sampling** — a tool can request a model completion through the
  client. Enables tools like "summarize this 50-page filing inside
  the tool"; cost flows through the user's profile.
- **Elicitation** — tools ask the user a structured mid-call
  question. Replaces the auth-hook pattern uniformly.
- **Progress / cancellation / logging** — long tools (backtests,
  RD-Agent, screens) emit progress; user can cancel; logs surface
  in the trace.

External transports:

- **Stdio** — existing `McpClient`. Used by the marketplace seed
  list and `financial-datasets/mcp-server`.
- **Streamable HTTP** — new `McpHttpClient`. Supports static bearer
  / API-key headers and OAuth 2.0 + Dynamic Client Registration.
  OAuth+DCR is required for hosted connectors (Gmail, Drive,
  GitHub) that may be enabled later.

Namespace: source-prefixed (`int:`, `fd:`, `ext:`). Per-agent
allowlists filter `list_tools_for(agent_id)` before either runtime
sees the catalogue, preventing tool soup.

Tool-result caching: `McpService` retains the existing 5s TTL cache
keyed by call-hash. Both runtimes hit the cache transparently;
per-call cache-control can disable it for non-idempotent reads.

### 4.4 Shared skills, slash commands, agent identities

Skills live as `SKILL.md` files under
`scripts/agents/finagent_core/skills/<vertical>/<name>/SKILL.md`.
We use the Anthropic SKILL.md grammar verbatim — no fork.

Skills are content. Updates are PRs against the markdown files; no
Python or C++ change.

Slash commands resolve at the chat layer to `(agent_id, skill, args)`
tuples. Implementation is shared: same resolution, dispatched to
whichever runtime the active profile selects.

Agent identities are SQLite-backed (`AgentConfigRepository`) and
declare: `system_prompt`, `skills: [...]`, `allow_tools: [...]`,
`runtime_hint` (optional), `budget` (max iterations, tokens, wall
time, USD).

### 4.5 Memory, files, embeddings

**Memory.** The Anthropic profile uses the SDK's native memory tool
(scoped, persistent, agent-managed). The local profile uses a
`MemoryTools` MCP family (we own) backed by sqlite-vec — same API
shape (`memory.search` / `memory.upsert` / `memory.list`), narrower
features. Both expose three scopes: per-thesis, per-workspace,
global. Every entry carries provenance.

**Files.** The Anthropic profile uses the Files API natively via SDK
(upload PDFs, images, CSVs; reference by handle across turns; native
PDF + vision). The local profile text-extracts to a local handle.

**Embeddings.** The Anthropic API has no native embeddings endpoint.
Both profiles use the same local server's `/embeddings` endpoint by
default (model: `nomic-embed-text` or `bge-m3` in the sibling
project). When the Anthropic profile is active and the local server
is unreachable, embeddings fall back to a user-pasted Voyage AI key.

### 4.6 Profile-driven routing

`LlmProfileRepository` resolves a `(role, entity)` query → runtime
+ model + settings. Roles: `ai_chat`, `agent`, `agent_default`,
`team`, `team_default`, `team_coordinator`. Resolution:
entity-specific → type default → global default.

A profile points at either `runtime: local` (with
`local_llm_base_url`, model name, params) or `runtime: anthropic`
(with API key reference in SecureStorage, model name, params).

No silent failover. If the local server is down and the active
profile is local, the chat surface shows a banner with a link to the
sibling project setup docs. If the Anthropic API is reachable but
the key is invalid, the surface shows the auth error verbatim.

### 4.7 Voice

STT options: local Whisper (default, free, via
`scripts/voice/whisper_stt.py` using `faster-whisper`, model
`medium`, with `large-v3` opt-in) and Deepgram (paid, opt-in). Wake
trigger: `clap_detector.py`. Surfaces: `AiChatBubble` mic + chat
composer mic.

Google SR removed. TTS remains a gap (Anthropic ships no TTS; a
future track may add local TTS via Piper / Kokoro).

### 4.8 Storage and keys

`SecureStorage` is the only path for LLM keys, broker keys, Deepgram
keys, MCP HTTP auth tokens. Backed by Windows DPAPI / macOS Keychain
/ Linux libsecret; degrades to XOR-obfuscated QSettings when
libsecret is absent (one-time warning surfaced in Settings).

`llm_configs.api_key` column is NULL across all rows — moved to
keychain at first run after the migration ships. Column kept for
schema stability.

The Fincept hosted endpoint (`fincept.in/research/llm/async`) is
feature-flagged off and removed from the provider dropdown. No-key
install falls back to the local profile.

### 4.9 Dormant provider adapters

The existing 8 non-local non-Anthropic provider branches in
`LlmService.cpp` (OpenAI, Gemini, Groq, DeepSeek, Kimi, MiniMax,
xAI, OpenRouter) remain in code as dormant adapters but are hidden
from the UI dropdown and excluded from default profile options.
Re-enabling later is a constant flip. The architecture is shaped to
take additional runtimes (most cleanly via their respective SDKs)
without refactor. (Fincept is not dormant — it is retired per R17.)

---

## 5. Capability matrix — what each runtime can do

| Capability | Local profile | Anthropic profile |
|---|---|---|
| Chat streaming | Yes | Yes (typed deltas via SDK) |
| Tool use (parallel calls) | Strict + text-parse fallback | Native via SDK |
| Prompt caching | n/a (no billing benefit) | Native; long SKILL system prompts cacheable |
| Extended thinking | Reasoning via system prompt | Native budget-controlled `thinking` |
| Citations | Tool-result metadata only | Native per-claim, character-offset |
| Vision | Only if local model supports (e.g., Qwen-VL) | Native |
| Native PDF | Text-extracted | Native (up to 100 pages) |
| Files API | Local file refs | Native handles |
| Memory tool | `MemoryTools` MCP family (sqlite-vec) | SDK-native + optional `MemoryTools` |
| Computer use (browser) | Playwright MCP server | Native server-side tool |
| Code execution | Local sandbox (we own) | Native server-side Python sandbox |
| Web search | Brave MCP server | Native server-side tool |
| Hooks (event-driven) | Limited (via scheduler) | Native SDK hooks |
| Subagents | Limited | Native via SDK |
| Checkpointing / resume | No | Native via SDK |
| Batch (non-real-time, 50% off) | No | Native via SDK |
| Token pre-flight | Provider-dependent | Native |
| Internal MCP (31 families + full spec) | Yes | Yes |
| External stdio MCPs | Yes | Yes |
| External HTTP MCPs (OAuth+DCR) | Yes | Yes |
| SKILL.md library | Composed by minimal loop | Loaded by SDK |
| Slash commands | Dispatched to local loop | Dispatched to SDK |

---

## 6. Investor workflows vs target coverage

The *why* behind the work tracks. Compressed; see prior revisions
for the long-form needs analysis.

### 6.1 Buy-side analyst

| Workflow | Status | Closed by |
|---|---|---|
| Initiate coverage | Will land | `initiating-coverage` skill + Edgar/news/markets tools + Anthropic citations |
| `/comps` | Will land | `comps-analysis` skill + financial-datasets normalized line items |
| `/dcf`, `/lbo` | Will land | `dcf-model` / `lbo-model` skills + code execution sandbox (Anthropic profile) |
| `/earnings`, `/earnings-preview` | Will land | Skills + news/Edgar/financial-datasets tools |
| Earnings post (call summary) | Partial | Same skill set; transcript-corpus gap remains |
| Thematic / factor screen | Will land | `idea-generation` skill + screens + financial-datasets |
| Catalyst / thesis tracking | Will land | `thesis-tracker` skill + scheduler + memory |
| Sector overview | Will land | `sector-overview` skill + DBnomics / gov data tools |

### 6.2 PM

| Workflow | Status | Closed by |
|---|---|---|
| Portfolio review with narrative | Will land | `portfolio-monitoring` skill + portfolio tools + Anthropic citations |
| Daily morning note | Will land | `morning-note` skill + scheduler + news tools |
| Factor attribution narrative | Will land | QuantLib + `quant_critic` agent identity |
| Rebalance proposal | Will land | `portfolio-rebalance` skill + paper trading + code exec |
| Position-level alerts | Partial | Existing alerts + Anthropic hooks |

### 6.3 Quant

| Workflow | Status | Closed by |
|---|---|---|
| Factor / model hypothesis generation | Covered | RD-Agent loops; LLM step routes through active runtime |
| Backtest narration | Will land | `QuantNarratorTools` + `narrate-backtest` skill |
| Parameter-sweep proposal | Will land | `propose-param-sweep` tool + Anthropic code execution |
| Regime detection narrative | Will land | Quant critic agent |
| Alpha-arena LLM trading | Covered | Migrates onto the two runtimes |

### 6.4 Cross-cutting gaps that remain

- No skills / methodology layer (closed by Track 7).
- No transcript corpus (open question — see §11).
- No embeddings + vector store wired end-to-end (closed by Track 9).
- No scheduled / event-driven agent runs (closed by Track 10).
- No agent-readable history of finterm activity (partial — closed
  by `MemoryTools` in Track 9).

---

## 7. Integration components

### 7.1 Anthropic Agent SDK

`claude-agent-sdk` (Apache 2.0, OSS). Free to install via pip;
pay-per-token Anthropic API credits required to run.

What it gives us:

- Agent loop (chain, parallel, sub-agents, autonomous)
- Native MCP client (we register our servers)
- Native tool use with parallel calls + structured output
- Prompt caching (auto-detected cache breakpoints + manual control)
- Extended thinking with budget control
- Citations (per-claim with character offsets)
- Vision + native PDF
- Files API
- Memory tool
- Computer use, code execution, web search (server-side tools)
- Hooks (event-driven)
- Checkpointing / resume
- Batch API
- Typed streaming events

What we configure per agent identity: system prompt, skills, allowed
tools, budget, runtime hint.

### 7.2 financial-datasets — data layer

Three repos under `github.com/financial-datasets`:

- **`mcp-server`** (Python, MIT, FastMCP stdio). 10 tools:
  `get_income_statements`, `get_balance_sheets`,
  `get_cash_flow_statements`, `get_current_stock_price`,
  `get_historical_stock_prices`, `get_company_news`,
  `get_available_crypto_tickers`, `get_crypto_prices`,
  `get_historical_crypto_prices`, `get_current_crypto_price`. Added
  to the `mcp_servers` table directly.
- **`web-crawler`** — general crawler. Not directly AI-shaped.
- **`llm-evaluations`** — eval harness; blueprint for our eval bench.

Coverage vs current finterm tools:

| Dataset | mcp-server | REST | finterm today |
|---|---|---|---|
| Income / balance / cash flow | yes | yes | FMP, Edgar XBRL |
| Stock prices (historical + snapshot) | yes | yes | yfinance, Alpha Vantage, FMP |
| Company news (ticker-tagged) | yes | yes | News service |
| Crypto prices | yes | yes | CoinGecko, Kraken, akshare |
| SEC filings, section-level (Item 1A, 7) | no | yes | sec_data.py (no sections) |
| Insider trades (Form 4) | no | yes | sec_data.py (raw) |
| Institutional holdings (13-F) | no | yes | sec_data.py (raw) |
| Segmented financials | no | yes | XBRL parseable, not first-class |
| Operational KPIs / non-GAAP | no | yes | Missing |
| Earnings estimates / surprises | no | yes | Partial via FMP |

What it adds beyond raw coverage:

1. Unified normalized line-item schema for financials. The real win;
   makes `/comps`-style workflows tractable.
2. Hosted-and-maintained (vs yfinance scrape, FMP free 250/day caps).
3. LLM-shaped tool descriptions.
4. Section-level SEC filing access (REST). We vendor these as
   `FinancialDatasetsTools` internal MCP (Track 6).
5. Does not fix: options, economics breadth, China, energy,
   Treasury, CFTC, geopolitics. Those stay on existing sources.

### 7.3 financial-services — methodology layer

Apache-2.0 plugin marketplace under
`github.com/anthropics/financial-services`. finterm absorbs three
things:

**Skills (60+ markdown).** Verticals: financial-analysis (core),
investment-banking, equity-research, private-equity, wealth-
management, fund-admin, operations. Representative skills:
`comps-analysis`, `dcf-model`, `lbo-model`, `3-statement-model`,
`earnings-analysis`, `earnings-preview`, `initiating-coverage`,
`model-update`, `morning-note`, `sector-overview`, `thesis-tracker`,
`catalyst-calendar`, `idea-generation`, `ic-memo`,
`portfolio-monitoring`, `value-creation-plan`, `client-review`,
`portfolio-rebalance`, `tax-loss-harvesting`.

Plain markdown, model-agnostic, constructed MCP-first.

**Slash commands (~30).** `/comps`, `/dcf`, `/lbo`, `/earnings`,
`/earnings-preview`, `/initiate`, `/sector`, `/screen`, `/ic-memo`,
`/morning-note`, `/thesis`, `/catalysts`, `/rebalance`, `/tlh`,
`/client-review`, etc. Map to chat slash commands or right-click
context actions in screens.

**Agent system prompts (10 named agents).** Pitch Agent, Meeting
Prep, Market Researcher, Earnings Reviewer, Model Builder, Valuation
Reviewer, GL Reconciler, Month-End Closer, Statement Auditor, KYC
Screener. Each is `system_prompt + bundled skills`.

Not absorbed:

- `pptx-author` / `xlsx-author` / `audit-xls` Office-host paths.
  Methodology survives; output substrate becomes ReportBuilder.
- 11 partner MCPs in `.mcp.json` (Daloopa, Morningstar, S&P Kensho,
  FactSet, Moody's, MT Newswires, Aiera, LSEG, PitchBook,
  Chronograph, Egnyte) — paid; future marketplace seed list.
- Managed-agent cookbooks — non-goal per §1.
- MS 365 add-in install tooling.

### 7.4 MCP marketplace seed list

| Server | Transport | Purpose | Key needed |
|---|---|---|---|
| `mcp-server-fetch` | stdio | Generic URL fetch | No |
| `mcp-server-filesystem` | stdio | Sandboxed file ops | No |
| `mcp-server-sqlite` | stdio | Local DB queries | No |
| `mcp-server-time` | stdio | Time / timezone | No |
| `mcp-server-sequentialthinking` | stdio | Reasoning aid | No |
| `mcp-server-brave-search` | stdio | Web search (local profile) | Free Brave key |
| `mcp-server-playwright` | stdio | Browser automation (local profile) | No |
| `yfinance-mcp` | stdio | Free quotes / fundamentals | No |
| `financial-datasets/mcp-server` | stdio | US fundamentals + news + crypto | Free-tier key |

On the Anthropic profile, some of these are redundant with native
SDK server-side tools (web search, computer use, code execution),
but the marketplace stays available for both runtimes.

### 7.5 What it composes to

`financial-datasets` answers *"what's Apple's FCF margin?"*.
`financial-services` answers *"now build me a comps table around
it."* Anthropic Agent SDK runs the loop. finterm provides the
chrome, the local fallback, the additional 31 finance tools, the
UI, and the storage.

---

## 8. Design decisions

**Routing**

- **R1.** Two first-class runtimes (local + Anthropic), per-profile.
  Fresh-install bias: local path suggested because it works with no
  keys. No silent failover in either direction.
- **R2.** Other providers (OpenAI, Gemini, Groq, …) remain in
  `LlmService.cpp` as dormant adapters but hidden from the UI
  dropdown. Re-enabling later is a constant flip; the architecture
  takes additional runtimes without refactor.

**Runtime**

- **R3.** Anthropic-profile runtime is the open-source
  `claude-agent-sdk`. We do not wrap it; we configure it.
- **R4.** Local-profile runtime is `finagent_core` slimmed to a
  minimal OpenAI-compatible agent loop. `deepagents` retired (its
  prompt-loop fallback ported into `core_agent.py` first).
  `alpha_arena` migrated to call into the active runtime via a
  "competitive" team mode.
- **R5.** `rdagents` stays as a specialized loop body. Each LLM step
  routes through the active runtime; it does not own a provider
  integration.

**Tools (MCP)**

- **R6.** Three transports, single namespace. Internal `McpProvider`
  (in-process), stdio `McpClient`, HTTP `McpHttpClient`. All feed
  `McpService::list_tools()` with source-prefixed names. HTTP
  supports static bearer / API-key headers and OAuth 2.0 + Dynamic
  Client Registration.
- **R7.** Per-agent tool scoping. Each identity declares
  `allow_tools: ["fd:*", "int:markets.*", "int:portfolio.peek"]`.
  `McpService::list_tools_for(agent_id)` filters before either
  runtime sees the catalogue.
- **R8.** Internal MCP servers implement the **full MCP spec** —
  tools, resources, prompts, sampling, elicitation, progress,
  cancellation, logging. This is what lets the agent read finterm
  state (portfolio snapshot, watchlist, current news digest, active
  thesis) without spending tool-call budget on every read, and what
  enables tools to call the model back, ask the user mid-call, or
  stream progress on long runs.
- **R9.** Data-source hierarchy is per-agent config (ordered list:
  `["fd", "int:edgar", "int:fmp", "int:yfinance"]`). SKILL.md text
  references "your data hierarchy," not specific providers, so
  swapping doesn't touch skills.

**Skills**

- **R10.** Skills as first-class data, not code. Each agent identity
  declares `skills: [...]`. SKILL.md files vendored from
  `anthropics/financial-services` + custom. Same files consumed by
  both runtimes.
- **R11.** Slash commands resolve to `(agent, skill, args)` at the
  chat layer and re-enter the active runtime. No per-command Python.
- **R12.** SKILL.md grammar is Anthropic's verbatim. No fork. Skills
  authored for finterm work in any Anthropic-compatible client
  unchanged.

**Memory, files, embeddings**

- **R13.** Anthropic profile uses SDK-native memory tool + Files
  API. Local profile uses a `MemoryTools` MCP family (we own) over
  sqlite-vec, same API shape. Both expose per-thesis / per-workspace
  / global scopes with provenance.
- **R14.** Embeddings come from `{local_llm_base_url}/embeddings`
  (default `nomic-embed-text` or `bge-m3`) regardless of which
  runtime is active. Voyage AI is a paid fallback when local server
  is unreachable on the Anthropic profile.

**Voice**

- **R15.** STT options: Whisper (local, default, free) and Deepgram
  (paid, opt-in). Google SR removed. TTS not yet wired.

**Storage and outbound**

- **R16.** SecureStorage migration NULLs the plaintext
  `llm_configs.api_key` column at first run. Every read path goes
  through SecureStorage. Column kept for schema stability.
- **R17.** Fincept hosted endpoint feature-flagged off and hidden
  from provider dropdown. No-key fallback is the local profile.

**UI**

- **R18.** "AI Workbench" unifies `chat_mode`, `agent_config`, and
  `mcp_servers` into one screen with left nav: Chat, Agents, Teams,
  Workflows, Tools, Servers, Profiles, System (traces, evals, costs,
  kill-switch). Floating `AiChatBubble` and screen-context actions
  remain fast paths.

**Scheduling**

- **R19.** `AgentService` cron-shaped scheduler (Qt timer +
  SQLite-backed queue). Entries: `(agent_id, skill, args, cron)`. On
  the Anthropic profile, scheduled runs can additionally register
  native SDK hooks for event triggers.

**Evals, observability, safety**

- **R20.** Eval harness in-tree (`scripts/agents/evals/`); vendored
  or interfacing with `financial-datasets/llm-evaluations`. Nightly
  fixture run, results persisted in SQLite, surfaced in Workbench →
  System.
- **R21.** Per-tool / per-agent / per-runtime counters: latency,
  error rate, token cost. Recorded in SQLite. Both runtimes write to
  the same trace + audit log schema.
- **R22.** Kill-switch in Workbench → Servers — one toggle disables
  all destructive tools globally. Per-tool gating via MCP
  elicitation under R8.
- **R23.** Prompt-injection guards on tool output: tool results
  tagged as untrusted; system prompt instructs the model to treat
  tool-content as data, not instructions. Defense-in-depth, not
  perfect.
- **R24.** Per-request budget envelope: max output tokens, max
  iterations, max tool calls, max wall time, max cost-USD. Enforced
  by the active runtime. UI shows running cost.

---

## 9. Implementation tracks

Each track ships with its e2e fixtures and snapshots — see
`plans/testing-strategy.md` for the harness, fixture format, and
per-track fixture inventory. The "iteration loop" becomes
`fix → /review → fix → build → e2e fixture lands → commit`.

### Track 0 — Testing scaffold (prerequisite)

Lands before Track 3 so every subsequent track can ship with
fixtures.

- Fixture YAML schema + parser.
- Headless harness (`evals run`, `evals run-all --parity`).
- Trace assertion library against the unified trace schema (R21).
- OpenAI conformance skeleton (grows alongside the local engine).
- MCP conformance skeleton (grows alongside Track 5).
- Walking-skeleton fixture (`hello_world`) asserting a single chat
  turn produces a complete trace on both runtimes.

### Track 1 — Foundation cleanup

1. SecureStorage migration for LLM keys (R16).
2. Gate Fincept hosted endpoint, hide from dropdown, repoint no-key
   fallback to local (R17).
3. Local Whisper STT, drop Google SR (R15).
4. Wire embeddings to `{local_llm_base_url}/embeddings` (R14).

### Track 2 — Local runtime

5. Settings → LLM Config "Local LLM service" section: editable
   `local_llm_base_url` (suggestion `http://localhost:11434/v1`),
   Probe button, deep link to sibling project.
6. Fresh-install fallback flip — local URL becomes the default
   no-key path; no silent failover.
7. Generalise the Ollama provider entry to "Local
   (OpenAI-compatible)" using `/v1/...`.
8. Slim `finagent_core` to a minimal agent loop; port deepagents'
   prompt-loop fallback into `core_agent.py`; delete
   `scripts/agents/deepagents/`.

### Track 3 — Anthropic runtime

9. Add `claude-agent-sdk` integration in
   `scripts/agents/finagent_core/runtimes/anthropic_runtime.py`.
10. Profile system gains `runtime: anthropic` option; UI exposes
    "Anthropic" as a first-class profile choice (alongside "Local").
11. Wire `McpService::list_tools_for(agent_id)` results into SDK MCP
    registration.
12. Wire SKILL.md loading path into SDK.
13. Bridge SDK streaming events + traces into our chat surface and
    trace schema.

### Track 4 — MCP HTTP transport + marketplace seed

14. Add HTTP/SSE transport to `McpClient` (R6). New `transport_type`
    column on `mcp_servers`. Static auth (bearer / API key) + OAuth
    2.0 + DCR. SecureStorage-backed.
15. Marketplace seed: insert the 9 servers (§7.4) into `mcp_servers`
    at first run.

### Track 5 — Full MCP spec on internal servers

16. Add spec capabilities to `McpProvider` — `resources`, `prompts`,
    `sampling`, `elicitation`, `progress`, `cancellation`,
    `logging`. Wire dispatch, serialization, and capability
    negotiation in the handshake. Then migrate the 31 internal tool
    families:
    - **Resources** on stateful surfaces: `PortfolioTools` →
      `portfolio_snapshot`; `WatchlistTools` → `watchlist`;
      `NewsTools` → `current_digest`; `NotesTools` →
      `active_thesis`.
    - **Prompts** on workflow surfaces: `NotesTools` →
      `daily_brief`; `MarketsTools` → `stock_deep_dive`;
      `GeopoliticsTools` → `sector_pulse`.
    - **Sampling** for tools that need a model call inside, e.g.
      `EdgarTools` filing-summarizer pre-step.
    - **Progress + cancellation** on long-running tools:
      `QuantLabTools` (backtests, RD-Agent), `AgentsTools` (agent
      runs), `EquityResearchTools` (deep-research workflows).
17. Replace per-tool auth hook with MCP elicitation across
    destructive tools (`PaperTradingTools::place_order`,
    `CryptoTradingTools` write paths, `SettingsTools` mutators).

### Track 6 — `FinancialDatasetsTools` internal MCP

18. New `src/mcp/tools/FinancialDatasetsTools.cpp` wrapping
    financial-datasets REST endpoints not in upstream MCP:
    - `get_sec_filing_section` (Item 1A, 7, etc.)
    - `get_insider_trades` (Form 4)
    - `get_institutional_holdings` (13-F)
    - `get_segmented_financials`
    - `get_operational_kpis`
    - `get_earnings`

    Uses `FINANCIAL_DATASETS_API_KEY` env var. Internal — served
    through `McpProvider`.

### Track 7 — Skills + slash commands + agent identities

19. Vendor `financial-services` skills under
    `scripts/agents/finagent_core/skills/<vertical>/<name>/SKILL.md`.
    Preserve NOTICE / Apache-2.0.
20. Register 10 named agents (Pitch, Meeting Prep, Market
    Researcher, Earnings Reviewer, Model Builder, Valuation
    Reviewer, GL Reconciler, Month-End Closer, Statement Auditor,
    KYC Screener) with skill bindings.
21. Slash command dispatch at chat layer: `/comps`, `/dcf`,
    `/earnings`, etc. resolve to `(agent, skill, args)` and re-enter
    the active runtime.
22. Retarget each SKILL.md data-source-priority block from upstream
    partner MCPs to `fd:*` + `int:*`.

### Track 8 — Per-agent tool scoping + namespacing

23. Source-prefixed tool names in `McpService::list_tools()`: `fd:`,
    `int:`, `ext:`.
24. Per-agent `allow_tools` glob patterns;
    `list_tools_for(agent_id)` filters before either runtime sees
    the catalogue.

### Track 9 — Memory + vector store + MemoryTools

25. sqlite-vec as default vector DB (no external service).
26. Per-workspace corpus seeded with notes, watched-ticker filings,
    transcripts (when ingested), recent news.
27. `MemoryTools` MCP family — `memory.search(query, k)`,
    `memory.upsert(doc)`, `memory.list_collections()`. Per-thesis
    / per-workspace / global scopes with provenance. Replaces
    `AgenticMemoryTools` (becomes a thin shim).
28. On the Anthropic profile, expose the SDK memory tool **and**
    `MemoryTools` — user picks one as primary per agent identity.

### Track 10 — Background scheduler + hooks

29. Cron-shaped scheduler in `AgentService` (Qt timer + SQLite
    queue). Entries: `(agent_id, skill, args, cron)`.
30. Scheduler UI in Workbench — list runs, view last output, edit
    cadence, pause/resume.
31. On the Anthropic profile, scheduler entries can additionally
    register SDK hooks for event triggers (news threshold,
    portfolio drift, file changed).

### Track 11 — Quant narrator

32. `QuantNarratorTools.cpp`: `narrate_backtest_result(run_id)`,
    `propose_param_sweep(run_id)`, `narrate_live_trade(trade_id)`.
    Stateless; consumes existing service outputs, returns
    commentary.
33. `quant_critic` agent identity: composed from
    `earnings-analysis` + `model-update` + quant-specific prelude.

### Track 12 — Migrate alpha_arena to two runtimes

34. `AlphaArenaService` keeps its public C++ surface. Internals stop
    importing `agno` directly; per-cycle decisions are turns run by
    the active runtime in a "competitive" team mode.

### Track 13 — AI Workbench (UI consolidation)

35. Single screen with left nav: Chat, Agents, Teams, Workflows,
    Tools, Servers, Profiles, System. Existing `chat_mode`,
    `agent_config`, `mcp_servers` become panels under Workbench.
    `AiChatBubble` and screen-context actions unchanged.
36. System tab: traces, per-tool/per-agent/per-runtime counters,
    evals, cost meter, kill-switch toggle.

### Track 15 — Community surface: open feed + closed rooms

Parallel-shippable; not on the AI-stack critical path.  Current
state: the `forum` screen, `ForumService`, and `ForumTools` MCP
family all point at `AppConfig::api_base_url()` (default
`http://127.0.0.1:8765`).  The bundled local stub server returns
empty envelopes for `/forum/*` so the UI doesn't crash but every
list renders empty.  Originally the forum hit `api.fincept.in`;
that path is gone in the localhost fork.

Replace with two side-by-side surfaces, both deep-link based —
finterm aggregates and links; the actual social platform stays
external.

#### 15a — Open feed via Reddit RSS (+ HackerNews fallback)

- Source: curated finance subreddits — `r/investing`, `r/stocks`,
  `r/options`, `r/SecurityAnalysis`, `r/wallstreetbets`,
  `r/CryptoCurrency`, etc.  User-editable list in settings.
- Transport: `https://www.reddit.com/r/<sub>.rss` (Atom XML).  No
  auth, no API key, no rate-limit issues for RSS.  Separate from
  Reddit's paid API.
- Scope: read-only.  No posting, no voting, no comments — those
  need OAuth.  Click-through opens the post in the browser.
- `ForumService` HTTP+JSON client swapped for an RSS feed reader.
  `ForumTools` MCP family auto-inherits (agents gain
  `forum.search` / `forum.top_posts` against subreddit feeds).
- Universality caveat: Reddit is blocked in CN/RU/KP.  Fallback:
  HackerNews API (`https://hacker-news.firebaseio.com/v0/`,
  unauthenticated) as a secondary feed.  If both fail, show a
  "community feed unavailable in your region" message.

#### 15b — Closed rooms via Discord deep-link (+ Matrix opt-in)

- Primary: **Discord** invite URLs.  finterm stores a user-managed
  list of named Discord server invites; the UI renders them as
  cards with "Open in Discord" buttons that hand off to the
  Discord desktop/web client.  Optional iframe-embed of Discord's
  server widget (`https://discord.com/widget?id=...`) for a
  read-only sidebar view.  Why Discord over Slack: workspace-
  creation friction in Slack is heavy for friends-inviting-friends
  rooms.  Discord's invite-link model matches the use case.
- Secondary: **Matrix / Element** for privacy- or federation-
  minded users.  Same invite-URL pattern (`https://matrix.to/#/...`),
  same deep-link model.  One-line settings toggle to add a Matrix
  card alongside Discord cards.
- Explicitly not embedded: actual chat client (auth, notifications,
  message state) stays in the user's installed Discord/Matrix app.
  finterm is the aggregator + launcher, not a chat client.
- Cross-cutting universality: Discord access varies regionally;
  Matrix federates and works almost everywhere; Telegram could
  serve as a third option for users where neither Discord nor
  Matrix is reachable.

Stages:

1. Read-only RSS reader against `r/investing` — smoke 15a's feed
   parsing and UI render.
2. User-editable subreddit list in settings; multi-feed merge.
3. HackerNews API fallback for blocked regions.
4. `ForumTools` MCP family rewritten on top of the new RSS client.
5. Discord invite-URL list management + "Open" button (15b).
6. Optional Discord widget iframe embed (read-only sidebar).
7. Matrix invite-URL support (15b secondary).

### Track 14 — Evals + observability + audit + safety + UX

37. `scripts/agents/evals/` directory built up across tracks: e2e
    fixtures + parity check + OpenAI conformance + MCP conformance
    + skill-quality bench (vendor or interface with
    `financial-datasets/llm-evaluations`). Nightly run; results in
    SQLite; surfaced in Workbench → System.
38. Unified trace + audit schema across both runtimes. Each turn
    records: prompts, tool calls, tool results, outputs, latency,
    tokens, cost, runtime, model snapshot, citations (where
    supported).
39. Per-tool kill-switch armed via Workbench → System (R22).
    Trading-adjacent tools gate via MCP elicitation under R8 / R17.
40. Prompt-injection guard: untrusted-content tagging on tool output
    strings (R23).
41. Per-request budgets enforced by both runtimes (R24); UI displays
    running cost; cache-hit ratio visible on the Anthropic profile.
42. UX click-through scripts mapped to §13 acceptance criteria
    (A1–A4, B1–B5, C1–C7). Qt Test where automatable; screenshot
    diff for visual regression; manual checklist
    (`tests/ui/acceptance_checklist.md`) for release sign-off.

---

## 10. Sequencing

```
   Track 0 (testing scaffold) ──┐
                                 │
   Track 1 (cleanup) ──┬─► Track 2 (local runtime) ─┐
                       │                             │
                       ├─► Track 3 (Anthropic SDK) ──┤
                       │                             │
                       ├─► Track 4 (MCP HTTP+seed) ──┤
                       │                             │
                       └─► Track 5 (full MCP spec) ──┤
                                                     │
                                Track 6 (fd internal MCP) ──┤
                                Track 7 (skills + slash) ───┤
                                Track 8 (tool scoping) ─────┤
                                                            ▼
                                            Track 9 (memory)
                                            Track 10 (scheduler/hooks)
                                            Track 11 (quant narrator)
                                            Track 12 (alpha-arena migration)
                                                            │
                                                            ▼
                                            Track 13 (AI Workbench)
                                            Track 14 (evals/obs/audit/safety/UX)
```

Concrete order, smallest user-visible wins first; every track ships
with its e2e fixture(s):

- **Week 1.** Track 0 (testing scaffold) + Track 1 items 1–3
  (SecureStorage, gate Fincept, local Whisper). Foundation clean;
  harness exists with a `hello_world` fixture.
- **Week 1–2.** Track 3 (Anthropic SDK; first agent identity runs
  against Claude via SDK; first Anthropic-path fixture lands —
  `comps_aapl.anthropic`).
- **Week 2.** Engine M1 (separate project; see
  `plans/local-ai-engine.md`). Once it's reachable, Track 1 item 4
  + Track 2 (local runtime; first local-path fixture lands —
  `comps_aapl.local`; parity check turns on).
- **Week 2.** Track 4 (MCP HTTP transport + marketplace seed).
- **Week 2–3.** Track 5 (full MCP spec on internal servers; MCP
  conformance suite extended).
- **Week 3.** Track 6 (`FinancialDatasetsTools` internal MCP;
  `insider_nvda`, `13f_brk` fixtures).
- **Week 3–4.** Track 7 (skills vendoring + agent identities + slash
  commands; one fixture per slash command).
- **Week 4.** Track 8 (tool scoping + namespacing).
- **Week 4–5.** Track 9 (memory; `memory_recall`,
  `thesis_continuity`).
- **Week 5.** Track 10 (scheduler + hooks;
  `morning_note_scheduled`).
- **Week 5–6.** Track 11 (quant narrator; `narrate_backtest`,
  `propose_sweep`).
- **Week 6.** Track 12 (alpha-arena migration; `arena_round`).
- **Week 6–7.** Track 13 (AI Workbench; UX click-through scenarios
  begin to land).
- **Week 7–8.** Track 14 (final eval bench, conformance closeout,
  acceptance checklist signed for release).
- **Parallel, any week.** Track 15 (forum → Reddit RSS).  Not on
  the AI critical path; pick up when bandwidth allows.

---

## 11. Open questions

Not yet decided; shape later work:

1. **Primary user clamp.** Individual investor vs PM/analyst vs
   prosumer mix. Affects UI density, onboarding flow, planning
   depth.
2. **Transcript corpus.** Self-serve via local Whisper + user-
   uploaded audio, vs partner (Quartr / SeekingAlpha / Tikr), vs
   build. Without one, "earnings post" workflows are structurally
   blocked.
3. **US-broker connect (read-side first).** Plaid (hosted, monthly
   cost) vs broker-direct (Alpaca, IBKR, unofficial Robinhood).
   Affects portfolio narration accuracy.
4. **Citations on the local profile.** Anthropic citations are
   native; local has none. Tool-result metadata is the floor;
   second-pass "ground" call is an option if quality demands.
5. **Cost ceiling and budget UX.** Per-session / per-day / per-agent
   cost limits on the Anthropic profile. Hard cap vs warning vs both.
6. **Skill drift management.** Refresh `financial-services` upstream
   how often? Manual review of data-source retargeting per refresh?
7. **MCP HTTP OAuth scope.** Which hosted connectors to wire first
   (Gmail, Drive, GitHub, Slack), and when?
8. **Audit log retention + export.** Trades, recommendations, agent
   decisions — how long, in what format, exportable to where?

---

## 12. Risks and mitigations

- **Local-model tool-calling reliability.** 7–14B models sometimes
  produce malformed tool-call JSON. *Mitigation:* prompt-loop
  fallback in the local runtime; per-model strictness toggle.
- **Sibling-project unreachable on local profile.** *Mitigation:*
  banner with sibling-project setup-doc link; no silent failover.
  Symmetric for Anthropic API outage on the Anthropic profile.
- **Anthropic API cost surprises.** Long SKILL.md system prompts ×
  frequent turns × no caching = unbounded bill. *Mitigation:* prompt
  caching automatic via SDK; per-request budgets enforce a ceiling;
  cost meter in UI.
- **Endpoint surface drift across local runtimes.** Ollama, vLLM,
  llama.cpp, LM Studio all claim OpenAI compatibility but diverge on
  edge cases. *Mitigation:* small compatibility shim per runtime if
  needed; tested runtimes documented in sibling project.
- **SecureStorage Linux fallback.** When `libsecret` is absent the
  storage degrades to XOR-obfuscated QSettings. *Mitigation:*
  one-time warning in Settings; do not block.
- **`deepagents` retirement may lose features.** Provider-text-parse
  fallback rides on `deepagents`. *Mitigation:* port the fallback
  into `core_agent.py` first; verify alpha-arena migration on a
  non-tool-calling local model; then delete.
- **Skill drift upstream.** Vendored `financial-services` evolves;
  retargeted data-source hierarchies will drift. *Mitigation:*
  vendored fork; refresh deliberately, not automatically.
- **Eval signal-to-noise.** Small fixture set won't catch long-tail
  regressions. *Mitigation:* start small; expand fixtures against
  known regressions.
- **Anthropic SDK breaking changes.** Adopting the SDK as runtime
  ties us to its release cycle. *Mitigation:* pin SDK version;
  upgrade deliberately; eval-bench gates upgrades.
- **MCP spec evolution.** New capabilities will land. *Mitigation:*
  our internal servers are versioned; capabilities are additive.

---

## 13. Acceptance criteria

Two install flavours must pass.

**Flavour A — Local only** (sibling project running, no Anthropic
key):

- A1. Settings → LLM Config → confirm/edit `local_llm_base_url` →
  Probe → list of models from the sibling project appears.
- A2. AI Chat → ask "what's 2+2" → streamed answer from the local
  model; no outbound traffic (`ss -tnp` verifies).
- A3. Stop the sibling project → reopen Chat → "Local LLM service
  not reachable at {url}" banner with link to sibling-project docs.
  No silent failover.
- A4. `/comps AAPL` → local agent loads `comps-analysis` skill,
  calls `fd:get_income_statements` for AAPL and peers via the
  financial-datasets MCP server, returns a comparable-company table.

**Flavour B — Anthropic** (no sibling project required, Anthropic
API key configured):

- B1. Paste an Anthropic key → set "Anthropic Claude" as the active
  profile.
- B2. AI Chat → ask "what's 2+2" → streamed answer from Claude. No
  probe attempts to `localhost`.
- B3. Revoke the key → reopen Chat → provider auth error surfaced
  clearly. No silent failover.
- B4. `/comps AAPL` → SDK loads `comps-analysis` skill, native MCP
  client calls our internal MCPs + financial-datasets, returns a
  comparable-company table with per-claim citations.
- B5. Long SKILL system prompt is cacheable; second turn shows
  cache-hit token reduction in the cost meter.

**Always-on (both flavours):**

- C1. MCP Servers tab → seeded marketplace list visible → install
  `mcp-server-fetch` → agent can fetch a URL.
- C2. MCP Servers tab → install `financial-datasets/mcp-server` with
  key → agent can call `get_income_statements("AAPL")`.
- C3. Voice settings → Whisper is the default (Google SR removed);
  Deepgram opt-in. Mic → chat input populates; no outbound in
  Whisper mode.
- C4. Workbench → Agents → "Equity Researcher" → `/comps AAPL`
  succeeds end-to-end.
- C5. Workbench → Schedule → "Morning note at 6:30am" → agent runs
  at scheduled time; output appears in Workbench → Chat → Daily.
- C6. Workbench → System → kill-switch armed → agent attempting
  `paper.place_order` is prompted via MCP elicitation; user denies;
  tool returns error to the loop without trade.
- C7. Workbench → System → trace of any turn shows: prompts, tool
  calls, results, outputs, latency, tokens, cost, runtime, model
  snapshot.

---

## Appendix A — Recommended local models

Guidance for the sibling project's README, based on the current dev
box (RTX 5070 Ti / 12 GB VRAM, Intel Ultra 9 285H, 30 GB RAM):

| Role | Suggested model | On-disk | Why |
|---|---|---|---|
| Primary chat + MCP tool calling | Qwen 2.5 14B Instruct (Q4/Q5) | ~9 GB | Best OSS function calling at 14B |
| Fast fallback | Llama 3.1 8B Instruct | ~5 GB | Sub-second TTFT |
| Coding / ReportBuilder | Qwen 2.5 Coder 14B | ~9 GB | Python / report tools |
| Embeddings | `nomic-embed-text` or `bge-m3` | ~280 MB / ~2 GB | Served via `/v1/embeddings` |
| Whisper STT | `faster-whisper` `medium` or `large-v3` | 1.5–3 GB | OpenAI-compatible `/v1/audio/transcriptions` |

## Appendix B — Embeddings on the Anthropic profile

The Anthropic API has no native embeddings endpoint. Both profiles
default to the sibling project's `/embeddings`. If the Anthropic
profile is active and the local server is unreachable, embeddings
fall back to a user-pasted Voyage AI key (Voyage is Anthropic-
acquired and commonly recommended for Claude pipelines).

# AI Stack — Implementation Status

**Status:** Current
**Date:** 2026-05-21
**Owner:** yumima
**Related:** `plans/ai-stack-free-local.md` (target architecture),
`plans/testing-strategy.md` (testing approach), `plans/local-ai-engine.md`
(sibling project), `plans/qt-ffmpeg-hwaccel-memo.md` (unrelated video memo).

The design doc (`ai-stack-free-local.md`) describes the *target* state.
This doc tracks what's *actually shipped* per the §9 implementation
tracks, so a new session can pick up without re-reading every commit.

---

## Track status at a glance

| # | Track | Status | Notes |
|---|---|---|---|
| 0 | Testing scaffold | ✅ done | 58 tests pass; 15 conformance skipped |
| 1.1 | SecureStorage migration for `api_key` columns | ✅ done | v022 migration; LlmSecureKeys helper |
| 1.2 | Fincept hosted endpoint retired | ✅ done | Feature-flagged, hidden, no-key→local |
| 1.3 | Local Whisper STT, drop Google SR | ✅ done | `scripts/voice/whisper_stt.py`; faster-whisper |
| 1.4 | Embeddings → `{local_llm_base_url}/embeddings` | ⏸ deferred | Needs Engine M1 |
| 2 | Local runtime (minimal OpenAI-compat loop) | ⏸ deferred | Needs Engine M1 |
| 3.9 | claude-agent-sdk shell | ✅ done | `finagent_core/runtimes/anthropic_runtime.py` |
| 3.10 | Profile `runtime` field + UI | ✅ done | v023 migration; KNOWN_PROVIDERS trimmed to 2 |
| 3.11 | ToolBridge — McpService → SDK | ✅ done | `mcp_bridge.py` + 6 tests |
| 3.12 | SKILL.md loader | ✅ done | `skills_loader.py`; SDK option wired |
| 3.13 | StreamHandler — typed events | ✅ done | `stream_handler.py` + 5 tests |
| 4.14a | HTTP transport (static auth) | ✅ done | McpClientBase + McpHttpClient; v025 columns |
| 4.14b | OAuth 2.0 + DCR | ⏸ pending | Reserved as `auth_scheme='oauth'`; rejected by McpHttpClient |
| 4.15 | Marketplace seed (9 servers) | ✅ done | v024 migration; `enabled=0` by default |
| 5.A–J | Full MCP spec on internal servers | ✅ done | See "Track 5 detail" below |
| 6 | `FinancialDatasetsTools` internal MCP | ✅ done | 6 tools wrapping REST endpoints; commit `32f1927a` |
| 7 | Skills + slash commands + agent identities | ⚠ partial | 10 named agents seeded (v028, `d51d457a`). Slash resolver `16f07207` + chat wiring `f9183006`. 3 starter SKILL.md files (morning-note, earnings-analysis, comps-analysis) under skills/. Full upstream `financial-services` vendoring (~60 skills) defers; data-source-priority pattern already set in starters. |
| 8 | Source-prefixed names + per-agent allowlists | ✅ done | A `98d256d9` (rename INTERNAL_SERVER_ID→"int"); B `00741864` (allow_tools globs) |
| 9 | Memory + sqlite-vec + MemoryTools | ⏸ pending | Embeddings dependency (Track 1#4 / Engine M1) |
| 10 | Background scheduler + hooks | ⚠ partial | Core ✅. UI ✅ — `SchedulerSection` in Settings: add/remove/toggle/run-now over `agent_schedule`. SDK hook registration on Anthropic profile defers to Track 3 follow-up. |
| 11 | Quant narrator | ✅ done | `c3c95681` — 3 tools + quant_critic agent identity (v026 seed) |
| 12 | Alpha-arena migration to two runtimes | ⚠ partial | Phase 1 ✅ (`finagent_core/__init__.py` PEP 562 lazy). Phase 2 ✅ entry-point: `anthropic_runtime.run_text(prompt, system_prompt=…)` + alpha_arena `BaseAgent._create_anthropic_runtime_llm()` adapter, opt-in via `advanced_config.runtime = "anthropic"`. Local runtime path waits for Engine M1. |
| 13 | AI Workbench (UI consolidation) | ⚠ partial | Min-viable: `AiSystemSection` added to SettingsScreen — surfaces agent_traces (Track 14 #38), spend today (#41), tool kill-switch list with disable/enable (#39). Full left-nav consolidation (Chat / Agents / Teams / Workflows / Tools / Servers / Profiles / System on a single screen) defers. |
| 14 | Evals + observability + audit + safety + UX | ✅ done | #37 ✅ (eval `result_store.py` jsonl, runner wired). #38 ✅. #39 ✅. #40 ✅. #41 ✅. #42 ✅ (`tests/ui/acceptance_checklist.md` mapping A1-A4 / B1-B5 / C1-C7 + safety spot-checks). Runtime-side in-flight token budgets defer to Engine M1. |
| 15 | Forum → Reddit RSS + Discord deep-link | 📋 filed | Off the AI critical path |

---

## Track 5 detail (full MCP spec on internal servers)

| Commit | Hash | Surface |
|---|---|---|
| A | `d2167bdc` | Resources API on McpProvider (struct, register, list, read) |
| B | `54236b37` | First resource: `finterm://portfolio/snapshot` |
| C | `2447805e` | More resources: `watchlist/all`, `news/digest`, `notes/active_thesis` |
| D | `1033e5b7` | Wire-level: `resources/list` + `resources/read` on McpClientBase + stdio + HTTP |
| E | `ce053c9e` | Python `mcp_bridge.py` forwards resources to SDK MCP server (via lowlevel `helper_types.ReadResourceContents`) |
| F | `7de4c285` | Prompts API on McpProvider; `daily_brief` prompt |
| G | `cb2ce5ef` | Progress emission in `dispatch_quant_async` (primitives already existed in ToolContext) |
| H | `6c055f3f` | Logging primitives on ToolContext; default sink forwards to core Logger |
| I | `b9230565` | Sampling + elicitation primitives on ToolContext |
| J | `a0f75082` | Auth gate via `ctx.elicit`, AuthChecker fallback |

### Track 5 follow-ups (deferred)

- Wire-level `prompts/list`, `prompts/get`, sampling, elicitation, logging
  notifications on McpClientBase (mirror of commit D for those capabilities).
- Python mcp_bridge forwards prompts/sampling/elicitation/logging to the
  SDK MCP server (mirror of commit E for those).
- Production AgentService wires `ctx.on_sample`, `ctx.on_elicit`,
  `ctx.on_log` to the chat UI and active LLM profile.
- More tool family migrations for progress emission
  (AgentsTools, EquityResearchTools).
- McpManager aggregator `get_all_external_resources()` for cross-server reads.

---

## Schema migrations applied this session

| # | Name | Purpose |
|---|---|---|
| v022 | `llm_secure_keys` | Move plaintext `api_key` columns into SecureStorage |
| v023 | `llm_profile_runtime` | `runtime` column on `llm_profiles` (anthropic / local / external) |
| v024 | `mcp_marketplace_seed` | Insert 9 marketplace MCP server rows (`enabled=0`) |
| v025 | `mcp_http_transport` | `transport_type` + `base_url` + `auth_scheme` + `auth_header` columns on `mcp_servers` |
| v026 | `quant_critic_agent` | Seed `agent_configs` row for quant_critic identity |
| v027 | `agent_schedule` | Table for `AgentScheduler` cron entries (Track 10 core) |
| v028 | `named_agents` | Seeds 10 named agent identities (Track 7 #20) |
| v029 | `agent_traces` | Unified trace + audit schema (Track 14 #38) |
| v030 | `tool_killswitch` | Per-tool kill-switch (Track 14 #39 / R22) |
| v031 | `untrusted_directive` | Prepend `<untrusted>`-aware preamble to every agent's system_prompt (Track 14 #40 / R23) |

Future migrations should start at **v032**.

---

## Key code locations

### finterm (C++ Qt6)
- `fincept-qt/src/mcp/McpProvider.{h,cpp}` — internal tool/resource/prompt registry
- `fincept-qt/src/mcp/McpClientBase.h` — abstract base for transport polymorphism
- `fincept-qt/src/mcp/McpClient.{h,cpp}` — stdio JSON-RPC client
- `fincept-qt/src/mcp/McpHttpClient.{h,cpp}` — HTTP JSON-RPC client (static auth)
- `fincept-qt/src/mcp/McpManager.{h,cpp}` — transport dispatch + lifecycle
- `fincept-qt/src/mcp/McpTypes.h` — `Resource`, `Prompt`, `LogLevel`, `SampleRequest/Response`, `ElicitRequest/Response`, `ToolContext`
- `fincept-qt/src/mcp/tools/*.cpp` — 32 tool families (was 31, +FinancialDatasetsTools); resources on Portfolio/Watchlist/News/Notes; prompts on Notes
- `fincept-qt/src/mcp/tools/FinancialDatasetsTools.{h,cpp}` — Track 6; 6 REST-only tools; API key in SecureStorage under `mcp.tools.financial-datasets`
- `fincept-qt/src/storage/secure/LlmSecureKeys.{h,cpp}` — naming convention for SecureStorage
- `fincept-qt/src/storage/repositories/LlmProfileRepository.{h,cpp}` — profile CRUD with `runtime` field
- `fincept-qt/src/screens/dashboard/widgets/VideoPlayerWidget.{h,cpp}` — pause/resume + live-edge restart
- `fincept-qt/src/services/agents/AgentScheduler.{h,cpp}` — Track 10 cron scheduler (`@daily`, `@every`); pure `parse_cron`/`is_due` for tests
- `fincept-qt/src/services/agents/SlashCommandService.{h,cpp}` — Track 7 #21 resolver (`/comps AAPL` → agent + skill + args); registry of 13 slash commands
- `fincept-qt/src/storage/repositories/AgentTraceRepository.{h,cpp}` — Track 14 #38 create/finish/list_recent over `agent_traces` (v029)
- `fincept-qt/src/storage/repositories/ToolKillswitchRepository.{h,cpp}` — Track 14 #39 disabled-set / disable / enable over `tool_killswitch` (v030); `McpService::execute_tool` gates on this
- `fincept-qt/src/mcp/UntrustedContent.{h,cpp}` — Track 14 #40 prompt-injection guard; wraps text with `<untrusted>` markers and entity-encodes `<`/`>` to prevent escape; applied to NewsTools + ForumTools
- `fincept-qt/src/services/agents/BudgetService.{h,cpp}` — Track 14 #41 per-agent daily USD cap; reads from `agent_traces.cost_usd`; `check_dispatch` runs ahead of every `AgentService::run_agent` call
- `fincept-qt/src/screens/settings/AiSystemSection.{h,cpp}` — Track 13 min-viable: AI System tab under Settings; surfaces traces / spend / kill-switch with disable/enable
- `fincept-qt/scripts/agents/finagent_core/skills/<vertical>/<name>/SKILL.md` — starter methodology library; 3 skills shipped (morning-note, earnings-analysis, comps-analysis); upstream vendoring deferred
- `fincept-qt/src/screens/settings/LlmConfigSection.cpp` — providers dropdown (anthropic + ollama only)
- `fincept-qt/src/screens/settings/VoiceConfigSection.cpp` — Whisper + Deepgram only
- `fincept-qt/src/services/stt/SpeechService.cpp` — WhisperSttProvider (replaces Google SR)
- `fincept-qt/src/ai_chat/LlmService.{h,cpp}` — `runtime_for_provider` helper; Fincept gated; legacy provider hidden

### Python (finagent_core)
- `fincept-qt/scripts/agents/finagent_core/runtimes/anthropic_runtime.py` — Claude Agent SDK wrapper
- `fincept-qt/scripts/agents/finagent_core/runtimes/mcp_bridge.py` — ToolBridge + resources → SDK
- `fincept-qt/scripts/agents/finagent_core/runtimes/stream_handler.py` — typed event sink
- `fincept-qt/scripts/agents/finagent_core/runtimes/skills_loader.py` — SKILL.md discovery
- `fincept-qt/scripts/voice/whisper_stt.py` — local STT

### Eval harness
- `fincept-qt/scripts/agents/evals/runner.py` — argparse CLI
- `fincept-qt/scripts/agents/evals/fixture.py` — YAML fixture loader
- `fincept-qt/scripts/agents/evals/trace.py` — unified trace + assertions
- `fincept-qt/scripts/agents/evals/runtime_adapter.py` — Runtime enum + dispatch
- `fincept-qt/scripts/agents/evals/parity.py` — cross-runtime parity check
- `fincept-qt/scripts/agents/evals/conformance/openai/` + `mcp/` — skeleton suites
- `fincept-qt/scripts/agents/evals/e2e/hello_world/fixture.yaml` — walking-skeleton fixture

### Tests
- 58 tests pass (5 conformance suites in `evals/conformance/*` — 15 tests — marked skip pending Track 2/5 wire impl)
- Run from `fincept-qt/scripts/agents/`:
  `/tmp/evals-venv/bin/pytest evals/`
- venv at `/tmp/evals-venv` has `claude-agent-sdk`, `agno`, `pyyaml`, `pytest`, `pydantic`, `mcp`

---

## Open questions (from design doc §11)

Still unresolved; flagged for later:

1. Primary user clamp (individual vs PM/analyst mix)
2. Transcript corpus strategy
3. US-broker connect (Plaid vs broker-direct)
4. Citations on the local profile
5. Cost ceiling / budget UX
6. Skill drift management cadence
7. MCP HTTP OAuth scope (Track 4 #14b)
8. Audit log retention + export

---

## Manual verification checklist for the work shipped

When picking back up, smoke-test:

- App starts cleanly; v022 → v025 migrations apply once.
- Settings → MCP Servers shows the 9 seeded marketplace rows, all
  disabled. Editing args/env then enabling fetches the upstream
  package.
- Settings → LLM Config dropdown shows Anthropic + Local
  (`ollama`) only; the 8 other adapters are absent from the Add
  dialog but legacy rows (if any) still appear in the saved list.
- Settings → Voice shows Whisper (default) + Deepgram; no Google.
- Pause/play on a live stream (e.g. YouTube live):
    - Short pause (<5s): audio resumes immediately, no freeze
    - Long pause (>5s on `127.0.0.1` proxy URL): restarts at live
      edge after ~1-2s rebuffer, audio present
    - VOD (non-proxy URL): cheap path always, cursor preserved
- Lock terminal during playback, unlock: auto-resume if still
  paused; otherwise leave alone.
- AI Chat ask "what's 2+2" with Anthropic profile (requires
  ANTHROPIC_API_KEY env + `claude` CLI on PATH): streamed answer
  from Claude.
- `python -m evals.runner run e2e/hello_world --runtime=anthropic`
  from `scripts/agents/` exits 0 with `[ok]` (if Claude CLI
  authenticated) or 2 `[skip]` otherwise.

---

## How to resume

A new session should:

1. Read `plans/ai-stack-free-local.md` for target architecture.
2. Read this file (`plans/implementation-status.md`) for what's done.
3. Read `plans/testing-strategy.md` for the test discipline.
4. Check `git log --oneline -40` to see the recent commit history.
5. Pick the next track from the "Track status at a glance" table —
   recommended order: **Track 6** (in progress) → **Track 8** (small
   architecture) → **Track 7** (skill vendoring, ~content-heavy) →
   **Engine M1** (sibling project, unblocks Track 1#4 + Track 2)
   → **Track 4 #14b** (OAuth+DCR) → **Track 9–14**.

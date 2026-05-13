# AI Stack — Dual-Path (Local + External) Wiring Plan

**Status:** Draft — decisions resolved 2026-05-12
**Owner:** yumima
**Date:** 2026-05-12
**Canonical tree:** `/home/yma/fin/finterm/fincept-qt/`
**Related:** `docs/datahub-phases/phase-09-ai-mcp-agents.md`, `docs/agents/datahub-guide.md`

---

## Goal

Finish wiring the AI / LLM / agent / MCP features that already exist in
the codebase so the terminal supports **two first-class paths**, picked
per-user, with both fully functional:

- **Local path** — point at a sibling LLM-services project running on
  `localhost`, OpenAI-compatible. Zero outbound calls, zero commercial
  keys.
- **External path** — paste any commercial / free-tier provider key
  (OpenAI, Anthropic, Gemini, Groq, Cerebras, Mistral, OpenRouter,
  DeepSeek, etc.). The existing multi-provider `LlmService` already
  handles all of these; we make sure the UX makes it obvious.

Neither path is "the default" architecturally — the user chooses via
profile. The only place a default appears is on a brand-new install
with *nothing* configured, where we suggest the local path because it
needs no keys; the user is free to ignore that and paste an external
key instead.

Concretely, after this plan, three flavours of install all work:

1. **Local only** — sibling project running, no commercial keys
   pasted. Chat / agents / mic all work, zero outbound.
2. **External only** — no sibling project running, one or more
   commercial / free-tier keys pasted. Chat / agents / mic all work,
   routed to whichever provider the user picked. STT (Whisper) still
   runs locally regardless.
3. **Mixed** — both configured. The profile system routes per role
   (e.g. fast chat → Groq, deep reasoning → local Qwen, embeddings →
   Gemini free). The user controls this from Settings → LLM Config.

## Non-goals

- Replacing the existing multi-provider `LlmService` design — it
  already handles both local and external; we are *configuring* it,
  not rewriting it.
- Forcing users onto one path or the other; finterm must not silently
  failover between local and external.
- Self-hosting embeddings model training or fine-tuning.
- Building a new MCP transport from scratch (we will *bridge* HTTP MCP
  servers, not rewrite `McpClient`).
- Touching the DataHub migration scoped to phase-09.

---

## Current state (audit)

Wired (works today, just needs config):

- `src/ai_chat/LlmService.{h,cpp}` — multi-provider chat + SSE streaming
  + OpenAI-style tool calling. 11 providers including **Ollama**
  (`http://localhost:11434`).
- `src/ai_chat/ModelCatalog.cpp` — output-token caps, last verified
  2026-04-28.
- `src/mcp/{McpService,McpManager,McpClient}.{h,cpp}` — stdio-only
  JSON-RPC 2.0; 30+ internal tools registered.
- `src/screens/mcp_servers/McpServersScreen.{h,cpp}` — marketplace and
  installed-server UI; relies on `mcp_servers` SQLite table.
- `src/services/stt/SpeechService.{h,cpp}` + `scripts/voice/` — Google
  SR (free) and Deepgram (paid) backends.
- `src/storage/repositories/{LlmConfigRepository,LlmProfileRepository}` —
  profile system (global / context / agent / team).

Partial:

- `src/services/agents/AgentService.{h,cpp}` — Python boundary works,
  some tool stubs are placeholders.
- `scripts/agents/deepagents/` — LangChain/LangGraph scaffold; tool
  hookups TODO.
- `scripts/agents/finagent_core/` — registries (models, embeddings,
  vector DBs) present; team workflows untested.

Needs decision before more wiring:

- **Key storage:** LLM keys live in plaintext `llm_configs.api_key`.
  `src/storage/secure/SecureStorage.cpp` (OS keychain, libsecret on
  Linux, XOR-obfuscated QSettings fallback) exists but is not used for
  LLM keys.
- **Fincept hosted endpoint** in `LlmService::fincept_async_request`
  posts to `fincept.in`. Violates the localhost-only fork rule.
- `McpClient` is **stdio only** — no HTTP/SSE transport.

---

## Plan

The plan has four tracks. Tracks 1, 2, 3 are independent and can land in
any order. Track 4 is cleanup that should land before the others ship to
users.

### Track 1 — Local LLM endpoint (managed by a separate project)

**Decoupling decision (2026-05-12):** the local LLM runtime — Ollama,
llama.cpp, vLLM, or whatever — is **out of scope for finterm** and will
live in a separate project that also serves non-finterm clients.
Finterm becomes a thin *client* of a localhost OpenAI-compatible
endpoint. We do not install, supervise, or update the runtime.

This shifts Track 1 from "ship an installer" to "detect and route to a
configured endpoint."

**Work items:**

1. **No install helper.** Settings → LLM Config gets a "Local LLM
   service" section with:
   - A `local_llm_base_url` field (default suggestion
     `http://localhost:11434/v1`, fully user-editable — vLLM users will
     point at `:8000/v1`, llama.cpp users at `:8080/v1`, etc.).
   - A "Probe" button that hits `GET {base_url}/models` and lists
     returned model IDs. Generic across any OpenAI-compatible runtime,
     not Ollama-specific.
   - A "Get started →" deep link to the sibling project's README.
2. **Fresh-install default (suggestion, not coercion).** In
   `LlmService.cpp` / `LlmConfigSection.cpp`, change the "no profile
   configured at all" fallback from Fincept → `local_llm_base_url`.
   Scope this narrowly: it applies *only* when the user has neither
   configured a local URL nor pasted any external key. The moment the
   user sets either, the active profile is whatever they chose —
   finterm never silently overrides. If the user is on a configured
   *local* profile and the probe fails, surface a "Local LLM service
   not reachable at {url} — see <link>" banner; if the user is on a
   configured *external* profile, finterm does not touch local at
   all.
3. **Generalise the Ollama provider entry.** The existing "Ollama"
   choice in `LlmService.cpp` is hardcoded for `:11434/api/...`. Either
   rename it to "Local (OpenAI-compatible)" and switch to the `/v1/...`
   path (cleaner, works for any local runtime), or add a second entry
   so both coexist. The OpenAI-compatible path is preferred long-term
   because the sibling project may not be Ollama.
4. **Tool-calling reliability flag.** Some local models produce
   malformed tool-call JSON. Keep `scripts/agents/deepagents/orchestrator.py`
   (prompt-loop fallback) as the default agent path when the active
   profile points at a local endpoint, with a "strict tool calling"
   opt-in toggle for users who trust their local model.

**Acceptance:** Both paths verified independently:
- *Local-only:* sibling project running, no commercial keys → Probe
  succeeds → Chat streams from local model.
- *External-only:* no sibling project running, single Anthropic /
  OpenAI / Gemini / Groq key pasted → Chat streams from that provider,
  no probe banner shown, no attempt to reach localhost.
- *Mixed:* both configured → profile routing works as the user set it
  (e.g. chat → Groq, agent → local Qwen).

**Appendix — recommended models (not finterm's job to install).** This
is guidance for the sibling project, based on the current dev box (RTX
5070 Ti / 12 GB VRAM, Intel Ultra 9 285H, 30 GB RAM):

| Role | Suggested model | On-disk | Why |
|---|---|---|---|
| Primary chat + MCP tool calling | Qwen 2.5 14B Instruct (Q4/Q5) | ~9 GB | Best OSS function calling at 14B |
| Fast fallback | Llama 3.1 8B Instruct | ~5 GB | Sub-second TTFT on the 5070 Ti |
| Coding / ReportBuilder | Qwen 2.5 Coder 14B | ~9 GB | For Python/report tools |
| Embeddings | nomic-embed-text or bge-m3 | ~280 MB / ~2 GB | Served via `/v1/embeddings` |
| Whisper (if STT also migrates) | faster-whisper `medium` or `large-v3` | 1.5–3 GB | OpenAI-compatible `/v1/audio/transcriptions` |

### Track 2 — Free cloud provider keys (optional quality boost)

Bring-your-own free keys. The profile system already supports per-role
routing.

| Provider | Free tier (as of 2026-01 cutoff — verify at wire time) | Recommended role |
|---|---|---|
| Google Gemini (AI Studio) | Generous RPM/RPD on Gemini 2.x Flash / Flash-Lite; free `text-embedding-004` | Agent reasoning, embeddings |
| Groq | High tokens/sec on Llama 3.x, Mixtral, Qwen | "Fast chat" lane |
| Cerebras | Free tier, extremely fast | Alt fast lane |
| Mistral La Plateforme | Free experimental tier | Optional |
| OpenRouter | `:free` tagged models (DeepSeek V3 free, Llama 3 free, etc.) | Catch-all |
| HuggingFace Inference | Rate-limited free | Niche models |
| Anthropic / OpenAI | No real free API tier — signup credits only | Do not plan around |

**Work items:**

1. Settings → LLM Config: surface a "Free providers" group with one-line
   "Get a free key →" deep links for Gemini, Groq, OpenRouter,
   Cerebras, Mistral.
2. Seed the default profile assignment so that **if** the user pastes a
   Gemini key, embeddings auto-route to `text-embedding-004` (free) and
   chat to `gemini-2.x-flash`.
3. No code changes to `LlmService.cpp` — all five providers are already
   in the multi-provider switch.

**Acceptance:** User pastes a Gemini key → embeddings + chat auto-route
without touching profile UI.

### Track 3 — External MCP servers (financial-datasets + free set)

Problem: `McpClient` is stdio only; `financial-datasets` is HTTP
(`https://mcp.financialdatasets.ai`).

**Two options.** Land A first; consider B if we adopt many HTTP MCP
servers.

**Option A (recommended first): bridge HTTP → stdio with `mcp-remote`.**

Add a row to the marketplace seed list:

```
name:    financial-datasets
command: npx
args:    -y mcp-remote https://mcp.financialdatasets.ai
env:     FINANCIAL_DATASETS_API_KEY=...
```

No code change in finterm. Requires Node/npx on PATH (already needed for
other MCP servers).

**Option B (later): add HTTP/SSE transport to `McpClient`.**

- Sibling `McpHttpClient` class implementing the same JSON-RPC 2.0
  surface against streamable HTTP / SSE.
- New column `transport_type` on `mcp_servers` (default `stdio`).
- Factory in `McpManager` picks the transport.
- ~1 day of work; pays off once we want >2 remote HTTP MCPs.

**Marketplace seed list** (all free, all stdio or bridge-via-mcp-remote):

| Server | Purpose | Key needed? |
|---|---|---|
| `mcp-server-fetch` | Generic URL fetch | No |
| `mcp-server-filesystem` | Sandboxed file ops | No |
| `mcp-server-sqlite` | Local DB queries | No |
| `mcp-server-time` | Time / timezone | No |
| `mcp-server-sequentialthinking` | Reasoning aid | No |
| `mcp-server-brave-search` | Web search | Free Brave key |
| `mcp-server-playwright` | Browser automation | No |
| `yfinance-mcp` (community) | Free quotes/fundamentals | No |
| `financial-datasets` (via mcp-remote) | Fundamentals, insiders, etc. | Free tier key |

**Impact:** Fills the data-tool gap that's currently blocking
`FinAgent Core` from leaving "partial" status.

**Acceptance:** Open MCP Servers screen → seeded list visible → install
`financial-datasets` → agent can call `get_company_facts` etc.

### Track 4 — Cleanup / pre-ship

These should land before tracks 1–3 are exposed to end users.

1. **SecureStorage migration.** Move LLM keys out of the plaintext
   `llm_configs.api_key` column into `SecureStorage` (`src/storage/secure/SecureStorage.cpp`).
   Migration step: on first run, read existing rows, write to keychain,
   zero the column. Schema kept; only the read path changes.
2. **Gate / stub the Fincept hosted endpoint.** Per the localhost-only
   fork rule. Either:
   - Feature-flag `LlmService::fincept_async_request` off by default and
     hide the "Fincept" provider from the dropdown, or
   - Repoint at a local stub if one exists.
   Either way the *default no-key* fallback moves from Fincept → Ollama
   (Track 1 item 2).
3. **Local STT backend (Whisper) + drop Google SR.** In
   `src/screens/settings/VoiceConfigSection.cpp`:
   - Add `scripts/voice/whisper_stt.py` using `faster-whisper`
     (`medium` default, `large-v3` opt-in). Same JSON-lines stdout
     protocol as the existing backends.
   - **Remove the Google SR backend.** It makes an outbound call to
     Google's speech endpoint, violating the localhost-only fork rule;
     keeping it is more maintenance surface for no architectural gain.
   - Resulting two backends: Whisper (free, local, default) and
     Deepgram (paid cloud, opt-in).
   - Decision: Whisper stays *in finterm* for v1 so a fresh install has
     working STT with no external project dependency. Design the
     `SpeechService` strategy interface so a future HTTP backend
     (POST audio chunks → `{local_llm_base_url}/audio/transcriptions`,
     OpenAI Whisper API shape) can slot in if/when the sibling project
     starts serving STT.
4. **Embeddings via OpenAI-compatible endpoint.** Wire
   `scripts/agents/finagent_core/vectordb_registry.py` to call
   `{local_llm_base_url}/embeddings` (default
   `http://localhost:11434/v1/embeddings`) as the default embeddings
   provider. The model name (`nomic-embed-text`, `bge-m3`, etc.) is
   whatever the sibling project serves — finterm just configures the
   URL + model name. Keep `text-embedding-004` (Gemini free) as a
   cloud option for users without the local stack.

---

## Sequencing

```
Track 4 (Cleanup) ─┬─► Track 1 (Ollama)  ─┐
                   │                       ├─► Acceptance demo
                   ├─► Track 3A (bridge)  ─┤
                   │                       │
                   └─► Track 2 (free keys)─┘
```

Concrete order, smallest user-visible wins first:

1. **Day 0** — Cleanup #2 (Fincept gate) + Cleanup #1 (SecureStorage
   migration, plaintext column wiped). Foundational; everything else
   assumes these.
2. **Day 1** — Track 1: `local_llm_base_url` setting + Probe button +
   default-fallback flip + generalise the Ollama provider entry to
   OpenAI-compatible `/v1`. With the sibling project running, user can
   chat with no API key.
3. **Day 2** — Track 3A: seed marketplace with the free-server list +
   `financial-datasets` bridge row. Agents gain web/fs/search/finance
   tools.
4. **Day 3** — Cleanup #3 (Whisper STT, drop Google SR) + Cleanup #4
   (embeddings via `{local_llm_base_url}/embeddings`).
5. **Day 4** — Track 2: free-provider deep links + auto-routing.
6. **Later** — Track 3B (native HTTP transport with static
   Bearer/header auth, no OAuth) if Track 3A is judged too brittle in
   practice.

---

## Risks & mitigations

- **Local-model tool-calling reliability.** 7–14B models sometimes
  produce malformed tool-call JSON. *Mitigation:* when the active
  profile points at `local_llm_base_url`, default to the
  `orchestrator.py` prompt-loop path; expose a "strict tool calling"
  opt-in toggle. Re-evaluate per model.
- **Sibling-project unreachable when active profile is local.** If
  the user's active profile points at `local_llm_base_url` and the
  service is down, requests will fail. *Mitigation:* show a banner
  pointing at the sibling project's setup docs and **do not silently
  fail over to any other provider**. If the user wants a fallback to
  an external provider on local outage, that's an explicit
  configuration choice in the profile UI, not an implicit behavior.
  This rule is symmetric: if the user's active profile points at an
  external provider and that provider 5xx's, we do not silently fall
  back to local either.
- **Endpoint surface drift.** Different local runtimes (Ollama, vLLM,
  llama.cpp, LM Studio) all *claim* OpenAI compatibility but diverge on
  edge cases (streaming chunk format, tool-call deltas, embeddings
  response shape). *Mitigation:* keep a small compatibility shim per
  runtime if needed; document tested runtimes in the sibling project's
  README, not here.
- **`mcp-remote` bridge brittleness.** Process-per-server adds latency
  and a fail mode (npx not on PATH). *Mitigation:* health-check in
  `McpManager::start`; if `npx` missing, surface a clear error in the
  MCP Servers screen instead of a silent dead row.
- **Free tier drift.** Free quotas/models change. *Mitigation:* the
  deep links in Track 2 point at the provider's own pricing/quota page,
  not at hardcoded numbers in our UI.
- **SecureStorage Linux fallback.** When `libsecret` is absent the
  storage degrades to XOR-obfuscated QSettings (file header is honest
  about this). *Mitigation:* surface a one-time warning in Settings
  when on the fallback; do not block.
- **Fincept endpoint reachability tests.** Any CI / dev tests currently
  pointing at fincept.in will break once gated. *Mitigation:* audit
  during Cleanup #2 and either remove or repoint at a local stub.

---

## Acceptance criteria

Three install flavours must all pass.

**Flavour A — Local only** (sibling project running, no commercial
keys configured):

A1. Settings → LLM Config → confirm/edit `local_llm_base_url` → click
    Probe → list of models from the sibling project appears.
A2. AI Chat → ask "what's 2+2" → streamed answer from the local model;
    no network calls leaving the box (verify with `ss -tnp` during
    reply).
A3. Stop the sibling project → reopen Chat → see "Local LLM service
    not reachable at {url}" banner with a link to the sibling
    project's docs. No silent failover.

**Flavour B — External only** (no sibling project running, one
commercial / free-tier key pasted):

B1. Settings → LLM Config → paste an Anthropic / OpenAI / Gemini /
    Groq key → set as active profile.
B2. AI Chat → ask "what's 2+2" → streamed answer from that provider.
    No probe attempts to `localhost`, no "local unreachable" banner.
B3. Revoke the key on the provider's dashboard → reopen Chat → see a
    provider-specific auth error surfaced clearly. No silent failover
    to local.

**Flavour C — Mixed** (both configured):

C1. Settings → LLM Config → both local URL and an external key
    present; profile assignments say chat → Groq, agent → local Qwen,
    embeddings → Gemini free.
C2. Send a chat message → routed to Groq (verify in network panel /
    logs).
C3. Run an agent → reasoning calls hit local Qwen.
C4. Trigger a RAG-backed query → embeddings call hits Gemini.

**Always-on (all flavours):**

D1. MCP Servers screen → seeded free list visible → install
    `mcp-server-fetch` → agent can fetch a URL.
D2. Voice settings → Whisper is the default (Google SR removed);
    Deepgram is opt-in. Mic → chat input populates, no outbound
    traffic in Whisper mode.

Out of scope but tracked: agent end-to-end workflows (FinAgent Core
team execution). Those resolve once embeddings + financial-datasets
land, but full coverage is its own task.

---

## Decisions (resolved 2026-05-12)

1. **Two first-class paths, picked by the user.** Local LLM (via a
   sibling project) and external API keys are both fully supported
   and equally valid. Neither is "the" default architecturally — the
   user picks via the existing profile system, and finterm respects
   that choice without silent failover in either direction. The only
   bias is on a brand-new install with *no* config at all, where we
   suggest the local path as a starting point because it works with
   zero keys; the user is free to ignore that and paste an external
   key instead.

1a. **Local LLM runtime: link-only, owned by a sibling project.**
    Finterm does *not* install, supervise, or update Ollama / vLLM /
    llama.cpp. Finterm holds a configurable `local_llm_base_url` and
    talks OpenAI-compatible HTTP to it. The sibling project is also
    intended to serve non-finterm clients, so this avoids duplication
    and gives the runtime room to evolve independently. Implication:
    the install helper is dropped from Track 1; the model
    recommendations move to an appendix as guidance for the sibling
    project's authors.

2. **Drop Google SR.** Once Whisper local lands, the STT options are
   Whisper (free, local, default) and Deepgram (paid, opt-in). Google
   SR makes an outbound, unauthenticated call to Google's speech
   endpoint and conflicts with the localhost-only fork rule; keeping
   it would mean three backends to maintain for no architectural gain.
   Trade-off accepted: Whisper needs a one-time model download
   (~1.5 GB for `medium`).

3. **Whisper stays in finterm for v1.** Self-contained STT means a
   fresh install has a working mic without depending on the sibling
   project being up. `SpeechService` should expose a strategy
   interface so a future HTTP backend (POST audio →
   `{local_llm_base_url}/audio/transcriptions`) can slot in if/when
   the sibling project starts serving STT.

4. **SecureStorage migration wipes the plaintext column.** On first
   run after the migration, move each `llm_configs.api_key` to the
   keychain and NULL the column (keep the column for schema
   stability). Reasons: this is a personal fork — downgrade isn't a
   real constraint; leaving plaintext defeats the migration's purpose
   since any read path still exposes keys; zeroing forces every code
   path through `SecureStorage`, eliminating a "forgot to migrate this
   read" bug class.

5. **Track 3B native HTTP transport supports static auth, not OAuth.**
   Implement `Authorization: Bearer …` and arbitrary custom headers
   (`X-API-Key: …` etc.). Skip OAuth — the MCP OAuth spec is still
   moving and `financial-datasets` plus most current HTTP MCPs do not
   need it. Auth config stored alongside the server row; the secret
   value pulled from `SecureStorage` at request time. Revisit when a
   target HTTP MCP actually requires OAuth.

## Implications of the decoupled-LLM decision

- **Embeddings story is the same shape as chat.** Both flow through
  `{local_llm_base_url}/...`. No special-case code path for "local
  embeddings."
- **No more "Detect local Ollama" UX.** Replaced by a runtime-agnostic
  "Probe `/models`" against whatever URL the user configured.
- **Ollama provider entry in `LlmService.cpp` is now legacy.** Prefer
  the OpenAI-compatible path going forward; the sibling project may
  not be Ollama. Keep or rename, but route both at `/v1/...`.
- **`scripts/setup/install_ollama.sh` is not built.** Remove the line
  item from any earlier draft TODOs.
- **The sibling project's README is on the user's critical path.** The
  "Local LLM service not reachable" banner in finterm must link to
  something real before this design ships to anyone but the developer.

# AI features — configure and use

finterm's AI surface runs on two runtimes side-by-side: **Anthropic
Claude** (via `claude-agent-sdk`) and **a local OpenAI-compatible
server** (Ollama, vLLM, llama.cpp, LM Studio, …). Pick whichever
fits the moment — there's no silent failover, you choose which one
is active.

## 1. Pick a runtime

### Anthropic (Claude)

1. Get an API key from <https://console.anthropic.com>.
2. **Settings → LLM Config** → provider dropdown → **Anthropic**.
3. Paste the key.  It lands in the OS keychain — never on disk in
   plain text.
4. Set this as the active profile.

### Local (hearth — default local engine)

With **no LLM provider configured**, finterm targets the local
**hearth** engine at `http://127.0.0.1:11435/v1` by default (the same
base its embeddings + semantic-memory use — one engine for both). Just
run it:

```bash
# hearth (https://github.com/yumima/hearth) — loopback :11435
hearth start          # brings up the bundled Ollama + the gateway
```

Chat then works with zero config (it addresses the `primary_chat`
role; hearth picks the model). Override the base with
`FINCEPT_ENGINE_BASE_URL` or in **Settings → LLM Config**.

**Bare Ollama (no hearth)?** Ollama defaults to `localhost:11434`,
not hearth's `:11435` — so set the base URL explicitly:

1. `ollama pull qwen2.5:14b-instruct && ollama serve`
2. **Settings → LLM Config** → provider **Ollama** → set base URL to
   `http://localhost:11434/v1` → **Probe** → pick a model → set active.

Other OpenAI-compat servers work the same way — change the base
URL.  Defaults: vLLM (`http://localhost:8000/v1`), LM Studio
(`http://localhost:1234/v1`), llama.cpp (`http://localhost:8080/v1`).

### No silent failover

If the active runtime is down, finterm tells you in the chat
surface — it does not fall back to the other runtime.  Switching
is a one-click change in **Settings → LLM Config**.

## 2. Chat with slash commands

Open **Chat** from the toolbar.  Type a question to get a
streaming answer from the active runtime.

For task-shaped queries, use a slash command — finterm resolves
the command to `(agent, skill, args)` and dispatches via the
matching named agent.

| Type | What happens |
|---|---|
| `/help` | Lists every available slash command |
| `/morning-note` | Market-open brief for your watchlist |
| `/comps AAPL` | Comparable-company table for AAPL |
| `/dcf AAPL` | DCF model |
| `/lbo AAPL` | LBO model |
| `/earnings AAPL Q1-2026` | Quarterly print walk |
| `/earnings-preview AAPL` | Pre-print analysis |
| `/initiate AAPL` | Initiating-coverage memo |
| `/sector tech` | Sector overview |
| `/ic-memo AAPL` | Investment-committee memo |
| `/catalysts AAPL` | Upcoming catalysts |
| `/thesis AAPL` | Track an existing thesis vs reality |
| `/rebalance` | Portfolio rebalance suggestions |
| `/client-review` | Client review prep pack |

Slash exchanges persist alongside your chat history — close and
reopen the session, they're still there.

## 3. Schedule recurring runs

Got a brief that should fire every day at 6:30am, or a catalyst
sweep that should run every 30 minutes during market hours?
Schedule it.

**Settings → Scheduler → Add**

| Field | Example |
|---|---|
| Agent ID | `market_researcher` |
| Skill | `morning-note` |
| Cron | `@daily 06:30` *or* `@every 30m` *or* `@every 4h` |
| Args | `{}` (JSON, optional) |

Cron grammar:

| Pattern | Fires |
|---|---|
| `@daily HH:MM` | Once a day at HH:MM (local time).  If the app was off at that minute, it fires when you reopen — same day. |
| `@every Nm` | Every N minutes (1–1440) |
| `@every Nh` | Every N hours (1–168) |

The scheduler runs whenever the app is open.  **Run now** forces
an immediate tick from the same screen.

## 4. Pick an agent

Slash commands route to named agents (see the table in §2).  You
can also pick an agent directly via the chat surface or schedule
it from §3.

Ten agents ship with finterm:

| Agent | What it does |
|---|---|
| `pitch_agent` | Long/short pitches grounded in comps + DCF + catalysts |
| `meeting_prep` | Client / IC / management briefing packs |
| `market_researcher` | Sector / theme overviews + morning notes |
| `earnings_reviewer` | Pre- and post-print analysis |
| `model_builder` | 3-statement / DCF / LBO models |
| `valuation_reviewer` | Critique someone else's model or comp set |
| `gl_reconciler` | GL-vs-custody break detection |
| `month_end_closer` | NAV + sign-off package |
| `statement_auditor` | 10-K / 10-Q internal-consistency audit |
| `kyc_screener` | Counterparty / name-match screening |

Agents are SQLite rows under `agent_configs` — edit the
`config_json` field directly to tweak system prompt, skills, or
tool allowlist.

## 5. Safety + observability

Open **Settings → AI System** (or **Workbench → System**).

### Today's spend

Top of the panel: total USD spent today across every agent, plus
a per-agent breakdown.  Sourced from `agent_traces.cost_usd`;
populated when the runtime reports cost (Anthropic does; local
runtimes vary).

### Per-agent daily cap

Edit an agent's `config_json` to add a budget:

```json
{
  "system_prompt": "…",
  "skills": [...],
  "allow_tools": [...],
  "budget": {
    "max_usd_per_day": 5.0
  }
}
```

Missing or zero `max_usd_per_day` = no cap.  When the cap is hit,
the next dispatch is refused *before* any LLM tokens are
consumed — the refusal reason names the cap.

### Recent dispatches table

Last 50 agent runs with status / latency / cost / source.  Click
**Refresh** to reload.

### Kill-switch

Some tools shouldn't run right now — say a paper-trade tool
that's broken on testnet, or a fetch tool that's hammering a
flaky endpoint.

**Disable tool** → enter the wire-form name (e.g. `int__get_news`
or `mcp-fetch__fetch`) → optional reason.  Disabled tools refuse
to dispatch system-wide, regardless of which agent or which
profile is calling.  Re-enable from the same panel.

The kill-switch wins over per-agent allowlists.  A tool a user
disabled stays disabled even for agents that have it in
`allow_tools`.

## 6. External MCP servers

Install community MCP servers to add tools beyond finterm's
built-in 32 families.

**Settings → MCP Servers** ships a 9-entry marketplace seed
(all disabled by default):

| Server | What it adds |
|---|---|
| `mcp-server-fetch` | Generic URL fetch |
| `mcp-server-filesystem` | Sandboxed file ops |
| `mcp-server-sqlite` | Local DB queries |
| `mcp-server-time` | Time / timezone helpers |
| `mcp-server-sequentialthinking` | Reasoning aid |
| `mcp-server-brave-search` | Web search (needs Brave key) |
| `mcp-server-playwright` | Browser automation |
| `yfinance-mcp` | Free quotes / fundamentals |
| `financial-datasets/mcp-server` | US fundamentals + news + crypto |

Enable a server → start it → its tools appear under
**Tools** in the same screen.

### Auth on external servers

| Scheme | Where the value lives |
|---|---|
| `bearer` / `api_key` | OS keychain under `mcp.auth.<server_id>` |
| `oauth` (client_credentials) | OS keychain under `mcp.oauth.<server_id>.*` |
| `oauth` (authorization_code) | not yet implemented — see [plans/ai-stack.md §10](../../plans/ai-stack.md#10-open-work) |

OAuth client_credentials supports Dynamic Client Registration
(RFC 7591) — if the server advertises a registration endpoint,
finterm registers automatically the first time it connects.

## 7. Voice

**Settings → Voice**:

- **Whisper** (default, free, local) — uses `faster-whisper`
  with the `medium` model out of the box; `large-v3` opt-in.  No
  outbound traffic.
- **Deepgram** (paid, opt-in) — needs a Deepgram API key.

Tap the mic icon in the chat composer to dictate.

## 8. What works on which runtime

Both runtimes share the same slash surface, agents, skills, MCP
tools, and safety primitives.  Where they differ — Files API,
citations, computer use, sub-agents, SDK-native memory vs the
local MemoryTools — is laid out in the **[capability matrix
table](capabilities.md)** generated from the same source the C++
gating layer reads.

Source of truth:
[`fincept-qt/resources/ai/capabilities.json`](../../fincept-qt/resources/ai/capabilities.json).
When a runtime adds a capability, change the JSON, re-run
`fincept-qt/scripts/gen_capabilities_doc.py`, commit both — UI
gating + docs update together.

## 9. Configure once vs configure-per-agent

Most settings are global (LLM Config, voice, MCP server
enablement, kill-switch).  A few are per-agent (system prompt,
skills, allow_tools, budget).  Per-agent settings live in
`agent_configs.config_json` — edit via SQLite browser or wait for
the **Workbench → Agents** editor port.

## 10. Multi-agent teams

A **team** is a named group of agents that run together on one
query: a **coordinator** plus N **members**.  Members each
contribute their lens (sector, quant, macro, earnings, …);
the coordinator synthesises a unified answer.

### Create a team

**Workbench → Teams → New team**.  A modal opens with:

- **ID** — kebab-case slug used by the dispatch handle (immutable
  after create).
- **Name** — display label.  Unique across teams (the slash
  dispatch resolves teams by name).
- **Coordinator** — dropdown of every agent in `agent_configs`.
  Runs after the members and synthesises.
- **Mode** — agno's coordination mode, read by
  `TeamModule.from_config`:
  - `coordinate` — leader routes work to members, synthesises a
    single answer (default; closest fit for "members weigh in,
    coordinator merges").
  - `route` — leader picks the best member, returns its answer
    verbatim (no synthesis).
  - `collaborate` — members work the problem together; the leader
    merges contributions (looser coordination).
- **Members** — checked agents weigh in.  The coordinator can
  also appear as a member; agno treats `leader_index=0` as the
  leader of the group.

### Dispatch a team

From the chat composer:

```
/team morning-note-team summarise what changed overnight for my watchlist
```

The slash handler resolves the team by name, builds the agno
`team_config` (coordinator prepended to members, `mode="coordinate"`,
`leader_index=0`), and dispatches via `AgentService::run_team` →
Python `run_team` action.  Trace / budget / kill-switch / feedback
all apply — a team turn writes one `agent_traces` row like any other
dispatch.

### Edit / delete

Workbench → Teams → select a row → **Edit** or **Delete**.
Delete cascades to `team_members` rows; agent_configs themselves
are untouched.

## 11. Inline completion (off by default)

IDE-style ghost-text suggestions in the chat composer.  After ~450ms
idle, finterm sends the preceding text to the active LLM and
displays the continuation inline (grayed italic).  **Tab** accepts;
any other key dismisses.

### Enable

Off by default — the latency is API-bound until a fast local model
lands.  To turn it on:

1. **Settings → LLM Config** — make sure a profile is configured.
2. From a SQLite browser (no UI surface yet), set:

   ```sql
   INSERT INTO settings (key, value, category)
   VALUES ('inline_completion.enabled', 'true', 'ai')
   ON CONFLICT(key) DO UPDATE SET value = excluded.value;
   ```

3. Restart finterm; the controller installs on the composer's
   QPlainTextEdit at construction.

### Behavior

- ~450 ms debounce so casual typing doesn't ping the model.
- 8-character minimum prefix so the model has something to ride
  on.
- 50-entry LRU cache keyed on the prefix → suggestion mapping —
  re-typing the same prefix after a dismiss returns the cached
  suggestion instantly.
- Suppressed while you have a text selection (drag / shift-arrow)
  — same UX as Cursor / Copilot.
- API-bound latency: an Anthropic round-trip lands a suggestion
  in ~1-3 s.  This will feel native once Engine M1 ships a fast
  local model.

## 12. Audit pillar: tool-call timeline

When a slash dispatch finishes, the trace dialog (**Settings → AI
System → double-click a row**) shows a **Tool calls** table
between the Config and Feedback rows.  Each row: index, tool name,
duration, result preview (red foreground for errors).  Source:
v036 `agent_traces.tool_calls_json`, populated from agno's
`RunOutput.tools` in the Python runtime.

Combined with the existing trace columns (latency, tokens, cost,
status) and the per-turn feedback verdict (right / wrong /
unsure), you get a complete per-dispatch audit record: what was
asked, what the agent called, what came back, what the user
thought of it.

Anthropic SDK runs don't surface tool calls yet (no clean way to
introspect that loop today); those traces render without the
Tool calls table.

## 13. Artefact lineage

The Workbench Artefacts panel (**Settings → Artefacts**) shows
typed slash output — comps tables, models, memos, reports.  Each
row has a **Lineage…** button that opens a modal listing every
artefact in the re-run chain, newest first.  The currently-selected
row is highlighted; predecessors (typically `status='superseded'`)
trail beneath.

Lineage is resolved in two layers:

1. **Explicit chain** (preferred, v039+).  Re-run dispatches set
   `supersedes_id` on the freshly-emitted artefact via the
   dispatch-finish callback — no model cooperation needed.  The
   lineage walker follows pointers backward and forward across the
   chain, handling branched re-runs (multiple successors of the
   same predecessor) correctly.
2. **Identity-based fallback** (legacy rows).  Artefacts emitted
   before v039 don't carry `supersedes_id`; the walker groups
   them by `(source_agent_id, source_skill, source_args_json)` and
   orders by `created_at` DESC.  Standalone artefacts (no
   source_skill) return just themselves.

## 14. Vendor skill drift review

**Settings → Skill diffs.**  Pair to the nightly CI PR-comment flow
(commit `853c1e95`) — runs `vendor_skills_diff.py` from inside the
app against a local clone of
[`anthropics/financial-services`](https://github.com/anthropics/financial-services)
and surfaces the drift in an inbox-style table.

1. Configure the upstream path (persists to SettingsRepository).
2. **Run diff** — spawns the script asynchronously; the table
   populates with diverged + new-upstream rows.
3. **View diff** — side-by-side `QPlainTextEdit` of local vs
   upstream `SKILL.md`.
4. **Accept upstream** — atomic `.tmp` + rename overwrite, gated
   behind a `QMessageBox` confirm.  Local + upstream paths are
   both canonicalised and required to live under their allow-list
   roots (vendored skills tree / configured upstream root) so a
   tampered JSON report can't write outside.

`new_upstream` rows surface for awareness; per-row actions are
disabled because the script doesn't carry a target path for skills
that don't exist locally yet.

## 15. Acceptance checks before relying on a new setup

Run through `fincept-qt/tests/ui/acceptance_checklist.md` after
any LLM-Config / MCP-Servers / Voice change.  It's a 14-item
manual list (A1–A4 local-only, B1–B5 Anthropic, C1–C7 always-on,
S1–S4 safety spot-checks).  Sign-off template at the bottom.

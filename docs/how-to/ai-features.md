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

### Local (Ollama default)

1. Start a local OpenAI-compatible server.  The fastest path:

   ```bash
   # Ollama (https://ollama.com) — defaults to localhost:11434
   ollama pull qwen2.5:14b-instruct
   ollama serve
   ```
2. **Settings → LLM Config** → provider **Ollama** → click
   **Probe**.  finterm fetches the model list at
   `http://localhost:11434/v1/models` and the dropdown populates.
3. Pick a model.  Set as active profile.

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

## 8. Anthropic-only conveniences

The Anthropic profile inherits a few SDK-native features that
local runtimes can't match yet:

- **Skills** — the SDK loads SKILL.md files when an agent's
  config lists them.  finterm ships 3 starter skills
  (`morning-note`, `earnings-analysis`, `comps-analysis`); the
  full `anthropics/financial-services` library is the vendoring
  target.
- **Prompt caching** — long system prompts (skills, agent
  identities) cache automatically.  Look for reduced
  input-token cost on the second turn against the same agent.
- **Server-side memory tool** — the SDK exposes a persistent
  memory primitive that scoping-respects threads.  Local profile
  uses our `MemoryTools` MCP family instead — same shape
  (`memory_upsert / search / list / delete`), narrower features.

## 9. Configure once vs configure-per-agent

Most settings are global (LLM Config, voice, MCP server
enablement, kill-switch).  A few are per-agent (system prompt,
skills, allow_tools, budget).  Per-agent settings live in
`agent_configs.config_json` — edit via SQLite browser or wait for
the **Workbench → Agents** editor port.

## 10. Acceptance checks before relying on a new setup

Run through `fincept-qt/tests/ui/acceptance_checklist.md` after
any LLM-Config / MCP-Servers / Voice change.  It's a 14-item
manual list (A1–A4 local-only, B1–B5 Anthropic, C1–C7 always-on,
S1–S4 safety spot-checks).  Sign-off template at the bottom.

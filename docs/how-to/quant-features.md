# Quant features — configure and use

finterm ships an AI Quant Lab, a paper-trading sandbox, and a
quant_critic agent that narrates results.  This guide walks you
through configuring and using them.

## 1. AI Quant Lab — what it gives you

Open **Quant Lab** from the main toolbar.

| Surface | What it does |
|---|---|
| **Strategy library** | 47 strategy entries — FX, factor, FI, vol, dispersion, macro, … — with intuition, math, citations |
| **Backtest runner** | Vectorised backtests over historical bars; configurable lookback, rebalance, costs |
| **Factor models** | Risk-factor decomposition + attribution |
| **Walk-forward** | Out-of-sample rolling window evaluation |
| **Parameter sweeps** | Grid + random search over a strategy's hyperparameters |

Backtests run in-process — no cloud round-trip.  Results land in
the local SQLite store; the AI Quant Lab tab and the
`run_quant_module` MCP tool both read from the same place.

## 2. Paper Trading

Open **Paper Trading** from the main toolbar.

- Multi-account paper portfolio (`paper_trading.*` tables).
- Live mark-to-market against the yfinance daemon (price feed).
- Order types: market, limit, stop, OCO.
- Position book + blotter + P&L.

Paper trades never hit a real broker.  Real-broker integration
("connect to Fidelity") is on the roadmap but not shipped.

## 3. quant_critic — the AI narrator for quant work

The `quant_critic` named agent is pre-wired to read your quant
lab + paper trading state and narrate it.  Three MCP tools power it:

| Tool | What it does |
|---|---|
| `narrate_backtest_result` | Given a backtest run id, produces a written read-out: Sharpe, drawdown, hit rate, turnover, regime sensitivity |
| `propose_param_sweep` | Suggests parameter ranges to try, with reasoning |
| `narrate_live_trade` | Walks the trader through what happened in a recent paper trade and what to learn |

You can call these tools directly via the chat surface (e.g.
*"Use narrate_backtest_result on run id 42"*), or use slash
commands that route through quant_critic:

- **Open chat** → ensure the active profile is set
- Type a request that names a backtest run, or ask quant_critic
  to surface the latest one and review

## 4. Configure quant_critic

quant_critic's identity lives in `agent_configs` (id =
`quant_critic`).  Tweak via SQLite browser, or wait for the
**Workbench → Agents** editor port.

```json
{
  "system_prompt": "…",
  "skills": ["earnings-analysis", "model-update"],
  "allow_tools": [
    "int__narrate_*",
    "int__propose_*",
    "int__quant_*",
    "int__run_quant_module",
    "int__list_quant_modules",
    "int__list_quant_module_commands",
    "int__get_holdings",
    "int__get_portfolios",
    "int__get_paper_*",
    "int__fd_*"
  ],
  "budget": {
    "max_usd_per_day": 2.0
  }
}
```

The shipped allowlist is read-side only — quant_critic can't
place orders or mutate state.  Adding `int__place_paper_order`
would let it run trades from its analysis; do that only if you
want it to.

## 5. Data sources

Backtests need price history.  Free sources baked in:

- **yfinance** (default; no key)
- **Stooq** (no key)
- **akshare** (China commodities; no key)

For US fundamentals + insider trades + 13-F + section-level SEC
filings, add the **financial-datasets** key:

**Settings → MCP Servers → install `financial-datasets/mcp-server`
→ enter key**.  Quant Lab + quant_critic automatically pick it
up via the `int__fd_*` tools.

For real-time ticks (Power Trader), add **Finnhub** —
**Settings → Credentials → Finnhub**.

## 6. Schedule a quant review

Want quant_critic to file a daily backtest report?  Schedule it.

**Settings → Scheduler → Add**:

| Field | Value |
|---|---|
| Agent ID | `quant_critic` |
| Skill | `narrate_backtest_result` |
| Cron | `@daily 17:30` (post-close review) |
| Args | `{"run_id": "latest"}` (or pin to a specific id) |

The agent runs on the configured runtime; output lands in the
chat history + `agent_traces` table.  Refer to
[AI features →  §3](ai-features.md#3-schedule-recurring-runs) for
the full cron grammar.

## 7. Safety — destructive tools require explicit consent

Paper-trading mutators (`place_paper_order`, `cancel_paper_order`,
…) are marked `is_destructive`.  When an agent tries to call
them, finterm asks you to confirm via the elicitation prompt —
even though it's paper.  Same gate covers settings mutators.

The per-tool kill-switch (**Settings → AI System** → Disable
tool) lets you turn off a specific tool while you investigate a
bug, without disabling the whole agent.

## 8. Tracing quant runs

Every agent run creates an `agent_traces` row.  For quant work
that means: who ran what backtest narration, how long it took,
how many tokens, how much it cost.

**Settings → AI System** shows the last 50 dispatches.  Use it
to spot:

- Slow runs (latency > 30s usually means a slow tool, not a slow
  model)
- Cost spikes (one bad prompt that ran 50 tool calls)
- Repeated errors (kill-switch the offending tool, fix the prompt)

## 9. Strategy catalogue access

The 47-strategy catalogue lives at
`fincept-qt/resources/knowledge/quant_strategies/`.  It's
markdown — readable directly, or surfaced inside the Knowledge
tab with practice tools attached.

To extend: drop a new markdown file there, register it in the
catalogue index.  No code change required.

## 10. When something looks off

| Symptom | First check |
|---|---|
| Backtest crashes | Logs at `~/.local/share/com.fincept.terminal/runtime/` |
| quant_critic returns gibberish | Check the active LLM profile + model (small local models can struggle with quant prose) |
| "Tool disabled by the kill-switch" | Settings → AI System → re-enable the tool, or remove it from the agent's `allow_tools` |
| "daily budget exceeded" | Edit `agent_configs.config_json.budget.max_usd_per_day` or wait for the day to roll |
| `fd_*` tools fail | Verify financial-datasets MCP server is installed + the key is set |

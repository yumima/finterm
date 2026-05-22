# AI-native finterm — research + roadmap

Strategic memo. Four questions about where finterm's AI surface
should head next, anchored to what other AI-native products
ship. Opinionated; not a spec.

Companion to `plans/ai-stack.md` (current state) and
`plans/local-ai-engine.md`. Behaviours I haven't personally
verified are flagged inline.

---

## 1. Value prop — why bundle vs `claude` CLI

Honest baseline: a power user can `claude --mcp
financial-services,financial-datasets` and get 80% of finterm's
slash surface. The bundling thesis must be sharper than "we
wired the MCPs."

**Reference points.** Bloomberg's moat was never data
(Reuters/Refinitiv shipped the same ticks) — it was workflow
lock-in: chat, OMS, IB, MSG, news, charts under consistent
mnemonics. Notion AI's pitch isn't GPT-4 access but that the AI
block reads/writes your existing document tree. Cursor's edge
over `gh copilot` is the diff-apply UI; same model. Linear's
AI-triage closes into the issue board, not a sidebar. Perplexity
Finance (not a daily user) looks mostly like finance-flavoured
web search — not workflow-integrated the Bloomberg way.

**Four real reasons to bundle, ranked:**

1. **Shared state surface.** The agent sees the *same*
   watchlist, portfolio, trace log, paper-trade book,
   kill-switch, scheduler the human edits. No "paste your CSV"
   tax. Notion-AI thesis, not Bloomberg's.
2. **Audit + safety as product.** `agent_traces`, per-tool
   kill-switch, `<untrusted>` wrapping, per-agent USD budget.
   CLI has none. For finance, "I can see every tool call and
   cost" is table stakes.
3. **Local-first switch.** One toggle from a local OpenAI-compat
   server. CLI is Anthropic-only. For workflows where positions
   can't leave the box, *the* differentiator.
4. **Deterministic surface across runtimes.** Same slash, same
   agents, same SKILL.md on Anthropic or local. CLI can't
   promise that.

"We wired the MCPs" is the *weakest* framing — every new
Anthropic skill on the CLI erodes it.

### What finterm should do

- **(S)** Reframe README / onboarding to the four-pillar story.
- **(M)** Always-on context panel showing which artefacts the
  agent has access to this turn (portfolio rows, watchlist
  tickers, open orders).
- **(S)** Local-first toggle on the chat composer header, not
  buried in Settings → LLM Config. Cursor's profile switcher.

---

## 2. Multiplying the value — agentic UI

Today: chat → AgentService → MCP tool → LLM. The reverse — model
proposes a UI mutation, follow-up run, or editable artefact — is
mostly missing.

**Reference points (flagging guesses):**

- **Linear auto-triage** (observed) — AI proposes label /
  assignee / priority; user accepts with one keystroke. Writes
  into the primary surface.
- **Cursor Composer** — multi-file diff, per-hunk accept/reject.
  AI output *is the artefact*, not a transcript.
- **Replit Agent / Devin / Manus** — long-running task with
  progress UI, not chat. I haven't driven Devin or Manus; from
  their videos the idea is "agent owns a workspace."
- **claude.ai artifacts** — generated content in a side panel,
  editable across turns. Chat is a control surface.
- **Notion AI blocks** — first-class block in document grammar
  (rerun, move, embed).

**Slot already wired.** Workbench (Track 13) is a left-nav
container filled with placeholders. Slash output goes into chat
next to the request — the *least* agentic shape possible.

### What finterm should do

1. **(M) Artefact surface for slash commands.** `/comps AAPL`
   produces a typed `TableArtefact` / `ModelArtefact` /
   `MemoArtefact` in Workbench: re-runnable, exportable.
   claude.ai artifacts analogue.
2. **(M) Suggested follow-ups.** After a slash run, agent
   proposes 2-3 next actions. One-click dispatches. Linear's
   accept-or-not pattern.
3. **(S) Inline actions on watchlist/portfolio rows.** Right-click
   → "ask agent: why is this down?" with row context as
   structured args, not pasted prose.
4. **(L) Long-running task tray** for scheduled + user-dispatched
   runs (progress, ETA, intermediate preview, cancel).
   Replit-Agent / Devin pattern.
5. **(S) "Why?" surface on every trace.** Click `agent_traces`
   row → expand tool-call tree (tool, args, result preview,
   latency).
6. **(S) Agent-suggested SKILL.md edits.** After a turn the user
   marks wrong, agent proposes a one-line SKILL.md change.

---

## 3. Keeping upstream integration current

Anthropic ships fast: Files API, per-claim citations, computer
use, Skills, sub-agents, agentic browser, memory tool. The
`claude-agent-sdk` pin lags. Per-feature engineering doesn't scale.

**How other integrators handle it:**

- **Cursor / Continue.dev** — abstract over a model registry
  with capability flags; features ride flags, not code paths.
  (Continue.dev's plugin model is clearer; Cursor I'm partly
  guessing on.)
- **Aider** — pins provider SDK versions per release, documents
  the matrix.
- **Open WebUI** — pulls capability metadata from upstream APIs
  at startup; lights up UI on what's available.

**Process finterm should adopt:**

1. **Capability matrix as code.** `capabilities.yaml` per runtime
   listing files_api, citations, computer_use, memory_tool,
   sub_agents. UI toggles read this. Today it's prose
   ("Anthropic-only conveniences").
2. **Nightly SDK-head eval CI.** Run `scripts/agents/evals/`
   against `claude-agent-sdk` HEAD + pinned release. Pass-rate
   diff lands in a status doc.
3. **Feature flags for half-shipped Anthropic features.** When
   citations land in SDK, gate behind a flag; flip default on
   once stable. Same pattern for MCP spec evolution.
4. **Vendor-script for skills.** `vendor_skills.sh` diffs
   `anthropics/financial-services/skills/` against ours, prints
   what we vendor, what diverges, what's new upstream. Runs in
   CI, posts to PR comments.
5. **One generated "what we support" page**, sourced from the
   matrix. Lives next to `ai-features.md`. Can't drift.

### What finterm should do

- **(M)** Build `capabilities.yaml` + `CapabilityRegistry` (C++
  and Python both read it). Columns: feature, supporting runtimes,
  UI surface that depends on it.
- **(S)** Nightly SDK-head eval CI, reusing
  `~/.fincept/eval_runs.jsonl`.
- **(S)** Vendor-skill diff tool, run on PR.
- **(XS)** Replace prose "Anthropic-only conveniences" with a
  generated table.

---

## 4. AI-native — defined for a financial terminal

"AI-first" is marketing. "AI-native" is how the product is built.
Anchors:

- **Notion AI blocks** — first-class block in the document grammar.
- **Cursor Tab / Cmd-K / Composer** — three AI surfaces at three
  latency / scope tiers, not one chat box.
- **Linear AI-triage** — AI writes the primary entity.
- **Adobe generative layers** — output is a named, editable,
  undoable layer.
- **Bloomberg GPT experiments** (BloombergGPT paper + IB / MSG
  NLU reporting — I haven't used the production surface, just
  the papers) — trained on the corpus the human reads; output
  anchors to CUSIP / FIGI.
- **Adept ACT** — predicted UI action, not text. I haven't used
  it; directional only.

**Eight pillars:**

| # | Property | Today | Gap |
|---|---|---|---|
| P1 | Every entity addressable by the agent | SQLite ids exist; MCP exposes most | No composer `@AAPL` / `@order:42` mention resolver |
| P2 | Agent writes back into primary surfaces | Slash → chat markdown; trade writes elicit | No artefact type; no "agent edited your watchlist" path |
| P3 | Multiple AI latency tiers | One chat + one slash path | No inline completion; no long-running task tray |
| P4 | Audit as product feature | `agent_traces` + spend panel | No tool-call tree drill-down; no per-artefact provenance |
| P5 | Output is re-runnable and editable | Slash is stateless, markdown | Artefact concept missing |
| P6 | Local-first switch is real | Local runtime works | sqlite-vec memory stub; no local-vs-Anthropic parity statement |
| P7 | Agent learns from corrections | None | No "mark wrong" → SKILL.md self-edit loop |
| P8 | Capability surface tracks upstream | Per-feature C++ wiring | Capability matrix (§3) missing |

### What finterm should do

- **(M) P1** — `@`-mention resolver. Parses `@TICKER`,
  `@order:N`, `@trace:N`, `@backtest:N`; passes structured args.
- **(M) P2 + P5** — `ChatArtefact` schema (kind, json blob,
  parent_run_id, editable_fields). Comps / DCF / memos all go
  through it. Editable in Workbench, exportable CSV / DOCX.
- **(L) P3** — Inline completion. Ghost text in note fields,
  watchlist names, agent-config areas via active local model.
  Cursor Tab analogue. <500 ms budget. Big lift.
- **(S) P4** — Trace drill-down: tool-call tree expansion.
- **(S) P7** — "Mark turn wrong" → SKILL.md proposal. Feedback
  lands in the eval store; next /review surface proposes diff.

---

## 5. Prioritised roadmap

Ordered. Dependencies inline.

1. **(S) Reframe README** around the four-pillar value prop.
   Sets the frame.
2. **(XS) Replace "Anthropic-only conveniences" prose** with a
   generated capability table. Forces matrix shape definition.
3. **(M) Capability matrix + `CapabilityRegistry`.** Depends on
   (2). Unblocks every "ship behind a flag" item.
4. **(S) Trace drill-down UI** (P4). Standalone; ships fast;
   pays for the audit-pillar pitch in (1).
5. **(S) Local-first toggle on composer header.** Same week as
   (4).
6. **(M) `@`-mention resolver** (P1). No deps. Foundation for
   every entity-aware feature after.
7. **(M) Artefact type system** (P2, P5). Depends on (6) for
   addressability and (3) for capability gating. Biggest single
   multiplier.
8. **(M) Suggested follow-ups + inline actions.** Depends on
   (7) — follow-ups operate on artefacts.
9. **(S) Nightly SDK-head eval CI** + **(S) vendor-skill diff
   tool.** Independent of UI; parallel from week 1.
10. **(L) Long-running task tray** (P3 partial). Depends on (7)
    for in-tray artefact preview.
11. **(S) "Mark turn wrong" → SKILL.md proposal** (P7). Depends
    on (4) and (9).
12. **(L) Inline-completion tier** (P3 full). Last, because it
    needs a local model fast enough to be honest about — Engine
    M1 dependency (`plans/local-ai-engine.md`).

Items 1-6 (6-8 weeks) move finterm from "chat surface that
bundles MCPs" to "agent participates in primary surfaces."
Items 7-12 (next quarter) make the "AI-native" claim defensible.

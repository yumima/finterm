# Testing Strategy — AI Stack

**Status:** Current
**Date:** 2026-05-20
**Owner:** yumima
**Related:** `plans/ai-stack.md`, `plans/local-ai-engine.md`

---

## 1. Goal

The AI stack's biggest claim is "two runtimes share MCP tools and
SKILL.md files; behavior differs only in capability surface." That
is only true if a test proves it. Without an explicit testing
discipline, "runtime-independent" is hope, not fact.

This doc defines a three-layer approach (unit / e2e / UX) anchored
on a **cross-runtime parity test** as the key invariant. Each
implementation track in the AI stack plan ships its e2e fixtures
alongside the code change.

### Principles

- **Cheap layers run often; expensive layers gate releases.** Unit
  + conformance tests run on every commit. E2E runs nightly.
  UX click-through runs pre-release.
- **Fixtures over assertions.** Each e2e check is a fixture file:
  input + expected tool calls + expected output shape. Code-as-
  config; PR diffs show what's being validated.
- **Allow text wording to differ across runtimes.** Compare *tool
  call sets* and *output shape*, not literal strings. Citation
  presence or `thinking` blocks vary by capability; that's
  expected.
- **Trace as ground truth.** Every turn produces a structured
  trace; assertions are made against the trace, not the chat
  surface scrape.

---

## 2. Three layers

### 2.1 Unit + contract tests

What it verifies (per-PR, every commit):

- Each adapter / parser / repository in isolation.
- The local engine's **OpenAI-shape conformance** — request/response
  envelopes match the OpenAI Chat Completions, Embeddings, and
  Audio Transcriptions v1 contract byte-for-byte where the backend
  supports it. Deviations are explicitly registered.
- The internal `McpProvider`'s **MCP spec conformance** — JSON-RPC
  framing, capability negotiation, resources/prompts/sampling/
  elicitation/progress/cancellation/logging messages. Driven by a
  generic MCP client conformance suite.
- `LlmProfileRepository` resolution order; `SecureStorage`
  migration idempotence; runtime selection by profile.

Lives under `fincept-qt/tests/` (C++ Qt Test) and
`scripts/agents/tests/` (Python pytest) — alongside the code they
test, not in a separate tree.

### 2.2 Integration / e2e — headless agent runs

What it verifies (nightly + pre-release):

- A real user input (`/comps AAPL`) routed through the active
  runtime produces:
  - the expected set of tool calls (set comparison, not order)
  - the expected output shape (markdown with a comparable-company
    table; not exact text)
  - a complete trace (prompts, tool results, citations where
    supported, costs, latency)

Lives under `scripts/agents/evals/e2e/`. Each fixture is one
directory:

```
e2e/
  comps_aapl/
    fixture.yaml           # input + assertions
    snapshot.local.json    # last-known trace, local runtime
    snapshot.anthropic.json
  morning_note/
    fixture.yaml
    ...
```

Driven by `evals run e2e/comps_aapl --runtime=local` and the same
with `--runtime=anthropic`. Both must pass; their traces are
diffed by the parity check (§3).

### 2.3 UX / UI smoke

What it verifies (pre-release, scripted; some manual):

- Workbench opens and renders Chat / Agents / Servers / Profiles
  panels.
- Chat sends a turn against a stubbed runtime; tokens stream;
  citation badges render where present.
- Slash menu lists skills; selecting `/comps` populates the
  composer with the skill template.
- Probe button hits `GET /v1/models` and shows the list.
- "Local LLM service not reachable" banner appears when the engine
  is down on the local profile.
- Kill-switch toggle persists and gates a destructive tool call
  via MCP elicitation; user denies → tool returns an error to the
  loop.
- Cost meter increments on Anthropic-profile turns; cache-hit
  reduction shows on the second turn of the same prompt.

Implementation: Qt Test for headless widget interactions where
possible; a screenshot-comparison gate for visual regressions on
the Workbench. Manual checklist for anything that needs a human
eye (does the answer feel right; does the layout breathe).

---

## 3. Cross-runtime parity test (the central invariant)

For each fixture in `e2e/`:

```
for runtime in (local, anthropic):
    trace = run_fixture(input, runtime=runtime)
    persist trace as snapshot.<runtime>.json
    assert trace.tool_calls.set() == fixture.expected_tool_calls.set()
    assert trace.output.shape() matches fixture.expected_shape
    assert trace.errors == []

# Cross-runtime parity:
assert local.tool_calls.set() == anthropic.tool_calls.set()
assert local.output.shape() == anthropic.output.shape()

# Capability-specific outputs are allowed to diverge:
if runtime == anthropic:
    assert trace.has_citations
    assert trace.cost.cache_hit_ratio > 0 on the 2nd+ turn
```

The parity test is what makes "runtime-independent SKILL.md +
MCP tools" a verified property instead of a design claim. If it
fails, either:

- a SKILL.md branches on runtime accidentally,
- a tool call differs (often because the local model needed a
  prompt repair the Anthropic SDK didn't),
- or the data-source-hierarchy resolution drifted.

All three are bugs the parity test catches before users do.

---

## 4. Fixture format

```yaml
# e2e/comps_aapl/fixture.yaml
name: comps_aapl
input:
  agent: equity_research
  message: "/comps AAPL"
  skills_loaded: [comps-analysis]

expected:
  tool_calls:
    must_include:
      - fd:get_income_statements
      - fd:get_balance_sheets
      - int:markets.quote
    must_not_include:
      - ext:fetch.url   # web search shouldn't be needed
  output:
    contains_table_with_columns:
      - ticker
      - revenue
      - operating_margin
      - p_e
    min_peer_count: 3
  trace:
    requires_complete_record: true
    max_tool_calls: 20
    max_iterations: 6
    must_not_error: true

allowed_divergence:
  text_wording: true
  citation_presence: anthropic_only
  thinking_blocks: anthropic_only
  cost_usd: any
  latency_ms: any
```

Snapshots (`snapshot.local.json`, `snapshot.anthropic.json`) are
committed and reviewed on PRs. A snapshot diff with no fixture
change is a behavioural regression to investigate, not auto-update.

---

## 5. Harness

`scripts/agents/evals/runner.py`:

- Boots the chosen runtime in process (or out-of-process for the
  local engine via the running service).
- Builds the agent identity per fixture, loads skills, configures
  tools.
- Executes the input as a chat turn.
- Captures the trace via the unified trace schema (see
  `ai-stack.md` R21).
- Compares against the fixture's assertions.
- Writes the new snapshot to disk; reports diff vs the prior
  snapshot.

CLI:

```
evals run e2e/<fixture> --runtime=local
evals run e2e/<fixture> --runtime=anthropic
evals run-all e2e/ --parity      # runs both, asserts parity
evals snapshot --update e2e/<f>  # rewrite snapshot deliberately
```

Pure unit tests stay in their language-native runners (`pytest`,
`ctest`). The harness owns only the e2e layer.

---

## 6. Conformance suites

Two specs, two suites.

### 6.1 OpenAI conformance (against the local engine)

`scripts/agents/evals/conformance/openai/` — replays a curated set
of `openai-python` SDK calls against the engine and asserts:

- Request body shape accepted (no schema rejection).
- Response body matches expected schema, field-for-field.
- Streaming chunks match OpenAI's SSE format.
- Tool calls round-trip cleanly.
- Embeddings response shape; transcriptions response shape.

Tied to a pinned `openai-python` version per release. Drift in
either direction (OpenAI changes their spec, or the engine
regresses) fails the suite loudly.

### 6.2 MCP conformance (against our internal `McpProvider`)

`scripts/agents/evals/conformance/mcp/` — runs the MCP spec's
client-side conformance harness against our internal server.
Verifies all capabilities (tools / resources / prompts / sampling
/ elicitation / progress / cancellation / logging) over JSON-RPC.
Also runs against each external stdio MCP server in the
marketplace seed list (Track 4) to catch upstream breakage.

---

## 7. Trace assertions

The unified trace (R21 in `ai-stack.md`) records per
turn: prompts, tool calls, tool results, outputs, latency, tokens,
cost, runtime, model snapshot.

Test code asserts on trace fields directly:

```python
assert trace.runtime in ("local", "anthropic")
assert trace.tool_calls.count() > 0
assert trace.tool_results.count() == trace.tool_calls.count()
assert trace.budget.token_in + trace.budget.token_out > 0
assert trace.budget.cost_usd >= 0
assert trace.errors == []
if trace.runtime == "anthropic":
    assert trace.citations.count() > 0   # for skills that cite
```

The trace schema is the test contract. Changes to it require a
fixture-schema bump and a sweep of snapshots.

---

## 8. UX click-through scripts

Map directly to §13 acceptance criteria of the AI stack doc. Each
criterion becomes one scripted scenario:

- `A1_probe_local_models` — Settings → Probe → assert model list.
- `A2_local_chat_no_outbound` — Send "2+2" → assert streamed
  answer + `ss -tnp` shows no outbound to non-loopback.
- `A3_local_unreachable_banner` — Kill engine → assert banner.
- `A4_local_comps` — `/comps AAPL` → assert table renders.
- `B1`–`B5_anthropic_*` — Anthropic profile equivalents.
- `C1`–`C7_always_on_*` — MCP install, Whisper default, Workbench
  flows, kill-switch, trace surface.

Runs through Qt Test where automatable; otherwise a manual
checklist scripted in `tests/ui/acceptance_checklist.md` for
release sign-off.

---

## 9. CI / cadence

| Cadence | Layer | Runs |
|---|---|---|
| Every commit | Unit + conformance | C++ + Python unit tests; OpenAI + MCP conformance |
| Nightly | E2E + parity | Full `evals run-all e2e/ --parity` on both runtimes |
| Pre-release | UX click-through | Acceptance scenarios, screenshot diffs |
| Pre-release (manual) | UX review | Walk the 5 user questions (portfolio, idea, stock, quant, world) and judge |

Cost note for nightly: Anthropic-profile e2e burns tokens. Budget:
- Per fixture, max 5k tokens × ~30 fixtures × ~$0.001/1k tokens
  (Sonnet-class) = ~$0.15 per nightly run, well under $5/month.
- If we add more fixtures, batch via Anthropic Batch API at 50%
  off and the nightly cost stays flat.

---

## 10. Per-track requirement

Every PR closing a track item in `ai-stack.md` §9 ships
its e2e fixture(s) and updated snapshot(s). The iteration loop
becomes:

```
fix → /review → fix findings → build → e2e fixture lands → commit
```

Tracks producing fixtures:

| Track | Fixtures it adds |
|---|---|
| 2 (local runtime) | `local_smoke`, parity baselines for existing fixtures |
| 3 (Anthropic SDK) | `anthropic_smoke`, parity baselines |
| 5 (full MCP spec) | conformance suite extended with resources/prompts/sampling/elicitation |
| 6 (FinancialDatasetsTools) | `comps_aapl`, `insider_nvda`, `13f_brk` |
| 7 (skills + slash) | one fixture per slash command (`comps`, `dcf`, `earnings`, `morning-note`, `sector`, `screen`, …) |
| 9 (memory) | `memory_recall`, `thesis_continuity` |
| 10 (scheduler + hooks) | `morning_note_scheduled` |
| 11 (quant narrator) | `narrate_backtest`, `propose_sweep` |
| 12 (alpha-arena) | `arena_round` |
| 13 (Workbench) | UX click-through scenarios |
| 14 (evals/obs/audit) | the harness, conformance suites, trace assertions, click-through scripts |

---

## 11. Track 0 — testing scaffold (one-time prerequisite)

Lands before Track 3 (Anthropic SDK adoption). Establishes:

- The fixture YAML schema and parser.
- The headless harness (`evals run`).
- The trace assertion library.
- The OpenAI conformance skeleton (empty suite that grows with
  the engine).
- The MCP conformance skeleton (empty suite that grows as internal
  servers extend the spec).
- One walking-skeleton fixture (`hello_world`) that asserts a
  single chat turn produces a complete trace on both runtimes.

Effort: ~3 days. Without it, every later track has to invent its
own validation, and the parity test never exists.

---

## 12. Open questions

1. **Snapshot review.** Snapshots committed to git → PR diff
   review catches behavioral changes. Acceptable PR noise vs
   signal? Lean: yes, keep snapshots in tree.
2. **Anthropic-profile flakiness.** Token output varies per call.
   Are shape-only assertions enough, or do we need LLM-as-judge?
   Lean: shape-only for v1; LLM-as-judge if false positives
   appear.
3. **Engine smoke in CI.** Should CI spin up the local engine for
   nightly e2e, or only run the Anthropic side in CI and engine-
   side locally? Lean: nightly runs both, with the engine in a
   Docker container in CI.
4. **UX visual regression.** Screenshot diff has known flakiness
   (font rendering, anti-aliasing). Threshold percentage vs exact
   match? Lean: 1% diff threshold, exclude rendering-fragile
   areas.
5. **Where lives the manual UX checklist** — repo or product
   doc? Lean: repo (`tests/ui/acceptance_checklist.md`) so it
   versions with the code.

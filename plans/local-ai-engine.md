# Local AI Engine — Vision, Requirements, Architecture

**Status:** Proposal
**Date:** 2026-05-20
**Owner:** yumima
**Working name:** `finterm-localai` (placeholder; see §6 Open
questions)
**Related:** `plans/ai-stack.md` (finterm's side of the
contract)

---

## 1. Vision

A separate project providing a **stable, OpenAI-compatible local AI
service** on `localhost`. finterm is its first consumer; the design
does not depend on finterm semantics — any local app that needs
chat, embeddings, or speech-to-text on the box can target it.

The engine abstracts the local-model landscape — Ollama, vLLM,
llama.cpp, LM Studio, faster-whisper — behind one HTTP API matching
OpenAI's contract. Consumers integrate once; the engine swaps
backends underneath.

Plausible non-finterm consumers (validation that the boundary is
right, not a commitment to support them): a personal note-taking
assistant doing local RAG; a code-completion plugin pointed at a
local coder model; a Whisper transcription utility; any
`openai`-SDK client a developer reconfigures to hit a local URL.

### Why a separate project

finterm should not own:

- Model download / install / version / swap lifecycle
- GPU and VRAM detection, per-model fit checks
- Backend runtime supervision
- Quantization / format conversion (GGUF, AWQ, GPTQ)
- Whisper streaming and audio pipelines
- Tool-calling JSON repair for weak local models
- Per-task model routing (primary / fast / coding / embeddings /
  STT)

These are general "local AI engine" concerns. Bundling them into
finterm contaminates the finance domain. A separate project also
means other apps benefit — this is infrastructure, not product.

### What the engine is not

- Not an AI training framework. No fine-tuning, SFT, or RLHF.
- Not a finance app. Zero finance domain knowledge.
- Not an MCP server. MCP lives in finterm.
- Not an agent runtime. Agent loops live in finterm (and in the
  Anthropic Agent SDK when that profile is active).
- Not a cloud relay. All inference is local; outbound only for
  model downloads from configured registries.
- Not a multi-tenant service. Single-user, single-host.

### What the engine provides

OpenAI-compatible HTTP endpoints, served on a loopback port:

- `POST /v1/chat/completions` — streaming SSE, function/tool calling
- `POST /v1/embeddings`
- `POST /v1/audio/transcriptions`
- `GET /v1/models`

Admin endpoints:

- `GET /admin/health`, `GET /admin/ready`
- `GET /admin/hardware` — GPU type, VRAM, RAM
- `GET /admin/backends` — installed backends + versions
- `GET /admin/roles`, `PUT /admin/roles/{role}` — view / rebind roles
- `POST /admin/models/pull` — install a model
- `DELETE /admin/models/{id}` — remove
- `POST /admin/models/swap` — hot-swap the model bound to a role
- `GET /admin/metrics` — counters (local only)

CLI mirroring those endpoints.

---

## 2. What finterm needs from the engine

The consumer contract — finterm-specific, but kept narrow so the
engine doesn't grow finance-shaped APIs.

| finterm capability | Engine endpoint | Required for finterm v1 |
|---|---|---|
| Chat / agent / tool calling | `POST /v1/chat/completions` (streaming, tool use) | Yes |
| Embeddings for memory + RAG | `POST /v1/embeddings` | Yes |
| STT for voice input | `POST /v1/audio/transcriptions` | Yes |
| Settings "Probe" button | `GET /v1/models` | Yes |
| Banner / connectivity check | `GET /admin/health` | Yes |
| Future: chart-image input | `POST /v1/chat/completions` with image content | Optional |

### Consumer expectations finterm relies on

- Stable OpenAI-compatible request/response shapes. SDK clients
  written against `openai-python` should work pointed at the
  engine's base URL.
- Streaming via SSE in OpenAI's chunk format.
- Tool/function calling that handles `tools: [...]` and emits
  `tool_calls` blocks (or text-parseable equivalent the engine
  normalizes — see §3.3).
- Embeddings response shape: `{ data: [{ embedding: [...] }] }`.
- Transcriptions response: `{ text: "..." }`, optional `segments`.
- A model id finterm sets (`primary_chat`, `fast_chat`,
  `embedding`, `stt`, …) resolves to whatever the engine's role
  registry says — finterm does not pick concrete model names.

### What finterm does NOT need

- Inference orchestration policies, custom samplers, beam search.
- Persistence beyond the engine's own config and model cache.
- Authentication on loopback. Engine binds loopback by default.
- Cluster/distributed inference.

---

## 3. Requirements

### 3.1 Functional

- **F1.** `/v1/chat/completions` with streaming, tool use,
  multi-turn history. JSON-mode supported when underlying backend
  supports it.
- **F2.** `/v1/embeddings` — text input → float vector array.
- **F3.** `/v1/audio/transcriptions` — audio file → text. File
  upload v1; streaming v2.
- **F4.** `/v1/models` listing all models the engine can currently
  serve. OpenAI shape (no extra fields). Role bindings are exposed
  separately at `/admin/roles` so OpenAI SDK clients don't choke on
  unknown keys.
- **F5.** Health, readiness, hardware-info endpoints.
- **F6.** Model lifecycle: pull, install, list, remove. Sources:
  Ollama registry, HuggingFace, local file path.
- **F7.** Hot-swap models per role without service restart.
- **F8.** Per-role model registry: `primary_chat`, `fast_chat`,
  `coding`, `embedding`, `stt`, `vision` (optional). Each role
  maps to `(model_id, backend)`.
- **F9.** Hardware detection: GPU type, VRAM, RAM, CUDA/Metal/ROCm
  availability. Suggests models that fit.
- **F10.** Concurrency control: per-endpoint queues with
  configurable limits; 429 + Retry-After beyond.
- **F11.** Tool-calling reliability layer: JSON-schema validation
  of tool args, one repair pass on parse failure, honest error if
  still malformed. Bypassable via request header.
- **F12.** Pluggable backend adapter pattern: v1 ships Ollama
  adapter; vLLM / llama.cpp / LM Studio adapters land later.
  Backend is selectable per-model, not per-service.
- **F13.** First-run wizard: hardware probe → suggest default
  models → install → bind roles.

### 3.2 Non-functional

- **N1.** Sub-second TTFT on 7–8B-class models on a 12 GB-VRAM
  consumer GPU.
- **N2.** Zero outbound network calls during inference. Network
  used only for model downloads from configured registries.
- **N3.** OpenAI compatibility to the byte where the backend
  supports it. Documented deviations per backend.
- **N4.** Cross-platform: Linux primary, macOS supported, Windows
  nice-to-have. Loopback bind by default; off-loopback bind
  requires explicit `--unsafe-bind`.
- **N5.** Single supervised process. Backends are external — the
  engine discovers them at startup (e.g. probing
  `localhost:11434` for Ollama). Optional supervised mode via
  `--manage-backends` starts/stops backends with the engine; off by
  default to avoid stepping on user-managed daemons.
- **N6.** CLI sufficient for ops (`start`, `stop`, `install`,
  `list`, `bind`, `swap`, `logs`). Web UI optional, future.
- **N7.** Config via `~/.finterm-localai/config.yaml` + env vars.
  No global system state.
- **N8.** Engine process resident footprint under 200 MB (loaded
  models excluded).
- **N9.** Telemetry: opt-in only; default off; no automatic upload
  pipe.

### 3.3 Compatibility

- **C1.** Track OpenAI Chat Completions / Embeddings / Audio
  Transcriptions v1 contract as of release date. Pin tested SDK
  versions (openai-python X.Y) per release.
- **C2.** Document tested backend versions per release (Ollama
  0.x, faster-whisper Y.z, vLLM N.M, llama.cpp commit hash).
- **C3.** Backend adapters report capability flags
  (`supports_tools`, `supports_streaming`, `supports_vision`,
  `supports_json_mode`). Engine refuses requests that require an
  unsupported capability with a clear error citing both the model
  and the missing capability.

---

## 4. Architecture proposal

```
              Consumers (finterm, other local apps)
                          │
                          │ HTTP (OpenAI-compatible)
                          ▼
              ┌──────────────────────────────────┐
              │ API gateway                       │
              │  - /v1/chat/completions           │
              │  - /v1/embeddings                 │
              │  - /v1/audio/transcriptions       │
              │  - /v1/models                     │
              │  - /admin/*                       │
              └────────────────┬─────────────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
       ┌────────────┐   ┌────────────┐   ┌────────────┐
       │ Chat       │   │ Embedding  │   │ STT        │
       │ router     │   │ router     │   │ router     │
       └─────┬──────┘   └─────┬──────┘   └─────┬──────┘
             │                │                │
             │    ┌───────────┼───────────┐    │
             ▼    ▼           ▼           ▼    ▼
        ┌──────────────────────────────────────────┐
        │ Backend adapters                          │
        │  - OllamaAdapter (chat, embed, vision)    │
        │  - LlamaCppAdapter (chat, embed)          │
        │  - VLLMAdapter (chat) — future            │
        │  - LMStudioAdapter (chat, embed) — future │
        │  - FasterWhisperAdapter (stt)             │
        └──────────────────────────────────────────┘
                               │
       Cross-cutting:
        - Role registry (role → model_id, backend)
        - Hardware probe (GPU, VRAM, RAM)
        - Model manager (pull, install, remove, swap)
        - Tool-call repair layer (JSON validate, retry once)
        - Concurrency manager (per-endpoint queues)
        - Config store (~/.finterm-localai/config.yaml)
        - Logs + metrics (local only, opt-in upload)
```

### 4.1 Process model

Single supervised engine process. It owns:

- The HTTP gateway
- The role registry (in-memory + persisted to config)
- The model manager (download, list, remove)
- A pool of persistent client connections to backend daemons

Backends are external processes the user (or `engine install …`)
has running:

- Ollama daemon → `localhost:11434`
- vLLM server → user-configured port
- llama.cpp server → user-configured port
- LM Studio → `localhost:1234`

The engine doesn't try to start every backend on its own. It
discovers what is installed/running and uses it. `finterm-localai
install ollama` is a convenience wrapper around the upstream
installer script for users who want zero-touch setup.
`--manage-backends` opts into supervised mode where the engine
starts and stops backends with itself.

### 4.2 Role-based routing

Requests resolve in two ways:

1. **Role alias.** `model: "primary_chat"` (or `fast_chat`,
   `embedding`, etc.) → role registry → concrete `(model_id,
   backend)`.
2. **Literal model id.** `model: "qwen2.5:14b"` routes directly to
   the backend that has it.

Default roles set at first-run by the hardware probe:

| Role | Default suggestion (12 GB VRAM box) |
|---|---|
| `primary_chat` | `qwen2.5:14b-instruct-q4_K_M` |
| `fast_chat` | `llama3.1:8b-instruct-q4_K_M` |
| `coding` | `qwen2.5-coder:14b` |
| `embedding` | `nomic-embed-text` (or `bge-m3`) |
| `stt` | `faster-whisper:medium` (`large-v3` opt-in) |
| `vision` | unbound (Qwen-VL / LLaVA opt-in) |

User rebinds via `finterm-localai bind primary_chat qwen2.5:7b`.

### 4.3 Tool-calling repair layer

Local models below ~14B sometimes emit malformed tool-call JSON.
The engine sits between the consumer and the backend:

1. Consumer sends `tools: [...]` in OpenAI format.
2. Backend returns text or `tool_calls`.
3. If `tool_calls` validates against the JSON schemas in `tools`,
   pass through.
4. If validation fails: one repair pass — send back to the model
   with a strict reformat prompt (`previous tool call was
   malformed; re-emit exactly matching this schema: …`).
5. If still invalid, return a 422 with the original raw output so
   the consumer (finterm's local loop) can fall back to its own
   text-parse path.

Consumers disable the repair layer via `X-Tool-Repair: off`.

### 4.4 Concurrency

Per-endpoint queues with configurable limits. Defaults:

- `chat`: 2 (one inferring, one queued — typical consumer GPU)
- `embedding`: 4 (cheap)
- `stt`: 1 (Whisper is heavy)

Beyond the queue: 429 + Retry-After. No silent backpressure.

### 4.5 Model installation flow

```
$ finterm-localai install qwen2.5:14b
[hardware] GPU: RTX 5070 Ti (12 GB), VRAM 11.6 GB free
[fit]      qwen2.5:14b-instruct-q4_K_M needs ~9 GB — fits
[backend]  ollama 0.x detected at localhost:11434
[pull]     ollama pull qwen2.5:14b ... done (8.4 GB)
[role]     bind 'primary_chat' to qwen2.5:14b? [Y/n]
```

First-run wizard runs the same flow non-interactively for the
default role set.

### 4.6 Discovery and consumer integration

finterm's "Probe" button hits `GET /v1/models` → engine returns the
list of installed + role-mapped models, plus capability flags. A
banner surfaces in finterm if `/admin/health` is unreachable.

Engine binds `127.0.0.1:11435` by default — one off from Ollama's
`11434` to avoid a clash when both run side-by-side. Configurable
via `--bind`. Optional unix-domain socket on Linux for stricter
isolation. Fronting Ollama transparently (binding 11434 and proxying
non-engine paths through) is left as a future option; see §6 Q4.

### 4.7 Security posture

Single-user, single-host, loopback-bound by default. Threat model
is intentionally narrow:

- **In scope.** Anything on the local box reaching the loopback
  port. The DNS-rebinding-via-browser path is the practical risk:
  a malicious page could try to hit `localhost:11435` as the user
  visits it. Mitigation: require the `Host` header to be `localhost`
  or `127.0.0.1` (reject anything else); set CORS to deny by
  default; do not echo arbitrary input in error pages.
- **Out of scope.** Multi-user separation, network exposure (LAN /
  internet), authenticated multi-tenant access.
- **Off-loopback bind requires `--unsafe-bind`** and forces a
  bearer-token requirement via `--api-key`. The flag name is
  intentional — operators must opt into the wider attack surface
  with a flag whose name documents the risk.
- **Model downloads** are the only outbound network surface during
  normal operation. The set of allowed registries is in config; no
  arbitrary URLs.
- **Logs and metrics** stay on disk under
  `~/.finterm-localai/logs/`. No upload path exists in the binary.

The engine does not validate or sandbox tool calls the model emits
— consumers (finterm, others) are responsible for executing them
safely. The engine's job is faithful transport of model output.

### 4.8 Tech choices (proposed; revisit at v1 freeze)

- **Language: Python** for v1 — fastest iteration, deepest ML
  ecosystem. Revisit for Go or Rust at v2 if footprint or
  distribution becomes the bottleneck.
- **HTTP framework: FastAPI** — mature, OpenAI-shaped serialization
  is straightforward, async friendly.
- **Backends called via HTTP** (each backend's own HTTP server) —
  no embedded inference. Engine stays slim. faster-whisper is the
  exception: it can be in-process Python.
- **Packaging:** pipx-installable; systemd unit / launchd plist
  templates included for headless runs. `engine start` runs in
  foreground for dev.
- **Distribution: single binary later** via Nuitka / PyInstaller /
  Briefcase if Python distribution becomes painful on
  Windows/macOS.

---

## 5. Milestones

**M1 — Walking skeleton (week 1–2)**

- FastAPI gateway with `/v1/chat/completions` (streaming),
  `/v1/embeddings`, `/v1/models`, `/admin/health`.
- Ollama adapter only.
- Role registry in YAML.
- CLI: `start`, `models`, `bind`.
- finterm's Probe and Chat work end-to-end against it.

**M2 — STT + tool repair (week 2–3)**

- `/v1/audio/transcriptions` via faster-whisper (in-process).
- Tool-call repair layer.
- Concurrency manager with default queue sizes.

**M3 — Model lifecycle (week 3–4)**

- `install`, `remove`, `swap` via Ollama pull / rm.
- Hardware probe + fit suggestions.
- First-run wizard binds default roles automatically.

**M4 — Second backend (week 4–6)**

- llama.cpp adapter (more portable than vLLM, easier than LM
  Studio).
- Per-model backend selection.
- Cross-backend role rebinding.

**M5 — Hardening (week 6–8)**

- Cross-platform install (macOS Homebrew, Windows MSI optional).
- Logs + metrics surface.
- Stability soak against finterm workloads (scheduled morning-note
  agent over a week).

---

## 6. Open questions

1. **Name.** `finterm-localai` is the working name. Alternatives:
   `finai`, `localai-svc`, `homeloom`, generic. If we want to
   attract non-finterm consumers, drop the `finterm-` prefix.
2. **License.** MIT or Apache-2.0. Match finterm's stance.
3. **Bundle vs adapter-only.** v1 assumes Ollama installed.
   Should `install` pull Ollama itself if absent? Lean: yes, with
   explicit consent.
4. **Default port.** Decided: `127.0.0.1:11435` (one off from
   Ollama). Open follow-on: whether to also offer an Ollama-fronting
   mode that binds 11434 and proxies non-engine paths through, so
   existing Ollama clients keep working unchanged.
5. **Vision models.** Separate role (`vision`) or capability flag
   on `primary_chat`? Lean: separate role; consumers opt in.
6. **Streaming STT.** File-upload v1, real-time stream v2. v2 needs
   WebSocket or chunked HTTP.
7. **Auth on loopback.** None vs optional API key. Lean: none on
   loopback; off-loopback requires `--unsafe-bind` and forces a key.
8. **Quantization choice.** Auto-pick best quant for VRAM vs
   always present options. Lean: auto + `--quant` override.
9. **Telemetry.** Local-only metrics by default. Opt-in upload for
   "help improve"? Lean: pure local, no upload pipe ever.
10. **Multi-user / multi-host.** Out of scope for v1. Revisit if
    consumers other than finterm need it.
11. **GPU detection on non-NVIDIA.** Apple Silicon via Metal, AMD
    via ROCm. v1 ships NVIDIA + Apple; AMD is best-effort.
12. **Backend supervision.** Engine starts Ollama if not running,
    or assumes the user starts it? Lean: optional supervised mode
    via `--manage-backends`.

---

## 7. Risks

- **OpenAI API drift.** Their spec evolves; clients break. Pin
  tested compatibility per release; integration test suite mirrors
  OpenAI's golden requests.
- **Backend API drift.** Ollama, vLLM, llama.cpp all change
  endpoints. Pin tested versions; adapter-internal version checks
  on startup.
- **Tool-call quality plateaus.** Small local models will keep
  struggling on complex tool schemas. Repair layer is a floor, not
  a fix. Model selection (Qwen 14B+) remains the real lever.
- **Distribution pain on Windows.** Python packaging is fragile.
  May force a binary repackage earlier than planned.
- **Hardware diversity.** Apple Silicon, NVIDIA, AMD all need
  different paths. Linux+NVIDIA is the dev box; Apple Silicon
  supported via llama.cpp Metal; AMD ROCm is best-effort.
- **Engine ↔ Ollama port clash.** Both default to 11434 today;
  decide once.
- **Scope creep.** "Local AI engine" is broad. Risk of becoming a
  general-purpose ML serving platform. Resist: every feature gates
  on "does finterm need it, or will the next obvious consumer?"

---

## 8. Relationship to finterm

finterm's design doc treats this project as an external dependency.
The contract is the HTTP surface and capability flags. Specifically:

- finterm holds `local_llm_base_url` in its settings; engine binds
  there.
- finterm's "local runtime" (`finagent_core` slimmed loop) speaks
  OpenAI-compatible to this engine. Engine's behavior dictates how
  much repair the finterm side has to do.
- finterm's Whisper voice path (R15) calls
  `POST /v1/audio/transcriptions`.
- finterm's embeddings (R14) call `POST /v1/embeddings` —
  regardless of whether finterm's chat profile is local or
  Anthropic (Anthropic API has no embeddings endpoint).
- finterm's Probe button hits `GET /v1/models`.

If the engine is down, finterm shows a banner pointing at the
engine's setup docs (R1 — no silent failover).

Bidirectional commitments:

- Engine commits to the OpenAI surface so finterm doesn't need
  engine-specific code.
- finterm commits to not embedding engine functions into its own
  binary — the engine stays out-of-process.

---

## 9. What's deferred

Not in scope for the engine. May go in a separate doc later:

- Fine-tuning / continued pre-training.
- Distillation pipelines.
- Multi-host / clustered inference.
- Model evaluation harness — finterm has its own eval bench
  (`scripts/agents/evals/`) at the agent-skill level; per-model
  bench is engine-side and starts as a thin smoke-test only.
- Image generation, audio generation. Not consumer needs today.
- Function-calling fine-tunes hosted by the engine. Use stock
  community quants; do not retrain.

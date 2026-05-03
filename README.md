# finterm — Localhost Fincept Terminal (Linux)

A self-hosted, **offline-capable** fork of [Fincept Terminal](https://github.com/Fincept-Corporation/FinceptTerminal) — the C++/Qt6 financial-research desktop. This fork strips out the cloud dependency so the entire app runs locally on Linux: no account, no `api.fincept.in`, no rate-limited SaaS backend. Just the Qt binary, a small localhost stub that fakes the cloud auth layer, and Python data scripts you already trust.

## What this is

| | Upstream Fincept Terminal | This fork |
|---|---|---|
| Backend | `api.fincept.in` (SaaS) | `127.0.0.1:8765` (local Python stub) |
| Account | Required signup + login | Stub accepts anything; no signup |
| Credits / billing | Server-side | Mocked — always positive |
| Platform support | Linux / macOS / Windows | **Linux only** |
| Market data | yfinance + akshare + (paid feeds) | Same — but with optional Stooq toggle for fast EOD |
| Data leaving your machine | Quote APIs + telemetry to `api.fincept.in` | **Quote APIs only.** No telemetry. No analytics. See [Data flow](#data-flow). |

The Qt UI is unchanged from upstream where it doesn't need to be. Where it does — auth, profile, credits — the client talks to the stub and gets stubbed answers, so the screens render normally.

## Data flow

This fork is **data-in only**: the app fetches market data *from* public quote APIs but **never sends your data anywhere**. No portfolio holdings, no watchlists, no notes, no chat history, no usage telemetry, no error reports leave the machine.

Concretely:

- **Outbound, on your behalf:** Yahoo Finance (`query1.finance.yahoo.com`, `query2.finance.yahoo.com`) and Stooq (`stooq.com`) for quotes/history. akshare hits Chinese exchange endpoints when you use the China futures panel. These are the same calls upstream makes; they fetch data **into** the app.
- **Outbound, never:** no calls to `api.fincept.in`, no analytics provider, no crash reporter, no LLM/AI provider unless *you* configure one explicitly via Settings → Agent.
- **Inbound only:** all auth, profile, credits, MFA, subscription, and account flows resolve against `127.0.0.1:8765` (the local stub). The stub is stdlib-only Python, no network code.
- **Local only:** portfolio, watchlists, notes, alerts, candles cache, chat history, settings — all in `~/.local/share/com.fincept.terminal/` (SQLite + json) and `~/.config/Fincept/`. Nothing in those dirs is uploaded.

If you wire in an external LLM via Settings → Agent (OpenAI, Anthropic, local Ollama, etc.), prompt+response pairs go to whatever endpoint *you* configured — that's the one and only outbound channel under your direct control. Default is no AI provider.

### Fork-specific additions

Beyond the cloud-removal, this branch adds:

- **FUTURES** top-level tab — 8 asset-class watchlist (Index, Rates, Energy, Metals, Ags, FX, Crypto, China) with heatmap, term structure, spread monitor, settlements, and continuous chart. Backed by a shared in-memory quote cache.
- **Portfolio → Futures & Ext Hours** sub-view — filters your holdings to futures-style symbols and shows pre/post-market values for the rest.
- **DATA: Yahoo / Stooq toggle** in the top toolbar — switch between Yahoo (live, ~3–5s) and Stooq (EOD, ~1s). Persists across launches.
- **Persistent yfinance daemon** — long-lived Python child process; eliminates the ~1.5s pandas/yfinance import cost per request.
- **Boot prefetch** — futures cache and active portfolios are warmed at app startup, so first-click feels instant.

See [`fincept-qt/CHANGELOG-fork.md`](fincept-qt/CHANGELOG-fork.md) if/when that lands; otherwise the diff against upstream is the source of truth.

## Where things live

```
finterm/                            ← this repo
├── start.sh                        ← single-command launcher (used by ~/bin/finterm)
├── setup.sh                        ← preflight: checks Qt / cmake / Python toolchain
├── fincept-qt/                     ← upstream Qt6 source tree (forked + patched)
│   ├── src/                        ← C++ application
│   ├── scripts/                    ← Python data layer (yfinance, akshare, stooq, ...)
│   ├── cmake/                      ← build helpers
│   └── build/linux-release/        ← compiled binary lands here
├── tools/
│   ├── local_stub/server.py        ← localhost backend that replaces api.fincept.in
│   └── systemd/fincept-stub.service ← optional user unit to keep stub running
└── docs/                           ← upstream docs (unchanged)
```

Two processes run when you launch:

1. **Local stub** (`tools/local_stub/server.py`, system Python) — listens on `127.0.0.1:8765`, fields auth/profile/credits/etc. requests. Stays alive across launches.
2. **FinceptTerminal** (`fincept-qt/build/linux-release/FinceptTerminal`) — the Qt app. Spawns its own Python child for market-data work (`yfinance_data.py --daemon`).

Neither process talks to anything outside `127.0.0.1` except the data scripts you explicitly invoke (Yahoo Finance, Stooq, akshare). Those reach out to the public quote APIs the same way upstream does.

## How to run

### Prerequisites

- Linux x86-64 (tested on Ubuntu / Debian-likes)
- Qt 6.8.3 (`gcc_64` kit) — the build expects it under `~/fin/finterm/.qt/6.8.3/gcc_64/` or wherever your `setup.sh` provisioned it
- CMake 3.27+, GCC 12.3+ (or Clang 15+)
- Python 3.11+ for the stub (system python is fine)
- A separate Python venv for the data scripts (`venv-numpy2`) — `setup.sh` provisions it under `~/.local/share/com.fincept.terminal/`

### First-time setup

```bash
git clone <this-fork-url> ~/fin/finterm
cd ~/fin/finterm
./setup.sh                     # checks toolchain, provisions venvs, fetches Qt if needed
cmake --preset linux-release   # configure
cmake --build --preset linux-release
```

### Launch

```bash
# Convenience: symlink so `finterm` works from anywhere
ln -s ~/fin/finterm/start.sh ~/bin/finterm
finterm
```

What `start.sh` does, in order:

1. **Strips Qt window/toolbar/dock-layout state from `~/.config/Fincept/FinceptTerminal.conf`** via `tools/reset.sh --window-only`. Surgical: portfolio, watchlists, theme, workspaces, component usage stats are all preserved. Defends against the duplicate-floating-window bug where any prior layout corruption persists across launches and steals keyboard focus from the PIN screen. Skip with `FINCEPT_KEEP_WINDOW=1 finterm` if you want Qt to remember a hand-arranged layout.
2. **Starts the localhost stub if it isn't already running**, via `nohup`. Detects an existing stub regardless of whether it was started by `start.sh`, by hand, or by the optional systemd user unit (`tools/systemd/fincept-stub.service`). Logs to `~/.cache/fincept-stub.log`.
3. **Execs the Qt binary in the foreground.** Closing the Qt window returns control to your shell; the stub keeps running so the next launch is instant.

### Verify

After launch:
- The Qt window title reads **`finterm @ <hostname>`**.
- The top status bar shows **● LIVE** in green and `DATA: YAHOO ▾`.
- `pgrep -af FinceptTerminal` shows the Qt binary.
- `pgrep -af "yfinance_data.py --daemon"` shows the data daemon (spawned by the Qt app).
- `pgrep -af "tools/local_stub/server.py"` shows the localhost stub.
- `curl http://127.0.0.1:8765/health` returns 200.

### Tear down

```bash
pkill -f FinceptTerminal              # kills the Qt app + its data daemon child
pkill -f tools/local_stub/server.py   # kills the localhost stub
```

### Reset / recovery

If the app gets into a bad state — duplicate floating windows after a layout edit, stuck PIN screen, broken dock layout, "Install Analytics Libraries" prompt looping on every launch — the right tool is `tools/reset.sh`.

```bash
tools/reset.sh                  # default: strip Qt window/dock/toolbar state only
                                # (~1s, preserves portfolio + venv + everything else)
tools/reset.sh --window-only    # explicit form of the default; what start.sh runs
tools/reset.sh --full           # nuke Qt config + Python venv + caches
                                # (next launch reprovisions ~120 pip packages, 5–15 min)
tools/reset.sh --list           # show timestamped backups created by previous resets
tools/reset.sh --restore <ts>   # roll back a specific reset
```

Every destructive reset moves the affected directory aside as `<dir>.bak.<unix-ts>` rather than deleting outright, so `--restore` is always available. None of these touch your portfolio data — that lives in SQLite under `~/.local/share/com.fincept.terminal/data/` and is never moved or rebuilt.

Common scenarios:

| Symptom | Try |
|---|---|
| Two floating empty windows on launch / PIN screen behind another window | `tools/reset.sh` (`--window-only` is the default) |
| App is fine but stuck on a weird theme / layout | `tools/reset.sh` |
| Setup screen keeps offering "Install Analytics Libraries" every launch | Install `portaudio19-dev` (or distro equivalent), then `tools/reset.sh --full` once. PyAudio's system dep is now in `setup.sh`, so a fresh clone won't hit this. |
| Daemon hung / Yahoo calls timing out | `pkill -f "yfinance_data.py --daemon"` — the Qt app respawns it on next request |
| Stub returning 500s | `pkill -f tools/local_stub/server.py` then relaunch via `finterm`; `start.sh` will restart the stub |

## Notes on the localhost stub

`tools/local_stub/server.py` is **stdlib only** (no pip deps) and stores users in `~/.fincept-localhost/users.db` (SQLite). It accepts:

- Any 6-digit OTP code (the "real" code is logged to stdout for convenience)
- Any password reset / MFA flow without external email
- Any subscription tier you set via `/user/set-tier`

Endpoints not relevant to local use (marine, research, quantlib, chat, forum) return empty success envelopes so the UI doesn't show errors for features that need a server-side backend.

## Upstream

Everything UI-side is owned by Fincept Corporation. This is a personal fork for offline use. If you want the full SaaS experience — paid tiers, Databento data, AI features, billing, hosted backend — go to:

→ **[github.com/Fincept-Corporation/FinceptTerminal](https://github.com/Fincept-Corporation/FinceptTerminal)** ←

This fork tracks upstream best-effort but does not redistribute the cloud product, marketing assets, or paid-tier features.

## License

Upstream is **AGPL-3.0** (see [`LICENSE`](LICENSE)). This fork inherits that license.

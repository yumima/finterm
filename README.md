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
| Data leaving your machine | Quote APIs to Yahoo / akshare / etc. | Same — quote APIs only. **Nothing else.** |

The Qt UI is unchanged from upstream where it doesn't need to be. Where it does — auth, profile, credits — the client talks to the stub and gets stubbed answers, so the screens render normally.

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

`start.sh` ensures the local stub is up (starts it via `nohup` if not), then `exec`s the Qt binary in the foreground. The stub logs to `~/.cache/fincept-stub.log`.

### Verify

After launch:
- The top status bar should show **● LIVE** in green and `DATA: YAHOO ▾`
- `pgrep -af FinceptTerminal` shows the Qt binary
- `pgrep -af "yfinance_data.py --daemon"` shows the data daemon
- `pgrep -af "tools/local_stub/server.py"` shows the localhost stub
- `curl http://127.0.0.1:8765/health` returns 200

### Tear down

```bash
pkill -f FinceptTerminal              # kills the Qt app + its daemon child
pkill -f tools/local_stub/server.py   # kills the localhost stub
```

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

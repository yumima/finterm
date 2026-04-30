# Local stub server

This directory contains a Python stub server that replaces `api.fincept.in` for
the localhost-only fork of FinceptTerminal. It handles signup, login, profile,
subscription, and the rest of the endpoints the desktop client hits at runtime.

**Nothing ever leaves your machine.** Users are stored in a local SQLite file at
`~/.fincept-localhost/users.db`.

## Run

```bash
python3 tools/local_stub/server.py
```

Defaults to `http://127.0.0.1:8765`. Stdlib only — no `pip install` needed.
Requires Python 3.8+.

Useful flags:

| Flag | Default | Purpose |
|------|---------|---------|
| `--host` | `127.0.0.1` | bind host |
| `--port` | `8765` | bind port (must match `AppConfig::api_base_url()`) |
| `--db PATH` | `~/.fincept-localhost/users.db` | SQLite users DB |
| `-v` / `--verbose` | off | DEBUG logging |

## Auth flow

1. Open the Terminal — it'll show the **signup** screen (no fincept account is
   pre-created).
2. Fill in username / email / password and submit. The stub creates the user.
3. The OTP screen appears. **Any 6-digit code** works (`000000`, `123456`, …).
4. You're logged in. The stub issues an api_key and session_token; the C++
   client persists them in OS-native secure storage and the SQLite settings
   table, exactly as it would with the real fincept server.
5. On subsequent launches the saved api_key is used to fetch the profile from
   the stub — no re-login needed (until you log out).

## What the stub does NOT provide

These endpoints return empty success envelopes so the UI doesn't error, but the
features themselves have no local backend in this fork:

- **AI chat / LLM** — the upstream "fincept" LLM provider is disabled. Open
  Settings → LLM and configure your own provider (OpenAI, Anthropic, Ollama,
  etc.). Direct provider keys never round-trip through the stub.
- **Maritime intelligence** — empty vessels list.
- **Geopolitics intelligence** — empty events list.
- **QuantLib REST suite** — no computations; use the Python-embedded analytics
  in the Terminal instead.
- **Forum** — empty.
- **Economic calendar** — empty (the Terminal also has direct provider scrapers
  you can wire up later).
- **Live news WebSocket** — not implemented (stdlib has no WS server). The RSS
  feed path inside `NewsService` works directly against Bloomberg/Reuters/CNBC
  without the stub.

## Data sources that DO work without the stub

These connectors talk straight to third-party providers from your machine —
they don't go through the stub at all:

| Feature | Provider hit | Auth |
|---------|--------------|------|
| Equity quotes / OHLC | Yahoo Finance | none |
| Macro / economic indicators | FRED | free API key (your own) |
| Macro datasets | DBnomics | none |
| Macro / dev indicators | World Bank, IMF | none |
| China markets | AkShare | none |
| Crypto | Kraken, HyperLiquid (direct WS) | optional |
| Prediction markets | Polymarket | none (read-only) |
| News | RSS (Bloomberg, Reuters, CNBC, …) | none |
| Brokers | Zerodha/IBKR/Alpaca/etc. | broker OAuth (your own) |

## Reset / fresh start

Delete `~/.fincept-localhost/users.db` to wipe all local users. Delete the
saved C++-side session via the Terminal's Settings → Account → Logout (or
remove the QSettings entry / SecureStorage `api_key`).

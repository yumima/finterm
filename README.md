# finterm

A local-first financial-research terminal for your own machine: a native Qt6/C++ desktop app with a thin Python data layer and a built-in AI stack. No account, no cloud backend, no telemetry.

## Today's commits (2026-11-15)

Latest first.

- [`5efa629dc`](https://github.com/yumima/finterm/commit/5efa629dc) **ci:** unused lambda captures in the daemon socket error handlers (Clang -Werror)
- [`10f278adc`](https://github.com/yumima/finterm/commit/10f278adc) selftest: the Linux AppImage and the macOS disk image are run and must fetch data too
- [`a0b52e98d`](https://github.com/yumima/finterm/commit/a0b52e98d) selftest: `finterm --selftest` proves an install gets data; the Windows release installs itself and runs it
- [`6705c6439`](https://github.com/yumima/finterm/commit/6705c6439) windows: the data daemon connects over loopback TCP — there was no data at all
- [`2367213b7`](https://github.com/yumima/finterm/commit/2367213b7) data: statement overlay review fixes — bounded waits, one basis per table, discontinued years
- [`68e97f9c9`](https://github.com/yumima/finterm/commit/68e97f9c9) **chore(release):** update README download links and updates.json for v4.0.5
- [`a415a6cc0`](https://github.com/yumima/finterm/commit/a415a6cc0) release: 4.0.5
- [`fa080b8b5`](https://github.com/yumima/finterm/commit/fa080b8b5) data: operating income is the figure the company filed; a zero equity is not shown
- [`cdcf7145b`](https://github.com/yumima/finterm/commit/cdcf7145b) **ui:** tooltips are one short line
- [`f047bdb79`](https://github.com/yumima/finterm/commit/f047bdb79) data: cross-check Yahoo's statements against the company's own SEC filings
- [`1b7ddd528`](https://github.com/yumima/finterm/commit/1b7ddd528) ai: a model may quote a figure, never originate one

[See all commits →](https://github.com/yumima/finterm/commits/main)

## Install

<!-- DOWNLOAD-TABLE-START -->
| System | Download | First launch |
|---|---|---|
| **Linux** x86-64 | [AppImage (.run)](https://github.com/yumima/finterm/releases/download/v4.0.5/finterm-4.0.5-linux-x64-setup.run) | `chmod +x finterm-*.run && ./finterm-*.run` |
| **Windows** 10/11 x64 | [Installer (.exe)](https://github.com/yumima/finterm/releases/download/v4.0.5/finterm-4.0.5-windows-x64-setup.exe) | Run it. At SmartScreen: **More info → Run anyway** |
| **macOS** 13+ Apple Silicon | [Disk image (.dmg)](https://github.com/yumima/finterm/releases/download/v4.0.5/finterm-4.0.5-macos-arm64-setup.dmg) | Drag to Applications, then **right-click → Open → Open** |

Current release: [v4.0.5](https://github.com/yumima/finterm/releases/tag/v4.0.5) · [all releases](https://github.com/yumima/finterm/releases)
<!-- DOWNLOAD-TABLE-END -->

- **Linux** — the file is an AppImage: nothing is installed and no root is needed. Ubuntu 22.04+ needs FUSE 2 once (`sudo apt install libfuse2`), or run it with `--appimage-extract-and-run`.
- **Unsigned** — the installers are free, so they carry no paid certificate; that is the one extra click on Windows and macOS. They are built from this source by the public CI.
- **First start** — finterm sets up its Python environment, once, in about 5–15 minutes.
- **Updates** — **Help › Check for Updates**. finterm never checks on its own.

## What's inside

- **Markets, Watchlist, Dashboard** — live global quotes, charts, sector heatmap, extended-hours and futures moves.
- **News** — a deduplicated feed with region and portfolio filters, reader-mode articles, an AI TL;DR, and read-aloud.
- **Equity Research** — financials, earnings, analyst targets, technicals, peers, ownership and sentiment per ticker.
- **Portfolio** — multi-account holdings, performance, heatmap, blotter, CSV/JSON import and export.
- **Screener, Ownership, IPO Watch, Pre-IPO** — screens and insider, institutional and IPO-pipeline views built on public SEC EDGAR filings.
- **AI chat** — text or live voice conversation with the provider you choose, cloud or local.
- **Knowledge** — a markets and quant curriculum with calculators and a 47-strategy catalogue.
- **Workspaces** — multi-window docking with saved layouts, and local multi-user profiles behind a PIN.

## AI

Agents, skills and MCP tools work on the same watchlist, portfolio and paper-trade book you do, on Anthropic or on a local OpenAI-compatible server (Ollama, vLLM, llama.cpp, LM Studio). Every run is traced with its prompts, tool calls and cost; per-tool kill-switches and per-agent daily spend caps apply before tokens are spent. No provider is configured by default.

- [AI features](docs/how-to/ai-features.md) — set up a runtime, slash commands, scheduled agents, spend limits
- [Quant features](docs/how-to/quant-features.md) — AI Quant Lab, backtests, paper trading
- [Capability matrix](docs/how-to/capabilities.md) — what works on which runtime

## Privacy

- **Goes out** — only requests to the public data sources you use (Yahoo Finance, SEC EDGAR and the like) and to an LLM provider, if you configure one.
- **Never goes out** — analytics, crash reports, account data. There is no finterm server.
- **Stays local** — accounts, portfolios, keys and settings, under `~/.local/share/finterm/` and `~/.config/finterm/`.

## Build from source

Needs Qt 6.8+, CMake 3.27+, Python 3.11+ and GCC 12 / Clang 15 / MSVC 2022. `setup` installs what is missing, then configures and builds.

```bash
git clone https://github.com/yumima/finterm.git && cd finterm
./finterm.sh setup      # Windows: .\finterm.ps1 setup
./finterm.sh            # launch
```

| Command | What it does |
|---|---|
| `finterm` / `finterm start` | Launch the app |
| `finterm build` | Incremental build (`--clean` rebuilds, `--tests` adds the tests) |
| `finterm repair` | Fix a misbehaving app and keep your data: clears caches and window layout, rebuilds a broken Python environment |
| `finterm reset` | Wipe to a clean install, after a confirmation and a backup |
| `finterm install` / `uninstall` | Add or remove the desktop launcher |
| `finterm status` / `stop` | Show or stop the running app |

`finterm` here is `./finterm.sh` (symlink it onto your `PATH`) or `.\finterm.ps1` on Windows. `repair` and `reset` both snapshot your data to `~/.local/share/finterm-backups/` first. Linux is the regularly tested platform; macOS and Windows build in CI and are less exercised.

Contributing? `git config core.hooksPath .githooks` turns on the commit hooks (changelog above, personal-data guard).

More: [development guide](docs/DEVELOPMENT.md) · [architecture](docs/ARCHITECTURE.md) · [AI stack](plans/ai-stack.md)

## Troubleshooting

| Symptom | Fix |
|---|---|
| Odd windows, stale layout, general flakiness | `finterm repair` |
| Data scripts or Python environment broken | `finterm repair` |
| Quotes stuck or timing out | `pkill -f "yfinance_data.py --daemon"` — the app restarts it |
| Forgot a PIN | Login picker → select the user → **Forgot PIN? Reset this user** (removes that user's data) |

## License

Free and open source under [AGPL-3.0](LICENSE). A community project, not a company product.

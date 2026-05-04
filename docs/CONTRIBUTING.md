# Contributing to finterm

finterm is a community open-source fork — a native C++20/Qt6 financial-research terminal with embedded Python analytics. This guide is the canonical **how-to** for contributors: build, architecture, conventions.

> **Before you open a PR**, also read the contribution **policy**: [`.github/CONTRIBUTING.md`](../.github/CONTRIBUTING.md). Policy defines which PRs are accepted (linked approved issue, minimum scope, no auto-formatter churn, etc.); this file covers how to build and where code lives.

---

## Contents

- [Ways to Contribute](#ways-to-contribute)
- [Tech Stack](#tech-stack)
- [Prerequisites](#prerequisites)
- [Build](#build)
- [Run](#run)
- [Project Architecture](#project-architecture)
- [Code Conventions](#code-conventions)
- [Development Workflow](#development-workflow)
- [Language-Specific Guides](#language-specific-guides)
- [Getting Help](#getting-help)

---

## Ways to Contribute

| Area          | Examples                                                          |
|---------------|-------------------------------------------------------------------|
| C++ / Qt      | New screens, services, core infrastructure, performance fixes     |
| Python        | Analytics scripts, AI agents, data fetchers                       |
| Data sources  | Broker integrations, government / market data connectors          |
| Documentation | Fix broken/outdated docs (see policy for scope)                   |
| Testing       | Reproduce bugs, review PRs, write tests                           |

---

## Tech Stack

| Layer         | Technology                                                                           |
|---------------|--------------------------------------------------------------------------------------|
| Language      | **C++20** — MSVC 19.38 (VS 2022 17.8) / GCC 12.3 / Apple Clang 15.0                  |
| UI            | **Qt 6.8.3 EXACT** Widgets (pinned)                                                  |
| Charts        | Qt6 Charts                                                                           |
| Networking    | Qt6 Network + Qt6 WebSockets                                                         |
| Database      | Qt6 Sql (SQLite)                                                                     |
| Analytics     | Embedded **Python 3.11.9** (4000+ scripts)                                           |
| Excel I/O     | QXlsx v1.4.9 (FetchContent, pinned commit)                                           |
| Mapping       | QGeoView (pinned commit)                                                             |
| Build         | **CMake 3.27.7 + Ninja 1.11.1** (pinned, unity build)                                |

---

## Prerequisites

Pinned toolchain versions — enforced by CMake. Mismatch produces a clear fail-fast error.

| Tool          | Version                                                                                 |
|---------------|-----------------------------------------------------------------------------------------|
| C++ compiler  | MSVC 19.38 (VS 2022 17.8) / GCC 12.3 / Apple Clang 15.0 (Xcode 15.2)                    |
| CMake         | **3.27.7** — [cmake.org](https://cmake.org/download/)                                   |
| Ninja         | **1.11.1** — [releases](https://github.com/ninja-build/ninja/releases)                  |
| Qt            | **6.8.3** — [Qt Online Installer](https://www.qt.io/download-qt-installer)              |
| Python        | **3.11.9** — [python.org](https://www.python.org/downloads/release/python-3119/)        |
| Git           | latest — [git-scm.com](https://git-scm.com)                                             |

Optional (speeds up rebuilds): **ccache 4.13.4** on Windows is auto-detected.

---

## Build

### Fastest — automated setup script

```bash
git clone <this-repo-url> ~/fin/finterm
cd ~/fin/finterm
./setup.sh        # Linux / macOS — installs toolchain + Qt via aqtinstall
./finterm.sh build
setup.bat         # Windows — run from a VS 2022 Developer Command Prompt
```

### Manual — CMake presets

All day-to-day development uses presets from `fincept-qt/CMakePresets.json`.

```bash
cd fincept-qt

# Configure (one-time, or after CMakeLists.txt changes)
cmake --preset win-release        # Windows
cmake --preset linux-release      # Linux
cmake --preset macos-release      # macOS

# Build (run after every code change — incremental)
cmake --build --preset win-release        # Windows
cmake --build --preset linux-release      # Linux
cmake --build --preset macos-release      # macOS
```

Debug builds: replace `release` with `debug` in both commands.

Manual configure if presets can't resolve Qt:

```powershell
cmake -B build/win-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build/win-release
```

---

## Run

```bash
.\build\win-release\FinceptTerminal.exe                                           # Windows
./build/linux-release/FinceptTerminal                                             # Linux
./build/macos-release/FinceptTerminal.app/Contents/MacOS/FinceptTerminal          # macOS
```

(The binary name is `FinceptTerminal` because the project's CMake target was inherited from upstream — renaming it would break installers and registered file types. Day-to-day, just use `finterm.sh start`.)

---

## Project Architecture

### Repository layout

```
finterm/
├── fincept-qt/                 # Main C++ application (all development happens here)
│   ├── src/                    # C++ source
│   ├── scripts/                # Embedded Python analytics (4000+ files)
│   ├── resources/              # Icons, knowledge content, Qt resources
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── CLAUDE.md               # Performance/architecture rules (P1–P15, D1–D5)
│   └── DESIGN_SYSTEM.md        # Obsidian UI/UX spec
├── docs/                       # Repo-wide documentation (this file lives here)
├── tools/                      # Localhost stub server, systemd unit
├── .github/                    # Issue/PR templates, workflows, contribution policy
├── finterm.sh                  # one-stop CLI: start / build / reset / install / stop / status
├── setup.sh                    # toolchain preflight + provisioner
└── README.md
```

### C++ source layout (`fincept-qt/src/`)

```
src/
├── app/              # Entry point, MainWindow, routing, splash
├── core/             # Config, events, logging, Result<T>, session
├── ui/               # Theme, reusable widgets, tables, charts, navigation
├── network/          # http/ and websocket/ clients
├── storage/          # SQLite, cache, secure storage, repositories
├── auth/             # Guest + registered auth, JWT (against localhost stub)
├── python/           # Embedded Python bridge, PythonRunner
├── datahub/          # DataHub producers/consumers
├── services/         # Service domains — market data, news, agents, workflow, etc.
├── trading/          # Trading core + broker integrations
├── mcp/              # Model Context Protocol infrastructure
├── ai_chat/          # AI chat UI + LlmService
└── screens/          # 50+ terminal screens, one subdirectory each
```

### Python scripts (`fincept-qt/scripts/`)

```
scripts/
├── Analytics/        # CFA-level quant modules — equity, portfolio, derivatives,
│                     #   fixed income, economics, corporate finance
├── agents/           # AI agent frameworks
├── ai_quant_lab/     # ML, factor discovery, HFT, RL trading, vision quant
├── agno_trading/     # Agno-based trading agents
└── *.py              # 100+ top-level data fetchers (market, gov, economic, alt)
```

See `fincept-qt/CLAUDE.md` for the detailed service catalog and architecture rules.

---

## Code Conventions

### C++

- **Standard:** C++20
- **Naming:** `snake_case` functions/variables, `PascalCase` types/classes
- **Namespaces:** `namespace fincept {}`, `namespace fincept::ui {}`; never `using namespace std;` (the `fincept` namespace name is a code-internal vestige from the upstream project — keep it, it's not user-visible)
- **Qt:** `Q_OBJECT` in all QObject subclasses; pointer-to-member signal/slot syntax only (never `SIGNAL()`/`SLOT()` string macros)
- **Error handling:** `Result<T>` — no raw exception-based APIs across module boundaries
- **Logging:** `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` / `LOG_DEBUG` with a context tag, e.g. `LOG_INFO("MarketData", "Fetched 12 quotes")`. Never log API keys or credentials.

### Python

- **Style:** match surrounding code. Do **not** run Black / autopep8 / isort on existing files.
- **Logging:** use the `logging` module (`logger = logging.getLogger(__name__)`, `logger.info(...)`), never `print()`. Scripts run inside an embedded Qt runtime; `print()` output never reaches the user.
- **Entry points:** C++ invokes scripts through `PythonRunner::instance().run(...)` — your script receives a JSON payload on stdin and must return JSON on stdout. See existing scripts in `scripts/` for the pattern.

### UI / design

Follow `fincept-qt/DESIGN_SYSTEM.md` (Obsidian design system). Do not introduce ad-hoc color values, typography, or spacing — use the tokens.

### Mandatory architecture rules (summary)

Full detail in `fincept-qt/CLAUDE.md`. These are **non-negotiable** — PRs that violate them will be sent back:

- **P1.** Never block the UI thread — no `waitForFinished()` on main thread.
- **P2.** Lazy screen construction — use `register_factory()` for any screen that fetches data.
- **P3.** Timers must start/stop in `showEvent()` / `hideEvent()`, never in constructors.
- **P4.** Python subprocesses go through `PythonRunner` (max 3 concurrent).
- **P6.** Screens render UI only; services do all fetching, caching, processing.
- **P14.** Logs use the `LOG_*` macros (C++) or `logger` (Python). Never `printf` / `print` / `std::cout`.
- **D1–D5.** Streaming data flows through the DataHub — never spawn Python directly from a screen. See `fincept-qt/DATAHUB_ARCHITECTURE.md`.

---

## Development Workflow

### Branch naming

```
feat/add-options-screen
fix/chart-render-crash
docs/update-python-guide
perf/market-data-coalesce
refactor/broker-http
```

Never PR from your fork's `main` — always a topic branch.

### Commit style

```
type: short imperative subject line

Optional body explaining why, not what. Wrap at ~72 cols.

Types: feat, fix, docs, refactor, test, chore, perf
```

### Before you open a PR

1. Confirm the issue you're fixing carries one of: `good-first-issue`, `help-wanted`, `scope:approved` (see [`.github/CONTRIBUTING.md`](../.github/CONTRIBUTING.md)).
2. Build locally and run the app — verify the fix / feature works end-to-end.
3. Keep the diff minimal. No auto-formatter churn.
4. Fill out the PR template honestly; don't tick boxes you didn't verify.

Review process, scope gate, and close-on-sight list are in [`.github/CONTRIBUTING.md`](../.github/CONTRIBUTING.md).

---

## Language-Specific Guides

| Guide                                                   | Coverage                                                  |
|---------------------------------------------------------|-----------------------------------------------------------|
| [C++ Guide](./CPP_CONTRIBUTOR_GUIDE.md)                 | Screens, services, core infrastructure, widgets, Qt patterns |
| [Python Guide](./PYTHON_CONTRIBUTOR_GUIDE.md)           | Analytics modules, data fetchers, AI agents, PythonRunner contract |
| [Architecture](./ARCHITECTURE.md)                       | System design, module boundaries, data flow               |

---

## Questions before you code?

Ask on the issue before writing code — maintainers can scope-approve early and save you a wasted PR.

| Channel     | Link                                                   |
|-------------|--------------------------------------------------------|
| Issues      | GitHub Issues on this repository                       |
| Discussions | GitHub Discussions on this repository                  |
| Discord     | [discord.gg/ae87a8ygbN](https://discord.gg/ae87a8ygbN) |

This is a community open-source project. There is no commercial support email or formal SLA — patches and PRs are the path to fixes. Good first issues carry the `good-first-issue` label.

---

**License:** AGPL-3.0-or-later

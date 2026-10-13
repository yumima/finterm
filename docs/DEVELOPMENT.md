# Developing finterm

finterm is a solo-maintained, local-first financial terminal: a native
C++20 / Qt6 desktop app with a thin Python data layer and a local AI
stack (hearth → Ollama on loopback). This doc covers working on the
code; for installing and running the app, the [README](../README.md)
quick start (`finterm setup` / `finterm build` / `finterm`) is the
canonical path.

## Prerequisites

- **C++20 compiler** — GCC 12+/Clang 15+ (Linux/macOS), MSVC 2022 (Windows)
- **CMake ≥ 3.27 + Ninja**
- **Qt 6.8+** — system-sourced (distro `qt6-*` packages / Homebrew `qt`),
  not vendored. Ship builds can pin the tested version via
  `FINCEPT_QT_STRICT`.
- **Python 3.11** — the data daemon's venv is provisioned by
  `finterm setup` under `~/.local/share/com.fincept.terminal/`.

## Build

The `finterm` CLI wraps everything (`finterm build`, `finterm build
--tests`). Under the hood these are CMake presets:

```bash
cd fincept-qt
cmake --preset linux-release          # or linux-debug / macos-* / win-*
cmake --build --preset linux-release -j"$(nproc)"
./build/linux-release/finterm.bin
```

Tests build in their own tree (`-DFINCEPT_BUILD_TESTS=ON`):

```bash
cmake --build build/linux-tests && (cd build/linux-tests/tests && ctest)
```

Don't launch the app itself for throwaway diagnostics: its OS-keyring
credentials (PIN + API keys) are global across `--profile`s.

## Layout

```
fincept-qt/
  src/
    app/         MainWindow, DockScreenRouter — shell + navigation
    screens/     one directory per screen (equity_research, portfolio, …)
    services/    data + domain services (QueryStore subscriptions, agents, TTS/STT)
    ai_chat/     LLM client (LlmService), ChatPane, personas, roles, hearth plumbing
    mcp/         in-process tool registry (300+ tools) + external MCP servers
    storage/     SQLite repositories + numbered migrations
    ui/          theme, navigation chrome, shared formatting (NumberFormat.h)
  resources/
    knowledge/   the built-in book — quant strategies/factors/risk, playbooks,
                 glossary — rendered by the Knowledge screen and the AI tutor
    ai/          capability matrix JSON (generates docs/how-to/capabilities.md)
  scripts/       Python data layer (yfinance daemon, quant modules)
  tests/         ctest suites
```

Deeper background: [ARCHITECTURE.md](./ARCHITECTURE.md) (whole app),
[`plans/ai-stack.md`](../plans/ai-stack.md) (AI stack).

## Conventions that bite

- **No fake data, ever.** Absent values render as placeholders
  (`NumberFormat.h`), never invented. Model output that can't be
  parsed is shown unparsed, not massaged into a value.
- **Formatting goes through `ui/formatting/NumberFormat.h`** —
  don't roll inline formatters.
- **Migrations are append-only** (`storage/sqlite/migrations/vNNN_*`),
  registered in `MigrationRunner.h` + `app/main.cpp`.
- **Screens pull, services own data.** Screens subscribe via
  `QueryStore`; blanket-unsubscribe by owner, never from inside a
  feed callback.
- **AI calls carry an explicit `PersonaScope`** (persona = tool
  curation, role = model binding via `scope_for_role`). Nothing
  reads a global persona.
- A pre-commit hook regenerates the README's "Today's commits"
  changelog — expect an extra `README.md` hunk in every commit.

## Common workflows

**New Python data script** — follow `scripts/yfinance_data.py`:
CLI args in, JSON on stdout; test standalone with the venv python,
then rebuild so it's copied into the build's `scripts/`.

**New screen** — directory under `src/screens/`, register a factory
in `MainWindow::setup_dock_screens`, a title in `DockScreenRouter`,
and (optionally) entries in FKeyBar/CommandBar. Add sources to
`CMakeLists.txt`.

**Schema change** — new numbered migration file; never edit an old
one. Repositories map columns explicitly.

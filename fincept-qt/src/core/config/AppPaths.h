#pragma once
#include <QString>

namespace fincept {

/// Central registry for all application file-system paths.
///
/// One root directory, split into what is the USER's and what is the APP's:
///   Windows : %LOCALAPPDATA%/com.fincept.terminal/
///   macOS   : ~/Library/Application Support/com.fincept.terminal/
///   Linux   : ~/.local/share/com.fincept.terminal/
///
///   user/          — personal data, the one folder to back up or erase
///     data/        —   databases: auth.db, users/<name>.db, cache.db
///     files/       —   the user's files (report drafts, Pre-IPO overrides)
///     workspaces/  —   saved layouts (.fwsp)
///     features/    —   per-feature databases (M&A deals, Alpha Arena, …)
///     agents/      —   agent memory, task store, eval history
///   (system — reinstallable or regenerable, never personal)
///   cache/         — downloads, safe to delete
///   market_data/   — public SEC/FINRA indexes built locally
///   models/        — ML model files
///   logs/ crashdumps/ runtime/ python/ venv-*/ uv/
///
/// A non-default profile keeps its own user/, cache/ and logs/ under
/// profiles/<name>/. Credentials are not files: they live in the OS keyring.
///
/// Call AppPaths::ensure_all() once at startup to create every sub-directory.
class AppPaths {
  public:
    /// Root: %LOCALAPPDATA%\com.fincept.terminal  (Windows)
    static QString root();

    /// <profile>/user — everything personal; nothing else is.
    static QString user();

    /// user/data  — the user's databases
    static QString data();

    /// root/logs  — log files
    static QString logs();

    /// user/files — the user's own files
    static QString files();

    /// user/features — per-feature databases
    static QString features();

    /// user/agents — agent memory, task store, eval history
    static QString agents();

    /// root/cache — tile caches and transient network data
    static QString cache();

    /// root/market_data — public-data indexes built locally (system)
    static QString market_data();

    /// root/models — ML model files
    static QString models();

    /// root/runtime — Python interpreter, UV, virtual environments
    static QString runtime();

    /// user/workspaces — saved workspace files (.fwsp)
    static QString workspaces();

    /// root/crashdumps — minidumps written by the unhandled-exception filter
    static QString crashdumps();

    /// Create all sub-directories if they don't exist.
    /// Call once before any path is used.
    static void ensure_all();
};

} // namespace fincept

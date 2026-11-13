#pragma once
#include <QString>

namespace finterm {

/// Central registry for all application file-system paths.
///
/// One root directory, split into what is the USER's and what is the APP's:
///   Windows : %LOCALAPPDATA%/finterm/
///   macOS   : ~/Library/Application Support/finterm/
///   Linux   : ~/.local/share/finterm/
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
    /// Root: %LOCALAPPDATA%\finterm  (Windows). While an older build's folder
    /// (AppIdentity::kBundleIdLegacy) has not been moved yet, that folder.
    static QString root();

    /// Move an older build's data root to the current name — one directory
    /// rename, refused while any instance holds a database lock in it — and
    /// repoint the Python venvs inside it. Call first thing in main(), before
    /// anything reads root(). False when an older build's folder is still
    /// there and could not be moved: the app must not run on it, since its
    /// settings and keyring entries have moved on (see main.cpp).
    static bool migrate_root();

    /// The older build's data root (AppIdentity::kBundleIdLegacy), and the
    /// current one whether or not it exists yet — for messages.
    static QString legacy_root_path();
    static QString current_root_path();

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

} // namespace finterm

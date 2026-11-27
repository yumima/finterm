// src/core/config/LayoutMigrations.h
#pragma once

#include <QString>

namespace finterm {

/// Versioned, idempotent migrations for on-disk *layout* changes — where the
/// app keeps its settings, caches, and data when those locations move between
/// releases (e.g. the settings-store and data-folder renames).
///
/// Design goals (in priority order):
///   1. Fast no-op. The common case — already up to date — is a single
///      QSettings read of one integer, then return. Nothing is scanned and no
///      extra process is spawned (this runs in-process at the top of main(),
///      not as a separate `--preflight` launch).
///   2. Self-healing. When the stored layout version is behind, the pending
///      steps run in order, each idempotent and safe to repeat.
///   3. Narrated. A step that actually does something prints one line to
///      stdout, so a terminal launch shows what happened before the GUI opens.
///      A no-op says nothing.
///
/// This owns *structural* moves only (paths / settings-store names). Content
/// migrations that are coupled to a subsystem (e.g. broker-credential format in
/// AccountManager) stay with that subsystem, gated by their own idempotent
/// flags — same pattern, local ownership.
///
/// AppIdentity is the single source of truth for the names/paths involved, so
/// the migration logic never hardcodes a path the way a shell launcher would.
class LayoutMigrations {
  public:
    /// The on-disk layout version this build expects. Bump by one and add a
    /// matching `if (stored < N)` step in run() whenever a release moves where
    /// data lives.
    static constexpr int kCurrentVersion = 4;

    /// Run any pending migrations (stored version < kCurrentVersion), then
    /// stamp the current version. Call once, early in main(), primary instance
    /// only. Safe to call on a fresh install (all steps no-op).
    static void run();

    /// The stored layout version is above kCurrentVersion: a newer finterm
    /// moved the data to a layout this build does not know.
    static bool newer_than_build();

    /// Before run(): note the stored version `build` found, when run() is
    /// about to raise it — what a rollback of `build` puts back.
    static void remember_before(const QString& build);

    /// An update being rolled back by `build`, which raised the layout
    /// version: put back the version the previous build left, so it does not
    /// take its own data for a newer finterm's. (Each layout step keeps the
    /// old location readable by the build before it — a step that can't must
    /// be undone here.)
    static void undo_for_rollback(const QString& build);
};

} // namespace finterm

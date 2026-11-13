#pragma once

namespace finterm {

/// Single source of truth for the application's identity.
///
/// Two deliberately *separate* concerns live here:
///
///  1. The **persistent storage identity** (kBundleId / kOrg / kApp / …). This
///     keys every QSettings store, the OS keychain / credential service, and
///     the on-disk data root (see AppPaths). Changing it moves every user's
///     data, so a change must ship with a migration (see the *Legacy values).
///
///  2. The **display brand** (kDisplayName) — what the user actually sees in
///     window titles, the About box, etc. Rebranding the app means changing
///     this (and the visible string literals), wired in via
///     QGuiApplication::setApplicationDisplayName(). It is intentionally
///     decoupled from the storage identity above, so the brand can change
///     without ever touching where bytes live on disk.
struct AppIdentity {
    // ── On-disk identity ─────────────────────────────────────────────────────
    // Names the data root, the QSettings directory and the OS keychain /
    // credential service. Changing any of these orphans existing users' data,
    // settings and stored secrets UNLESS a migration moves them: the previous
    // values below are kept only so AppPaths::migrate_root(),
    // LayoutMigrations and SecureStorage::migrate_identity() can find and move
    // what an older build left behind.
    /// On-disk data root (AppPaths::root) and OS keychain/credential service id.
    static constexpr const char* kBundleId = "finterm";
    /// QSettings organization (settings directory, e.g. ~/.config/finterm).
    static constexpr const char* kOrg = "finterm";
    /// Previous data root / keychain service and QSettings organization.
    /// Read once, by the identity migration, and never written.
    static constexpr const char* kBundleIdLegacy = "com.fincept.terminal";
    static constexpr const char* kOrgLegacy = "Fincept";

    // ── Settings-store name ──────────────────────────────────────────────────
    /// QSettings application (primary settings store → finterm.conf).
    static constexpr const char* kApp = "finterm";
    /// QSettings application for the secure/secret XOR-fallback store.
    static constexpr const char* kAppSecure = "finterm-Secure";
    /// Older settings-store names (under kOrgLegacy) — read once, for migration only.
    static constexpr const char* kAppLegacy = "FinceptTerminal";
    static constexpr const char* kAppSecureLegacy = "FinceptTerminal-Secure";

    // ── Display brand ────────────────────────────────────────────────────────
    /// User-visible product name. Set via setApplicationDisplayName() and used
    /// in window titles / About text.
    static constexpr const char* kDisplayName = "finterm";
};

} // namespace finterm

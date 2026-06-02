#pragma once

namespace fincept {

/// Single source of truth for the application's identity.
///
/// Two deliberately *separate* concerns live here:
///
///  1. The **persistent storage identity** (kBundleId / kOrg / kApp / …). This
///     keys every QSettings store, the OS keychain / credential service, and
///     the on-disk data root (see AppPaths). It is an opaque, stable identifier
///     — NOT a brand. It is **frozen**: changing any of these values orphans
///     existing users' data, settings, and stored secrets. Leave them alone.
///
///  2. The **display brand** (kDisplayName) — what the user actually sees in
///     window titles, the About box, etc. Rebranding the app means changing
///     this (and the visible string literals), wired in via
///     QGuiApplication::setApplicationDisplayName(). It is intentionally
///     decoupled from the storage identity above, so the brand can change
///     without ever touching where bytes live on disk.
struct AppIdentity {
    // ── Frozen on-disk identity — DO NOT CHANGE ──────────────────────────────
    // Names the data root and the OS keychain/credential service. Opaque, stable,
    // invisible to users; changing these orphans existing portfolio/auth data and
    // stored secrets.
    /// On-disk data root (AppPaths::root) and OS keychain/credential service id.
    static constexpr const char* kBundleId  = "com.fincept.terminal";
    /// QSettings organization (settings directory, e.g. ~/.config/Fincept).
    static constexpr const char* kOrg       = "Fincept";

    // ── Settings-store name (rebranded; migrated, not frozen) ────────────────
    // The QSettings *application* name = the .conf file basename. Rebranded to
    // "finterm"; a one-time copy from kAppLegacy preserves existing settings
    // (see the settings migration in app/main.cpp).
    /// QSettings application (primary settings store → finterm.conf).
    static constexpr const char* kApp       = "finterm";
    /// QSettings application for the secure/secret XOR-fallback store.
    static constexpr const char* kAppSecure = "finterm-Secure";
    /// Pre-rebrand settings-store names — read once, for migration only.
    static constexpr const char* kAppLegacy       = "FinceptTerminal";
    static constexpr const char* kAppSecureLegacy = "FinceptTerminal-Secure";

    // ── Display brand — safe to rebrand ──────────────────────────────────────
    /// User-visible product name. Set via setApplicationDisplayName() and used
    /// in window titles / About text.
    static constexpr const char* kDisplayName = "finterm";
};

} // namespace fincept

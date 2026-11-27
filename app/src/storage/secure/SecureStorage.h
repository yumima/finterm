#pragma once
#include "core/result/Result.h"

#include <QString>
#include <QStringList>

namespace finterm {

/// Secure credential storage using OS-native backends:
///   Windows : Windows Credential Manager (DPAPI-encrypted, per-user DPAPI key)
///   macOS   : Security.framework Keychain (per-user Keychain, OS-protected)
///   Linux   : libsecret / Secret Service (GNOME Keyring, KWallet) when the
///             build found libsecret-1 — the credential is held by the
///             desktop keyring and encrypted at rest with the login keyring.
///             WITHOUT libsecret the fallback is XOR-obfuscated QSettings,
///             which is NOT cryptographically secure: the obfuscation key is
///             derived from machine-local identifiers and stored beside the
///             data, so anyone who can read the user profile can recover
///             every secret. That fallback stops casual grep-through-config
///             inspection and nothing more. Build with libsecret-1-dev for
///             production; SecureStorage::backend_name() reports which is
///             actually in use at runtime.
/// The PIN-manager PBKDF2 path still protects the PIN itself even on Linux —
/// but API keys, session tokens, and PIN-salt/hash stored here inherit only
/// the XOR obfuscation on that platform.
class SecureStorage {
  public:
    static SecureStorage& instance();

    Result<void> store(const QString& key, const QString& value);
    Result<QString> retrieve(const QString& key);
    Result<void> remove(const QString& key);

    /// True when `r` failed because the key does not exist — as opposed to
    /// the keyring being locked or unreachable, which must never be read as
    /// "unset" (a migration would then overwrite a real value).
    static bool is_not_found(const Result<QString>& r);

    /// Read `key`; if it is absent and `legacy_key` holds a value, move that
    /// value to `key`, delete `legacy_key`, and return it. For credentials a
    /// service once stored under its own id, invisible to Settings.
    Result<QString> retrieve_migrating(const QString& key, const QString& legacy_key);

    /// Whether this backend can list its entries (list_keys). Only the
    /// Linux Secret Service can; the Windows/macOS/obfuscated-file backends
    /// can only be asked for a named key.
    static bool can_enumerate();

    /// Every stored key under the active profile that starts with `prefix`
    /// (unscoped names, as passed to store()). Values are never returned.
    /// An error — distinct from an empty list — when the search itself
    /// failed or the backend cannot enumerate (see can_enumerate()).
    Result<QStringList> list_keys(const QString& prefix = {});

    /// Remove every secret of the active profile, on every backend (a fresh
    /// start). With `backup_tag`, each one is first copied, in the same
    /// keyring, to "<profile scope><backup_tag>/<key>" — out of the way of the
    /// new setup, which never reads that name, but still there; an entry that
    /// can't be copied is left in place. Earlier backups ("pre-reset-…/") are
    /// never touched. The number removed, or an error when the keyring could
    /// not be searched or an entry would not go.
    Result<int> remove_all_in_profile(const QString& backup_tag = {});

    /// Run every known legacy-id migration once. Called at startup so
    /// Settings › Credentials shows the value services actually use.
    void migrate_legacy_ids();

    /// Once: move every secret an older build filed under the previous keyring
    /// identity (AppIdentity::kBundleIdLegacy) to the current one, across all
    /// profiles, never overwriting a value already set. Deferred while that
    /// build's data folder is still in use, and retried while the keyring is
    /// unavailable. Call at startup before anything reads a secret.
    void migrate_identity();

    /// Which backend is actually protecting these secrets on this build and
    /// platform — "Windows Credential Manager", "macOS Keychain",
    /// "libsecret (Secret Service)", or "obfuscated file (NOT SECURE)".
    ///
    /// Exposed because the answer differs by build, and a user storing API
    /// keys deserves to know whether they are in the OS keyring or merely
    /// obscured in a config file.
    static QString backend_name();

  private:
    SecureStorage() = default;
};

} // namespace finterm

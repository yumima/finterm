#pragma once
#include "core/result/Result.h"

#include <QString>

namespace fincept {

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

} // namespace fincept

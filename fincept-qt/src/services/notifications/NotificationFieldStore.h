#pragma once
// Where a notification provider's configuration fields live.
//
// Plain settings (chat id, SMTP host, phone number) stay in the per-user
// settings table. Secrets (bot tokens, SMTP passwords, API keys, webhook URLs
// — a webhook URL IS the credential) go to the OS keyring, scoped to the
// signed-in local user exactly as the settings table is: two local users on
// one profile never share or overwrite each other's channels. They used to
// sit in the settings table in plaintext, readable from the SQL console and
// by AI tools; a value still found there is moved on first read.
//
// Both the providers and Settings › Notifications go through these helpers,
// so the two can never disagree about where a field is.

#include "auth/AuthManager.h"
#include "storage/repositories/SettingsRepository.h"
#include "storage/secure/SecureStorage.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace fincept::notifications {

inline bool is_secret_field(const QString& field) {
    const QString f = field.toLower();
    // Exact names: the generic webhook's whole URL and ntfy's private topic
    // are bearer credentials even though their names say nothing secret.
    if (f == QLatin1String("url") || f == QLatin1String("topic"))
        return true;
    static const char* kSecretParts[] = {"pass", "token", "secret", "api_key", "apikey", "webhook",
                                         "auth", "private", "user_key", "routing_key", "sid"};
    for (const char* part : kSecretParts)
        if (f.contains(QLatin1String(part)))
            return true;
    return false;
}

/// Keyring prefix holding a local user's notification secrets. Empty when no
/// user is signed in — secrets are then neither readable nor writable.
inline QString user_secret_prefix(const QString& username) {
    return username.isEmpty() ? QString{} : QStringLiteral("users/") + username.toLower() + QLatin1Char('/');
}

inline QString current_user_secret_prefix() {
    return user_secret_prefix(auth::AuthManager::instance().session().user_info.username);
}

/// The field's value; std::nullopt when it is a secret that cannot be read
/// right now (keyring locked/unreachable, or nobody signed in). Callers must
/// not treat nullopt as "empty" — saving it back would erase the real secret.
inline std::optional<QString> read_field(const QString& category, const QString& field) {
    const QString key = category + QLatin1Char('.') + field;
    auto& repo = SettingsRepository::instance();
    if (!is_secret_field(field)) {
        auto r = repo.get(key);
        return r.is_ok() ? r.value() : QString{};
    }
    const QString prefix = current_user_secret_prefix();
    if (prefix.isEmpty())
        return std::nullopt;
    auto& store = SecureStorage::instance();
    auto s = store.retrieve(prefix + key);
    if (s.is_ok())
        return s.value();
    if (!SecureStorage::is_not_found(s))
        return std::nullopt;
    auto legacy = repo.get(key);
    if (!legacy.is_ok() || legacy.value().isEmpty())
        return QString{};
    if (store.store(prefix + key, legacy.value()).is_ok())
        repo.remove(key);
    return legacy.value();
}

/// Per-user list of the secret keys written, so a user's secrets can be
/// removed on every backend (only libsecret can enumerate the keyring).
inline QString index_key(const QString& prefix) {
    return prefix + QStringLiteral("notif_index");
}

inline void remember_key(const QString& prefix, const QString& key) {
    auto& store = SecureStorage::instance();
    auto r = store.retrieve(index_key(prefix));
    QStringList keys = r.is_ok() ? r.value().split(QLatin1Char('\n'), Qt::SkipEmptyParts) : QStringList{};
    if (!keys.contains(key)) {
        keys.append(key);
        store.store(index_key(prefix), keys.join(QLatin1Char('\n')));
    }
}

/// Returns false when a secret could not be written (the caller should say so).
inline bool write_field(const QString& category, const QString& field, const QString& value) {
    const QString key = category + QLatin1Char('.') + field;
    auto& repo = SettingsRepository::instance();
    if (!is_secret_field(field))
        return repo.set(key, value, category).is_ok();
    const QString prefix = current_user_secret_prefix();
    if (prefix.isEmpty())
        return false;
    auto& store = SecureStorage::instance();
    const bool ok = value.isEmpty() ? store.remove(prefix + key).is_ok() : store.store(prefix + key, value).is_ok();
    if (ok) {
        if (!value.isEmpty())
            remember_key(prefix, key);
        repo.remove(key); // never leave a plaintext copy behind
    }
    return ok;
}

/// Drop every notification secret a local user stored (on user deletion).
inline void remove_user_secrets(const QString& username) {
    const QString prefix = user_secret_prefix(username);
    if (prefix.isEmpty())
        return;
    auto& store = SecureStorage::instance();
    QStringList keys;
    if (auto r = store.retrieve(index_key(prefix)); r.is_ok())
        for (const QString& k : r.value().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            keys.append(prefix + k);
    for (const QString& k : store.list_keys(prefix + QStringLiteral("notif_")))
        if (!keys.contains(k))
            keys.append(k);
    for (const QString& k : keys)
        store.remove(k);
    store.remove(index_key(prefix));
}

} // namespace fincept::notifications

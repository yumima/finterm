#include "auth/AuthManager.h"

#include "auth/AuthService.h"
#include "auth/PinManager.h"
#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "services/notifications/NotificationFieldStore.h"
#include "storage/repositories/SettingsRepository.h"
#include "storage/secure/SecureStorage.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSysInfo>

namespace finterm::auth {

AuthManager& AuthManager::instance() {
    static AuthManager s;
    return s;
}

AuthManager::AuthManager() {}

void AuthManager::set_loading(bool v) {
    if (is_loading_ != v) {
        is_loading_ = v;
        emit loading_changed(v);
    }
}

QJsonObject AuthManager::unwrap_data(const QJsonObject& raw) const {
    if (raw.contains("data") && raw["data"].isObject()) {
        const auto inner = raw["data"].toObject();
        if (inner.contains("data") && inner["data"].isObject()) {
            return inner["data"].toObject();
        }
        return inner;
    }
    return raw;
}

QString AuthManager::generate_device_id() const {
    QString info =
        QSysInfo::machineHostName() + "|" + QSysInfo::productType() + "|" + QSysInfo::currentCpuArchitecture();
    QByteArray hash = QCryptographicHash::hash(info.toUtf8(), QCryptographicHash::Sha256).toHex();
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    quint32 random = QRandomGenerator::global()->generate();
    return QString("finterm_desktop_%1_%2_%3")
        .arg(QString::fromUtf8(hash.left(16)), timestamp, QString::number(random, 36));
}

// ── Token helper — single place to set/clear tokens on HttpClient ────────────

static void apply_tokens(const QString& api_key, const QString& session_token) {
    auto& http = finterm::HttpClient::instance();
    http.set_auth_header(api_key);
    http.set_session_token(session_token);
}

static void clear_tokens() {
    apply_tokens({}, {});
}

// ── Session persistence (SQLite via SettingsRepository) ──────────────────────

bool AuthManager::save_session() {
    if (session_.api_key.isEmpty())
        return false;
    QJsonDocument doc(session_.to_json());
    const QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    // Persist to auth.db (always open) so load_session() can read it before
    // the per-user DB is opened.
    AuthService::instance().save_session(session_.api_key, json);
    // Also keep in OS-native SecureStorage as the durable credential.
    auto sr = finterm::SecureStorage::instance().store("api_key", session_.api_key);
    if (sr.is_err()) {
        LOG_WARN("Auth", "SecureStorage: failed to persist api_key");
        return false;
    }
    return true;
}

void AuthManager::load_session() {
    // Step 1: recover api_key from SecureStorage — doesn't need any DB.
    const auto secure_key = finterm::SecureStorage::instance().retrieve("api_key");
    if (!secure_key.is_ok() || secure_key.value().isEmpty()) {
        session_.authenticated = false;
        return;
    }
    session_.api_key = secure_key.value();

    // Step 2: restore full session JSON from auth.db (always open).
    const QString json = AuthService::instance().load_session(session_.api_key);
    if (!json.isEmpty()) {
        auto doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isNull())
            session_ = SessionData::from_json(doc.object());
        session_.api_key = secure_key.value(); // SecureStorage is authoritative
    }

    // Step 3: open the per-user DB now — synchronous, so repos work during
    // validate_saved_session() and any startup code that follows.
    AuthService::instance().open_user_db(session_.api_key);

    // Never trust saved authenticated flag — must be re-validated against auth.db.
    session_.authenticated = false;
}

void AuthManager::clear_session(bool wipe_device_pin) {
    const QString api_key = session_.api_key;
    // Close per-user DB before wiping session state.
    AuthService::instance().close_user_db();
    session_ = SessionData{};
    clear_tokens();
    // Remove session record from auth.db and OS keychain.
    AuthService::instance().remove_session(api_key);
    finterm::SecureStorage::instance().remove("api_key");
    // The device PIN survives an ordinary sign-out: wiping it here is what
    // used to turn every logout (including a stray toolbar click) into a
    // forced PIN re-creation.
    PinManager::instance().set_account({}, {}, {});
    if (wipe_device_pin)
        PinManager::instance().clear_pin();
}

// ── Initialize ───────────────────────────────────────────────────────────────

void AuthManager::initialize() {
    set_loading(true);
    load_session();

    if (!session_.api_key.isEmpty()) {
        // Apply api_key ONLY — do NOT send the stale session_token during
        // startup validation. The server enforces single-session via
        // X-Session-Token; sending a stale one triggers 401 even though
        // the api_key is perfectly valid and permanent.
        auto& http = finterm::HttpClient::instance();
        http.set_auth_header(session_.api_key);
        http.clear_session_token();

        validate_saved_session();
        return;
    }

    set_loading(false);
    emit auth_state_changed();
}

void AuthManager::validate_saved_session() {
    // Validate the saved api_key by fetching the user profile.
    // We intentionally do NOT send X-Session-Token here — the api_key is
    // the permanent credential. A stale session_token would cause a false 401.
    //
    // Flow:
    //   api_key valid (200) → mark authenticated → save
    //   api_key rejected (401/403) → show login but KEEP stored secrets —
    //       auto-validation must never destroy credentials on a transient or
    //       stale rejection; only an explicit user logout clears them
    //   network error → trust cached session data (offline-friendly)
    LOG_INFO("Auth", "Validating saved session via profile fetch (api_key only, no session_token)");
    fetch_user_profile();
}

void AuthManager::fetch_user_profile(std::function<void()> on_done) {
    AuthService::instance().get_user_profile(
        session_.api_key, [this, on_done = std::move(on_done)](ApiResponse r) mutable {
            if (!r.success && (r.status_code == 401 || r.status_code == 403)) {
                // Do NOT clear_session() here. Auto-validation must be non-destructive:
                // a transient, stale, or offline-induced 401/403 must never erase the
                // stored PIN + api_key. Credentials are now per-profile in SecureStorage
                // (see ProfileManager::secure_key_prefix), so a throwaway --profile can
                // no longer clobber another profile's secrets — which is how a diagnostic
                // profile once silently erased the user's credentials; this guard remains
                // as defense-in-depth within a profile. Drop to unauthenticated and show
                // login; only an explicit user logout erases stored secrets, and a
                // successful re-login overwrites the (possibly stale) key.
                LOG_WARN("Auth", "Profile fetch: api_key rejected — showing login (credentials preserved)");
                session_.authenticated = false;
                set_loading(false);
                emit auth_state_changed();
                return;
            }
            if (r.success) {
                session_.user_info = UserProfile::from_json(unwrap_data(r.data));
            } else {
                // DB unavailable — keep cached session data
                LOG_WARN("Auth", "Profile fetch failed — using cached data");
                session_.authenticated = !session_.api_key.isEmpty();
            }
            complete_auth_flow(std::move(on_done));
        });
}

// ── Shared post-auth completion ───────────────────────────────────────────────
// Syncs state, saves session, then calls on_done().
// Used by login_local / session restore.

void AuthManager::complete_auth_flow(std::function<void()> on_done) {
    if (!session_.api_key.isEmpty()) {
        session_.authenticated = true;
        // open_user_db is idempotent: skips reopen if the correct path is
        // already open (handles both fresh login and session-restore paths).
        AuthService::instance().open_user_db(session_.api_key);

        // The device PIN survives logout, so it is bound to an owner: a PIN
        // left behind by a different account must not gate (or be able to
        // unlock) this one. A PIN with no recorded owner (installs that
        // predate the binding) is adopted by whoever signs in first.
        const QString identity =
            !session_.user_info.username.isEmpty() ? session_.user_info.username : session_.user_info.email;
        if (!identity.isEmpty()) {
            auto& store = finterm::SecureStorage::instance();
            const auto owner = store.retrieve("pin_owner");
            const QString owner_val = owner.is_ok() ? owner.value() : QString();
            if (PinManager::instance().has_pin() && !owner_val.isEmpty() && owner_val != identity) {
                LOG_INFO("Auth", "Device PIN belongs to another account — wiping it for " + identity);
                PinManager::instance().clear_pin();
            }
            store.store("pin_owner", identity);
        }
        // The account PIN is what the lock screen asks for.
        const QString user = session_.user_info.username;
        if (!user.isEmpty()) {
            PinManager::instance().set_account(
                user, [user](const QString& pin) { return AuthService::instance().verify_local_pin(user, pin); },
                [user](const QString& pin) { return AuthService::instance().set_local_pin(user, pin); });
        }
    }
    save_session();
    set_loading(false);
    if (on_done)
        on_done();
    emit auth_state_changed();
}

// ── Local (no email, no OTP) ─────────────────────────────────────────────────

void AuthManager::signup_local(const QString& username, const QString& pin) {
    set_loading(true);
    AuthService::instance().register_local_user(username, pin, [this](ApiResponse r) {
        set_loading(false);
        if (r.success) {
            emit local_users_changed();
            emit signup_succeeded(r.data["username"].toString(), r.data["recovery_key"].toString());
        }
        else
            emit signup_failed(r.error.isEmpty() ? "Could not create user" : r.error);
    });
}

void AuthManager::login_local(const QString& username, const QString& pin) {
    // Each user's wrong PINs count against that user only.
    const QString user = username.toLower().trimmed();
    auto& gate = PinManager::instance();
    auto locked_text = [&gate](const QString& u) {
        const int secs = gate.lockout_remaining_seconds(u);
        return QStringLiteral("Too many wrong PINs — try again in %1, or choose \"Forgot PIN?\".")
            .arg(secs >= 120 ? QStringLiteral("%1 min").arg((secs + 59) / 60) : QStringLiteral("%1 s").arg(secs));
    };
    if (gate.is_locked_out(user)) {
        emit login_failed(locked_text(user));
        return;
    }
    set_loading(true);
    AuthService::instance().login_local(username, pin, [this, user, locked_text](ApiResponse r) {
        auto& pins = PinManager::instance();
        if (r.status_code == 401)
            pins.record_attempt(user, false);
        if (!r.success) {
            set_loading(false);
            // The wrong PIN that starts a wait says so now, not on the next try.
            if (r.status_code == 401 && pins.is_locked_out(user))
                emit login_failed(QStringLiteral("Incorrect PIN. ") + locked_text(user));
            else
                emit login_failed(r.error.isEmpty() ? "Login failed" : r.error);
            return;
        }
        pins.record_attempt(user, true);
        const auto data = unwrap_data(r.data);
        const QString api_key = data["api_key"].toString();
        if (api_key.isEmpty()) {
            set_loading(false);
            emit login_failed("Internal error (no api_key)");
            return;
        }
        const QString session_token = data["session_token"].toString();
        apply_tokens(api_key, session_token);

        session_.authenticated = true;
        session_.api_key = api_key;
        session_.session_token = session_token;
        session_.device_id = generate_device_id();

        fetch_user_profile([this] { emit login_succeeded(); });
    });
}

QString AuthManager::reset_pin_with_recovery_key(const QString& username, const QString& key,
                                                 const QString& new_pin) {
    const QString why = AuthService::instance().reset_pin_with_recovery_key(username, key, new_pin);
    if (why.isEmpty())
        PinManager::instance().pin_was_reset(username.toLower().trimmed());
    return why;
}

bool AuthManager::has_recovery_key(const QString& username) const {
    return AuthService::instance().has_recovery_key(username);
}

QString AuthManager::create_recovery_key() {
    if (!session_.authenticated || session_.user_info.username.isEmpty())
        return {};
    return AuthService::instance().create_recovery_key(session_.user_info.username);
}

QVector<QJsonObject> AuthManager::list_local_users() const {
    return AuthService::instance().list_local_users();
}

void AuthManager::delete_local_user(const QString& username, std::function<void(bool, const QString&)> on_done,
                                    const QString& keep_in) {
    AuthService::instance().delete_local_user(
        username,
        [this, username, on_done = std::move(on_done)](ApiResponse r) {
        // Only once the user row is really gone: a failed delete leaves a
        // live user whose channels and device PIN must keep working.
        if (!r.success) {
            if (on_done)
                on_done(false, r.error.isEmpty() ? QStringLiteral("Could not delete user.") : r.error);
            return;
        }
        notifications::remove_user_secrets(username);
        PinManager::instance().forget_user(username.toLower().trimmed());
        // "Reset this user" is the forgotten-credentials recovery path: the
        // deleted user's device PIN must not gate a future account re-created
        // under the same name.
        const auto owner = finterm::SecureStorage::instance().retrieve("pin_owner");
        if (owner.is_ok() && owner.value() == username.toLower().trimmed()) {
            PinManager::instance().clear_pin();
            finterm::SecureStorage::instance().remove("pin_owner");
        }
        emit local_users_changed();
        if (on_done)
            on_done(true, {});
        },
        keep_in);
}

// ── Logout ───────────────────────────────────────────────────────────────────

void AuthManager::logout(bool wipe_device_pin) {
    if (is_logging_out_)
        return;
    is_logging_out_ = true;

    if (!session_.api_key.isEmpty()) {
        AuthService::instance().logout(session_.api_key, [](ApiResponse) {});
    }

    clear_session(wipe_device_pin);
    is_logging_out_ = false;

    emit logged_out();
    emit auth_state_changed();
}

// ── Refresh user data ────────────────────────────────────────────────────────
// A light re-read of the profile row from auth.db. Unlike the login path
// (fetch_user_profile → complete_auth_flow) it writes nothing: no session row,
// no keyring — those calls are slow on the GUI thread and nothing here changes
// the credentials.

void AuthManager::refresh_user_data() {
    if (!session_.authenticated || session_.api_key.isEmpty())
        return;
    AuthService::instance().get_user_profile(session_.api_key, [this](ApiResponse r) {
        if (!r.success) {
            LOG_WARN("Auth", "Profile refresh failed: " + r.error);
            return;
        }
        session_.user_info = UserProfile::from_json(unwrap_data(r.data));
        emit auth_state_changed();
    });
}

// ── Regenerate API key ───────────────────────────────────────────────────────

void AuthManager::regenerate_api_key(std::function<void(bool, const QString&)> on_done) {
    auto done = [on_done](bool ok, const QString& err) {
        if (on_done)
            on_done(ok, err);
    };
    if (!session_.authenticated || session_.api_key.isEmpty()) {
        done(false, QStringLiteral("Not signed in."));
        return;
    }
    const QString old_key = session_.api_key;
    AuthService::instance().regenerate_api_key(old_key, [this, old_key, done](ApiResponse r) {
        const QString new_key = r.data["api_key"].toString();
        if (!r.success || new_key.isEmpty()) {
            done(false, r.error.isEmpty() ? QStringLiteral("Could not regenerate the API key.") : r.error);
            return;
        }
        // auth.db now only knows the new key. The keyring copy is what the
        // next launch signs in with, so it must take the new key too —
        // otherwise roll auth.db back so keyring, auth.db and the session
        // keep agreeing on the old key.
        session_.api_key = new_key;
        if (!save_session()) {
            session_.api_key = old_key;
            AuthService::instance().remove_session(new_key);
            bool rolled_back = false;
            AuthService::instance().replace_api_key(new_key, old_key,
                                                    [&rolled_back](ApiResponse rb) { rolled_back = rb.success; });
            if (!rolled_back) {
                // auth.db holds the new key and the keyring the old one: keep
                // this session on the key auth.db accepts; the next launch
                // will ask for the PIN, and that login re-saves the keyring.
                LOG_ERROR("Auth", "API key regenerated but neither the keyring save nor the rollback succeeded");
                session_.api_key = new_key;
                apply_tokens(new_key, session_.session_token);
                AuthService::instance().remove_session(old_key);
                save_session();
                done(false, QStringLiteral("The new key could not be saved to the system keyring, and the old key "
                                           "could not be restored. This session uses the new key; you will be "
                                           "asked for your PIN at the next launch."));
                return;
            }
            AuthService::instance().save_session(old_key, QString::fromUtf8(QJsonDocument(session_.to_json())
                                                                                 .toJson(QJsonDocument::Compact)));
            LOG_WARN("Auth", "API key regeneration rolled back: keyring save failed");
            done(false, QStringLiteral("The new key could not be saved to the system keyring, so the old key "
                                       "was kept."));
            return;
        }
        apply_tokens(new_key, session_.session_token);
        AuthService::instance().remove_session(old_key);
        LOG_INFO("Auth", "API key regenerated and adopted by the session");
        done(true, {});
        refresh_user_data();
    });
}

} // namespace finterm::auth

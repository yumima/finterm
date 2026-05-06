// src/auth/AuthService.cpp
#include "auth/AuthService.h"

#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "storage/sqlite/Database.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace fincept::auth {

static constexpr const char* kConnName = "fincept_auth";

AuthService& AuthService::instance() {
    static AuthService s;
    return s;
}

// ── Initialisation ────────────────────────────────────────────────────────────

void AuthService::initialize() {
    const QString path = fincept::AppPaths::data() + "/auth.db";
    db_ = QSqlDatabase::addDatabase("QSQLITE", kConnName);
    db_.setDatabaseName(path);
    if (!db_.open()) {
        LOG_ERROR("AuthService", "Failed to open auth.db: " + db_.lastError().text());
        return;
    }
    init_schema();
    LOG_INFO("AuthService", "auth.db ready at " + path);
}

void AuthService::init_schema() {
    QSqlQuery q(db_);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            username      TEXT    UNIQUE NOT NULL COLLATE NOCASE,
            email         TEXT    UNIQUE NOT NULL COLLATE NOCASE,
            password_hash TEXT    NOT NULL,
            api_key       TEXT    UNIQUE,
            otp_verified  INTEGER NOT NULL DEFAULT 0,
            created_at    TEXT    NOT NULL DEFAULT (datetime('now')),
            last_login_at TEXT
        )
    )");
    // sessions table: persists AuthManager session JSON so the user DB path
    // (derived from username) can be opened before fincept.db is available.
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            api_key      TEXT PRIMARY KEY,
            session_json TEXT NOT NULL,
            updated_at   TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )");
}

// ── Session persistence ───────────────────────────────────────────────────────

void AuthService::save_session(const QString& api_key, const QString& session_json) {
    if (api_key.isEmpty()) return;
    if (!db_.isOpen()) { LOG_WARN("AuthService", "save_session: auth.db not open"); return; }
    QSqlQuery q(db_);
    q.prepare(R"(INSERT INTO sessions (api_key, session_json, updated_at)
                 VALUES (?, ?, datetime('now'))
                 ON CONFLICT(api_key) DO UPDATE SET session_json=excluded.session_json,
                                                    updated_at=excluded.updated_at)");
    q.addBindValue(api_key);
    q.addBindValue(session_json);
    q.exec();
}

QString AuthService::load_session(const QString& api_key) {
    if (api_key.isEmpty()) return {};
    if (!db_.isOpen()) { LOG_WARN("AuthService", "load_session: auth.db not open"); return {}; }
    QSqlQuery q(db_);
    q.prepare("SELECT session_json FROM sessions WHERE api_key = ?");
    q.addBindValue(api_key);
    q.exec();
    return q.next() ? q.value(0).toString() : QString{};
}

void AuthService::remove_session(const QString& api_key) {
    if (api_key.isEmpty()) return;
    QSqlQuery q(db_);
    q.prepare("DELETE FROM sessions WHERE api_key = ?");
    q.addBindValue(api_key);
    q.exec();
}

// ── Per-user DB ───────────────────────────────────────────────────────────────

QString AuthService::username_for_key(const QString& api_key) const {
    QSqlQuery q(db_);
    q.prepare("SELECT username FROM users WHERE api_key = ?");
    q.addBindValue(api_key);
    q.exec();
    return q.next() ? q.value(0).toString() : QString{};
}

QString AuthService::user_db_path(const QString& username) const {
    const QString dir = fincept::AppPaths::data() + "/users";
    QDir().mkpath(dir);
    return dir + "/" + username + ".db";
}

void AuthService::open_user_db(const QString& api_key) {
    const QString username = username_for_key(api_key);
    if (username.isEmpty()) {
        LOG_WARN("AuthService", "open_user_db: no user found for api_key");
        return;
    }
    const QString path = user_db_path(username);
    // Skip reopen if the correct DB is already open — avoids an unnecessary
    // close/reopen when load_session() and complete_auth_flow() both call us.
    if (fincept::Database::instance().is_open() &&
        fincept::Database::instance().path() == path) {
        return;
    }
    auto r = fincept::Database::instance().reopen(path);
    if (r.is_err())
        LOG_ERROR("AuthService", "Failed to open user DB for " + username + ": "
                  + QString::fromStdString(r.error()));
    else
        LOG_INFO("AuthService", "Opened user DB: " + path);
}

void AuthService::close_user_db() {
    fincept::Database::instance().close();
    LOG_INFO("AuthService", "User DB closed");
}

// ── Password helpers ──────────────────────────────────────────────────────────

QString AuthService::hash_password(const QString& password) const {
    // salt_hex:sha256(salt_bytes || password_bytes)
    QByteArray salt(16, 0);
    for (int i = 0; i < 16; ++i)
        salt[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
    const QString salt_hex = QString::fromLatin1(salt.toHex());
    QByteArray payload = salt + password.toUtf8();
    const QString hash_hex = QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    return salt_hex + ":" + hash_hex;
}

bool AuthService::verify_password(const QString& password, const QString& stored) const {
    const int colon = stored.indexOf(':');
    if (colon < 0)
        return false;
    const QByteArray salt = QByteArray::fromHex(stored.left(colon).toLatin1());
    const QString expected_hash = stored.mid(colon + 1);
    QByteArray payload = salt + password.toUtf8();
    const QString actual_hash = QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    return actual_hash == expected_hash;
}

QString AuthService::make_token() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject AuthService::profile_json(int id, const QString& username, const QString& email,
                                       const QString& created_at, const QString& last_login) const {
    QJsonObject obj;
    obj["id"]            = id;
    obj["username"]      = username;
    obj["email"]         = email;
    obj["account_type"]  = "local";
    obj["credit_balance"] = 9999;
    obj["is_verified"]   = true;
    obj["is_admin"]      = false;
    obj["mfa_enabled"]   = false;
    obj["created_at"]    = created_at;
    obj["last_login_at"] = last_login;
    return obj;
}

// ── register_user ─────────────────────────────────────────────────────────────

void AuthService::register_user(const RegisterRequest& req, Callback cb) {
    // Rely on the UNIQUE constraints for atomicity — avoid a TOCTOU window from
    // two-phase SELECT→INSERT. Map the constraint name to a user-friendly message.
    QSqlQuery q(db_);
    q.prepare("INSERT INTO users (username, email, password_hash) VALUES (?, ?, ?)");
    q.addBindValue(req.username.toLower().trimmed());
    q.addBindValue(req.email.toLower().trimmed());
    q.addBindValue(hash_password(req.password));
    if (!q.exec()) {
        const QString err = q.lastError().text();
        LOG_ERROR("AuthService", "register_user failed: " + err);
        if (err.contains("users.email", Qt::CaseInsensitive))
            cb({false, {}, "An account with this email already exists.", 409});
        else if (err.contains("users.username", Qt::CaseInsensitive))
            cb({false, {}, "This username is already taken.", 409});
        else
            cb({false, {}, "Registration failed. Please try again.", 500});
        return;
    }
    LOG_INFO("AuthService", "Registered user: " + req.email);
    cb({true, {}, {}, 200});
}

// ── login ─────────────────────────────────────────────────────────────────────

void AuthService::login(const LoginRequest& req, Callback cb) {
    QSqlQuery q(db_);
    q.prepare("SELECT username, email, password_hash, api_key, otp_verified "
              "FROM users WHERE email = ?");
    q.addBindValue(req.email.toLower().trimmed());
    q.exec();
    if (!q.next()) {
        cb({false, {}, "Account not found. Please check your email.", 404});
        return;
    }

    const QString username     = q.value(0).toString();
    const QString email        = q.value(1).toString();
    const QString stored_hash  = q.value(2).toString();
    const QString api_key      = q.value(3).toString();
    const bool    otp_verified = q.value(4).toBool();

    if (!verify_password(req.password, stored_hash)) {
        cb({false, {}, "Incorrect email or password.", 401});
        return;
    }
    if (!otp_verified) {
        cb({false, {}, "Your account is not verified yet. Please complete OTP verification.", 403});
        return;
    }

    // Update last_login_at and issue a fresh session_token
    const QString now           = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString session_token = make_token();

    QSqlQuery upd(db_);
    upd.prepare("UPDATE users SET last_login_at = ? WHERE email = ?");
    upd.addBindValue(now);
    upd.addBindValue(email);
    upd.exec();

    QJsonObject data;
    data["api_key"]        = api_key;
    data["session_token"]  = session_token;
    data["active_session"] = false;
    data["mfa_required"]   = false;

    LOG_INFO("AuthService", "Login: " + email);
    cb({true, data, {}, 200});
}

// ── verify_otp ────────────────────────────────────────────────────────────────

void AuthService::verify_otp(const VerifyOtpRequest& req, Callback cb) {
    // Validate: must be exactly 6 ASCII digits (including "000000").
    const QString otp = req.otp.trimmed();
    const bool all_digits = otp.length() == 6 &&
        std::all_of(otp.begin(), otp.end(), [](QChar c) { return c.isDigit(); });
    if (!all_digits) {
        cb({false, {}, "Please enter a 6-digit verification code.", 400});
        return;
    }

    QSqlQuery q(db_);
    q.prepare("SELECT id, username, email, api_key, created_at FROM users WHERE email = ?");
    q.addBindValue(req.email.toLower().trimmed());
    q.exec();
    if (!q.next()) {
        cb({false, {}, "Account not found.", 404});
        return;
    }

    const int     id         = q.value(0).toInt();
    const QString username   = q.value(1).toString();
    const QString email      = q.value(2).toString();
    QString       api_key    = q.value(3).toString();
    const QString created_at = q.value(4).toString();

    // Generate api_key once (first OTP verification)
    if (api_key.isEmpty())
        api_key = make_token();

    const QString now           = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString session_token = make_token();

    QSqlQuery upd(db_);
    upd.prepare("UPDATE users SET api_key = ?, otp_verified = 1, last_login_at = ? WHERE id = ?");
    upd.addBindValue(api_key);
    upd.addBindValue(now);
    upd.addBindValue(id);
    if (!upd.exec()) {
        LOG_ERROR("AuthService", "verify_otp update failed: " + upd.lastError().text());
        cb({false, {}, "Verification failed. Please try again.", 500});
        return;
    }

    QJsonObject data;
    data["api_key"]       = api_key;
    data["session_token"] = session_token;
    data["user"]          = profile_json(id, username, email, created_at, now);

    LOG_INFO("AuthService", "OTP verified: " + email);
    cb({true, data, {}, 200});
}

// ── forgot_password ───────────────────────────────────────────────────────────

void AuthService::forgot_password(const ForgotPasswordRequest& req, Callback cb) {
    // Local build: no email server. Instruct user to use any 6-digit code.
    LOG_INFO("AuthService",
             "Password reset requested for " + req.email +
             " — local build: use any 6-digit code in reset_password()");
    cb({true, {}, {}, 200});
}

// ── reset_password ────────────────────────────────────────────────────────────

void AuthService::reset_password(const ResetPasswordRequest& req, Callback cb) {
    const QString otp = req.otp.trimmed();
    if (otp.length() != 6 || !otp.toULongLong()) {
        cb({false, {}, "Please enter a 6-digit code.", 400});
        return;
    }

    QSqlQuery q(db_);
    q.prepare("SELECT id FROM users WHERE email = ?");
    q.addBindValue(req.email.toLower().trimmed());
    q.exec();
    if (!q.next()) {
        cb({false, {}, "Account not found.", 404});
        return;
    }
    const int id = q.value(0).toInt();

    QSqlQuery upd(db_);
    upd.prepare("UPDATE users SET password_hash = ? WHERE id = ?");
    upd.addBindValue(hash_password(req.new_password));
    upd.addBindValue(id);
    if (!upd.exec()) {
        cb({false, {}, "Password reset failed. Please try again.", 500});
        return;
    }
    LOG_INFO("AuthService", "Password reset: " + req.email);
    cb({true, {}, {}, 200});
}

// ── logout ────────────────────────────────────────────────────────────────────

void AuthService::logout(const QString& /*api_key*/, Callback cb) {
    // Local build: sessions are stateless — nothing to revoke server-side.
    cb({true, {}, {}, 200});
}

// ── get_user_profile ─────────────────────────────────────────────────────────

void AuthService::get_user_profile(const QString& api_key, Callback cb) {
    if (api_key.isEmpty()) {
        cb({false, {}, "No API key.", 401});
        return;
    }
    QSqlQuery q(db_);
    q.prepare("SELECT id, username, email, created_at, last_login_at "
              "FROM users WHERE api_key = ?");
    q.addBindValue(api_key);
    q.exec();
    if (!q.next()) {
        // Unknown api_key → treat as revoked so AuthManager forces re-login
        cb({false, {}, "Invalid or expired API key.", 401});
        return;
    }
    const int     id           = q.value(0).toInt();
    const QString username     = q.value(1).toString();
    const QString email        = q.value(2).toString();
    const QString created_at   = q.value(3).toString();
    const QString last_login   = q.value(4).toString();

    cb({true, profile_json(id, username, email, created_at, last_login), {}, 200});
}

// ── update_user_profile ───────────────────────────────────────────────────────

void AuthService::update_user_profile(const QString& api_key, const QJsonObject& data, Callback cb) {
    if (api_key.isEmpty()) {
        cb({false, {}, "No API key.", 401});
        return;
    }
    // Only username update supported locally
    if (data.contains("username")) {
        const QString new_username = data["username"].toString().toLower().trimmed();
        if (!new_username.isEmpty()) {
            QSqlQuery q(db_);
            q.prepare("UPDATE users SET username = ? WHERE api_key = ?");
            q.addBindValue(new_username);
            q.addBindValue(api_key);
            if (!q.exec()) {
                cb({false, {}, "Failed to update profile.", 500});
                return;
            }
        }
    }
    cb({true, {}, {}, 200});
}

// ── regenerate_api_key ────────────────────────────────────────────────────────

void AuthService::regenerate_api_key(const QString& api_key, Callback cb) {
    if (api_key.isEmpty()) {
        cb({false, {}, "No API key.", 401});
        return;
    }
    const QString new_key = make_token();
    QSqlQuery q(db_);
    q.prepare("UPDATE users SET api_key = ? WHERE api_key = ?");
    q.addBindValue(new_key);
    q.addBindValue(api_key);
    if (!q.exec() || q.numRowsAffected() == 0) {
        cb({false, {}, "Failed to regenerate API key.", 500});
        return;
    }
    QJsonObject data;
    data["api_key"] = new_key;
    LOG_INFO("AuthService", "API key regenerated");
    cb({true, data, {}, 200});
}

} // namespace fincept::auth

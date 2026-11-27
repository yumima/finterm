// src/auth/AuthService.cpp
#include "auth/AuthService.h"

#include "auth/PinPolicy.h"

#include "core/config/AppPaths.h"
#include "core/diagnostics/SlowOpTimer.h"
#include "core/logging/Logger.h"
#include "storage/sqlite/Database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace finterm::auth {

static constexpr const char* kConnName = "finterm_auth";

AuthService& AuthService::instance() {
    static AuthService s;
    return s;
}

// ── Initialisation ────────────────────────────────────────────────────────────

void AuthService::initialize() {
    const QString path = finterm::AppPaths::data() + "/auth.db";
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
    // Added in 4.2.2; an older auth.db gains it here (the error on a second
    // run — "duplicate column" — is the expected no-op).
    q.exec("ALTER TABLE users ADD COLUMN recovery_hash TEXT");
    // sessions table: persists AuthManager session JSON so the user DB path
    // (derived from username) can be opened before the per-user DB is available.
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
    if (api_key.isEmpty())
        return;
    if (!db_.isOpen()) {
        LOG_WARN("AuthService", "save_session: auth.db not open");
        return;
    }
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
    if (api_key.isEmpty())
        return {};
    if (!db_.isOpen()) {
        LOG_WARN("AuthService", "load_session: auth.db not open");
        return {};
    }
    QSqlQuery q(db_);
    q.prepare("SELECT session_json FROM sessions WHERE api_key = ?");
    q.addBindValue(api_key);
    q.exec();
    return q.next() ? q.value(0).toString() : QString{};
}

void AuthService::remove_session(const QString& api_key) {
    if (api_key.isEmpty())
        return;
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
    const QString dir = finterm::AppPaths::data() + "/users";
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
    if (finterm::Database::instance().is_open() && finterm::Database::instance().path() == path) {
        return;
    }
    auto r = finterm::Database::instance().reopen(path);
    if (r.is_err())
        LOG_ERROR("AuthService", "Failed to open user DB for " + username + ": " + QString::fromStdString(r.error()));
    else
        LOG_INFO("AuthService", "Opened user DB: " + path);
}

void AuthService::close_user_db() {
    finterm::Database::instance().close();
    LOG_INFO("AuthService", "User DB closed");
}

// ── PIN helpers ───────────────────────────────────────────────────────────────

QString AuthService::stored_pin_hash(const QString& username) {
    QSqlQuery q(db_);
    q.prepare("SELECT password_hash FROM users WHERE username = ? AND otp_verified = 1");
    q.addBindValue(username.toLower().trimmed());
    if (!q.exec() || !q.next())
        return {};
    return q.value(0).toString();
}

bool AuthService::verify_local_pin(const QString& username, const QString& pin) {
    const QString stored = stored_pin_hash(username);
    return !stored.isEmpty() && pin_policy::verify(pin, stored);
}

QString AuthService::set_local_pin(const QString& username, const QString& new_pin) {
    const QString why = pin_policy::problem(new_pin);
    if (!why.isEmpty())
        return why;
    QSqlQuery q(db_);
    q.prepare("UPDATE users SET password_hash = ? WHERE username = ?");
    q.addBindValue(pin_policy::hash(new_pin));
    q.addBindValue(username.toLower().trimmed());
    if (!q.exec() || q.numRowsAffected() == 0) {
        LOG_ERROR("AuthService", "set_local_pin failed: " + q.lastError().text());
        return QStringLiteral("The new PIN could not be saved.");
    }
    LOG_INFO("AuthService", "PIN changed for " + username.toLower().trimmed());
    return {};
}

bool AuthService::has_recovery_key(const QString& username) {
    QSqlQuery q(db_);
    q.prepare("SELECT recovery_hash FROM users WHERE username = ?");
    q.addBindValue(username.toLower().trimmed());
    return q.exec() && q.next() && !q.value(0).toString().isEmpty();
}

QString AuthService::create_recovery_key(const QString& username) {
    const QString key = pin_policy::new_recovery_key();
    QSqlQuery q(db_);
    q.prepare("UPDATE users SET recovery_hash = ? WHERE username = ?");
    q.addBindValue(pin_policy::hash(pin_policy::normalize_recovery_key(key), pin_policy::kRecoveryIterations));
    q.addBindValue(username.toLower().trimmed());
    if (!q.exec() || q.numRowsAffected() == 0) {
        LOG_ERROR("AuthService", "create_recovery_key failed: " + q.lastError().text());
        return {};
    }
    LOG_INFO("AuthService", "Recovery key created for " + username.toLower().trimmed());
    return key;
}

QString AuthService::reset_pin_with_recovery_key(const QString& username, const QString& typed_key,
                                                 const QString& new_pin) {
    const QString key = pin_policy::normalize_recovery_key(typed_key);
    if (key.isEmpty())
        return QStringLiteral("A recovery key is 20 letters and digits, like 7KQ2M-9XD4T-B3N8V-WQ5ZR.");
    QSqlQuery q(db_);
    q.prepare("SELECT recovery_hash FROM users WHERE username = ?");
    q.addBindValue(username.toLower().trimmed());
    if (!q.exec() || !q.next() || q.value(0).toString().isEmpty())
        return QStringLiteral("This user has no recovery key.");
    if (!pin_policy::verify(key, q.value(0).toString())) {
        LOG_WARN("AuthService", "Recovery key rejected for " + username.toLower().trimmed());
        return QStringLiteral("That recovery key is not this user's.");
    }
    return set_local_pin(username, new_pin);
}

QString AuthService::make_token() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject AuthService::profile_json(int id, const QString& username, const QString& email, const QString& created_at,
                                      const QString& last_login) const {
    QJsonObject obj;
    obj["id"] = id;
    obj["username"] = username;
    obj["email"] = email;
    obj["created_at"] = created_at;
    obj["last_login_at"] = last_login;
    return obj;
}

// ── Local multi-user (username + PIN) ─────────────────────────────────────────

QVector<QJsonObject> AuthService::list_local_users() {
    QVector<QJsonObject> out;
    QSqlQuery q(db_);
    if (!q.exec("SELECT username, last_login_at FROM users "
                "WHERE otp_verified = 1 ORDER BY last_login_at DESC, username ASC")) {
        LOG_WARN("AuthService", "list_local_users query failed: " + q.lastError().text());
        return out;
    }
    while (q.next()) {
        QJsonObject row;
        row["username"] = q.value(0).toString();
        row["last_login_at"] = q.value(1).toString();
        out.append(row);
    }
    return out;
}

void AuthService::register_local_user(const QString& username, const QString& pin, Callback cb) {
    const QString u = username.toLower().trimmed();
    if (u.isEmpty()) {
        cb({false, {}, "Username can't be empty.", 400});
        return;
    }
    if (const QString why = pin_policy::problem(pin); !why.isEmpty()) {
        cb({false, {}, why, 400});
        return;
    }
    const QString recovery_key = pin_policy::new_recovery_key();
    // Synthetic email — keeps the existing unique-email constraint satisfied
    // without exposing it in the local-user flow.
    const QString synthetic_email = u + QLatin1String(kSyntheticEmailSuffix);
    const QString api_key = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QSqlQuery q(db_);
    q.prepare("INSERT INTO users (username, email, password_hash, recovery_hash, api_key, otp_verified, created_at) "
              "VALUES (?, ?, ?, ?, ?, 1, ?)");
    q.addBindValue(u);
    q.addBindValue(synthetic_email);
    q.addBindValue(pin_policy::hash(pin));
    q.addBindValue(pin_policy::hash(pin_policy::normalize_recovery_key(recovery_key), pin_policy::kRecoveryIterations));
    q.addBindValue(api_key);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!q.exec()) {
        const QString err = q.lastError().text();
        LOG_ERROR("AuthService", "register_local_user failed: " + err);
        if (err.contains("username", Qt::CaseInsensitive) || err.contains("email", Qt::CaseInsensitive))
            cb({false, {}, "That username is already taken on this device.", 409});
        else
            cb({false, {}, "Could not create user. Try a different name.", 500});
        return;
    }
    LOG_INFO("AuthService", "Registered local user: " + u);
    QJsonObject data;
    data["username"] = u;
    data["api_key"] = api_key;
    data["recovery_key"] = recovery_key; // shown once, never stored
    cb({true, data, {}, 200});
}

void AuthService::login_local(const QString& username, const QString& pin, Callback cb) {
    // Instrumentation only — login_local runs one indexed SELECT and one PIN
    // verify: PBKDF2 at 100,000 iterations (~90 ms; a pre-4.2.2 SHA-256 hash
    // is sub-ms). Wrapped so a slow path beyond that (FS-level lock
    // contention, AV intercept on auth.db) surfaces in the perf log instead
    // of silently delaying UNLOCK clicks.
    FT_TIME_SLOT("AuthService.login_local", 400);
    const QString u = username.toLower().trimmed();
    QSqlQuery q(db_);
    q.prepare("SELECT username, email, password_hash, api_key "
              "FROM users WHERE username = ? AND otp_verified = 1");
    q.addBindValue(u);
    q.exec();
    if (!q.next()) {
        cb({false, {}, "User not found on this device.", 404});
        return;
    }

    const QString stored_username = q.value(0).toString();
    const QString stored_email = q.value(1).toString();
    const QString stored_hash = q.value(2).toString();
    const QString api_key = q.value(3).toString();

    if (!pin_policy::verify(pin, stored_hash)) {
        cb({false, {}, "Incorrect PIN.", 401});
        return;
    }
    // An older account's hash is NOT rewritten here: builds before 4.2.2
    // read only the old form, and the updater's rollback (or running an older
    // release on the same data) would then refuse the right PIN — whose only
    // way out there deletes the user. It becomes PBKDF2 when the user sets a
    // PIN (Change PIN, recovery key), a step they take knowingly.

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString session_token = make_token();

    QSqlQuery upd(db_);
    upd.prepare("UPDATE users SET last_login_at = ? WHERE username = ?");
    upd.addBindValue(now);
    upd.addBindValue(stored_username);
    upd.exec();

    QJsonObject data;
    data["api_key"] = api_key;
    data["session_token"] = session_token;
    data["active_session"] = false;
    data["mfa_required"] = false;
    data["username"] = stored_username;
    data["email"] = stored_email;
    LOG_INFO("AuthService", "Local login: " + stored_username);
    cb({true, data, {}, 200});
}

void AuthService::delete_local_user(const QString& username, Callback cb) {
    const QString u = username.toLower().trimmed();
    // The per-user DB must really go: a re-created account of the same name
    // would otherwise open users/<name>.db and inherit the deleted user's
    // data. Move the DB (and any WAL/SHM side files) aside first — a rename
    // fails while another process or an open handle on Windows holds it, and
    // then nothing has changed. Only once the files are out of the way is
    // the auth row deleted; if that fails the files are moved back.
    const QString base = user_db_path(u);
    QStringList moved;
    auto restore = [&moved]() {
        for (const QString& p : moved)
            QFile::rename(p + ".deleted", p);
    };
    for (const QString& p : {base, base + "-wal", base + "-shm"}) {
        if (!QFile::exists(p))
            continue;
        QFile::remove(p + ".deleted"); // leftover from an earlier failed cleanup
        if (!QFile::rename(p, p + ".deleted")) {
            LOG_ERROR("AuthService", "delete_local_user: cannot move " + p);
            restore();
            cb({false, {}, "Could not remove the user's database file (" + p + "). Close anything using it and try again.",
                500});
            return;
        }
        moved.append(p);
    }

    QSqlQuery q(db_);
    q.prepare("DELETE FROM users WHERE username = ?");
    q.addBindValue(u);
    if (!q.exec()) {
        restore();
        cb({false, {}, "Could not delete user.", 500});
        return;
    }
    for (const QString& p : moved) {
        // Under its new name the file can no longer be picked up by a new
        // account, so a failed removal here is only disk litter.
        if (!QFile::remove(p + ".deleted"))
            LOG_WARN("AuthService", "delete_local_user: could not remove " + p + ".deleted");
    }
    LOG_INFO("AuthService", "Deleted local user: " + u);
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
    const int id = q.value(0).toInt();
    const QString username = q.value(1).toString();
    const QString email = q.value(2).toString();
    const QString created_at = q.value(3).toString();
    const QString last_login = q.value(4).toString();

    cb({true, profile_json(id, username, email, created_at, last_login), {}, 200});
}

// ── regenerate_api_key ────────────────────────────────────────────────────────

void AuthService::regenerate_api_key(const QString& api_key, Callback cb) {
    replace_api_key(api_key, make_token(), std::move(cb));
}

void AuthService::replace_api_key(const QString& old_key, const QString& new_key, Callback cb) {
    if (old_key.isEmpty() || new_key.isEmpty()) {
        cb({false, {}, "No API key.", 401});
        return;
    }
    QSqlQuery q(db_);
    q.prepare("UPDATE users SET api_key = ? WHERE api_key = ?");
    q.addBindValue(new_key);
    q.addBindValue(old_key);
    if (!q.exec() || q.numRowsAffected() == 0) {
        cb({false, {}, "Failed to replace the API key.", 500});
        return;
    }
    QJsonObject data;
    data["api_key"] = new_key;
    LOG_INFO("AuthService", "API key replaced");
    cb({true, data, {}, 200});
}

} // namespace finterm::auth

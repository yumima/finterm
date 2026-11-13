// src/auth/AuthService.h
#pragma once
#include "auth/AuthTypes.h"

#include <QObject>
#include <QSqlDatabase>

#include <functional>

namespace finterm::auth {

/// In-process authentication service backed by a local SQLite database.
/// Replaces the external stub server that used to run on :8765 (since deleted).
/// All users are stored in AppPaths::data()/auth.db — nothing leaves this machine.
///
/// Users are local: username + PIN, no email, OTP or MFA.
/// PIN storage:    SHA-256 with random salt ("salt_hex:hash_hex").
/// API keys:       UUIDs issued when the user is created; regenerable.
/// Session tokens: UUIDs generated at each login, passed to HttpClient for
///                 compatibility with existing token-clearing logic.
class AuthService : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(ApiResponse)>;

    static AuthService& instance();

    /// Open (or create) auth.db and initialise the schema. Must be called once
    /// before AuthManager::initialize().
    void initialize();

    // ── Session persistence (stored in auth.db, always open) ─────────────────
    void save_session(const QString& api_key, const QString& session_json);
    QString load_session(const QString& api_key);
    void remove_session(const QString& api_key);

    // ── Per-user DB lifecycle ─────────────────────────────────────────────────
    /// Open the per-user fincept DB (users/<username>.db). Creates it fresh and
    /// runs all migrations if this is the user's first login.
    void open_user_db(const QString& api_key);
    /// Close the per-user DB (called at logout). Safe to call when not open.
    void close_user_db();

    // ── Local multi-user (username + PIN, no email/OTP/MFA) ──────────────────
    /// Listed for the LoginScreen user picker. Each entry is { "username":
    /// QString, "last_login_at": QString (ISO) }. Empty list → first launch.
    QVector<QJsonObject> list_local_users();
    /// Create a new local user. Stores a synthetic email "<username>@local"
    /// so the existing email-keyed columns stay populated; auto-flags
    /// otp_verified = true. Fails 409 if username already exists.
    void register_local_user(const QString& username, const QString& pin, Callback cb);
    /// Log in by username + PIN. Mirrors the email-based login response so
    /// AuthManager can stay agnostic.
    void login_local(const QString& username, const QString& pin, Callback cb);
    /// Remove a local user and the per-user DB at users/<username>.db (plus
    /// its WAL/SHM files). Fails, changing nothing, when the DB file cannot be
    /// moved out of the way. Used by LoginScreen "reset user" and Settings →
    /// Account "delete local account" (which closes the DB first).
    void delete_local_user(const QString& username, Callback cb);

    // ── Authenticated (api_key identifies the caller) ─────────────────────────
    void logout(const QString& api_key, Callback cb);
    void get_user_profile(const QString& api_key, Callback cb);
    void regenerate_api_key(const QString& api_key, Callback cb);
    /// Set the user holding old_key to new_key. Used by regenerate_api_key
    /// and to roll a regeneration back. Response data: { "api_key": new_key }.
    void replace_api_key(const QString& old_key, const QString& new_key, Callback cb);

  private:
    AuthService() = default;

    void init_schema();
    QString hash_password(const QString& password) const;
    bool verify_password(const QString& password, const QString& stored) const;
    QString make_token() const;
    QJsonObject profile_json(int id, const QString& username, const QString& email, const QString& created_at,
                             const QString& last_login) const;
    QString username_for_key(const QString& api_key) const;
    QString user_db_path(const QString& username) const;

    QSqlDatabase db_; // auth.db connection
};

} // namespace finterm::auth

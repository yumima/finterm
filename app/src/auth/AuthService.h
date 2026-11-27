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
/// PIN storage:    PBKDF2-SHA256 (auth/PinPolicy); accounts made before
///                 4.2.2 hold a salted SHA-256 until the user sets a new PIN.
/// Recovery key:   optional, its PBKDF2 hash beside the PIN; it resets a
///                 forgotten PIN without losing the user's data.
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
    /// Open the per-user DB (users/<username>.db). Creates it fresh and
    /// runs all migrations if this is the user's first login.
    void open_user_db(const QString& api_key);
    /// Close the per-user DB (called at logout). Safe to call when not open.
    void close_user_db();
    /// Why this account's data can't be opened by this build, or "": its
    /// database was saved by a newer finterm, or it still needs upgrading
    /// and the copy that must come first could not be made
    /// (MigrationRunner::set_hold). Checked before a login goes through —
    /// opening it would refuse it anyway, and the user would be in with no
    /// data and no word why.
    QString user_data_refusal(const QString& api_key);

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
    /// Delete a user: the auth row and their database. With `keep_in`, the
    /// database files are moved into that folder instead of deleted.
    void delete_local_user(const QString& username, Callback cb, const QString& keep_in = {});

    // ── The account PIN and its recovery key ─────────────────────────────────
    /// Whether `pin` is `username`'s PIN. Counts nothing: lockout is the
    /// caller's (PinManager).
    bool verify_local_pin(const QString& username, const QString& pin);
    /// Replace `username`'s PIN (checked against pin_policy::problem). The
    /// caller has already proved who it is.
    QString set_local_pin(const QString& username, const QString& new_pin);
    /// Whether `username` has a recovery key.
    bool has_recovery_key(const QString& username);
    /// A new recovery key for `username` (replacing any earlier one), or ""
    /// when it could not be stored. Shown once: only its hash is kept.
    QString create_recovery_key(const QString& username);
    /// Set a new PIN with the recovery key. "" on success, else why not.
    QString reset_pin_with_recovery_key(const QString& username, const QString& typed_key, const QString& new_pin);

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
    QString stored_pin_hash(const QString& username);
    QString make_token() const;
    QJsonObject profile_json(int id, const QString& username, const QString& email, const QString& created_at,
                             const QString& last_login) const;
    QString username_for_key(const QString& api_key) const;
    QString user_db_path(const QString& username) const;

    QSqlDatabase db_; // auth.db connection
};

} // namespace finterm::auth

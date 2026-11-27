#pragma once
#include "auth/AuthTypes.h"

#include <QObject>

#include <functional>

namespace finterm::auth {

/// Central auth state machine.
/// Manages local login/signup (username + PIN), session persistence, and validation.
class AuthManager : public QObject {
    Q_OBJECT
  public:
    static AuthManager& instance();

    const SessionData& session() const { return session_; }
    bool is_authenticated() const { return session_.authenticated; }
    bool is_loading() const { return is_loading_; }

    // Local users: multi-user picker + PIN. No email, OTP or MFA.
    void signup_local(const QString& username, const QString& pin);
    /// Wrong PINs here count against the same lockout as the lock screen.
    void login_local(const QString& username, const QString& pin);
    /// Set a forgotten PIN with the recovery key. "" or why not.
    QString reset_pin_with_recovery_key(const QString& username, const QString& key, const QString& new_pin);
    bool has_recovery_key(const QString& username) const;
    /// A new recovery key for the signed-in user ("" on failure).
    QString create_recovery_key();
    QVector<QJsonObject> list_local_users() const;
    /// Delete a local user (auth.db row + users/<name>.db). on_done, when set,
    /// receives (ok, error) after the attempt.
    void delete_local_user(const QString& username, std::function<void(bool, const QString&)> on_done = {});
    /// Sign out. An older build's device PIN is kept unless wipe_device_pin
    /// (account deletion).
    void logout(bool wipe_device_pin = false);

    // Session management
    void initialize();
    /// Re-read the profile from auth.db and emit auth_state_changed. Writes
    /// nothing (no session row, no keyring).
    void refresh_user_data();
    /// Replace the signed-in user's local API key and adopt the new one in
    /// the live session (HttpClient header, auth.db session row, keyring).
    /// If the keyring write fails the change is rolled back in auth.db and
    /// on_done reports failure, so keyring, auth.db and session stay in step.
    void regenerate_api_key(std::function<void(bool, const QString&)> on_done = {});


  signals:
    void auth_state_changed();
    void login_succeeded();
    void login_failed(const QString& error);
    /// `recovery_key` is shown to the user once, then forgotten.
    void signup_succeeded(const QString& username, const QString& recovery_key);
    void signup_failed(const QString& error);
    /// A user was created or deleted: the sign-in picker lists them again.
    void local_users_changed();
    void logged_out();
    void loading_changed(bool loading);

    /// Emitted after login + PIN verify (or setup) — terminal is fully unlocked.
    void terminal_unlocked();

  private:
    AuthManager();

    void set_loading(bool v);
    /// Persist the session row in auth.db and the api_key in the keyring.
    /// False when the keyring write failed.
    bool save_session();
    void load_session();
    void clear_session(bool wipe_device_pin);
    void validate_saved_session();
    void fetch_user_profile(std::function<void()> on_done = {});
    void complete_auth_flow(std::function<void()> on_done);
    QString generate_device_id() const;
    QJsonObject unwrap_data(const QJsonObject& raw) const;

    SessionData session_;
    bool is_loading_ = true;
    bool is_logging_out_ = false;
};

} // namespace finterm::auth

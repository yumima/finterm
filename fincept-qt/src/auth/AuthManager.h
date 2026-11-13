#pragma once
#include "auth/AuthTypes.h"

#include <QObject>

#include <functional>

namespace fincept::auth {

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
    void login_local(const QString& username, const QString& pin);
    QVector<QJsonObject> list_local_users() const;
    /// Delete a local user (auth.db row + users/<name>.db). on_done, when set,
    /// receives (ok, error) after the attempt.
    void delete_local_user(const QString& username, std::function<void(bool, const QString&)> on_done = {});
    /// Sign out. The device PIN is kept by default so the next login unlocks
    /// with the existing PIN instead of forcing a new SECURITY SETUP.
    /// Pass wipe_device_pin = true only when the PIN itself must go: the
    /// lockout "SIGN IN AGAIN" recovery (user cannot produce the PIN) and
    /// account deletion.
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

    /// True if user needs to set up a PIN (authenticated but no PIN configured).
    bool needs_pin_setup() const;

  signals:
    void auth_state_changed();
    void login_succeeded();
    void login_failed(const QString& error);
    void signup_succeeded();
    void signup_failed(const QString& error);
    void logged_out();
    void loading_changed(bool loading);

    /// Emitted after successful login when PIN setup is needed.
    void pin_setup_required();

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

} // namespace fincept::auth

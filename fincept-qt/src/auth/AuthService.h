// src/auth/AuthService.h
#pragma once
#include "auth/AuthTypes.h"

#include <QObject>
#include <QSqlDatabase>

#include <functional>

namespace fincept::auth {

/// In-process authentication service backed by a local SQLite database.
/// Replaces the external stub server (tools/local_stub/server.py).
/// All users are stored in AppPaths::data()/auth.db — nothing leaves this machine.
///
/// Password storage: SHA-256 with random salt ("salt_hex:hash_hex").
/// API keys:         UUIDs issued at OTP verification, permanent credentials.
/// Session tokens:   UUIDs generated at each login, passed to HttpClient for
///                   compatibility with existing token-clearing logic.
/// OTP verification: any 6-digit string accepted (mirrors stub behaviour).
class AuthService : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(ApiResponse)>;

    static AuthService& instance();

    /// Open (or create) auth.db and initialise the schema. Must be called once
    /// before AuthManager::initialize().
    void initialize();

    // ── Unauthenticated ───────────────────────────────────────────────────────
    void register_user(const RegisterRequest& req, Callback cb);
    void login(const LoginRequest& req, Callback cb);
    void verify_otp(const VerifyOtpRequest& req, Callback cb);

    /// Local reset: logs a console message ("use any 6-digit OTP"), returns success.
    void forgot_password(const ForgotPasswordRequest& req, Callback cb);
    /// Accepts any 6-digit OTP; updates the stored password hash.
    void reset_password(const ResetPasswordRequest& req, Callback cb);

    // ── Authenticated (api_key identifies the caller) ─────────────────────────
    void logout(const QString& api_key, Callback cb);
    void get_user_profile(const QString& api_key, Callback cb);
    void update_user_profile(const QString& api_key, const QJsonObject& data, Callback cb);
    void regenerate_api_key(const QString& api_key, Callback cb);

  private:
    AuthService() = default;

    void        init_schema();
    QString     hash_password(const QString& password) const;
    bool        verify_password(const QString& password, const QString& stored) const;
    QString     make_token() const;
    QJsonObject profile_json(int id, const QString& username, const QString& email,
                              const QString& created_at, const QString& last_login) const;

    QSqlDatabase db_;
};

} // namespace fincept::auth

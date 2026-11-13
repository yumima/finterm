#pragma once
#include <QJsonObject>
#include <QString>

namespace finterm::auth {

// ── Response types ───────────────────────────────────────────────────────────

struct ApiResponse {
    bool success = false;
    QJsonObject data;
    QString error;
    int status_code = 0;
};

struct UserProfile {
    int id = 0;
    QString username;
    QString email;
    QString phone;
    QString country;
    QString country_code;
    QString created_at;
    QString last_login_at;

    static UserProfile from_json(const QJsonObject& obj) {
        UserProfile p;
        p.id = obj["id"].toInt();
        p.username = obj["username"].toString();
        p.email = obj["email"].toString();
        p.phone = obj["phone"].toString();
        p.country = obj["country"].toString();
        p.country_code = obj["country_code"].toString();
        p.created_at = obj["created_at"].toString();
        p.last_login_at = obj["last_login_at"].toString();
        return p;
    }
};

// ── Session data ─────────────────────────────────────────────────────────────

struct SessionData {
    bool authenticated = false;
    QString api_key;
    QString session_token;
    QString device_id;
    UserProfile user_info;

    QJsonObject to_json() const {
        QJsonObject obj;
        obj["authenticated"] = authenticated;
        obj["api_key"] = api_key;
        obj["session_token"] = session_token;
        obj["device_id"] = device_id;

        QJsonObject ui;
        ui["username"] = user_info.username;
        ui["email"] = user_info.email;
        obj["user_info"] = ui;

        return obj;
    }

    /// Sessions persisted by older builds also carry has_subscription, a
    /// "subscription" object and user_info.account_type / credit_balance /
    /// is_verified / mfa_enabled from the removed plan model; they are ignored.
    static SessionData from_json(const QJsonObject& obj) {
        SessionData s;
        s.authenticated = obj["authenticated"].toBool();
        s.api_key = obj["api_key"].toString();
        s.session_token = obj["session_token"].toString();
        s.device_id = obj["device_id"].toString();

        if (obj.contains("user_info")) {
            auto ui = obj["user_info"].toObject();
            s.user_info.username = ui["username"].toString();
            s.user_info.email = ui["email"].toString();
        }
        return s;
    }
};

// ── Local-user email ─────────────────────────────────────────────────────────

/// Local users get "<username>@local" in auth.db only to fill its unique
/// email column (AuthService::register_local_user). It is not an address.
inline constexpr const char* kSyntheticEmailSuffix = "@local";

/// The email worth showing: empty for none or the synthetic filler.
inline QString displayable_email(const QString& email) {
    if (email.isEmpty() || email.endsWith(QLatin1String(kSyntheticEmailSuffix), Qt::CaseInsensitive))
        return {};
    return email;
}

} // namespace finterm::auth

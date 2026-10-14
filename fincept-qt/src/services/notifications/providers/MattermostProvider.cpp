#include "services/notifications/providers/MattermostProvider.h"

#include "network/http/HttpClient.h"

#include <QJsonObject>

namespace fincept::notifications {

void MattermostProvider::load_fields(SettingsRepository& r, const QString& cat) {
    webhook_url_ = get_str(r, cat + ".webhook_url");
    channel_ = get_str(r, cat + ".channel");
    username_ = get_str(r, cat + ".username");
}

void MattermostProvider::save_fields(SettingsRepository& r, const QString& cat) {
    set_str(r, cat + ".webhook_url", webhook_url_, cat);
    set_str(r, cat + ".channel", channel_, cat);
    set_str(r, cat + ".username", username_, cat);
}

void MattermostProvider::send(const NotificationRequest& req, std::function<void(bool, QString)> cb) {
    if (!is_configured()) {
        cb(false, "Not configured");
        return;
    }

    QJsonObject body;
    body["text"] = QString("**%1**\n%2").arg(req.title, req.message);
    if (!channel_.isEmpty())
        body["channel"] = channel_;
    if (!username_.isEmpty())
        body["username"] = username_;

    HttpClient::instance().post(webhook_url_, body, [cb](Result<QJsonDocument> res) {
        if (res.is_err()) {
            cb(false, QString::fromStdString(res.error()));
            return;
        }
        cb(true, {});
    });
}

} // namespace fincept::notifications

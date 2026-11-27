#include "services/notifications/providers/DiscordProvider.h"

#include "network/http/HttpClient.h"

#include <QJsonArray>
#include <QJsonObject>

namespace finterm::notifications {

void DiscordProvider::load_fields(SettingsRepository& r, const QString& cat) {
    webhook_url_ = get_str(r, cat + ".webhook_url");
}

void DiscordProvider::save_fields(SettingsRepository& r, const QString& cat) {
    set_str(r, cat + ".webhook_url", webhook_url_, cat);
}

void DiscordProvider::send(const NotificationRequest& req, std::function<void(bool, QString)> cb) {
    if (!is_configured()) {
        cb(false, "Not configured");
        return;
    }

    const int color = [&]() -> int {
        switch (req.level) {
            case NotifLevel::Warning:
                return 0xF59E0B; // amber
            case NotifLevel::Alert:
                return 0xF97316; // orange
            case NotifLevel::Critical:
                return 0xEF4444; // red
            default:
                return 0x06B6D4; // cyan
        }
    }();

    QJsonObject embed;
    embed["title"] = req.title;
    embed["description"] = req.message;
    embed["color"] = color;
    embed["timestamp"] = req.timestamp.toUTC().toString(Qt::ISODate);

    QJsonArray embeds;
    embeds.append(embed);

    QJsonObject body;
    body["embeds"] = embeds;

    HttpClient::instance().post(webhook_url_, body, [cb](Result<QJsonDocument> res) {
        // Discord answers 204 No Content on success: HttpClient's ok().
        if (res.is_ok())
            cb(true, {});
        else
            cb(false, QString::fromStdString(res.error()));
    });
}

} // namespace finterm::notifications

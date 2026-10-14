#pragma once
#include "services/notifications/NotificationFieldStore.h"
#include "services/notifications/NotificationService.h"
#include "storage/repositories/SettingsRepository.h"

#include <QSet>

namespace fincept::notifications {

/// Convenience base class for HTTP-based notification providers.
/// Subclasses implement provider_id(), display_name(), icon(), and send().
/// load_config() / save_config() / is_configured() / is_enabled() are handled here
/// via SettingsRepository under category "notif_<provider_id>".
class BaseProvider : public INotificationProvider {
  public:
    bool is_enabled() const override { return enabled_; }

    void load_config() override {
        auto& r = SettingsRepository::instance();
        const QString cat = category();
        enabled_ = get_bool(r, cat + ".enabled", false);
        load_fields(r, cat);
    }

    void save_config() override {
        auto& r = SettingsRepository::instance();
        const QString cat = category();
        r.set(cat + ".enabled", enabled_ ? "1" : "0", cat);
        save_fields(r, cat);
    }

  protected:
    bool enabled_{false};

    QString category() const { return QString("notif_%1").arg(provider_id()); }

    static bool get_bool(SettingsRepository& r, const QString& key, bool def) {
        auto res = r.get(key);
        return res.is_ok() && !res.value().isEmpty() ? (res.value() == "1") : def;
    }

    // Fields go through NotificationFieldStore: secrets to the OS keyring,
    // plain settings to the settings table. `key` is "<category>.<field>".
    // A secret that cannot be read right now (locked keyring) loads as empty
    // — the provider is simply unconfigured — and is remembered, so a later
    // save_config() never writes that blank back over the real value.
    QString get_str(SettingsRepository&, const QString& key) {
        const auto v = read_field(key.section(QLatin1Char('.'), 0, -2), key.section(QLatin1Char('.'), -1));
        if (v)
            unreadable_.remove(key);
        else
            unreadable_.insert(key);
        return v.value_or(QString{});
    }
    void set_str(SettingsRepository&, const QString& key, const QString& value, const QString&) {
        if (unreadable_.contains(key))
            return;
        write_field(key.section(QLatin1Char('.'), 0, -2), key.section(QLatin1Char('.'), -1), value);
    }

  private:
    QSet<QString> unreadable_;

  protected:
    /// Subclasses load their specific credential fields here.
    virtual void load_fields(SettingsRepository& r, const QString& cat) = 0;
    /// Subclasses persist their specific credential fields here.
    virtual void save_fields(SettingsRepository& r, const QString& cat) = 0;
};

} // namespace fincept::notifications

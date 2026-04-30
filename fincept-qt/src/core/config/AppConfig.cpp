#include "core/config/AppConfig.h"

namespace fincept {

AppConfig& AppConfig::instance() {
    static AppConfig s;
    return s;
}

AppConfig::AppConfig() : settings_("Fincept", "FinceptTerminal") {}

QVariant AppConfig::get(const QString& key, const QVariant& default_val) const {
    return settings_.value(key, default_val);
}

void AppConfig::set(const QString& key, const QVariant& value) {
    settings_.setValue(key, value);
}

void AppConfig::remove(const QString& key) {
    settings_.remove(key);
}

QString AppConfig::api_base_url() const {
    // Localhost-only fork: default base URL points at the bundled local stub server
    // (tools/local_stub_server.py). No traffic ever leaves this machine.
    return settings_.value("api/base_url", "http://127.0.0.1:8765").toString();
}

bool AppConfig::dark_mode() const {
    return settings_.value("ui/dark_mode", true).toBool();
}

int AppConfig::refresh_interval_ms() const {
    return settings_.value("data/refresh_interval_ms", 30000).toInt();
}

} // namespace fincept

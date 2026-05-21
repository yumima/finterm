#include "storage/secure/LlmSecureKeys.h"

#include "core/logging/Logger.h"
#include "storage/secure/SecureStorage.h"

namespace fincept {

namespace {
constexpr const char* kTag = "LlmSecureKeys";
}

QString llm_secure_key_for_provider(const QString& provider) {
    return QStringLiteral("llm.configs.") + provider;
}

QString llm_secure_key_for_model(const QString& model_id) {
    return QStringLiteral("llm.models.") + model_id;
}

QString llm_secure_key_for_profile(const QString& profile_id) {
    return QStringLiteral("llm.profiles.") + profile_id;
}

QString llm_secure_load(const QString& key) {
    auto r = SecureStorage::instance().retrieve(key);
    if (r.is_err()) {
        LOG_DEBUG(kTag, QString("retrieve(%1) miss: %2").arg(key, QString::fromStdString(r.error())));
        return {};
    }
    return r.value();
}

Result<void> llm_secure_store(const QString& key, const QString& value) {
    if (value.isEmpty()) {
        return llm_secure_remove(key);
    }
    auto r = SecureStorage::instance().store(key, value);
    if (r.is_err()) {
        LOG_WARN(kTag, QString("store(%1) failed: %2").arg(key, QString::fromStdString(r.error())));
    }
    return r;
}

Result<void> llm_secure_remove(const QString& key) {
    auto r = SecureStorage::instance().remove(key);
    if (r.is_err()) {
        // Treat not-found as success — caller already has the desired state.
        LOG_DEBUG(kTag, QString("remove(%1) ignored: %2").arg(key, QString::fromStdString(r.error())));
        return Result<void>::ok();
    }
    return r;
}

} // namespace fincept

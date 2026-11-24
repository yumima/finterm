#include "storage/repositories/EngineLinkRepository.h"

#include "storage/sqlite/Database.h"

namespace finterm {

namespace {
constexpr const char* kLink = "hearth.link";
constexpr const char* kPrevious = "hearth.previous_provider";
constexpr const char* kStart = "hearth.start_with_app";
// "ollama" is the row's old name (v051); both are hearth.
constexpr const char* kIsHearth = "lower(provider) IN ('hearth', 'ollama')";

QString get(const char* key) {
    auto r = Database::instance().execute("SELECT value FROM settings WHERE key = ?", {QString::fromLatin1(key)});
    return r.is_ok() && r.value().next() ? r.value().value(0).toString() : QString();
}

Result<void> run(const QString& sql, const QVariantList& params = {}) {
    auto r = Database::instance().execute(sql, params);
    return r.is_err() ? Result<void>::err(r.error()) : Result<void>::ok();
}

Result<void> put(const char* key, const QString& value) {
    return run("INSERT OR REPLACE INTO settings (key, value, category) VALUES (?, ?, 'ai')",
               {QString::fromLatin1(key), value});
}

/// All of it or none: half a switch is a chat with no provider.
template <typename F> Result<void> in_transaction(F f) {
    auto& db = Database::instance();
    if (auto b = db.begin_transaction(); b.is_err())
        return b;
    if (auto r = f(); r.is_err()) {
        db.rollback();
        return r;
    }
    return db.commit();
}
} // namespace

EngineLinkRepository& EngineLinkRepository::instance() {
    static EngineLinkRepository s;
    return s;
}

engine::Link EngineLinkRepository::link() const {
    return engine::link_from(get(kLink));
}

QString EngineLinkRepository::chat_provider() const {
    auto r = Database::instance().execute("SELECT provider FROM llm_configs WHERE is_active = 1 ORDER BY provider LIMIT 1");
    const QString p = r.is_ok() && r.value().next() ? r.value().value(0).toString().toLower() : QString();
    return (p == "hearth" || p == "ollama") && !linked() ? QString() : p; // not used, whatever its row says
}

Result<void> EngineLinkRepository::connect(bool for_chat) {
    return in_transaction([this, for_chat]() -> Result<void> {
        if (auto r = put(kLink, engine::to_stored(engine::Link::Connected)); r.is_err())
            return r;
        // (Read as connected, now: a hearth row that is active is the chat's.)
        const QString now = chat_provider();
        const bool hearth_chats = now == "hearth" || now == "ollama";
        if (!now.isEmpty() && !for_chat && !hearth_chats)
            return Result<void>::ok();
        if (!hearth_chats)
            if (auto r = put(kPrevious, now); r.is_err())
                return r;
        // A row the user configured keeps its model and address; the old name
        // counts as that row.
        if (auto r = run(QString("INSERT INTO llm_configs (provider, base_url, model, is_active) "
                                 "SELECT 'hearth', '', 'primary_chat', 0 "
                                 "WHERE NOT EXISTS (SELECT 1 FROM llm_configs WHERE %1)")
                             .arg(kIsHearth));
            r.is_err())
            return r;
        if (auto r = run("UPDATE llm_configs SET is_active = 0"); r.is_err())
            return r;
        return run(QString("UPDATE llm_configs SET is_active = 1 WHERE provider = "
                           "(SELECT provider FROM llm_configs WHERE %1 ORDER BY provider LIMIT 1)")
                       .arg(kIsHearth));
    });
}

Result<void> EngineLinkRepository::disconnect() {
    return in_transaction([this]() -> Result<void> {
        if (auto r = put(kLink, engine::to_stored(engine::Link::Declined)); r.is_err())
            return r;
        const QString previous = get(kPrevious);
        if (auto r = put(kPrevious, {}); r.is_err())
            return r;
        const QString now = chat_provider();
        if (!now.isEmpty() && now != "hearth" && now != "ollama")
            return Result<void>::ok(); // the chat is with another: it stays
        if (auto r = run("UPDATE llm_configs SET is_active = 0"); r.is_err())
            return r;
        return run(QString("UPDATE llm_configs SET is_active = 1 WHERE lower(provider) = ? AND NOT (%1)").arg(kIsHearth),
                   {previous});
    });
}

Result<void> EngineLinkRepository::decline() {
    return disconnect();
}

bool EngineLinkRepository::start_with_app() const {
    return get(kStart) == QLatin1String("true");
}

Result<void> EngineLinkRepository::set_start_with_app(bool on) {
    return put(kStart, on ? QStringLiteral("true") : QStringLiteral("false"));
}

} // namespace finterm

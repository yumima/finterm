#include "storage/repositories/ChatArtefactRepository.h"

#include "storage/sqlite/Database.h"

#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

namespace fincept {

namespace {

constexpr int kMaxListLimit = 500;

ChatArtefactRow map_row(QSqlQuery& q) {
    ChatArtefactRow r;
    r.id = q.value(0).toString();
    r.kind = q.value(1).toString();
    r.title = q.value(2).toString();
    r.payload_json = q.value(3).toString();
    r.source_request_id = q.value(4).toString();
    r.source_agent_id = q.value(5).toString();
    r.source_skill = q.value(6).toString();
    r.source_args_json = q.value(7).toString();
    r.status = q.value(8).toString();
    r.supersedes_id = q.value(9).toString();
    r.created_at = q.value(10).toString();
    r.updated_at = q.value(11).toString();
    return r;
}

constexpr const char* kCols =
    "id, kind, title, payload_json, "
    "source_request_id, source_agent_id, source_skill, source_args_json, "
    "status, supersedes_id, created_at, updated_at";

QString serialise_json(const QJsonObject& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // anonymous namespace

ChatArtefactRepository& ChatArtefactRepository::instance() {
    static ChatArtefactRepository r;
    return r;
}

Result<QString> ChatArtefactRepository::create(const ChatArtefactCreate& c) {
    if (c.kind.isEmpty() || c.title.isEmpty())
        return Result<QString>::err(
            "ChatArtefactRepository::create: kind and title are required");

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString sql = QStringLiteral(
        "INSERT INTO chat_artefacts "
        "(id, kind, title, payload_json, source_request_id, source_agent_id, "
        " source_skill, source_args_json, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    auto r = Database::instance().execute(sql, {
        id, c.kind, c.title, serialise_json(c.payload),
        c.source_request_id, c.source_agent_id, c.source_skill,
        serialise_json(c.source_args),
        c.status.isEmpty() ? QStringLiteral("final") : c.status,
    });
    if (r.is_err())
        return Result<QString>::err(r.error());
    return Result<QString>::ok(id);
}

Result<void> ChatArtefactRepository::update_payload(const QString& id, const QJsonObject& payload) {
    if (id.isEmpty())
        return Result<void>::err("update_payload: empty id");
    auto r = Database::instance().execute(
        QStringLiteral("UPDATE chat_artefacts "
                       "SET payload_json = ?, updated_at = datetime('now') "
                       "WHERE id = ?"),
        {serialise_json(payload), id});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<void> ChatArtefactRepository::set_supersedes(const QString& artefact_id,
                                                    const QString& predecessor_id) {
    if (artefact_id.isEmpty())
        return Result<void>::err("set_supersedes: empty artefact_id");
    auto r = Database::instance().execute(
        QStringLiteral("UPDATE chat_artefacts SET supersedes_id = ?, "
                       "updated_at = datetime('now') WHERE id = ?"),
        {predecessor_id, artefact_id});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<std::optional<ChatArtefactRow>> ChatArtefactRepository::latest_for_request(const QString& request_id) {
    using Out = Result<std::optional<ChatArtefactRow>>;
    if (request_id.isEmpty())
        return Out::ok(std::nullopt);
    const QString sql = QStringLiteral(
        "SELECT %1 FROM chat_artefacts "
        "WHERE source_request_id = ? "
        "ORDER BY created_at DESC LIMIT 1").arg(kCols);
    auto r = Database::instance().execute(sql, {request_id});
    if (r.is_err())
        return Out::err(r.error());
    auto& q = r.value();
    if (q.next())
        return Out::ok(map_row(q));
    return Out::ok(std::nullopt);
}

Result<void> ChatArtefactRepository::mark_superseded(const QString& id) {
    if (id.isEmpty())
        return Result<void>::err("mark_superseded: empty id");
    auto r = Database::instance().execute(
        QStringLiteral("UPDATE chat_artefacts "
                       "SET status = 'superseded', updated_at = datetime('now') "
                       "WHERE id = ?"),
        {id});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<QVector<ChatArtefactRow>> ChatArtefactRepository::list_recent(int limit) {
    const int n = std::clamp(limit, 1, kMaxListLimit);
    const QString sql = QStringLiteral(
        "SELECT %1 FROM chat_artefacts "
        "ORDER BY created_at DESC LIMIT ?").arg(kCols);
    auto r = Database::instance().execute(sql, {n});
    if (r.is_err())
        return Result<QVector<ChatArtefactRow>>::err(r.error());
    QVector<ChatArtefactRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<ChatArtefactRow>>::ok(std::move(rows));
}

Result<QVector<ChatArtefactRow>> ChatArtefactRepository::list_by_request(const QString& request_id) {
    const QString sql = QStringLiteral(
        "SELECT %1 FROM chat_artefacts WHERE source_request_id = ? "
        "ORDER BY created_at DESC").arg(kCols);
    auto r = Database::instance().execute(sql, {request_id});
    if (r.is_err())
        return Result<QVector<ChatArtefactRow>>::err(r.error());
    QVector<ChatArtefactRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<ChatArtefactRow>>::ok(std::move(rows));
}

Result<std::optional<ChatArtefactRow>> ChatArtefactRepository::get(const QString& id) {
    const QString sql = QStringLiteral(
        "SELECT %1 FROM chat_artefacts WHERE id = ?").arg(kCols);
    auto r = Database::instance().execute(sql, {id});
    if (r.is_err())
        return Result<std::optional<ChatArtefactRow>>::err(r.error());
    auto& q = r.value();
    if (q.next())
        return Result<std::optional<ChatArtefactRow>>::ok(map_row(q));
    return Result<std::optional<ChatArtefactRow>>::ok(std::nullopt);
}

Result<QVector<ChatArtefactRow>> ChatArtefactRepository::list_lineage_for(const QString& artefact_id) {
    using Out = Result<QVector<ChatArtefactRow>>;
    if (artefact_id.isEmpty())
        return Out::err("list_lineage_for: empty id");
    auto seed = get(artefact_id);
    if (seed.is_err())
        return Out::err(seed.error());
    if (!seed.value().has_value())
        return Out::err("list_lineage_for: artefact not found");
    const auto& a = *seed.value();

    // v039 — if this artefact (or any artefact pointing at it) has
    // an explicit supersedes_id chain, prefer that.  Walk both
    // directions (back via supersedes_id, forward via "who supersedes
    // me?") so the seed isn't required to be the head of the chain.
    auto has_supersedes = [this](const ChatArtefactRow& row) {
        return !row.supersedes_id.isEmpty();
    };
    if (has_supersedes(a)) {
        // Walk backward through supersedes_id pointers, then forward
        // by finding rows that point at any link in the chain.  In
        // practice the chain is short (a re-run history is typically
        // <10 rows); guard against cycles with a visited set.
        QVector<ChatArtefactRow> chain;
        QSet<QString> visited;
        chain.append(a);
        visited.insert(a.id);
        // Backward.
        QString cur_pred = a.supersedes_id;
        while (!cur_pred.isEmpty() && !visited.contains(cur_pred)) {
            auto pr = get(cur_pred);
            if (pr.is_err() || !pr.value().has_value())
                break;
            chain.append(*pr.value());
            visited.insert(cur_pred);
            cur_pred = pr.value()->supersedes_id;
        }
        // Forward — find every row whose supersedes_id is in the
        // visited set.  This catches branches where multiple
        // re-runs all point at the same predecessor.
        QStringList visited_list(visited.begin(), visited.end());
        QStringList placeholders;
        for (int i = 0; i < visited_list.size(); ++i)
            placeholders.append(QStringLiteral("?"));
        const QString fwd_sql = QStringLiteral(
            "SELECT %1 FROM chat_artefacts "
            "WHERE supersedes_id IN (%2) AND id NOT IN (%2) "
            "ORDER BY created_at DESC")
                                    .arg(kCols, placeholders.join(QStringLiteral(",")));
        QVariantList params;
        for (const QString& v : visited_list) params.append(v);
        for (const QString& v : visited_list) params.append(v);
        auto fwd = Database::instance().execute(fwd_sql, params);
        if (fwd.is_ok()) {
            auto& q = fwd.value();
            while (q.next()) {
                ChatArtefactRow row = map_row(q);
                if (!visited.contains(row.id)) {
                    chain.append(row);
                    visited.insert(row.id);
                }
            }
        }
        // Sort by created_at DESC for stable rendering.
        std::sort(chain.begin(), chain.end(),
                  [](const ChatArtefactRow& l, const ChatArtefactRow& r) {
                      return l.created_at > r.created_at;
                  });
        return Out::ok(std::move(chain));
    }

    // Check forward: maybe the seed is itself the head of a chain
    // (other rows supersede this one).  If yes, walk forward.
    {
        auto fwd = Database::instance().execute(
            QStringLiteral("SELECT id FROM chat_artefacts WHERE supersedes_id = ? LIMIT 1"),
            {a.id});
        if (fwd.is_ok() && fwd.value().next()) {
            // Seed has at least one successor — recurse from the
            // most-recent successor so the head-finding logic above
            // takes over and produces the same canonical chain.
            const QString successor_id = fwd.value().value(0).toString();
            return list_lineage_for(successor_id);
        }
    }

    // No source_skill ⇒ standalone artefact; lineage is just self.
    // Avoids returning every other source_skill='' row in the table.
    if (a.source_skill.isEmpty())
        return Out::ok(QVector<ChatArtefactRow>{a});

    // SQLite `IS` (a.k.a. IS NOT DISTINCT FROM) treats NULL as equal
    // to NULL and behaves like `=` for non-NULL values.  The schema
    // (v034) allows the three identity columns to be NULL — current
    // producers write empty strings, but legacy / externally-inserted
    // rows could have NULL.  `=` would evaluate UNKNOWN there and
    // exclude the seed row from its own lineage; `IS` groups
    // correctly.
    const QString sql = QStringLiteral(
        "SELECT %1 FROM chat_artefacts "
        "WHERE source_agent_id IS ? AND source_skill IS ? AND source_args_json IS ? "
        "ORDER BY created_at DESC").arg(kCols);
    auto r = Database::instance().execute(sql, {
        a.source_agent_id, a.source_skill, a.source_args_json});
    if (r.is_err())
        return Out::err(r.error());
    QVector<ChatArtefactRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    return Out::ok(std::move(rows));
}

} // namespace fincept

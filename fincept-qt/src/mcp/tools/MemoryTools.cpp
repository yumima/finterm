#include "mcp/tools/MemoryTools.h"

#include "storage/sqlite/Database.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>

namespace fincept::mcp::tools {

namespace {

constexpr int kDefaultSearchLimit = 10;
constexpr int kDefaultListLimit = 50;
constexpr int kMaxLimit = 200;

QString req_string(const QJsonObject& args, const char* key) {
    return args.value(QString::fromUtf8(key)).toString().trimmed();
}

int opt_int(const QJsonObject& args, const char* key, int fallback) {
    const auto v = args.value(QString::fromUtf8(key));
    if (v.isDouble())
        return std::max(1, std::min(kMaxLimit, static_cast<int>(v.toInt())));
    return fallback;
}

QJsonObject row_to_json(QSqlQuery& q) {
    QJsonObject o;
    o["id"] = q.value(0).toInt();
    o["scope"] = q.value(1).toString();
    o["key"] = q.value(2).toString();
    o["content"] = q.value(3).toString();
    const QString meta_str = q.value(4).toString();
    if (!meta_str.isEmpty()) {
        const auto doc = QJsonDocument::fromJson(meta_str.toUtf8());
        if (doc.isObject())
            o["metadata"] = doc.object();
    }
    o["created_at"] = q.value(5).toString();
    o["updated_at"] = q.value(6).toString();
    return o;
}

ToolResult tool_upsert(const QJsonObject& args) {
    const QString scope = req_string(args, "scope");
    const QString key = req_string(args, "key");
    const QString content = args.value("content").toString();
    if (scope.isEmpty() || key.isEmpty())
        return ToolResult::fail("memory_upsert requires non-empty `scope` and `key`");

    QString metadata_json;
    const auto meta_v = args.value("metadata");
    if (meta_v.isObject())
        metadata_json = QString::fromUtf8(
            QJsonDocument(meta_v.toObject()).toJson(QJsonDocument::Compact));

    const QString sql = QStringLiteral(
        "INSERT INTO memory_entries (scope, key, content, metadata_json) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(scope, key) DO UPDATE SET "
        "  content = excluded.content, "
        "  metadata_json = excluded.metadata_json, "
        "  updated_at = datetime('now')");
    auto r = Database::instance().execute(sql, {scope, key, content, metadata_json});
    if (r.is_err())
        return ToolResult::fail(QString::fromStdString(r.error()));

    QJsonObject data;
    data["scope"] = scope;
    data["key"] = key;
    return ToolResult::ok(QStringLiteral("Stored memory entry"), data);
}

ToolResult tool_search(const QJsonObject& args) {
    const QString scope = req_string(args, "scope");
    const QString query = req_string(args, "query");
    if (scope.isEmpty() || query.isEmpty())
        return ToolResult::fail("memory_search requires non-empty `scope` and `query`");
    const int limit = opt_int(args, "limit", kDefaultSearchLimit);

    // Plain LIKE — semantic search lands when sqlite-vec wires up.
    // % wildcards bracket the query so it matches anywhere; SQLite
    // bound parameters keep the user value safely escaped.
    const QString sql = QStringLiteral(
        "SELECT id, scope, key, content, metadata_json, created_at, updated_at "
        "FROM memory_entries "
        "WHERE scope = ? AND (content LIKE ? OR key LIKE ?) "
        "ORDER BY updated_at DESC LIMIT ?");
    const QString like = QStringLiteral("%%%1%%").arg(query);
    auto r = Database::instance().execute(sql, {scope, like, like, limit});
    if (r.is_err())
        return ToolResult::fail(QString::fromStdString(r.error()));

    QJsonArray rows;
    auto& q = r.value();
    while (q.next())
        rows.append(row_to_json(q));

    QJsonObject data;
    data["matches"] = rows;
    data["count"] = rows.size();
    return ToolResult::ok(QStringLiteral("Found %1 match(es)").arg(rows.size()), data);
}

ToolResult tool_list(const QJsonObject& args) {
    const QString scope = req_string(args, "scope");
    if (scope.isEmpty())
        return ToolResult::fail("memory_list requires non-empty `scope`");
    const int limit = opt_int(args, "limit", kDefaultListLimit);

    const QString sql = QStringLiteral(
        "SELECT id, scope, key, content, metadata_json, created_at, updated_at "
        "FROM memory_entries WHERE scope = ? "
        "ORDER BY updated_at DESC LIMIT ?");
    auto r = Database::instance().execute(sql, {scope, limit});
    if (r.is_err())
        return ToolResult::fail(QString::fromStdString(r.error()));

    QJsonArray rows;
    auto& q = r.value();
    while (q.next())
        rows.append(row_to_json(q));

    QJsonObject data;
    data["entries"] = rows;
    data["count"] = rows.size();
    return ToolResult::ok(QStringLiteral("Listed %1 entry(ies)").arg(rows.size()), data);
}

ToolSchema schema_with_scope(const QJsonObject& extra_props,
                             const QStringList& extra_required) {
    ToolSchema s;
    QJsonObject props{
        {"scope", QJsonObject{
                      {"type", "string"},
                      {"description",
                       "Memory scope — `thesis` (per open thesis), "
                       "`workspace` (per account), `global` (cross-thesis)."}}},
    };
    for (auto it = extra_props.constBegin(); it != extra_props.constEnd(); ++it)
        props.insert(it.key(), it.value());
    s.properties = props;
    s.required = QStringList{"scope"} + extra_required;
    return s;
}

} // anonymous namespace

std::vector<ToolDef> get_memory_tools() {
    std::vector<ToolDef> tools;
    tools.reserve(3);

    {
        ToolDef t;
        t.name = "memory_upsert";
        t.description =
            "Store or update an entry in the agent's persistent memory. "
            "Idempotent on (scope, key) — calling with the same scope + key "
            "overwrites the existing content and bumps updated_at.";
        t.category = "memory";
        t.input_schema = schema_with_scope(
            QJsonObject{
                {"key", QJsonObject{{"type", "string"},
                                    {"description", "Stable identifier for this entry"}}},
                {"content", QJsonObject{{"type", "string"},
                                        {"description", "Text content to remember"}}},
                {"metadata", QJsonObject{{"type", "object"},
                                         {"description", "Optional structured metadata (JSON object)"}}},
            },
            {"key", "content"});
        t.handler = tool_upsert;
        tools.push_back(std::move(t));
    }

    {
        ToolDef t;
        t.name = "memory_search";
        t.description =
            "Search memory entries within a scope by keyword.  Today this "
            "is a substring match on content + key (case-sensitive); when "
            "sqlite-vec lands the same API switches to semantic similarity.";
        t.category = "memory";
        t.input_schema = schema_with_scope(
            QJsonObject{
                {"query", QJsonObject{{"type", "string"},
                                      {"description", "Search query"}}},
                {"limit", QJsonObject{{"type", "integer"},
                                      {"description", "Max results (default 10, max 200)"}}},
            },
            {"query"});
        t.handler = tool_search;
        tools.push_back(std::move(t));
    }

    {
        ToolDef t;
        t.name = "memory_list";
        t.description =
            "List recent memory entries in a scope, newest first.  Useful "
            "for surfacing context the agent has accumulated without a "
            "specific query in mind.";
        t.category = "memory";
        t.input_schema = schema_with_scope(
            QJsonObject{
                {"limit", QJsonObject{{"type", "integer"},
                                      {"description", "Max results (default 50, max 200)"}}},
            },
            {});
        t.handler = tool_list;
        tools.push_back(std::move(t));
    }

    return tools;
}

} // namespace fincept::mcp::tools

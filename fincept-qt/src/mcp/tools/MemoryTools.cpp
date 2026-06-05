#include "mcp/tools/MemoryTools.h"

#include "ai_chat/EmbeddingsClient.h"
#include "mcp/AsyncDispatch.h"
#include "storage/sqlite/Database.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPromise>
#include <QSqlQuery>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace fincept::mcp::tools {

namespace {

constexpr int kDefaultSearchLimit = 10;
constexpr int kDefaultListLimit = 50;
constexpr int kMaxLimit = 200;

// Cosine floor below which a semantic hit is treated as "not really a match".
// nomic-embed-text puts related text well above this and unrelated text below;
// when nothing clears the floor we fall back to keyword search so exact-token
// queries and "no match" still behave like the old LIKE path.
constexpr double kMinCosine = 0.35;

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

// ── embedding (de)serialization ───────────────────────────────────────────
// Stored as a packed little-endian float32 BLOB; dim lives in embedding_dim.

QByteArray embedding_to_blob(const QVector<float>& v) {
    return QByteArray(reinterpret_cast<const char*>(v.constData()),
                      static_cast<int>(v.size() * sizeof(float)));
}

double cosine(const QVector<float>& a, const float* b, int n) {
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < n; ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na += static_cast<double>(a[i]) * a[i];
        nb += static_cast<double>(b[i]) * b[i];
    }
    if (na == 0.0 || nb == 0.0)
        return 0.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

// ── LIKE fallback (the pre-embeddings behaviour) ──────────────────────────
ToolResult like_search(const QString& scope, const QString& query, int limit) {
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
    data["mode"] = "keyword";
    return ToolResult::ok(QStringLiteral("Found %1 match(es)").arg(rows.size()), data);
}

// ── semantic search over stored embeddings ────────────────────────────────
// Brute-force cosine over the scope's embedded rows. Returns nullopt when no
// row in the scope has an embedding (caller then falls back to LIKE).
std::optional<ToolResult> semantic_search(const QString& scope,
                                          const QVector<float>& qvec, int limit) {
    auto r = Database::instance().execute(
        QStringLiteral(
            "SELECT id, scope, key, content, metadata_json, created_at, updated_at, "
            "       embedding, embedding_dim "
            "FROM memory_entries WHERE scope = ? AND embedding IS NOT NULL"),
        {scope});
    if (r.is_err())
        return std::nullopt; // treat as "no semantic path"; caller does LIKE

    struct Scored {
        double score;
        QJsonObject row;
    };
    std::vector<Scored> scored;
    auto& q = r.value();
    while (q.next()) {
        const QByteArray blob = q.value(7).toByteArray();
        const int dim = q.value(8).toInt();
        const int count = static_cast<int>(blob.size() / sizeof(float));
        // Skip rows whose embedding dim doesn't match the query (e.g. a
        // different embedding model was bound when they were written).
        if (count == 0 || count != qvec.size() || (dim != 0 && dim != count))
            continue;
        const float* vec = reinterpret_cast<const float*>(blob.constData());
        const double s = cosine(qvec, vec, count);
        if (s < kMinCosine)
            continue; // too weak to count as a match
        QJsonObject o = row_to_json(q);
        o["score"] = s;
        scored.push_back({s, std::move(o)});
    }

    // No row cleared the relevance floor (or none had a usable embedding) →
    // signal the caller to fall back to keyword search.
    if (scored.empty())
        return std::nullopt;

    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) { return a.score > b.score; });
    if (static_cast<int>(scored.size()) > limit)
        scored.resize(limit);

    QJsonArray rows;
    for (auto& s : scored)
        rows.append(s.row);

    QJsonObject data;
    data["matches"] = rows;
    data["count"] = rows.size();
    data["mode"] = "semantic";
    return ToolResult::ok(QStringLiteral("Found %1 match(es)").arg(rows.size()), data);
}

// ── upsert (async — embeds content after storing) ─────────────────────────
// AsyncDispatch runs the body on the application (GUI) thread: tool dispatch
// arrives on a worker thread, but EmbeddingsClient's shared QNetworkAccessManager
// lives on the app thread (cross-thread use of it corrupts QObject parentage —
// see NewsTools), and `resolve` is idempotent so it can't double-finish against
// the provider's timeout watchdog.
void async_upsert(const QJsonObject& args, ToolContext ctx,
                  std::shared_ptr<QPromise<ToolResult>> promise) {
    AsyncDispatch::callback_to_promise(qApp, ctx, promise, [args](auto resolve) {
        const QString scope = req_string(args, "scope");
        const QString key = req_string(args, "key");
        const QString content = args.value("content").toString();
        if (scope.isEmpty() || key.isEmpty()) {
            resolve(ToolResult::fail("memory_upsert requires non-empty `scope` and `key`"));
            return;
        }

        QString metadata_json;
        const auto meta_v = args.value("metadata");
        if (meta_v.isObject())
            metadata_json = QString::fromUtf8(
                QJsonDocument(meta_v.toObject()).toJson(QJsonDocument::Compact));

        // Storing content nulls any stale embedding; we recompute it below. This
        // keeps search correct even if the embedding step then fails (the row
        // simply falls back to keyword matching until the next successful upsert).
        const QString sql = QStringLiteral(
            "INSERT INTO memory_entries (scope, key, content, metadata_json, "
            "                            embedding, embedding_dim, embedding_model) "
            "VALUES (?, ?, ?, ?, NULL, NULL, NULL) "
            "ON CONFLICT(scope, key) DO UPDATE SET "
            "  content = excluded.content, "
            "  metadata_json = excluded.metadata_json, "
            "  embedding = NULL, embedding_dim = NULL, embedding_model = NULL, "
            "  updated_at = datetime('now')");
        auto r = Database::instance().execute(sql, {scope, key, content, metadata_json});
        if (r.is_err()) {
            resolve(ToolResult::fail(QString::fromStdString(r.error())));
            return;
        }

        auto finish_ok = [scope, key, resolve](const QString& note) {
            QJsonObject data;
            data["scope"] = scope;
            data["key"] = key;
            resolve(ToolResult::ok(QStringLiteral("Stored memory entry%1").arg(note), data));
        };

        // Best-effort embedding; never blocks the upsert's success. The UPDATE
        // is guarded by `content = ?` so a late embedding callback can't bind a
        // stale vector to content a newer upsert has already changed.
        EmbeddingsClient::embed(
            content, [scope, key, content, finish_ok](std::optional<QVector<float>> emb) {
                if (emb && !emb->isEmpty()) {
                    auto ur = Database::instance().execute(
                        QStringLiteral("UPDATE memory_entries SET embedding = ?, "
                                       "embedding_dim = ?, embedding_model = ? "
                                       "WHERE scope = ? AND key = ? AND content = ?"),
                        {embedding_to_blob(*emb), static_cast<int>(emb->size()),
                         QStringLiteral("embedding"), scope, key, content});
                    (void)ur;
                    finish_ok(QString());
                } else {
                    finish_ok(QStringLiteral(" (keyword-only; embedding unavailable)"));
                }
            });
    });
}

// ── search (async — embeds query, semantic with LIKE fallback) ────────────
void async_search(const QJsonObject& args, ToolContext ctx,
                  std::shared_ptr<QPromise<ToolResult>> promise) {
    AsyncDispatch::callback_to_promise(qApp, ctx, promise, [args](auto resolve) {
        const QString scope = req_string(args, "scope");
        const QString query = req_string(args, "query");
        if (scope.isEmpty() || query.isEmpty()) {
            resolve(ToolResult::fail("memory_search requires non-empty `scope` and `query`"));
            return;
        }
        const int limit = opt_int(args, "limit", kDefaultSearchLimit);

        EmbeddingsClient::embed(
            query, [scope, query, limit, resolve](std::optional<QVector<float>> qemb) {
                if (qemb && !qemb->isEmpty()) {
                    if (auto sem = semantic_search(scope, *qemb, limit)) {
                        resolve(*sem);
                        return;
                    }
                }
                // No embedding (engine down), no embedded rows, or nothing
                // cleared the relevance floor → keyword fallback.
                resolve(like_search(scope, query, limit));
            });
    });
}

// ── delete / list (sync — no embeddings needed) ───────────────────────────
ToolResult tool_delete(const QJsonObject& args) {
    const QString scope = req_string(args, "scope");
    const QString key = req_string(args, "key");
    if (scope.isEmpty() || key.isEmpty())
        return ToolResult::fail("memory_delete requires non-empty `scope` and `key`");

    auto r = Database::instance().execute(
        QStringLiteral("DELETE FROM memory_entries WHERE scope = ? AND key = ?"),
        {scope, key});
    if (r.is_err())
        return ToolResult::fail(QString::fromStdString(r.error()));

    const int affected = r.value().numRowsAffected();
    QJsonObject data;
    data["scope"] = scope;
    data["key"] = key;
    data["deleted"] = affected;
    return ToolResult::ok(
        affected > 0
            ? QStringLiteral("Deleted memory entry (%1, %2)").arg(scope, key)
            : QStringLiteral("No entry matched (%1, %2)").arg(scope, key),
        data);
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
    tools.reserve(4);

    {
        ToolDef t;
        t.name = "memory_upsert";
        t.description =
            "Store or update an entry in the agent's persistent memory. "
            "Idempotent on (scope, key) — calling with the same scope + key "
            "overwrites the existing content and bumps updated_at. The content "
            "is embedded (via the local engine) so memory_search can find it "
            "by meaning, not just keywords.";
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
        t.async_handler = async_upsert;
        tools.push_back(std::move(t));
    }

    {
        ToolDef t;
        t.name = "memory_search";
        t.description =
            "Search memory entries within a scope. Uses semantic (cosine) "
            "similarity over embeddings when the local engine is available, "
            "falling back to a substring keyword match otherwise. The response "
            "`mode` field reports which path ran (`semantic` or `keyword`).";
        t.category = "memory";
        t.async_handler = async_search;
        t.input_schema = schema_with_scope(
            QJsonObject{
                {"query", QJsonObject{{"type", "string"},
                                      {"description", "Search query"}}},
                {"limit", QJsonObject{{"type", "integer"},
                                      {"description", "Max results (default 10, max 200)"}}},
            },
            {"query"});
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

    {
        ToolDef t;
        t.name = "memory_delete";
        t.description =
            "Remove an entry from the agent's persistent memory.  No-op "
            "(returns deleted=0) when no entry matches — caller can treat "
            "that as success.  Destructive, so the auth gate fires.";
        t.category = "memory";
        t.is_destructive = true;
        t.input_schema = schema_with_scope(
            QJsonObject{
                {"key", QJsonObject{{"type", "string"},
                                    {"description", "Stable identifier of the entry to delete"}}},
            },
            {"key"});
        t.handler = tool_delete;
        tools.push_back(std::move(t));
    }

    return tools;
}

} // namespace fincept::mcp::tools

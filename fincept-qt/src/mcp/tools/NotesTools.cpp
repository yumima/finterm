// NotesTools.cpp — Notes tab MCP tools + resources (research notes)

#include "mcp/tools/NotesTools.h"

#include "core/events/EventBus.h"
#include "core/logging/Logger.h"
#include "storage/repositories/NotesRepository.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QVariantMap>

#include <algorithm>

namespace fincept::mcp::tools {

std::vector<ToolDef> get_notes_tools() {
    std::vector<ToolDef> tools;

    // ── create_note ─────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "create_note";
        t.description = "Create a research note with optional category, priority, tickers, and sentiment.";
        t.category = "notes";
        t.input_schema.properties = QJsonObject{
            {"title", QJsonObject{{"type", "string"}, {"description", "Note title"}}},
            {"content", QJsonObject{{"type", "string"}, {"description", "Note content (markdown)"}}},
            {"category",
             QJsonObject{
                 {"type", "string"},
                 {"description", "RESEARCH, TRADE_IDEA, MARKET_ANALYSIS, EARNINGS, ECONOMIC, PORTFOLIO, GENERAL"}}},
            {"priority", QJsonObject{{"type", "string"}, {"description", "HIGH, MEDIUM, LOW"}}},
            {"tickers", QJsonObject{{"type", "string"}, {"description", "Comma-separated ticker symbols"}}},
            {"sentiment", QJsonObject{{"type", "string"}, {"description", "BULLISH, BEARISH, NEUTRAL"}}},
            {"tags", QJsonObject{{"type", "string"}, {"description", "Comma-separated tags"}}}};
        t.input_schema.required = {"title", "content"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            QString title = args["title"].toString().trimmed();
            if (title.isEmpty())
                return ToolResult::fail("Missing 'title'");

            FinancialNote note;
            note.title = title;
            note.content = args["content"].toString();
            note.category = args["category"].toString("GENERAL");
            note.priority = args["priority"].toString("MEDIUM");
            note.tickers = args["tickers"].toString();
            note.sentiment = args["sentiment"].toString("NEUTRAL");
            note.tags = args["tags"].toString();

            auto result = NotesRepository::instance().create(note);
            if (result.is_err())
                return ToolResult::fail("Failed to create note: " + QString::fromStdString(result.error()));

            EventBus::instance().publish("notes.created", QVariantMap{{"id", static_cast<qlonglong>(result.value())}});
            return ToolResult::ok("Note created: " + title,
                                  QJsonObject{{"id", static_cast<int>(result.value())}, {"title", title}});
        };
        tools.push_back(std::move(t));
    }

    // ── get_notes ───────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_notes";
        t.description = "Get all research notes, optionally filtered by search query.";
        t.category = "notes";
        t.input_schema.properties =
            QJsonObject{{"query", QJsonObject{{"type", "string"}, {"description", "Search query (optional)"}}}};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            QString query = args["query"].toString().trimmed();
            auto result =
                query.isEmpty() ? NotesRepository::instance().list_all() : NotesRepository::instance().search(query);

            if (result.is_err())
                return ToolResult::fail("Failed to load notes: " + QString::fromStdString(result.error()));

            QJsonArray arr;
            for (const auto& n : result.value()) {
                arr.append(QJsonObject{{"id", n.id},
                                       {"title", n.title},
                                       {"category", n.category},
                                       {"priority", n.priority},
                                       {"tickers", n.tickers},
                                       {"sentiment", n.sentiment},
                                       {"is_favorite", n.is_favorite},
                                       {"created_at", n.created_at},
                                       {"updated_at", n.updated_at}});
            }
            return ToolResult::ok_data(arr);
        };
        tools.push_back(std::move(t));
    }

    // ── delete_note ─────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "delete_note";
        t.description = "Delete a research note by ID.";
        t.category = "notes";
        t.input_schema.properties =
            QJsonObject{{"id", QJsonObject{{"type", "integer"}, {"description", "Note ID to delete"}}}};
        t.input_schema.required = {"id"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            int id = args["id"].toInt(-1);
            if (id < 0)
                return ToolResult::fail("Missing or invalid 'id'");

            auto r = NotesRepository::instance().remove(id);
            if (r.is_err())
                return ToolResult::fail("Failed to delete note: " + QString::fromStdString(r.error()));

            EventBus::instance().publish("notes.deleted", QVariantMap{{"id", id}});
            return ToolResult::ok("Note deleted");
        };
        tools.push_back(std::move(t));
    }

    return tools;
}

// ════════════════════════════════════════════════════════════════════
// Resources
// ════════════════════════════════════════════════════════════════════
//
// finterm://notes/active_thesis — top recent non-archived notes
// (priority order, then updated_at desc).  Proxy for "what the user
// is currently thinking about"; a richer "active thesis" first-class
// concept lands when the agent surface needs it.

namespace {
int notes_priority_rank(const QString& priority) {
    const QString p = priority.toUpper();
    if (p == "CRITICAL") return 0;
    if (p == "HIGH") return 1;
    if (p == "MEDIUM") return 2;
    if (p == "LOW") return 3;
    return 4;
}
} // namespace

std::vector<Resource> get_notes_resources() {
    std::vector<Resource> resources;

    {
        Resource r;
        r.uri = QStringLiteral("finterm://notes/active_thesis");
        r.name = QStringLiteral("Active thesis (recent notes)");
        r.description = QStringLiteral(
            "Top 20 non-archived research notes ordered by priority then "
            "recency.  Read this to pick up the user's current thinking "
            "before writing or refining theses.  A first-class 'active "
            "thesis' object will replace this proxy when the agent surface "
            "needs explicit thesis-tracking.");
        r.mime_type = QStringLiteral("application/json");
        r.handler = []() -> ResourceContent {
            ResourceContent rc;
            rc.uri = QStringLiteral("finterm://notes/active_thesis");
            rc.mime_type = QStringLiteral("application/json");

            auto loaded = NotesRepository::instance().list_all(/*include_archived=*/false);
            if (loaded.is_err()) {
                rc.error = "Failed to load notes: " +
                           QString::fromStdString(loaded.error());
                return rc;
            }

            QVector<FinancialNote> notes = loaded.value();
            std::sort(notes.begin(), notes.end(),
                      [](const FinancialNote& a, const FinancialNote& b) {
                          int pa = notes_priority_rank(a.priority);
                          int pb = notes_priority_rank(b.priority);
                          if (pa != pb)
                              return pa < pb;
                          return a.updated_at > b.updated_at;
                      });

            constexpr int kMaxNotes = 20;
            const int take = std::min<int>(notes.size(), kMaxNotes);
            QJsonArray arr;
            for (int i = 0; i < take; ++i) {
                const auto& n = notes[i];
                arr.append(QJsonObject{
                    {"id", n.id},
                    {"title", n.title},
                    {"content", n.content},
                    {"category", n.category},
                    {"priority", n.priority},
                    {"sentiment", n.sentiment},
                    {"tickers", n.tickers},
                    {"tags", n.tags},
                    {"is_favorite", n.is_favorite},
                    {"created_at", n.created_at},
                    {"updated_at", n.updated_at},
                });
            }

            QJsonObject envelope{
                {"as_of", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                {"note_count", arr.size()},
                {"total_active", notes.size()},
                {"notes", arr},
            };
            rc.text =
                QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
            return rc;
        };
        resources.push_back(std::move(r));
    }

    return resources;
}

// ════════════════════════════════════════════════════════════════════
// Prompts
// ════════════════════════════════════════════════════════════════════
//
// daily_brief — user-invokable from the slash menu; expands into a
// short system + user message pair that asks the agent to draft a
// morning brief covering portfolio, watchlist, and a one-paragraph
// digest of the user's active thesis notes.  Uses the finterm://
// resources (portfolio_snapshot, watchlist/all, news/digest,
// notes/active_thesis) — the agent can read those directly without
// spending tool-call budget.

std::vector<Prompt> get_notes_prompts() {
    std::vector<Prompt> prompts;

    {
        Prompt p;
        p.name = QStringLiteral("daily_brief");
        p.description = QStringLiteral(
            "Draft a morning brief: portfolio summary, watchlist moves, "
            "headline news digest, and a one-paragraph synthesis of the "
            "user's active thesis notes.");
        p.arguments = {
            {QStringLiteral("focus"),
             QStringLiteral(
                 "Optional focus area — e.g. 'risk', 'macro', 'AAPL'.  "
                 "Defaults to a general portfolio + market brief."),
             /*required=*/false},
        };
        p.handler = [](const QHash<QString, QString>& args) -> PromptResult {
            PromptResult r;
            r.description = QStringLiteral("Daily brief draft");

            const QString focus = args.value(QStringLiteral("focus")).trimmed();
            const QString focus_clause = focus.isEmpty()
                ? QStringLiteral("a general portfolio + market view")
                : QStringLiteral("a focus on %1").arg(focus);

            PromptMessage sys;
            sys.role = QStringLiteral("system");
            sys.text = QStringLiteral(
                "You are drafting the user's daily morning brief.  Pull "
                "context from the finterm:// resources before writing:\n"
                "  - finterm://portfolio/snapshot   current holdings\n"
                "  - finterm://watchlist/all        watched symbols\n"
                "  - finterm://news/digest          last 24h news\n"
                "  - finterm://notes/active_thesis  the user's thinking\n"
                "Write four short sections (Portfolio / Watchlist / News / "
                "Thesis synthesis), <= 60 words each.  Cite sources from "
                "the news digest where claims rest on a specific article.");
            r.messages.push_back(std::move(sys));

            PromptMessage user;
            user.role = QStringLiteral("user");
            user.text = QStringLiteral(
                "Draft my morning brief with %1.  Skip preamble; start "
                "directly with the Portfolio section.").arg(focus_clause);
            r.messages.push_back(std::move(user));

            return r;
        };
        prompts.push_back(std::move(p));
    }

    return prompts;
}

} // namespace fincept::mcp::tools

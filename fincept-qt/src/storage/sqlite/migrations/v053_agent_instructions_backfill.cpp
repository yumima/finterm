// v053_agent_instructions_backfill — make the seeded agents run with the
// prompts they were seeded with, and with tools that actually exist.
//
// v026 (quant_critic) and v028 (the ten named agents) stored their prompt
// under config_json.system_prompt, but every runtime reader (AgentsViewPanel,
// the workflow agent node, finagent_core's config loader) reads
// config_json.instructions — so those agents silently ran as "You are a
// helpful AI assistant.".  This copies system_prompt -> instructions wherever
// instructions is empty.  Readers also fall back to system_prompt now; this
// fixes the persisted rows so every path (incl. the JSON editor) agrees.
//
// It also rewrites the allow_tools globs from v026/v028 that matched no
// registered tool (int__get_filing*, int__get_earnings*, int__get_portfolios,
// int__get_paper_*) to the real wire names.  Rows a user has already edited
// are only touched where a stale glob is still present.
//
// Idempotent: a second run finds nothing to change.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVector>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration053";

// Stale glob -> real tool wire names (see src/mcp/tools/*).
QStringList replacement_for(const QString& glob) {
    if (glob == QLatin1String("int__get_filing*"))
        return {QStringLiteral("int__edgar_search_filings"), QStringLiteral("int__edgar_get_filing_*"),
                QStringLiteral("int__fd_get_sec_filing_section")};
    if (glob == QLatin1String("int__get_earnings*"))
        return {QStringLiteral("int__get_equity_earnings_outlook"), QStringLiteral("int__fd_get_earnings")};
    if (glob == QLatin1String("int__get_portfolios"))
        return {QStringLiteral("int__list_portfolios"), QStringLiteral("int__get_portfolio")};
    if (glob == QLatin1String("int__get_paper_*"))
        return {QStringLiteral("int__pt_list_portfolios"), QStringLiteral("int__pt_get_*")};
    return {};
}

Result<void> apply_v053(QSqlDatabase& db) {
    QSqlQuery exists(db);
    if (!exists.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='agent_configs'"))
        return Result<void>::err(exists.lastError().text().toStdString());
    if (!exists.next()) {
        LOG_WARN(kTag, "agent_configs table missing — skipping v053");
        return Result<void>::ok();
    }

    struct Row {
        QString id;
        QString json;
    };
    QVector<Row> rows;
    {
        QSqlQuery sel(db);
        if (!sel.exec("SELECT id, config_json FROM agent_configs"))
            return Result<void>::err(sel.lastError().text().toStdString());
        while (sel.next())
            rows.append({sel.value(0).toString(), sel.value(1).toString()});
    }

    int updated = 0;
    for (const Row& r : rows) {
        const QJsonDocument doc = QJsonDocument::fromJson(r.json.toUtf8());
        if (!doc.isObject())
            continue;
        QJsonObject cfg = doc.object();
        bool changed = false;

        const QString instr = cfg.value("instructions").toString().trimmed();
        const QString sys = cfg.value("system_prompt").toString().trimmed();
        if (instr.isEmpty() && !sys.isEmpty()) {
            cfg["instructions"] = sys;
            changed = true;
        }

        if (cfg.value("allow_tools").isArray()) {
            QStringList out;
            bool globs_changed = false;
            for (const auto& v : cfg.value("allow_tools").toArray()) {
                const QString g = v.toString();
                const QStringList rep = replacement_for(g);
                if (rep.isEmpty()) {
                    if (!out.contains(g))
                        out.append(g);
                    continue;
                }
                globs_changed = true;
                for (const QString& n : rep)
                    if (!out.contains(n))
                        out.append(n);
            }
            if (globs_changed) {
                cfg["allow_tools"] = QJsonArray::fromStringList(out);
                changed = true;
            }
        }

        if (!changed)
            continue;
        QSqlQuery up(db);
        up.prepare("UPDATE agent_configs SET config_json = ? WHERE id = ?");
        up.addBindValue(QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)));
        up.addBindValue(r.id);
        if (!up.exec())
            return Result<void>::err(up.lastError().text().toStdString());
        ++updated;
    }
    LOG_INFO(kTag, QString("Backfilled instructions / tool globs on %1 agent config(s)").arg(updated));
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v053() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({53, "agent_instructions_backfill", apply_v053});
}

} // namespace fincept

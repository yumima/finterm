// v028_named_agents — Seed the 10 named agent identities from
// Track 7 #20 (Pitch / Meeting Prep / Market Researcher / Earnings
// Reviewer / Model Builder / Valuation Reviewer / GL Reconciler /
// Month-End Closer / Statement Auditor / KYC Screener).
//
// Each row carries (system_prompt, skills[], allow_tools[]).  Skill
// names come from `anthropics/financial-services`; vendoring under
// scripts/agents/finagent_core/skills/ lands in Track 7 #19+22.
// Until then, skills_loader silently drops unknown names so the
// agents run on system_prompt + tools alone.
//
// allow_tools globs (Track 8 surface) follow the existing tool-name
// convention: internal McpProvider tools surface as `int__<name>`
// and external servers as `<server_id>__<name>`.  Each agent gets
// only the read-side tools it needs.  No agent here gets destructive
// tools (place_order, write-side settings, etc.); those stay gated
// behind explicit user invocation.
//
// Idempotent: INSERT OR IGNORE on the id primary key.  Users can
// edit any of these rows by hand without fear of being overwritten
// on re-migration.  Removing a row from this file does NOT remove
// it from disk — that's a deliberate cleanup script's job.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVector>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration028";

struct AgentSeed {
    const char* id;
    const char* name;
    const char* description;
    const char* category;
    const char* system_prompt;
    QStringList skills;
    QStringList allow_tools;
};

// Common read-side tool families that several agents share.
static QStringList market_reads() {
    return {
        QStringLiteral("int__fd_*"),         // financial-datasets REST
        QStringLiteral("int__get_quote*"),
        QStringLiteral("int__get_news*"),
        QStringLiteral("int__get_filing*"),
        QStringLiteral("int__get_earnings*"),
    };
}
static QStringList portfolio_reads() {
    return {
        QStringLiteral("int__get_holdings"),
        QStringLiteral("int__get_portfolios"),
        QStringLiteral("int__get_watchlist*"),
        QStringLiteral("int__get_paper_*"),  // read-side only
        QStringLiteral("int__get_notes*"),
    };
}

static QVector<AgentSeed> all_seeds() {
    return {
        // ── Equity research / IB ─────────────────────────────────────
        {
            "pitch_agent",
            "Pitch Agent",
            "Builds investment pitches.  Combines comps, DCF, catalyst "
            "research, and narrative into a defensible long/short case.",
            "research",
            "You are pitch_agent, finterm's pitch-building specialist.  "
            "Produce a defensible long/short case grounded in numbers: "
            "comps multiples vs peers, DCF sensitivity, near-term "
            "catalysts, downside-case anchor.  Cite the data source for "
            "every claim.  Hedge only when the data genuinely disagrees "
            "with itself; don't pad with caveats.",
            {QStringLiteral("comps-analysis"),
             QStringLiteral("dcf-model"),
             QStringLiteral("ic-memo"),
             QStringLiteral("idea-generation"),
             QStringLiteral("initiating-coverage")},
            market_reads() + portfolio_reads(),
        },
        {
            "meeting_prep",
            "Meeting Prep",
            "Prepares client / IC / management meetings: agenda, "
            "questions, briefing pack from filings and recent news.",
            "research",
            "You are meeting_prep, finterm's meeting-preparation "
            "specialist.  Produce a briefing pack: company snapshot, "
            "last quarter highlights, top open questions for "
            "management, anticipated objections from the audience.  "
            "Pull from recent filings and news; mark items where the "
            "evidence is older than 30 days.",
            {QStringLiteral("morning-note"),
             QStringLiteral("client-review"),
             QStringLiteral("ic-memo")},
            market_reads() + portfolio_reads(),
        },
        {
            "market_researcher",
            "Market Researcher",
            "Sector- and theme-level research: TAM, competitive "
            "landscape, regulatory backdrop, key data points.",
            "research",
            "You are market_researcher, finterm's sector- and theme-"
            "level analyst.  Produce a structured sector overview: "
            "TAM, growth drivers, competitive landscape, regulatory "
            "exposure, key listed names.  Distinguish between "
            "data-supported claims and inference; flag the gap.",
            {QStringLiteral("sector-overview"),
             QStringLiteral("idea-generation"),
             QStringLiteral("thesis-tracker"),
             QStringLiteral("catalyst-calendar")},
            market_reads(),
        },
        {
            "earnings_reviewer",
            "Earnings Reviewer",
            "Pre- and post-earnings analysis: KPI walk, guide change, "
            "sell-side reaction map.",
            "research",
            "You are earnings_reviewer.  For an upcoming print: "
            "summarise consensus, identify the 3-5 KPIs that drive "
            "the stock, list the bull/bear set-ups.  For a printed "
            "quarter: walk the KPIs vs consensus, flag the guide "
            "change, surface the call's biggest reveal in one "
            "sentence.  No hedging — be specific.",
            {QStringLiteral("earnings-analysis"),
             QStringLiteral("earnings-preview"),
             QStringLiteral("morning-note")},
            market_reads(),
        },
        {
            "model_builder",
            "Model Builder",
            "Builds and updates 3-statement / LBO / DCF models.  "
            "Drives the model-update workflow.",
            "research",
            "You are model_builder.  Build or update 3-statement / "
            "DCF / LBO models from the user's inputs.  Surface every "
            "assumption with its source.  Reject impossible inputs "
            "(negative revenue growth + expanding margins forever, "
            "etc.) instead of silently building.",
            {QStringLiteral("3-statement-model"),
             QStringLiteral("dcf-model"),
             QStringLiteral("lbo-model"),
             QStringLiteral("model-update")},
            market_reads(),
        },
        {
            "valuation_reviewer",
            "Valuation Reviewer",
            "Reviews a model or comp set for methodology issues — "
            "wrong multiple, peer mismatch, terminal-value abuse.",
            "research",
            "You are valuation_reviewer.  Critique the model or "
            "comp set the user supplies.  Look for: wrong multiple "
            "for the business, mis-anchored peer set, terminal-value "
            "growth that exceeds long-run GDP, mid-cycle margins "
            "that ignore the cycle.  Be direct about flaws.",
            {QStringLiteral("comps-analysis"),
             QStringLiteral("dcf-model"),
             QStringLiteral("model-update")},
            market_reads(),
        },
        // ── Fund admin / accounting / compliance ─────────────────────
        {
            "gl_reconciler",
            "GL Reconciler",
            "Reconciles general-ledger entries against custody / "
            "broker statements.  Flags breaks for investigation.",
            "ops",
            "You are gl_reconciler.  Compare GL entries against the "
            "supplied custody / broker file and produce a list of "
            "breaks: line-item differences, missing entries, FX "
            "mark mismatches.  For every break, propose the "
            "investigation step (re-fetch statement, contact "
            "counterparty, etc.).  Don't auto-close breaks.",
            {QStringLiteral("portfolio-monitoring")},
            portfolio_reads(),
        },
        {
            "month_end_closer",
            "Month-End Closer",
            "Drives the month-end close: accrual list, NAV "
            "calculation, sign-off package.",
            "ops",
            "You are month_end_closer.  Drive the close: list "
            "outstanding accruals, compute the NAV at month-end "
            "marks, assemble the sign-off package (positions, "
            "P&L, NAV walk, exposures).  Refuse to advance if "
            "any required input is missing — list what's blocking.",
            {QStringLiteral("portfolio-monitoring"),
             QStringLiteral("portfolio-rebalance")},
            portfolio_reads(),
        },
        {
            "statement_auditor",
            "Statement Auditor",
            "Audits financial-statement filings for internal "
            "consistency and tie-out vs prior period.",
            "compliance",
            "You are statement_auditor.  Audit the supplied 10-K / "
            "10-Q for: internal consistency (balance-sheet ties, "
            "cash-flow ties), tie-out vs prior-period filing, "
            "unusual line items, footnote-vs-statement mismatches.  "
            "Report findings ranked by materiality.",
            {QStringLiteral("earnings-analysis")},
            market_reads(),
        },
        {
            "kyc_screener",
            "KYC Screener",
            "Screens counterparties / accounts against name-match "
            "and sanctions criteria.  Read-only; never auto-clears.",
            "compliance",
            "You are kyc_screener.  Given counterparty identifiers, "
            "screen for name-match hits and known sanctions list "
            "presence using the supplied tools.  Surface every hit "
            "as a finding requiring human review.  Never auto-clear "
            "a hit — the user makes that call.",
            {},  // skills TBD when KYC sources land
            {QStringLiteral("int__fd_*"), QStringLiteral("int__get_news*")},
        },
    };
}

static QString build_config_json(const AgentSeed& s) {
    QJsonObject cfg;
    cfg["system_prompt"] = QString::fromUtf8(s.system_prompt);

    QJsonArray skills;
    for (const QString& sk : s.skills)
        skills.append(sk);
    cfg["skills"] = skills;

    QJsonArray allow;
    for (const QString& t : s.allow_tools)
        allow.append(t);
    cfg["allow_tools"] = allow;

    return QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
}

static Result<void> apply_v028(QSqlDatabase& db) {
    QSqlQuery exists(db);
    if (!exists.exec(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='agent_configs'")) {
        return Result<void>::err(exists.lastError().text().toStdString());
    }
    if (!exists.next()) {
        LOG_WARN(kTag, "agent_configs table missing — skipping v028");
        return Result<void>::ok();
    }

    int inserted = 0;
    for (const AgentSeed& s : all_seeds()) {
        QSqlQuery q(db);
        q.prepare("INSERT OR IGNORE INTO agent_configs "
                  "(id, name, description, config_json, category, is_default, is_active) "
                  "VALUES (?, ?, ?, ?, ?, 0, 0)");
        q.bindValue(0, QString::fromUtf8(s.id));
        q.bindValue(1, QString::fromUtf8(s.name));
        q.bindValue(2, QString::fromUtf8(s.description));
        q.bindValue(3, build_config_json(s));
        q.bindValue(4, QString::fromUtf8(s.category));
        if (!q.exec()) {
            LOG_WARN(kTag, QString("Failed to seed agent '%1': %2")
                              .arg(QString::fromUtf8(s.id), q.lastError().text()));
            continue;
        }
        if (q.numRowsAffected() > 0)
            ++inserted;
    }

    LOG_INFO(kTag, QString("v028 seeded %1 named agent(s)").arg(inserted));
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v028() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({28, "named_agents", apply_v028});
}

} // namespace fincept

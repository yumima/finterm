// v045_physical_ai_watchlist — Seed a curated "Physical AI" watchlist of
// PUBLIC tickers (humanoids, robotics, autonomy, industrial automation) plus
// the two listed robotics funds (BOT, ROBO).
//
// Runs exactly once: MigrationRunner skips any version <= the recorded
// schema_version, so editing kStocks here will NOT push updates to installs
// that already applied v045 — ship a later migration for that.
//
// Symbols are real, currently-listed tickers (verified Jun 2026). No holdings
// or quantities are implied — a watchlist tracks quotes only; ownership lives
// in the portfolio tables (portfolios / portfolio_assets), which this does not
// touch.
//
// INSERT OR IGNORE keeps this single application safe against a pre-existing
// "physical-ai" id or symbol (e.g. a user who hand-made the same list under
// this id), inserting only the missing rows.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration045";
constexpr const char* kWatchlistId = "physical-ai";

struct StockSeed {
    const char* symbol;
    const char* name;
    const char* exchange;
};

// Order mirrors the curated grouping: physical-AI pure-plays, then established
// industrial names with robotics/automation divisions, then the listed funds.
static const StockSeed kStocks[] = {
    {"TSLA", "Tesla",                "NASDAQ"},
    {"NVDA", "NVIDIA",               "NASDAQ"},
    {"ISRG", "Intuitive Surgical",   "NASDAQ"},
    {"TER",  "Teradyne",             "NASDAQ"},
    {"SYM",  "Symbotic",             "NASDAQ"},
    {"AVAV", "AeroVironment",        "NASDAQ"},
    {"SERV", "Serve Robotics",       "NASDAQ"},
    {"RR",   "Richtech Robotics",    "NASDAQ"},
    {"ACHR", "Archer Aviation",      "NYSE"},
    {"JOBY", "Joby Aviation",        "NYSE"},
    {"ABB",  "ABB Ltd",              "NYSE"},
    {"FANUY","Fanuc",                "OTC"},
    {"CGNX", "Cognex",               "NASDAQ"},
    {"DE",   "Deere & Company",      "NYSE"},
    {"ROK",  "Rockwell Automation",  "NYSE"},
    {"BOT",  "RoboStrategy",         "NASDAQ"},
    {"ROBO", "ROBO Global Robotics & Automation ETF", "NYSE Arca"},
};

static Result<void> apply_v045(QSqlDatabase& db) {
    {
        QSqlQuery q(db);
        q.prepare("INSERT OR IGNORE INTO watchlists "
                  "(id, name, description, color, sort_order, is_default) "
                  "VALUES (?, ?, ?, ?, 0, 0)");
        q.bindValue(0, QString::fromUtf8(kWatchlistId));
        q.bindValue(1, QStringLiteral("Physical AI"));
        q.bindValue(2, QStringLiteral("Public companies and ETFs across humanoids, "
                                      "robotics, autonomy, and physical AI."));
        q.bindValue(3, QStringLiteral("#26C6DA"));
        if (!q.exec())
            return Result<void>::err(q.lastError().text().toStdString());
    }

    int inserted = 0, order = 0;
    for (const auto& s : kStocks) {
        QSqlQuery q(db);
        q.prepare("INSERT OR IGNORE INTO watchlist_stocks "
                  "(watchlist_id, symbol, name, exchange, sort_order) "
                  "VALUES (?, ?, ?, ?, ?)");
        q.bindValue(0, QString::fromUtf8(kWatchlistId));
        q.bindValue(1, QString::fromUtf8(s.symbol));
        q.bindValue(2, QString::fromUtf8(s.name));
        q.bindValue(3, QString::fromUtf8(s.exchange));
        q.bindValue(4, order++);
        if (!q.exec())
            return Result<void>::err(q.lastError().text().toStdString());
        if (q.numRowsAffected() > 0)
            ++inserted;
    }

    LOG_INFO(kTag, QString("Seeded 'Physical AI' watchlist (%1 of %2 symbols added; "
                           "rest already present)")
                       .arg(inserted)
                       .arg(static_cast<int>(sizeof(kStocks) / sizeof(kStocks[0]))));
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v045() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({45, "physical_ai_watchlist", apply_v045});
}

} // namespace fincept

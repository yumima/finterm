// v024_mcp_marketplace_seed — Seed the mcp_servers table with the
// marketplace baseline from plans/ai-stack-free-local.md §7.4.
//
// All rows insert with enabled=0 so users opt-in via Settings → MCP
// Servers after reviewing each entry's command + env requirements.
// Several servers need a per-user value (filesystem path, sqlite
// db path, API keys); descriptions tell the user where to look /
// what to fill before enabling.
//
// Idempotent: INSERT OR IGNORE on the id primary key, so users who
// already added one of these by hand (with their own id) get a
// second row but their custom one isn't disturbed; users who
// already added one with the seed id get the migration as a no-op.
//
// args + env are stored as JSON strings — McpManager parses both
// shapes (JSON array / object) preferentially before falling back
// to the legacy space-separated forms.  See McpManager::load() and
// McpManager::save_server() for the storage contract.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration024";

struct Seed {
    const char* id;
    const char* name;
    const char* description;
    const char* command;
    const char* args_json;   // JSON array string of argv entries
    const char* env_json;    // JSON object string of env KEY -> VAL
    const char* category;
};

// Canonical install commands per the upstream READMEs.  Commands the
// user is most likely to need to tweak (filesystem path, sqlite
// db path, API keys) are flagged in the description so they
// configure before enabling.
static const Seed kSeeds[] = {
    {"mcp-fetch",
     "Fetch",
     "Generic URL fetch — no auth required.",
     "uvx",
     R"(["mcp-server-fetch"])",
     "{}",
     "web"},
    {"mcp-filesystem",
     "Filesystem (sandboxed)",
     "Sandboxed local file ops.  Edit args to set the allowed path before enabling.",
     "npx",
     R"(["-y","@modelcontextprotocol/server-filesystem","/tmp/finterm-fs"])",
     "{}",
     "files"},
    {"mcp-sqlite",
     "SQLite",
     "Local DB queries.  Edit args to set the db path before enabling.",
     "uvx",
     R"(["mcp-server-sqlite","--db-path","/tmp/finterm.db"])",
     "{}",
     "data"},
    {"mcp-time",
     "Time / timezone",
     "Time + timezone helpers.  No auth.",
     "uvx",
     R"(["mcp-server-time"])",
     "{}",
     "utility"},
    {"mcp-sequentialthinking",
     "Sequential Thinking",
     "Reasoning aid — emits structured intermediate thoughts.  No auth.",
     "npx",
     R"(["-y","@modelcontextprotocol/server-sequential-thinking"])",
     "{}",
     "ai"},
    {"mcp-brave-search",
     "Brave Search",
     "Web search via Brave.  Fill BRAVE_API_KEY (free at brave.com/api).",
     "npx",
     R"(["-y","@modelcontextprotocol/server-brave-search"])",
     R"({"BRAVE_API_KEY":""})",
     "web"},
    {"mcp-playwright",
     "Playwright (browser)",
     "Browser automation.  No auth.",
     "npx",
     R"(["-y","@playwright/mcp@latest"])",
     "{}",
     "web"},
    {"mcp-yfinance",
     "yfinance (community)",
     "Free quotes / fundamentals via yfinance scrape.  No auth — community-maintained, "
     "verify the install command against the upstream README before enabling.",
     "uvx",
     R"(["yfinance-mcp"])",
     "{}",
     "finance"},
    {"mcp-financial-datasets",
     "financial-datasets",
     "US fundamentals + news + crypto.  Fill FINANCIAL_DATASETS_API_KEY "
     "(free tier at financialdatasets.ai).",
     "uvx",
     R"(["mcp-server-financial-datasets"])",
     R"({"FINANCIAL_DATASETS_API_KEY":""})",
     "finance"},
};

static Result<void> apply_v024(QSqlDatabase& db) {
    int inserted = 0;

    for (const auto& s : kSeeds) {
        QSqlQuery q(db);
        q.prepare("INSERT OR IGNORE INTO mcp_servers "
                  "(id, name, description, command, args, env, category, "
                  " enabled, auto_start, status) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, 0, 0, 'stopped')");
        q.bindValue(0, QString::fromUtf8(s.id));
        q.bindValue(1, QString::fromUtf8(s.name));
        q.bindValue(2, QString::fromUtf8(s.description));
        q.bindValue(3, QString::fromUtf8(s.command));
        q.bindValue(4, QString::fromUtf8(s.args_json));
        q.bindValue(5, QString::fromUtf8(s.env_json));
        q.bindValue(6, QString::fromUtf8(s.category));

        if (!q.exec()) {
            return Result<void>::err(q.lastError().text().toStdString());
        }
        // numRowsAffected() returns 0 when INSERT OR IGNORE skipped a
        // duplicate; we only count actual inserts.
        if (q.numRowsAffected() > 0)
            ++inserted;
    }

    LOG_INFO(kTag, QString("Seeded %1 marketplace MCP server(s) (the rest already present)")
                       .arg(inserted));
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v024() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({24, "mcp_marketplace_seed", apply_v024});
}

} // namespace fincept

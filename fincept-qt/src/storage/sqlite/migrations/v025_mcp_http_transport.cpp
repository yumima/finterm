// v025_mcp_http_transport — Add HTTP-transport columns to mcp_servers.
//
// Track 4 #14a: McpHttpClient (sibling of the stdio McpClient) needs
// a base URL and an auth scheme.  Three new columns:
//
//   transport_type  TEXT NOT NULL DEFAULT 'stdio'
//     'stdio'   subprocess JSON-RPC (existing McpClient)
//     'http'    streamable HTTP JSON-RPC (McpHttpClient, Track 4 #14a)
//
//   base_url        TEXT
//     Base endpoint for HTTP transport.  NULL for stdio.
//
//   auth_scheme     TEXT NOT NULL DEFAULT 'none'
//     'none'    no auth header
//     'bearer'  Authorization: Bearer <value>
//     'api_key' <auth_header>: <value>          (custom header name)
//     'oauth'   reserved for Track 4 #14b — McpHttpClient rejects
//               this scheme until OAuth+DCR lands
//
//   auth_header     TEXT
//     For auth_scheme='api_key', the header name (default X-API-Key
//     when blank).  Ignored for other schemes.
//
// The auth *value* (token / key) is NOT stored in this table — it
// lives in SecureStorage under id 'mcp.auth.<server_id>' (same
// pattern as LlmSecureKeys uses for LLM keys).  Migrating an existing
// stdio row to HTTP requires writing the token to SecureStorage
// separately; the migration here only adds the columns.
//
// Idempotent: duplicate-column errors on re-run are swallowed.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration025";

static Result<void> add_column_if_missing(QSqlDatabase& db, const char* ddl) {
    QSqlQuery q(db);
    if (q.exec(ddl))
        return Result<void>::ok();
    const std::string err = q.lastError().text().toStdString();
    if (err.find("duplicate column") != std::string::npos ||
        err.find("already exists") != std::string::npos) {
        return Result<void>::ok();
    }
    return Result<void>::err(err);
}

static Result<void> apply_v025(QSqlDatabase& db) {
    // mcp_servers exists from v003; a fresh DB passes through there
    // before reaching v025 so the table is guaranteed to exist.
    QSqlQuery exists(db);
    if (!exists.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='mcp_servers'"))
        return Result<void>::err(exists.lastError().text().toStdString());
    if (!exists.next()) {
        LOG_WARN(kTag, "mcp_servers table missing — skipping v025");
        return Result<void>::ok();
    }

    if (auto r = add_column_if_missing(
            db, "ALTER TABLE mcp_servers ADD COLUMN transport_type TEXT NOT NULL DEFAULT 'stdio'");
        r.is_err())
        return r;
    if (auto r = add_column_if_missing(db, "ALTER TABLE mcp_servers ADD COLUMN base_url TEXT");
        r.is_err())
        return r;
    if (auto r = add_column_if_missing(
            db, "ALTER TABLE mcp_servers ADD COLUMN auth_scheme TEXT NOT NULL DEFAULT 'none'");
        r.is_err())
        return r;
    if (auto r = add_column_if_missing(db, "ALTER TABLE mcp_servers ADD COLUMN auth_header TEXT");
        r.is_err())
        return r;

    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v025() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({25, "mcp_http_transport", apply_v025});
}

} // namespace fincept

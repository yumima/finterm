// v033_mcp_oauth — OAuth 2.0 + DCR columns on mcp_servers (Track 4 #14b).
//
// Builds on v025's transport_type / base_url / auth_scheme columns.
// auth_scheme='oauth' previously errored at startup; now it routes to
// McpOAuth helper which handles client_credentials grant out of the
// box.  Authorization-code (browser-redirect) grant is the next layer
// up — needs a callback server + browser launch.
//
// New columns (all NULL on stdio + non-oauth rows):
//
//   auth_token_url   TEXT   — OAuth token endpoint (e.g. https://
//                              auth.example.com/oauth/token).  For
//                              servers that support discovery, the
//                              helper resolves it from
//                              <issuer>/.well-known/oauth-authorization-server.
//   auth_scope       TEXT   — Space-separated scopes (OAuth-style).
//   oauth_grant_type TEXT   — 'client_credentials' | 'authorization_code'.
//                              Defaults to client_credentials when
//                              unset (the m2m path we ship today).
//
// Secrets (client_id, client_secret, access_token, refresh_token,
// expires_at) live in SecureStorage under:
//   mcp.oauth.<server_id>.client_id
//   mcp.oauth.<server_id>.client_secret
//   mcp.oauth.<server_id>.access_token
//   mcp.oauth.<server_id>.refresh_token
//   mcp.oauth.<server_id>.expires_at_unix
//
// Dynamic Client Registration (DCR): when client_id is absent and the
// server advertises a registration endpoint, McpOAuth POSTs to it
// (RFC 7591) and caches the returned client_id + client_secret in
// SecureStorage.  Pre-registered clients work too — set the
// SecureStorage keys directly and skip DCR.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration033";

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

static Result<void> apply_v033(QSqlDatabase& db) {
    QSqlQuery exists(db);
    if (!exists.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='mcp_servers'"))
        return Result<void>::err(exists.lastError().text().toStdString());
    if (!exists.next()) {
        LOG_WARN(kTag, "mcp_servers table missing — skipping v033");
        return Result<void>::ok();
    }

    if (auto r = add_column_if_missing(
            db, "ALTER TABLE mcp_servers ADD COLUMN auth_token_url TEXT");
        r.is_err())
        return r;
    if (auto r = add_column_if_missing(
            db, "ALTER TABLE mcp_servers ADD COLUMN auth_scope TEXT");
        r.is_err())
        return r;
    if (auto r = add_column_if_missing(
            db, "ALTER TABLE mcp_servers ADD COLUMN oauth_grant_type TEXT");
        r.is_err())
        return r;

    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v033() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({33, "mcp_oauth", apply_v033});
}

} // namespace fincept

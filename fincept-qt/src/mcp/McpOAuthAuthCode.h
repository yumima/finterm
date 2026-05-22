#pragma once
// McpOAuthAuthCode.h — OAuth 2.0 authorization_code grant with PKCE.
//
// Handles the user-redirect flow for hosted MCP servers (Gmail-style,
// GitHub-style) that can't use machine-to-machine client_credentials.
//
// Flow:
//
//   1. Generate PKCE challenge (RFC 7636) + state.
//   2. Start a localhost TCP listener on a stable high port.
//   3. Build the auth URL with response_type=code, redirect_uri pointing
//      at the listener, code_challenge, code_challenge_method=S256.
//   4. QDesktopServices::openUrl launches the user's browser.
//   5. Block the calling thread on a QEventLoop until either:
//      - the callback fires with code+state, OR
//      - the 5-minute timeout elapses.
//   6. Validate state, swap code → access_token + refresh_token via
//      the token endpoint.
//   7. Cache tokens in SecureStorage (mcp.oauth.<server_id>.*).
//
// Subsequent calls use the refresh_token to refresh the access_token
// without re-prompting the user.
//
// Header is split from McpOAuth.h to keep McpOAuth itself small and
// independent of QtNetwork's TCP server machinery.

#include "core/result/Result.h"
#include "mcp/McpOAuth.h"

#include <QString>

namespace fincept::mcp {

struct AuthCodeConfig {
    QString server_id;                ///< Used to namespace SecureStorage keys.
    QString authorization_url;        ///< OAuth authorization endpoint.
    QString token_url;                ///< OAuth token endpoint.
    QString registration_url;         ///< Optional RFC 7591 DCR endpoint.
    QString scope;                    ///< Space-separated scopes.
    int callback_port = 47823;        ///< Localhost port for the redirect URI.
    int browser_wait_seconds = 300;   ///< How long to wait for the callback.
};

class McpOAuthAuthCode {
  public:
    /// Run the full authorization_code flow.  Blocks the calling
    /// thread (must NOT be the UI thread) until tokens are obtained
    /// or the timeout fires.  Stores access_token + refresh_token +
    /// expires_at_unix in SecureStorage; subsequent calls to
    /// `McpOAuth::ensure_access_token` find the cached pair and
    /// refresh transparently.
    ///
    /// Errors surface verbatim — no silent fallback.
    static Result<QString> run_flow(const AuthCodeConfig& cfg);

    /// Refresh an access_token using a stored refresh_token.  Used by
    /// `McpOAuth::ensure_access_token` on the cache-stale path when the
    /// configured grant_type is "authorization_code".
    static Result<QString> refresh(const AuthCodeConfig& cfg);
};

} // namespace fincept::mcp

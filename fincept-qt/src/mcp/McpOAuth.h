#pragma once
// McpOAuth.h — OAuth 2.0 client for McpHttpClient (Track 4 #14b).
//
// Synchronous helper.  McpHttpClient calls `ensure_access_token` before
// every request when auth_scheme=='oauth'; the helper caches the token
// + expiry in SecureStorage so a freshly-attached server doesn't hit
// the token endpoint on every call.
//
// Grants supported today:
//   - client_credentials  (machine-to-machine; what we ship)
//
// Grants deferred:
//   - authorization_code  (user-redirect flow; needs a callback
//                          server + browser launch — covered in
//                          plans/ai-stack.md §10)
//
// Dynamic Client Registration (DCR, RFC 7591): when the SecureStorage
// `client_id` slot is empty and the config supplies a registration
// endpoint, ensure_access_token POSTs registration metadata first,
// stores the returned credentials, then proceeds with the token
// exchange.  Pre-registered clients skip DCR — just write the
// `client_id` (and `client_secret` if confidential) into SecureStorage
// before the first request.

#include "core/result/Result.h"

#include <QString>
#include <QStringList>

namespace fincept::mcp {

struct OAuthConfig {
    QString server_id;              ///< Used to namespace SecureStorage keys.
    QString token_url;              ///< OAuth token endpoint (required).
    QString authorization_url;      ///< Required for authorization_code grant.
    QString registration_url;       ///< Optional RFC 7591 dynamic registration endpoint.
    QString scope;                  ///< Space-separated scopes (optional).
    QString grant_type;             ///< "client_credentials" | "authorization_code".
    int callback_port = 0;          ///< Localhost port for authorization_code redirect URI (0 = default 47823).
};

class McpOAuth {
  public:
    /// Returns a usable Bearer token.  Reads the cached access_token
    /// from SecureStorage when still valid (expires_at_unix > now +
    /// 30s skew).  Otherwise runs:
    ///   - DCR if client_id absent + registration_url present
    ///   - token request per grant_type
    ///   - cache the response
    /// Errors surface verbatim — no silent fallback to another auth
    /// scheme.
    static Result<QString> ensure_access_token(const OAuthConfig& cfg);

    /// Invalidate the cached token — called after a 401 so the next
    /// ensure_access_token() does a fresh token request.
    static void invalidate_cache(const QString& server_id);

    /// SecureStorage key generators (exposed for tests + administrators
    /// who want to set the values out of band).
    static QString secure_key_client_id(const QString& server_id);
    static QString secure_key_client_secret(const QString& server_id);
    static QString secure_key_access_token(const QString& server_id);
    static QString secure_key_refresh_token(const QString& server_id);
    static QString secure_key_expires_at(const QString& server_id);
};

} // namespace fincept::mcp

#pragma once

#include <QString>

namespace fincept {

/// Resolves the local AI engine (hearth) base URL that chat + embeddings share.
///
/// hearth runs out-of-process as a local OpenAI-compatible service (it is NOT
/// linked like Qt). finterm depends on it like a local daemon: it routes local
/// chat + embeddings to one base and degrades with a clear banner when the
/// engine is down (no silent failover).
///
/// Phase 1 is a deterministic, I/O-free resolver — finterm uses hearth's
/// loopback port by default (zero config), overridable via env or Settings:
///   1. env `FINCEPT_ENGINE_BASE_URL`  (explicit override — remote/custom)
///   2. hearth's default `http://127.0.0.1:11435/v1`
///
/// It does NOT probe the network (so it never blocks a UI thread or a held
/// lock). Active discovery — GET /admin/version for a status chip + a
/// contract-version floor — is a follow-up; the engine already exposes
/// `/admin/version` and a `Server: hearth/<v>` header for it.
class HearthService {
  public:
    static HearthService& instance();

    struct Resolution {
        QString base_url;  // ".../v1" base for chat + embeddings
        bool is_hearth;    // base is hearth's (use role aliases like primary_chat)
    };

    /// Cached/deterministic resolution. Pure — no network, no locks needed.
    Resolution resolve() const;

    /// Convenience: just the base URL.
    QString engine_base_url() const { return resolve().base_url; }

    static constexpr const char* kDefaultBase = "http://127.0.0.1:11435/v1";

  private:
    HearthService() = default;
};

} // namespace fincept

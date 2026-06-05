#pragma once

#include <QMutex>
#include <QObject>
#include <QString>

namespace fincept {

/// Resolves + (optionally) discovers the local AI engine (hearth) that chat +
/// embeddings share.
///
/// hearth runs out-of-process as a local OpenAI-compatible service (it is NOT
/// linked like Qt). finterm depends on it like a local daemon: route local
/// chat + embeddings to one base, and degrade with a clear banner when it's
/// down (no silent failover).
///
/// `resolve()` is the deterministic, **I/O-free** base resolver used on the hot
/// path (never blocks a UI thread or a held lock):
///   1. env `FINCEPT_ENGINE_BASE_URL`  (explicit override — remote/custom)
///   2. hearth's default `http://127.0.0.1:11435` (bare root, no /v1)
///
/// `detect()` is a separate, **async** discovery overlay used for UI status +
/// a contract version-floor. It probes `/admin/version` on a worker thread and
/// emits `detected()` on completion. It does NOT influence `resolve()`.
class HearthService : public QObject {
    Q_OBJECT

  public:
    static HearthService& instance();

    struct Resolution {
        // Root URL without /v1 (e.g. "http://127.0.0.1:11435").
        // LlmService appends /v1/chat/completions; EmbeddingsClient appends /v1/embeddings.
        QString base_url;
        bool is_hearth;    // base is hearth's (use role aliases like primary_chat)
    };
    /// Deterministic + I/O-free. Pure resolution; safe under any lock.
    Resolution resolve() const;
    QString engine_base_url() const { return resolve().base_url; }

    struct Status {
        bool probed = false;    // a detect() has completed at least once
        bool present = false;   // the engine answered
        bool is_hearth = false; // it identified as hearth
        QString version;        // engine build (when hearth)
        QString contract;       // consumer contract (when hearth)
        QString base_url;       // the base that was probed
    };
    /// Kick off a non-blocking probe of the resolved base's /admin/version.
    /// Emits detected() on the GUI thread when done. Coalesces concurrent calls.
    void detect();
    Status status() const;

    // Root URL without /v1 — consumers that need /v1 append it themselves.
    // LlmService appends /v1/chat/completions; EmbeddingsClient appends /v1/embeddings.
    static constexpr const char* kDefaultBase = "http://127.0.0.1:11435";
    static constexpr int kKnownContractMajor = 0;

  signals:
    void detected();

  private:
    explicit HearthService(QObject* parent = nullptr) : QObject(parent) {}
    void apply_status(const Status& s); // store + emit detected() (GUI thread)

    mutable QMutex mutex_;
    Status status_;
    bool detecting_ = false;
};

} // namespace fincept

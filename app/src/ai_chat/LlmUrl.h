#pragma once
// LlmUrl.h — one place for OpenAI-compatible base-URL normalization.
//
// The convention across finterm: a STORED base URL is the bare root (e.g.
// "http://127.0.0.1:11435"); each consumer appends its own path
// ("/v1/chat/completions", "/v1/models", "/v1/embeddings", "/admin/version").
//
// Users (and the minimax default, and env overrides, and old DB rows) routinely
// type or store a base WITH a trailing "/v1" or "/". Without normalization that
// doubles into ".../v1/v1/..." → 404. normalize_llm_base() strips trailing
// slashes and any repeated trailing "/v1" so appending is always safe — the
// single chokepoint that kills the whole double-/v1 bug class.

#include <QHostAddress>
#include <QString>
#include <QUrl>

namespace finterm {

inline QString normalize_llm_base(QString base) {
    base = base.trimmed();
    bool changed = true;
    while (changed) {
        changed = false;
        while (base.endsWith(QLatin1Char('/'))) {
            base.chop(1);
            changed = true;
        }
        if (base.endsWith(QLatin1String("/v1"))) {
            base.chop(3);
            changed = true;
        }
    }
    return base;
}

/// Is the URL on this computer?
inline bool is_loopback_url(const QString& url) {
    const QString host = QUrl(url).host().toLower();
    return host == QLatin1String("localhost") || host == QLatin1String("::1") ||
           (host.startsWith(QLatin1String("127.")) && QHostAddress(host).isLoopback());
}

/// How long a chat request may take. An engine on this computer answers one
/// request at a time, in order, for every program that uses it: one that waits
/// behind another program's long answer is not stuck (measured 2026-11-24: 85 s
/// behind a 1500-token one). A dead engine refuses the connection at once, so
/// the longer limit costs nothing there.
inline constexpr int kChatTimeoutMs = 120000;
inline constexpr int kLocalChatTimeoutMs = 300000;
inline int chat_timeout_ms(const QString& url) {
    return is_loopback_url(url) ? kLocalChatTimeoutMs : kChatTimeoutMs;
}

} // namespace finterm

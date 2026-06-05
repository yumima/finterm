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

#include <QString>

namespace fincept {

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

} // namespace fincept

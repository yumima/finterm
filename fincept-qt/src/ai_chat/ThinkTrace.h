// src/ai_chat/ThinkTrace.h
#pragma once
// Removing a local reasoning model's <think>…</think> traces from text that
// reaches the user. One definition: the rule used to live file-local in
// EquityAiTab (prose_only) and was re-implemented verbatim for the FinGPT
// tab — two copies of "also drop a dangling open tag" is how the next trace
// delimiter gets handled on one AI surface and leaks on the other.

#include <QRegularExpression>
#include <QString>

namespace fincept::ai_chat {

/// Strip closed <think>…</think> blocks AND a dangling unclosed <think>
/// (a truncated stream ends mid-trace; the raw deliberation must never
/// reach the shown or persisted text).
inline QString strip_think_traces(QString text) {
    static const QRegularExpression closed(QStringLiteral("<think>.*?</think>"),
                                           QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression dangling(QStringLiteral("<think>.*"),
                                             QRegularExpression::DotMatchesEverythingOption);
    text.remove(closed);
    text.remove(dangling);
    return text;
}

} // namespace fincept::ai_chat

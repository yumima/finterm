#pragma once
// Symbol-search failures, said the same way by every search box. A failed
// search says so — never "No results", which reads as "no such ticker" — and
// says "try again" only when a retry can help (SymbolSearchReply::transient,
// from the one rule the worker uses for every reply:
// PythonWorker::reply_is_transient).
#include <QString>

namespace finterm::ui {

inline QString search_failed_text(bool transient = true) {
    return transient ? QStringLiteral("Search failed — no answer. Try again.") : QStringLiteral("Search failed.");
}

} // namespace finterm::ui

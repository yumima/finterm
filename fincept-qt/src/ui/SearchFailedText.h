#pragma once
// Symbol-search replies, read the same way by every search box. A failed
// search says so — never "No results", which reads as "no such ticker" — and
// says "try again" only when a retry can help — the one rule the worker uses
// for every reply (PythonWorker::reply_is_transient): the daemon's body says
// whether Yahoo refused; no body (timeout, no daemon yet) is worth a retry.
#include "python/PythonWorker.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace fincept::ui {

inline QString search_failed_text(bool transient = true) {
    return transient ? QStringLiteral("Search failed — no answer. Try again.")
                     : QStringLiteral("Search failed.");
}

struct SearchReply {
    QJsonArray results;
    QString    failure;   // empty when the search answered (even with no matches)
};

/// The daemon's "search" reply: results under "_value" (flat array) or
/// "results"; a failure is ok=false.
inline SearchReply read_search_reply(bool ok, const QJsonObject& result) {
    SearchReply r;
    if (result.contains(QStringLiteral("_value")) && result.value(QStringLiteral("_value")).isArray())
        r.results = result.value(QStringLiteral("_value")).toArray();
    else if (result.contains(QStringLiteral("results")))
        r.results = result.value(QStringLiteral("results")).toArray();
    if (!ok)
        r.failure = search_failed_text(python::PythonWorker::reply_is_transient(ok, result));
    return r;
}

} // namespace fincept::ui

#include "core/config/BuildInfo.h"

#include <QLocale>
#include <QTimeZone>

// Defined for this file alone, so a new commit recompiles one file.
#ifndef FINTERM_BUILD_COMMIT
#    define FINTERM_BUILD_COMMIT ""
#endif
#ifndef FINTERM_BUILD_TIME
#    define FINTERM_BUILD_TIME 0 // seconds since 1970, UTC
#endif

namespace finterm::build_info {

QString commit() {
    return QStringLiteral(FINTERM_BUILD_COMMIT);
}

QDateTime commit_time() {
    constexpr qint64 t = FINTERM_BUILD_TIME;
    // EVENT-STAMP: when the commit was made
    return t > 0 ? QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC) : QDateTime();
}

QString label() {
    return label(commit(), commit_time());
}

QString label(const QString& commit, const QDateTime& time) {
    if (commit.isEmpty())
        return {};
    return QStringLiteral("%1 · %2 UTC")
        .arg(commit.left(7), QLocale::c().toString(time.toUTC(), QStringLiteral("d MMM yyyy HH:mm")));
}

} // namespace finterm::build_info

#pragma once

#include <QDateTime>
#include <QString>

/// What this binary was built from. Set for release builds only (CMake,
/// FINTERM_RELEASE_BUILD): a version can be released again from a newer
/// commit, and then the version alone does not say which build this is.
namespace finterm::build_info {

/// The commit, in full; empty for a development build.
QString commit();
/// When that commit was made (UTC); invalid for a development build.
QDateTime commit_time();
/// The same for people: "0232794 · 24 Nov 2026 16:12 UTC"; empty like commit().
QString label();
QString label(const QString& commit, const QDateTime& time);

} // namespace finterm::build_info

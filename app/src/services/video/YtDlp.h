// src/services/video/YtDlp.h
#pragma once
#include "core/config/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace finterm::services::video {

/// The yt-dlp to run: a copy next to the binary (a build may put one there),
/// else the one finterm's own Python environment installs (requirements-
/// numpy2.txt — what a release relies on), else one on PATH; empty if none
/// exists. Shared by the dashboard video player and the earnings-call audio
/// player.
inline QString ytdlp_program() {
#ifdef _WIN32
    const QString exe_name = "yt-dlp.exe";
#else
    const QString exe_name = "yt-dlp";
#endif
    const QString exe_dir = QCoreApplication::applicationDirPath();
    for (const QString& candidate : {QDir(exe_dir).filePath(exe_name), QDir(exe_dir).filePath("tools/" + exe_name),
                                     QDir(exe_dir).filePath("bin/" + exe_name),
                                     QDir(exe_dir).filePath("../tools/" + exe_name)}) {
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
#ifdef _WIN32
    const QString in_venv = finterm::AppPaths::root() + "/venv-numpy2/Scripts/yt-dlp.exe";
#else
    const QString in_venv = finterm::AppPaths::root() + "/venv-numpy2/bin/yt-dlp";
#endif
    if (QFileInfo::exists(in_venv))
        return QDir::cleanPath(in_venv);
    return QStandardPaths::findExecutable("yt-dlp");
}

/// The arguments for resolving `url` to a direct stream URL (`-g`) with the
/// format chain `format`. One place for the flags both players depend on:
/// --js-runtimes node (yt-dlp 2025+ needs a JS runtime for YouTube).
inline QStringList ytdlp_resolve_args(const QString& format, const QString& url) {
    return {"-f", format, "--no-playlist", "--quiet", "--js-runtimes", "node", "-g", url};
}

/// "Update yt-dlp" is worth saying: the failure looks like an outdated
/// yt-dlp (its own "is older than" notice, or an extractor that no longer
/// understands the site), and the yt-dlp in use is finterm's own — the one
/// Settings › Python Env › Upgrade updates. Not for answers an update can't
/// change (not live yet, private, removed).
inline bool ytdlp_update_may_help(const QByteArray& stderr_bytes) {
    if (!ytdlp_program().startsWith(finterm::AppPaths::root()))
        return false;
    const QString s = QString::fromUtf8(stderr_bytes);
    for (const char* sign : {"is older than", "Unable to extract", "nsig", "n challenge", "Signature extraction",
                             "player response", "Unsupported URL"})
        if (s.contains(QLatin1String(sign), Qt::CaseInsensitive))
            return true;
    return false;
}

/// Where the update is, for the messages that show it.
inline QString ytdlp_update_hint() {
    return QStringLiteral("yt-dlp may be out of date: Settings › Python Env › Upgrade (yt-dlp)");
}

/// Answers that mean the stream isn't on yet (or no more), not a failure.
inline bool ytdlp_not_live(const QString& err) {
    for (const char* sign : {"will begin in", "not currently live", "is offline", "not live", "Premieres in",
                             "live event will begin"})
        if (err.contains(QLatin1String(sign), Qt::CaseInsensitive))
            return true;
    return false;
}

/// The line(s) of yt-dlp's stderr that say why it failed. Its stderr opens
/// with boilerplate — a version-age nag, an update hint, a JS-runtime notice —
/// and the actual ERROR is underneath; warnings only as a fallback.
inline QString ytdlp_error(const QByteArray& stderr_bytes) {
    const QStringList lines = QString::fromUtf8(stderr_bytes).split(QLatin1Char('\n'));
    QStringList errors, warnings;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1String("ERROR"), Qt::CaseInsensitive)) {
            errors << line;
        } else if (line.startsWith(QLatin1String("WARNING"), Qt::CaseInsensitive)) {
            if (!line.contains(QLatin1String("is older than"), Qt::CaseInsensitive) &&
                !line.contains(QLatin1String("To suppress this warning"), Qt::CaseInsensitive))
                warnings << line;
        } else if (errors.isEmpty() && warnings.isEmpty()) {
            warnings << line; // continuation of an unlabelled traceback / usage message
        }
    }
    const QStringList& pick = !errors.isEmpty() ? errors : warnings;
    return pick.isEmpty() ? QString() : pick.join(QLatin1String(" · "));
}

} // namespace finterm::services::video

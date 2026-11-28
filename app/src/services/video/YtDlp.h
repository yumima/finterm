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

/// The yt-dlp finterm's Python environment installs (requirements-numpy2.txt)
/// — the one Settings › Python Env upgrades.
inline QString ytdlp_venv_program() {
#ifdef _WIN32
    return finterm::AppPaths::root() + "/venv-numpy2/Scripts/yt-dlp.exe";
#else
    return finterm::AppPaths::root() + "/venv-numpy2/bin/yt-dlp";
#endif
}

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
    const QString in_venv = ytdlp_venv_program();
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

/// "If YouTube changed, update yt-dlp" is worth adding to `err` (yt-dlp's
/// ERROR text): the yt-dlp in use is finterm's own (the one Settings ›
/// Python Env upgrades), and the error is not a definite answer about the
/// stream that no update changes (not live, private, removed, no such page).
/// No guessing at which errors an update fixes: YouTube breaks yt-dlp in
/// too many ways.
inline bool ytdlp_hint_applies(const QString& err) {
    if (QDir::cleanPath(ytdlp_program()) != QDir::cleanPath(ytdlp_venv_program()))
        return false;
    for (const char* answer : {"will begin in", "not currently live", "is offline", "not live", "Premieres in",
                               "Private video", "Video unavailable", "has been removed", "This video is not available",
                               "members-only", "Unsupported URL", "HTTP Error 404"})
        if (err.contains(QLatin1String(answer), Qt::CaseInsensitive))
            return false;
    return true;
}

/// Where the update is, for the messages that show it.
inline QString ytdlp_update_hint() {
    return QStringLiteral("If YouTube changed, updating yt-dlp helps: Settings › Python Env › Upgrade (yt-dlp).");
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

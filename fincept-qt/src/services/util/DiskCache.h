// src/services/util/DiskCache.h
//
// Tiny helper for the persistent on-disk JSON cache shared across services.
//
// Pattern (used by every data-fetching service in this codebase):
//   1. On construct, the service hydrates from `<AppData>/cache/<svc>/*.json`
//      via DiskCache::load(...) so the UI can paint immediately even if the
//      network is down or the user just launched the app.
//   2. After a successful Python/HTTP fetch, the raw response gets written
//      back via DiskCache::save(...). Next launch picks it up.
//
// Files are written atomically (temp + rename) so a crash mid-write can't
// leave a half-truncated JSON that fails to parse on next launch.
#pragma once

#include "core/logging/Logger.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace fincept::services::util {

class DiskCache {
public:
    // service_id becomes the subdirectory under <AppData>/cache/. Use short
    // lowercase identifiers ("pre_ipo", "portfolio", "markets", etc.). The
    // directory is created lazily on first save.
    explicit DiskCache(QString service_id) : service_id_(std::move(service_id)) {}

    QString dir() const {
        const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        const QString d = root + QStringLiteral("/cache/") + service_id_;
        QDir().mkpath(d);
        // Clamp dir perms to 0700 so cached payload filenames (which can
        // leak account/ticker context) aren't world-traversable. Cheap
        // and idempotent — no-op if already 0700.
        QFile::setPermissions(d, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return d;
    }

    QString path(const QString& filename) const { return dir() + QLatin1Char('/') + filename; }

    // Reads filename and returns the parsed JSON document, or an empty
    // document on any failure (missing file, unreadable, malformed). Callers
    // typically test the return with isObject() / isArray().
    QJsonDocument load(const QString& filename) const {
        QFile f(path(filename));
        if (!f.open(QIODevice::ReadOnly)) return {};
        const QByteArray bytes = f.readAll();
        f.close();
        return QJsonDocument::fromJson(bytes);
    }

    // Atomic save via QSaveFile, which does the temp+rename dance correctly
    // on every platform (rename(2) on POSIX, ReplaceFile on Windows) and
    // skips the destination-remove window where QFile::remove + QFile::rename
    // could leave a hole if a crash landed between the two calls.
    //
    // File permissions are clamped to 0600 (owner read/write only) so any
    // sensitive payload — portfolio holdings, account-linked tickers — is
    // not world-readable on multi-user systems.
    bool save(const QString& filename, const QJsonDocument& doc) const {
        const QString final_path = path(filename);
        QSaveFile f(final_path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            LOG_WARN(service_id_.toUtf8().constData(),
                     QStringLiteral("DiskCache: failed to open ") + final_path);
            return false;
        }
        const QByteArray bytes = doc.toJson(QJsonDocument::Compact);
        if (f.write(bytes) != bytes.size()) {
            f.cancelWriting();
            LOG_WARN(service_id_.toUtf8().constData(),
                     QStringLiteral("DiskCache: short write for ") + final_path);
            return false;
        }
        // Clamp perms on the temp file BEFORE commit() does the rename so
        // the chmod survives the rename atomically — a setPermissions
        // after commit() leaves a brief window where the renamed file
        // carries umask-default perms (typically 0644) and is readable by
        // other users on a multi-user box.
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (!f.commit()) {
            LOG_WARN(service_id_.toUtf8().constData(),
                     QStringLiteral("DiskCache: commit failed for ") + final_path);
            return false;
        }
        return true;
    }

    bool exists(const QString& filename) const { return QFile::exists(path(filename)); }

    void remove(const QString& filename) const { QFile::remove(path(filename)); }

    // Lists every cached file under the service directory (top-level only,
    // no recursion). Used by services that key by symbol / series id and
    // need to enumerate at startup. Sorted by modification time, newest
    // first (matches QDir::Time semantics) so the first N entries are the
    // most recently touched.
    QStringList files() const {
        QDir d(dir());
        return d.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
    }

    // LRU trim: keep at most `keep_most_recent` files in the service's
    // cache dir, sorted by mtime. Older files are unlinked. No-op if the
    // dir holds <= keep_most_recent. Returns the number of files removed.
    //
    // Per-key services (equity_research by ticker, economics by query
    // hash, geopolitics per-country) call this once at startup so users
    // who've browsed thousands of distinct entries over months don't end
    // up with a gigabyte of cached JSON. The cap is intentionally generous
    // (call sites typically pass 200-500): cold-start hydration cost is
    // proportional to file count, not file size, and 200 small JSON
    // entries hydrate in ~50ms.
    int trim_to(int keep_most_recent) const {
        if (keep_most_recent < 0) return 0;
        QDir d(dir());
        const QStringList by_newest = d.entryList(
            QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
        if (by_newest.size() <= keep_most_recent) return 0;
        int removed = 0;
        for (int i = keep_most_recent; i < by_newest.size(); ++i) {
            if (QFile::remove(d.filePath(by_newest[i]))) ++removed;
        }
        if (removed > 0) {
            LOG_INFO(service_id_.toUtf8().constData(),
                     QStringLiteral("DiskCache: trimmed %1 old files").arg(removed));
        }
        return removed;
    }

private:
    QString service_id_;
};

} // namespace fincept::services::util

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
        if (!f.commit()) {
            LOG_WARN(service_id_.toUtf8().constData(),
                     QStringLiteral("DiskCache: commit failed for ") + final_path);
            return false;
        }
        QFile::setPermissions(final_path, QFile::ReadOwner | QFile::WriteOwner);
        return true;
    }

    bool exists(const QString& filename) const { return QFile::exists(path(filename)); }

    void remove(const QString& filename) const { QFile::remove(path(filename)); }

    // Lists every cached file under the service directory (top-level only,
    // no recursion). Used by services that key by symbol / series id and
    // need to enumerate at startup.
    QStringList files() const {
        QDir d(dir());
        return d.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
    }

private:
    QString service_id_;
};

} // namespace fincept::services::util

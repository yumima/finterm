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
#include <QJsonDocument>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

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

    // Atomic save: write to a sibling `.tmp` file, then rename over the
    // destination. Avoids the half-truncated-on-crash failure mode where a
    // partial JSON kills the next-launch hydration path.
    bool save(const QString& filename, const QJsonDocument& doc) const {
        const QString final_path = path(filename);
        const QString tmp_path   = final_path + QStringLiteral(".tmp");
        QFile f(tmp_path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            LOG_WARN(service_id_.toUtf8().constData(),
                     QStringLiteral("DiskCache: failed to open ") + tmp_path);
            return false;
        }
        const QByteArray bytes = doc.toJson(QJsonDocument::Compact);
        const qint64 written = f.write(bytes);
        f.close();
        if (written != bytes.size()) {
            QFile::remove(tmp_path);
            LOG_WARN(service_id_.toUtf8().constData(),
                     QStringLiteral("DiskCache: short write for ") + tmp_path);
            return false;
        }
        // QFile::rename refuses to overwrite — explicit remove first.
        QFile::remove(final_path);
        if (!QFile::rename(tmp_path, final_path)) {
            LOG_WARN(service_id_.toUtf8().constData(),
                     QStringLiteral("DiskCache: rename failed for ") + final_path);
            return false;
        }
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

#include "services/screener/ScreenerService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "services/util/DiskCache.h"

#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

namespace finterm::services {

namespace {
constexpr const char* kTag = "Screener";
constexpr int kMetaTimeoutMs = 20'000;
constexpr int kScanTimeoutMs = 10 * 60 * 1000; // a cold price store downloads the universe

util::DiskCache scans() {
    return util::DiskCache(QStringLiteral("screener"));
}

/// The script's JSON answer, or why there is none.
ScreenerService::Reply read_reply(const python::PythonResult& res, const QString& failed_text) {
    ScreenerService::Reply r;
    if (!res.success) {
        r.error = res.error.isEmpty() ? failed_text : res.error;
        return r;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(python::extract_json(res.output).toUtf8());
    if (!doc.isObject()) {
        r.error = QStringLiteral("unreadable scan output");
        return r;
    }
    r.error = doc.object().value(QStringLiteral("error")).toString();
    if (r.error.isEmpty()) {
        r.ok = true;
        r.root = doc.object();
    }
    return r;
}
} // namespace

ScreenerService& ScreenerService::instance() {
    static ScreenerService s;
    return s;
}

QString ScreenerService::scan_file(double min_mcap) {
    return QStringLiteral("scan_v5_%1.json").arg(qint64(min_mcap)); // v5: setting up for every screen
}

QJsonDocument ScreenerService::latest_scan(double min_mcap) const {
    return scans().load(scan_file(min_mcap));
}

void ScreenerService::prune_old_scans() {
    // Scans of an earlier format (scan_v1…v4) are never read again. The
    // directory also holds the price store's files, which are not scans and
    // are left alone.
    static const QRegularExpression kScan(QStringLiteral("^scan_v(\\d+)_"));
    scans().prune([](const QString& name) {
        const auto m = kScan.match(name);
        return !m.hasMatch() || name.startsWith(QLatin1String("scan_v5_"));
    });
}

void ScreenerService::fetch_meta(QObject* ctx, std::function<void(const Reply&)> done) {
    QPointer<QObject> guard = ctx;
    python::PythonRunner::instance().run_interactive(
        QStringLiteral("screener_data.py"), {QStringLiteral("meta")},
        [guard, done = std::move(done)](python::PythonResult res) {
            const Reply r = read_reply(res, QStringLiteral("screener meta failed"));
            // Queued: PythonRunner answers a missing script inside the call.
            if (guard)
                QTimer::singleShot(0, guard.data(), [done, r]() { done(r); });
        },
        kMetaTimeoutMs);
}

int ScreenerService::run_scan(double min_mcap, QObject* ctx, Progress progress,
                              std::function<void(const Reply&)> done) {
    const int gen = ++gen_;
    const QString file = scan_file(min_mcap);
    const QString payload =
        QString::fromUtf8(QJsonDocument(QJsonObject{{"min_mcap", min_mcap}}).toJson(QJsonDocument::Compact));
    QPointer<QObject> guard = ctx;
    QPointer<ScreenerService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("screener_data.py"), {QStringLiteral("scan"), payload},
        [self, guard, gen, file, done = std::move(done)](python::PythonResult res) {
            if (!self)
                return;
            const Reply r = read_reply(res, QStringLiteral("screener scan failed"));
            if (!r.ok) {
                LOG_WARN(kTag, "scan failed: " + r.error.left(200));
            } else if (gen >= self->saved_gen_.value(file, -1)) {
                // A superseded scan's result is still its universe's latest
                // unless a newer scan of it saved already.
                self->saved_gen_.insert(file, gen);
                scans().save(file, QJsonDocument(r.root));
            }
            if (guard)
                QTimer::singleShot(0, guard.data(), [done, r]() { done(r); });
        },
        [guard, progress = std::move(progress)](QString line, bool is_stderr) {
            if (!guard || !progress || !is_stderr || !line.startsWith(QLatin1String("PROGRESS ")))
                return;
            // "PROGRESS <done> <total> <text>"
            progress(line.section(QLatin1Char(' '), 1, 1).toInt(), line.section(QLatin1Char(' '), 2, 2).toInt(),
                     line.section(QLatin1Char(' '), 3));
        },
        kScanTimeoutMs);
    return gen;
}

} // namespace finterm::services

#include "storage/sweep/RetiredStorage.h"

#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"

#include <QDir>
#include <QtConcurrent>
#include <QSettings>

namespace finterm::storage {

namespace {

// Cache directories no code reads or writes any more.
struct RetiredDir {
    const char* name;
    const char* why;
};
constexpr RetiredDir kRetiredDirs[] = {
    {"relationship_map", "per-ticker JSON copy of the relmap: cache entry (cache.db is the one store)"},
    {"futures", "pre-Databento futures quotes (Databento writes cache/databento)"},
    {"futures_cot", "COT positioning is held in memory by FuturesService"},
    {"power_trader", "the Power Trader cache has no reader"},
};

// Settings groups nothing reads.
constexpr const char* kRetiredSettingsGroups[] = {
#ifndef Q_OS_WIN
    // SessionManager's copy of ADS's "Perspectives" group (ADS reads its own
    // now). Not on Windows: the registry backend is case-insensitive, so
    // "perspectives" IS "Perspectives" there — the live layouts — and there
    // was never a second copy to remove.
    "perspectives",
#endif
    "power_trader", // no reader
};

} // namespace

void sweep_retired_storage() {
    // Files off the GUI thread (a first run removes hundreds of them).
    (void)QtConcurrent::run([] {
        const QDir cache(AppPaths::cache());
        for (const auto& d : kRetiredDirs) {
            QDir dir(cache.filePath(QString::fromLatin1(d.name)));
            if (dir.exists() && dir.removeRecursively())
                LOG_INFO("RetiredStorage", QString("removed cache/%1 — %2").arg(d.name, d.why));
        }
    });
    // Settings on the GUI thread (QSettings is not shared across threads).
    QSettings settings;
    bool changed = false;
    for (const char* g : kRetiredSettingsGroups) {
        if (settings.childGroups().contains(QString::fromLatin1(g), Qt::CaseSensitive)) {
            settings.remove(QString::fromLatin1(g));
            changed = true;
            LOG_INFO("RetiredStorage", QString("removed settings group [%1]").arg(g));
        }
    }
    if (changed)
        settings.sync();
}

} // namespace finterm::storage

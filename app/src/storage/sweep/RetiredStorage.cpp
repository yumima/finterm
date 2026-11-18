#include "storage/sweep/RetiredStorage.h"

#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "storage/repositories/SettingsRepository.h"

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
    {"economics", "per-query JSON copy of EconomicsService's cache.db entries (dispatch records now in cache.db)"},
    {"equity_research", "per-symbol JSON copy of Equity Research's cache.db entries (cache.db is the one store)"},
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

// Single QSettings keys nothing reads.
constexpr const char* kRetiredSettingsKeys[] = {
    "window_count", // superseded by the window id list SessionManager restores
    "news/variant", // the News variant persists in the screen's saved state
};

// Rows of the settings table (SettingsRepository) nothing reads.
constexpr const char* kRetiredDbSettings[] = {
    "inline_completion.enabled",          // the ghost-text completion it toggled never existed
    "workspace.active_dashboard_profile", // the dashboard has no profiles; workspaces restore its layout
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
    for (const char* k : kRetiredSettingsKeys) {
        if (settings.contains(QString::fromLatin1(k))) {
            settings.remove(QString::fromLatin1(k));
            changed = true;
            LOG_INFO("RetiredStorage", QString("removed setting %1").arg(k));
        }
    }
    if (changed)
        settings.sync();
    for (const char* k : kRetiredDbSettings) {
        auto& repo = SettingsRepository::instance();
        const auto r = repo.get(QString::fromLatin1(k));
        if (r.is_ok() && !r.value().isEmpty() && repo.remove(QString::fromLatin1(k)).is_ok())
            LOG_INFO("RetiredStorage", QString("removed settings row %1").arg(k));
    }
}

} // namespace finterm::storage

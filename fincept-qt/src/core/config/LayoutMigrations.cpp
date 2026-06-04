// src/core/config/LayoutMigrations.cpp
#include "core/config/LayoutMigrations.h"

#include "core/config/AppIdentity.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace fincept {

namespace {

// Stored in the main QSettings store under this key. Absent (== 0) on a fresh
// install or a pre-versioning build, so the first run of a versioning build
// walks every step (they're all idempotent, so re-running a step a returning
// user already got is a silent no-op).
constexpr const char* kVersionKey = "meta/layout_version";

// stdout, not Logger: this runs before the logger is initialised, and we want
// the line in the launching terminal ahead of the GUI. Flush so ordering holds.
void narrate(const QString& msg) {
    std::fprintf(stdout, "[migrate] %s\n", qUtf8Printable(msg));
    std::fflush(stdout);
}

// ── Step v1: settings-store rebrand ──────────────────────────────────────────
// The QSettings *application* name moved FinceptTerminal → finterm. The
// organization ("Fincept") and the bundle-id data root (com.fincept.terminal)
// are frozen, so only the .conf basename changed. Copy the legacy store into
// the new one once — idempotent (skips when the new store already has keys) and
// never clobbering. Covers both the normal and the -Secure (credentials) store.
void migrate_settings_store(const char* new_app, const char* old_app) {
    QSettings dst(AppIdentity::kOrg, new_app);
    if (!dst.allKeys().isEmpty())
        return;  // new store already populated — never overwrite
    QSettings src(AppIdentity::kOrg, old_app);
    const QStringList keys = src.allKeys();
    if (keys.isEmpty())
        return;  // no legacy store to import (e.g. a fresh install)
    for (const QString& k : keys)
        dst.setValue(k, src.value(k));
    dst.sync();
    narrate(QString("settings: imported %1 keys from legacy \"%2\" store")
                .arg(keys.size())
                .arg(QString::fromLatin1(old_app)));
}

void step_v1_settings_rebrand() {
    migrate_settings_store(AppIdentity::kApp,       AppIdentity::kAppLegacy);
    migrate_settings_store(AppIdentity::kAppSecure, AppIdentity::kAppSecureLegacy);
}

// ── Step v2: orphaned legacy cache directory ─────────────────────────────────
// The per-app QStandardPaths data dir (AppLocalDataLocation = <Org>/<App>) moved
// from <Org>/FinceptTerminal to <Org>/finterm. Everything under it is pure,
// re-derivable cache (market data, pre-IPO/SEC caches, disk caches), so the
// correct action is to DELETE the orphaned legacy directory — copying it forward
// would only be re-fetched. We locate it as the sibling of the current dir named
// after the legacy app, so the path is still derived from AppIdentity, never
// hardcoded.
void step_v2_orphan_cache_cleanup() {
    const QString cur =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (cur.isEmpty())
        return;

    const QString cur_leaf = QDir(cur).dirName();  // == AppIdentity::kApp
    const QString legacy_leaf = QString::fromLatin1(AppIdentity::kAppLegacy);
    if (cur_leaf == legacy_leaf)
        return;  // names coincide (shouldn't happen) — nothing safe to do
    // Resolve the parent by string, NOT QDir::cdUp(): cdUp() returns false when
    // the parent doesn't exist yet, which would silently leave us looking in the
    // wrong place. absolutePath() is pure string manipulation.
    const QString parent = QFileInfo(cur).absolutePath();
    if (parent.isEmpty())
        return;
    const QString legacy_path = parent + "/" + legacy_leaf;

    QDir legacy(legacy_path);
    if (!legacy.exists())
        return;  // no orphan — the overwhelmingly common case
    // Path-aliasing safety: never delete the directory we're actively using.
    if (legacy.canonicalPath() == QDir(cur).canonicalPath())
        return;

    if (legacy.removeRecursively())
        narrate(QString("cache: removed orphaned legacy data dir %1").arg(legacy_path));
    else
        narrate(QString("cache: could not remove legacy dir %1 (left in place)")
                    .arg(legacy_path));
}

} // namespace

void LayoutMigrations::run() {
    QSettings s(AppIdentity::kOrg, AppIdentity::kApp);
    const int stored = s.value(QString::fromLatin1(kVersionKey), 0).toInt();
    if (stored >= kCurrentVersion)
        return;  // fast path: up to date — one read, no work, no output

    if (stored < 1) step_v1_settings_rebrand();
    if (stored < 2) step_v2_orphan_cache_cleanup();
    // Future moves: bump kCurrentVersion and add `if (stored < N) step_vN();`.

    s.setValue(QString::fromLatin1(kVersionKey), kCurrentVersion);
    s.sync();
}

} // namespace fincept

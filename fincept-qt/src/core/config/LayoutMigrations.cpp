// src/core/config/LayoutMigrations.cpp
#include "core/config/LayoutMigrations.h"

#include "core/config/AppIdentity.h"
#include "core/config/AppPaths.h"
#include "core/config/ProfileManager.h"
#include "storage/sqlite/SqliteFiles.h"
#include "python/PythonRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
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

// ── Step v3: personal data under user/, every feature's data in one home ─────
// Features wrote to many places: the profile root, <Org>/<App> (Qt's
// AppLocalDataLocation), ~/.fincept, <GenericData>/fincept-dev and
// fincept-terminal, the scripts tree and /tmp. Now, per profile:
//   user/{data,files,workspaces,features,agents}   — personal, and only that
//   cache/ market_data/ models/ logs/ …             — the app's, regenerable
// Rules, because this moves the user's real data:
//   * a profile's data/files/workspaces move with ONE directory rename each
//     (atomic: moved or not). AppPaths resolves each of the three on its own
//     (old folder while it still holds the files), so a failed or partial
//     move never hides data or opens an empty account;
//   * everything else merges per FILE — never a whole-folder rename that
//     strands one copy when two legacy folders map to one place;
//   * never overwrite: a name already taken goes to imported/conflicts/;
//   * a SQLite file moves with its -wal/-shm/-journal (companions first,
//     main last, rolled back on failure) or not at all;
//   * only files finterm itself wrote are moved, never a whole foreign folder;
//   * QFile::rename falls back to copy+remove across filesystems (/tmp);
//   * a failed move leaves v3 un-stamped to retry — at most three launches.

enum class V3Result { Done, Failed, Deferred };

struct V3 {
    bool failed = false;

    static QString unique_group(const QString& path) {
        if (sqlite_files::unit_free(path))
            return path;
        for (int i = 1;; ++i) {
            const QString p = path + "." + QString::number(i);
            if (sqlite_files::unit_free(p))
                return p;
        }
    }

    static bool is_companion(const QString& name) {
        return name.endsWith("-wal") || name.endsWith("-shm") || name.endsWith("-journal");
    }

    // A file with its SQLite companions, as one unit: companions first, the
    // main file last, everything put back if any rename fails. A taken
    // destination sends the whole unit to the conflicts folder instead.
    void move_group(const QString& src, const QString& dst, const QString& conflicts) {
        if (!QFileInfo(src).isFile())
            return;
        const bool taken = !sqlite_files::unit_free(dst);
        const QString target = taken ? unique_group(conflicts + "/" + QFileInfo(src).fileName()) : dst;
        QDir().mkpath(QFileInfo(target).absolutePath());
        bool split = false;
        if (!sqlite_files::move_unit(src, target, &split)) {
            narrate(split ? QString("data: WARNING — %1 is split between %1 and %2 (a companion file could not "
                                    "be put back); move the remaining parts together by hand").arg(src, target)
                          : QString("data: could not move %1 → %2 (left in place, will retry)").arg(src, target));
            failed = true;
            return;
        }
        narrate(taken ? QString("data: %1 kept as %2 (%3 already exists)").arg(src, target, dst)
                      : QString("data: moved %1 → %2").arg(src, target));
    }

    void merge_dir(const QString& src, const QString& dst, const QString& conflicts) {
        QDir dir(src);
        if (!dir.exists() || QDir(src).canonicalPath() == QDir(dst).canonicalPath())
            return;
        const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo& fi : entries) {
            const QString name = fi.fileName();
            if (fi.isDir() && !fi.isSymLink()) {
                merge_dir(fi.absoluteFilePath(), dst + "/" + name, conflicts + "/" + name);
                continue;
            }
            if (is_companion(name)) {
                const QString main = name.left(name.lastIndexOf('-'));
                if (QFileInfo::exists(src + "/" + main))
                    continue; // travels with its database
            }
            move_group(fi.absoluteFilePath(), dst + "/" + name, conflicts);
        }
        if (dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            QDir().rmdir(src);
    }

    // A profile's own folder: one atomic rename when the destination is new
    // (or an empty placeholder), else a per-file merge.
    void move_dir(const QString& src, const QString& dst, const QString& conflicts) {
        if (!QDir(src).exists())
            return;
        QDir d(dst);
        if (d.exists() && d.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            QDir().rmdir(dst);
        if (!QFileInfo::exists(dst)) {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (QDir().rename(src, dst)) {
                narrate(QString("data: moved %1 → %2").arg(src, dst));
                return;
            }
            narrate(QString("data: could not move %1 → %2 (left in place, will retry)").arg(src, dst));
            failed = true;
            return;
        }
        merge_dir(src, dst, conflicts);
    }
};

int profile_names_and_roots(QStringList& roots) {
    auto& profiles = ProfileManager::instance();
    QStringList names = profiles.list_profiles();
    for (const QString& n : {QStringLiteral("default"), profiles.active()})
        if (!names.contains(n))
            names.append(n);
    // Folders on disk too: a profile missing from the manifest must not keep
    // its data in the old place forever.
    for (const QString& n : QDir(AppPaths::root() + "/profiles").entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!names.contains(n))
            names.append(n);
    for (const QString& n : names)
        roots.append(profiles.profile_root_for(n));
    return roots.size();
}

V3Result step_v3_user_data_home() {
    V3 m;
    const QString root = AppPaths::root();
    const QString home = QDir::homePath();

    // 1. Each profile's personal folders, whole, under its user/. A profile
    //    whose instance is running right now (it holds its database lock) is
    //    left alone: renaming folders under a live SQLite connection can split
    //    its journal from its database. It is retried on a later launch.
    QStringList proots;
    profile_names_and_roots(proots);
    bool any_live = false;
    for (const QString& proot : proots) {
        // The lock lives in whichever data/ the instance runs from.
        bool live_here = false;
        for (const QString& d : {proot + "/data", proot + "/user/data"}) {
            QLockFile live(d + "/fincept.db.lock");
            // Match main.cpp: a held lock is never "stale" by age — Qt's
            // default 30 s would declare a long-running instance dead and
            // break its lock.
            live.setStaleLockTime(0);
            if (QFileInfo::exists(d) && !live.tryLock(0))
                live_here = true;
            else
                live.unlock();
        }
        // (A same-profile instance starting between this probe and the move
        // below is the one remaining window: the lock file lives inside the
        // folder being renamed, so it cannot be held across the rename.)
        if (live_here) {
            narrate(QString("data: profile at %1 is in use — its data moves on a later launch").arg(proot));
            any_live = true;
            continue;
        }
        const QString user = proot + "/user";
        const bool before = m.failed;
        m.failed = false;
        for (const char* sub : {"data", "files", "workspaces"})
            m.move_dir(proot + "/" + sub, user + "/" + sub, user + "/imported/conflicts/" + sub);
        m.failed = m.failed || before;
    }
    // Everything below moves stores every profile shared into the default
    // profile's user/. It runs NOW, whatever step 1 did: this launch's scripts
    // already read the new locations, and a deferred move would let them
    // create fresh, empty stores there, pushing the real ones into conflicts
    // later. These were never per-profile; they go to the default profile,
    // where the user's own data lives, and other profiles start them fresh.
    // Targets inside files/ follow the default profile's resolved layout.
    const bool default_files_moved =
        !QDir(root + "/files").exists() ||
        QDir(root + "/files").isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    const QString files_dir = default_files_moved ? root + "/user/files" : root + "/files";

    // Machine-wide legacy stores were shared by every profile; they go to the
    // default profile's user/ (system data to root-level folders).
    const QString user = root + "/user";
    const QString conflicts = user + "/imported/conflicts";
    const QString share = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString roaming = qEnvironmentVariable("APPDATA"); // Windows only

    // 2. Qt's per-app folder <Org>/<App>: its cache/ is the live DiskCache
    //    store (last-known-good payloads, paid Databento pulls) — merged, not
    //    dropped. Not swept wholesale: main.cpp's legacy AppDataLocation
    //    migration still reads its own files from this folder.
    const QString applocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!applocal.isEmpty() && QDir(applocal).canonicalPath() != QDir(root).canonicalPath()) {
        m.merge_dir(applocal + "/cache", root + "/cache", root + "/cache/.imported");
        m.move_group(applocal + "/pre_ipo_valuation_seed.json", files_dir + "/pre_ipo/valuation_overrides.json",
                     conflicts);
        m.move_group(applocal + "/managers_13f.json", user + "/imported/app-local/managers_13f.json", conflicts);
    }
    m.move_group(root + "/pre_ipo_valuation_seed.json", files_dir + "/pre_ipo/valuation_overrides.json", conflicts);
    // Every profile used to read these shared overrides; each keeps a copy.
    for (const QString& proot : proots) {
        const QString dst = proot + "/user/files/pre_ipo/valuation_overrides.json";
        if (proot != root && QFileInfo::exists(files_dir + "/pre_ipo/valuation_overrides.json") &&
            !QFileInfo::exists(dst)) {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            QFile::copy(files_dir + "/pre_ipo/valuation_overrides.json", dst);
        }
    }

    // 3. Public-data indexes are the app's, not the user's.
    for (const char* db : {"form4.sqlite", "form4_history.sqlite", "form13f.sqlite", "short_interest.sqlite",
                           "ownership_watch.sqlite"})
        m.move_group(root + "/" + db, root + "/market_data/" + db, root + "/market_data/imported");

    // 4. ~/.fincept (only finterm wrote here).
    const QString dot = home + "/.fincept";
    m.move_group(dot + "/eval_runs.jsonl", user + "/agents/eval_runs.jsonl", conflicts);
    m.merge_dir(dot + "/alpha_arena", user + "/features/alpha_arena", conflicts + "/alpha_arena");
    for (const char* mdl : {"advanced_models", "meta_learning", "online_learning"})
        m.merge_dir(dot + "/" + mdl, root + "/models/qlib/" + mdl, root + "/models/imported/" + mdl);
    // The user's own retraining schedules and configs are personal settings.
    m.move_group(dot + "/rolling_schedules.json", user + "/features/qlib_rolling/rolling_schedules.json",
                 conflicts);
    m.merge_dir(dot + "/configs", user + "/features/qlib_rolling/configs", conflicts + "/qlib_rolling");
    m.merge_dir(dot + "/vision_quant", root + "/models/vision_quant", root + "/models/imported/vision_quant");
    QDir().rmdir(dot); // only finterm's known items moved; anything else stays

    // 5. Per-feature folders scripts made: only the files finterm wrote.
    for (const QString& base : {share, roaming}) {
        if (base.isEmpty())
            continue;
        m.move_group(base + "/fincept-dev/ma_deals.db", user + "/features/ma/ma_deals.db", conflicts);
        m.merge_dir(base + "/fincept-dev/alpha_arena", user + "/features/alpha_arena", conflicts + "/alpha_arena");
        m.move_group(base + "/fincept-terminal/agents.db", user + "/agents/agents.db", conflicts);
        m.merge_dir(base + "/fincept-terminal/rentech", user + "/features/renaissance/rentech",
                    conflicts + "/rentech");
        for (const char* d : {"/fincept-dev", "/fincept-terminal"})
            QDir().rmdir(base + d); // only if now empty
    }
    m.move_group(home + "/fincept-terminal/agents.db", user + "/agents/agents.db", conflicts);
    QDir().rmdir(home + "/fincept-terminal");
    m.move_group(root + "/ma_deals.db", user + "/features/ma/ma_deals.db", conflicts);
    m.merge_dir(root + "/alpha_arena", user + "/features/alpha_arena", conflicts + "/alpha_arena");
    m.merge_dir(root + "/finagent", user + "/agents/finagent", conflicts + "/finagent");
    m.merge_dir(root + "/vision_quant", root + "/models/vision_quant", root + "/models/imported/vision_quant");
    QDir().rmdir(home + "/.oecd_cache"); // finterm's was always empty; a non-empty one is not ours

    // 6. The agent task store sat in the scripts tree — two copies, since
    //    Python and C++ disagreed on the name. Python's (the one the agents
    //    wrote) wins; the other is kept under conflicts, never dropped.
    const QString scripts = python::PythonRunner::find_scripts_dir();
    m.move_group(scripts + "/agent_tasks.db", user + "/agents/agent_tasks.db", conflicts);
    m.move_group(scripts + "/agents/agent_tasks.db", user + "/agents/agent_tasks.db", conflicts);

    // 7. The report draft sat in /tmp — only if it is this account's file.
    const QFileInfo draft(QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                          "/fincept_report_autosave.fincept");
    if (draft.exists() && draft.isWritable() && draft.ownerId() == QFileInfo(home).ownerId())
        m.move_group(draft.absoluteFilePath(), files_dir + "/reports/autosave.fincept", conflicts);

    if (m.failed)
        return V3Result::Failed;
    // A profile whose instance was running keeps its old folders (AppPaths
    // resolves them) and moves on a later quiet launch — not an attempt.
    return any_live ? V3Result::Deferred : V3Result::Done;
}

} // namespace

void LayoutMigrations::run() {
    QSettings s(AppIdentity::kOrg, AppIdentity::kApp);
    const int stored = s.value(QString::fromLatin1(kVersionKey), 0).toInt();
    if (stored >= kCurrentVersion)
        return;  // fast path: up to date — one read, no work, no output

    if (stored < 1) step_v1_settings_rebrand();
    if (stored < 2) step_v2_orphan_cache_cleanup();
    if (stored < 3) {
        const V3Result r = step_v3_user_data_home();
        if (r == V3Result::Deferred) {
            // Another instance is running: not an attempt, just not yet.
            s.setValue(QString::fromLatin1(kVersionKey), 2);
            s.sync();
            return;
        }
        if (r == V3Result::Failed) {
            // Left in place (the old layout stays in use for whatever did not
            // move). Retry — but a file that can never move must not re-sweep
            // every start.
            const QString tries_key = QStringLiteral("meta/layout_v3_attempts");
            const int tries = s.value(tries_key, 0).toInt() + 1;
            s.setValue(tries_key, tries);
            if (tries < 3) {
                s.setValue(QString::fromLatin1(kVersionKey), 2);
                s.sync();
                return;
            }
            narrate("data: some items could not be moved after 3 attempts; they were left in place");
        }
    }
    // Future moves: bump kCurrentVersion and add `if (stored < N) step_vN();`.

    s.setValue(QString::fromLatin1(kVersionKey), kCurrentVersion);
    s.sync();
}

} // namespace fincept

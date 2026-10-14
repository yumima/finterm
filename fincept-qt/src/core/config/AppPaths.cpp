#include "core/config/AppPaths.h"

#include "core/config/AppIdentity.h"
#include "core/config/ProfileManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstdio>

namespace fincept {

QString AppPaths::root() {
#ifdef _WIN32
    // Use GenericDataLocation which returns %LOCALAPPDATA% directly on Windows,
    // avoiding the fragile double-cdUp() from AppLocalDataLocation.
    // GenericDataLocation = %LOCALAPPDATA% on Windows (Qt docs: QStandardPaths).
    const QString local_app_data =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return local_app_data + "/" + AppIdentity::kBundleId;
#elif defined(__APPLE__)
    return QDir::homePath() + "/Library/Application Support/" + AppIdentity::kBundleId;
#else
    return QDir::homePath() + "/.local/share/" + AppIdentity::kBundleId;
#endif
}

QString AppPaths::user() {
    return ProfileManager::instance().profile_root() + "/user";
}

namespace {
// data/, files/ and workspaces/ each resolve on their own: the new
// user/<sub> once LayoutMigrations v3 has moved it, the old <profile>/<sub>
// while that still holds the user's things — so a move that only half
// completed can never hide the half that did not move. For data/ the test is
// the account database itself (auth.db): a stray file in user/data must not
// switch the app onto an empty account while the real one sits in data/.
QString pick_layout(const char* sub) {
    const QString proot = ProfileManager::instance().profile_root();
    const QString old_path = proot + "/" + sub;
    const QString new_path = proot + "/user/" + sub;
    if (qstrcmp(sub, "data") == 0) {
        const bool old_has = QFileInfo::exists(old_path + "/auth.db");
        const bool new_has = QFileInfo::exists(new_path + "/auth.db");
        return (old_has && !new_has) ? old_path : new_path;
    }
    const auto entries = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden;
    const QDir old_dir(old_path), new_dir(new_path);
    if (old_dir.exists() && !old_dir.isEmpty(entries) && (!new_dir.exists() || new_dir.isEmpty(entries)))
        return old_path;
    return new_path;
}
} // namespace

QString AppPaths::data() {
    return pick_layout("data");
}

QString AppPaths::features() {
    return user() + "/features";
}

QString AppPaths::agents() {
    return user() + "/agents";
}

QString AppPaths::market_data() {
    return root() + "/market_data";
}

QString AppPaths::logs() {
    return ProfileManager::instance().profile_root() + "/logs";
}

QString AppPaths::files() {
    return pick_layout("files");
}

QString AppPaths::cache() {
    return ProfileManager::instance().profile_root() + "/cache";
}

QString AppPaths::models() {
    return root() + "/models";
}

QString AppPaths::runtime() {
    return root() + "/runtime";
}

QString AppPaths::workspaces() {
    return pick_layout("workspaces");
}

QString AppPaths::crashdumps() {
    return ProfileManager::instance().profile_root() + "/crashdumps";
}

void AppPaths::ensure_all() {
    // Use fprintf to stderr (not qWarning) — main.cpp installs a Qt message
    // handler that routes qWarning into Logger, but Logger's file isn't open
    // yet when ensure_all() runs during startup. Raw stderr is the safe channel.
    const auto try_mkpath = [](const QString& p) {
        if (p.isEmpty()) {
            fprintf(stderr, "[AppPaths] empty path — skipping mkpath\n");
            return;
        }
        if (QDir().mkpath(p))
            return;
        const QFileInfo info(p);
        fprintf(stderr,
                "[AppPaths] mkpath failed: %s exists=%d isDir=%d writable=%d\n",
                qUtf8Printable(p), info.exists(), info.isDir(), info.isWritable());
    };
    try_mkpath(root());
    try_mkpath(data());
    try_mkpath(logs());
    try_mkpath(files());
    try_mkpath(cache());
    try_mkpath(models());
    try_mkpath(runtime());
    try_mkpath(workspaces());
    try_mkpath(features());
    try_mkpath(agents());
    try_mkpath(market_data());
    try_mkpath(crashdumps());

    // A plain map at the root, so the split is obvious from a file browser.
    // Generic text only — nothing about the user goes in it.
    QFile readme(root() + "/README.txt");
    if (!readme.exists() && readme.open(QIODevice::WriteOnly | QIODevice::Text)) {
        readme.write("finterm data folder\n"
                     "\n"
                     "user/         YOUR data - databases, files, workspaces, feature and agent data.\n"
                     "              Back up or erase this folder to keep or remove everything personal.\n"
                     "              (A non-default profile has its own: profiles/<name>/user/)\n"
                     "\n"
                     "Everything else is the app's own and holds nothing personal:\n"
                     "cache/ (safe to delete), market_data/ (public SEC/FINRA indexes),\n"
                     "models/, logs/, crashdumps/, runtime/, python/, venv-*/, uv/.\n"
                     "\n"
                     "API keys and passwords are not stored here: they are in the OS keyring.\n");
    }
}

} // namespace fincept

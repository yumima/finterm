#include "core/setup/SetupPaths.h"

#include "core/config/AppPaths.h"
#include "core/config/BuildInfo.h"
#include "core/config/ProfileManager.h"
#include "core/config/StartFresh.h"

#include <QCoreApplication>

namespace finterm::setup_paths {

data_upgrade::Plan current_plan() {
    data_upgrade::Plan p;
    p.profile_root = ProfileManager::instance().profile_root();
    p.data_dir = AppPaths::data();
    p.backup_root = start_fresh::backup_root();
    p.profile = ProfileManager::instance().active();
    p.app_version = QCoreApplication::applicationVersion();
    // As update_policy::Release::id() names a build.
    const QString commit = build_info::commit();
    p.app_build = commit.isEmpty() ? p.app_version : p.app_version + QLatin1Char('+') + commit.left(12);
    p.known_version = data_upgrade::known_version();
    return p;
}

data_transfer::Where current_where() {
    const data_upgrade::Plan p = current_plan();
    data_transfer::Where w;
    w.profile_root = p.profile_root;
    w.data_dir = p.data_dir;
    w.backup_root = p.backup_root;
    w.profile = p.profile;
    w.app_version = p.app_version;
    w.known_version = p.known_version;
    return w;
}

} // namespace finterm::setup_paths

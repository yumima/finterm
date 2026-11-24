#pragma once
// EngineRelease.h — the hearth "Get hearth" installs.
//
// Its install script, by hash: what runs is the script that was looked at for
// this finterm, whatever the address serves by then. It carries the hashes of
// everything it downloads in turn. hearth updates itself afterwards, so this
// need not be the newest. Filled in when hearth is released (plans/hearth-install.md
// 6.1); empty, the button opens hearth's page instead.

#include <QString>

namespace finterm::engine {

struct InstallScript {
    QString url;
    QString sha256;
};

inline constexpr const char* kReleaseVersion = "";
inline constexpr const char* kInstallShSha256 = "";
inline constexpr const char* kInstallPs1Sha256 = "";

/// The script for this system, empty when there is none.
inline InstallScript install_script(const QString& version, const QString& sh, const QString& ps1, bool windows) {
    const QString sum = windows ? ps1 : sh;
    if (version.isEmpty() || sum.size() != 64)
        return {};
    return {QStringLiteral("https://github.com/yumima/hearth/releases/download/v%1/install.%2")
                .arg(version, windows ? QStringLiteral("ps1") : QStringLiteral("sh")),
            sum};
}

inline InstallScript install_script() {
#if defined(Q_OS_WIN)
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    return install_script(QString::fromLatin1(kReleaseVersion), QString::fromLatin1(kInstallShSha256),
                          QString::fromLatin1(kInstallPs1Sha256), windows);
}

} // namespace finterm::engine

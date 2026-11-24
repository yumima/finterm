#pragma once
// The environment for a program that is not part of this app and may outlive
// it: the relaunched app after an update, hearth.

#include <QLatin1Char>
#include <QProcessEnvironment>
#include <QStringList>

namespace finterm {

/// An AppImage's runtime puts its own mount (APPDIR …) into the environment —
/// libraries of ours to another program, and gone once this process exits.
/// Path lists (LD_LIBRARY_PATH, QT_PLUGIN_PATH, PATH, XDG_DATA_DIRS…) lose only
/// their entries under it; the user's own entries stay.
inline QProcessEnvironment without_app_mount(QProcessEnvironment env) {
    const QString appdir = env.value(QStringLiteral("APPDIR"));
    for (const QString& k : {QStringLiteral("APPDIR"), QStringLiteral("APPIMAGE"), QStringLiteral("ARGV0"),
                             QStringLiteral("OWD")})
        env.remove(k);
    if (appdir.isEmpty())
        return env;
    for (const QString& k : env.keys()) {
        const QString v = env.value(k);
        if (!v.contains(appdir))
            continue;
        QStringList kept;
        for (const QString& part : v.split(QLatin1Char(':')))
            if (!part.contains(appdir))
                kept << part;
        if (kept.isEmpty() || (kept.size() == 1 && kept.first().isEmpty()))
            env.remove(k);
        else
            env.insert(k, kept.join(QLatin1Char(':')));
    }
    return env;
}

inline QProcessEnvironment clean_environment() {
    return without_app_mount(QProcessEnvironment::systemEnvironment());
}

} // namespace finterm

#include "services/agents/SkillStore.h"

#include "core/config/AppPaths.h"
#include "python/PythonRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace finterm::services::skill_store {

namespace {

// A path within the tree, and nothing else.
bool is_within(const QString& rel) {
    return !rel.isEmpty() && QDir::isRelativePath(rel) && QDir::cleanPath(rel) == rel && rel != QLatin1String("..") &&
           !rel.startsWith(QLatin1String("../"));
}

} // namespace

QString shipped_root() {
    return QFileInfo(python::PythonRunner::find_scripts_dir() + QStringLiteral("/agents/finagent_core/skills"))
        .canonicalFilePath();
}

QString user_root() {
    return AppPaths::user() + QStringLiteral("/skills");
}

QString relative_to(const QString& root, const QString& path) {
    const QString base = QFileInfo(root).canonicalFilePath();
    const QString file = QFileInfo(path).canonicalFilePath();
    if (base.isEmpty() || !file.startsWith(base + QLatin1Char('/')))
        return {};
    return file.mid(base.size() + 1);
}

QString user_copy(const QString& rel) {
    return is_within(rel) ? user_root() + QLatin1Char('/') + rel : QString();
}

QString in_use(const QString& rel) {
    if (!is_within(rel))
        return {};
    const QString mine = user_copy(rel);
    return QFileInfo(mine).isFile() ? mine : shipped_root() + QLatin1Char('/') + rel;
}

bool save(const QString& rel, const QByteArray& content, QString* error) {
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    const QString path = user_copy(rel);
    if (path.isEmpty())
        return fail(QStringLiteral("not a skill of this installation: %1").arg(rel));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(QStringLiteral("could not create %1").arg(QFileInfo(path).absolutePath()));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size() || !f.commit())
        return fail(f.errorString());
    return true;
}

} // namespace finterm::services::skill_store

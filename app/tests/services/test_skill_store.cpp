// The user's copy of a skill: kept with their data, wins over the shipped one,
// which is never written.
#include "core/config/AppPaths.h"
#include "python/PythonRunner.h"
#include "services/agents/SkillStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace store = finterm::services::skill_store;

namespace {
QTemporaryDir& tmp() {
    static QTemporaryDir d;
    return d;
}
QByteArray read(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("(unreadable)");
}
const QString kRel = QStringLiteral("v/comps/SKILL.md");
} // namespace

// The two places, in the test's folder.
QString finterm::AppPaths::user() {
    return tmp().filePath(QStringLiteral("user"));
}
QString finterm::python::PythonRunner::find_scripts_dir() {
    return tmp().filePath(QStringLiteral("scripts"));
}

class TestSkillStore : public QObject {
    Q_OBJECT
  private slots:
    void init() {
        QDir(finterm::AppPaths::user()).removeRecursively();
        const QString dir =
            finterm::python::PythonRunner::find_scripts_dir() + QStringLiteral("/agents/finagent_core/skills/v/comps");
        QVERIFY(QDir().mkpath(dir));
        QFile f(dir + QStringLiteral("/SKILL.md"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("shipped");
    }

    void the_shipped_one_until_there_is_a_copy() { QCOMPARE(read(store::in_use(kRel)), QByteArray("shipped")); }

    void the_copy_wins_and_the_shipped_one_is_untouched() {
        QString error;
        QVERIFY2(store::save(kRel, "mine", &error), qPrintable(error));
        QCOMPARE(read(store::in_use(kRel)), QByteArray("mine"));
        QCOMPARE(read(store::shipped_root() + QLatin1Char('/') + kRel), QByteArray("shipped"));
        QVERIFY(store::user_copy(kRel).startsWith(finterm::AppPaths::user()));
    }

    // An update replaces the shipped tree: the copy is still the one in use.
    void the_copy_outlives_the_shipped_tree() {
        QVERIFY(store::save(kRel, "mine", nullptr));
        QFile f(store::shipped_root() + QLatin1Char('/') + kRel);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("shipped, next version");
        f.close();
        QCOMPARE(read(store::in_use(kRel)), QByteArray("mine"));
        QVERIFY(QFile::remove(store::user_copy(kRel)));
        QCOMPARE(read(store::in_use(kRel)), QByteArray("shipped, next version"));
    }

    void nothing_outside_the_tree() {
        const QStringList outside = {QString(), QStringLiteral("../x"), QStringLiteral("v/../../x"),
                                     QStringLiteral("/etc/x"), QStringLiteral("..")};
        for (const QString& rel : outside) {
            QString error;
            QVERIFY(!store::save(rel, "x", &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(store::in_use(rel).isEmpty());
        }
    }

    void relative_only_within_the_root() {
        const QString root = store::shipped_root();
        QCOMPARE(store::relative_to(root, root + QLatin1Char('/') + kRel), kRel);
        QVERIFY(store::relative_to(root, root + QStringLiteral("/nope")).isEmpty());
        QVERIFY(store::relative_to(root, QDir::tempPath()).isEmpty());
        QVERIFY(store::relative_to(QString(), QStringLiteral("/")).isEmpty());
#ifndef Q_OS_WIN
        const QString outside = tmp().filePath(QStringLiteral("outside.md"));
        QFile f(outside);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(QFile::link(outside, root + QStringLiteral("/v/link.md")));
        QVERIFY(store::relative_to(root, root + QStringLiteral("/v/link.md")).isEmpty());
#endif
    }
};

QTEST_GUILESS_MAIN(TestSkillStore)
#include "test_skill_store.moc"

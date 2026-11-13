// tests/storage/test_chat_sessions.cpp
//
// Pins chat_sessions.context_tag binding. v055 declared the column
// NOT NULL DEFAULT '', but SQLite applies a DEFAULT only when the column is
// omitted from the INSERT — create_session always names it, and a caller
// that took the `context_tag = {}` default bound a null QString, i.e. SQL
// NULL. Every untagged session (the floating chat bubble, the MCP
// create-session tool) failed with "NOT NULL constraint failed", and the
// bubble showed "Session error — please restart" forever.
//
// Runs against a real temp SQLite file with the actual migrations applied.

#include "storage/repositories/ChatRepository.h"
#include "storage/sqlite/Database.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using fincept::ChatRepository;
using fincept::Database;

class TestChatSessions : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void untagged_session_is_created_with_empty_tag();
    void tagged_session_is_found_by_its_tag();

  private:
    QTemporaryDir dir_;
};

void TestChatSessions::initTestCase() {
    QVERIFY(dir_.isValid());
    fincept::register_migration_v002();
    fincept::register_migration_v043();
    fincept::register_migration_v055();
    auto opened = Database::instance().open(dir_.filePath(QStringLiteral("test.db")));
    QVERIFY2(opened.is_ok(), opened.is_err() ? opened.error().c_str() : "");
}

void TestChatSessions::untagged_session_is_created_with_empty_tag() {
    // Exactly the bubble's call shape: no persona, no context tag.
    auto r = ChatRepository::instance().create_session(QStringLiteral("Quick Chat"), QStringLiteral("openai"),
                                                       QStringLiteral("gpt"));
    QVERIFY2(r.is_ok(), r.is_err() ? r.error().c_str() : "");
    QCOMPARE(r.value().context_tag, QString());

    // Stored as '' — not NULL — so it never matches a tagged lookup.
    QSqlQuery q(Database::instance().raw_db());
    QVERIFY(q.exec(QStringLiteral("SELECT context_tag IS NULL, context_tag = '' FROM chat_sessions WHERE id = '%1'")
                       .arg(r.value().id)));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    QCOMPARE(q.value(1).toInt(), 1);

    // Title-only call (the MCP tool's shape) also succeeds.
    QVERIFY(ChatRepository::instance().create_session(QStringLiteral("MCP")).is_ok());
}

void TestChatSessions::tagged_session_is_found_by_its_tag() {
    auto r = ChatRepository::instance().create_session(QStringLiteral("NVDA"), {}, {}, QStringLiteral("general"),
                                                       QStringLiteral("symbol:NVDA"));
    QVERIFY(r.is_ok());
    auto found = ChatRepository::instance().latest_session_for_context(QStringLiteral("symbol:NVDA"));
    QVERIFY(found.is_ok());
    QVERIFY(found.value().has_value());
    QCOMPARE(found.value()->id, r.value().id);
}

QTEST_GUILESS_MAIN(TestChatSessions)
#include "test_chat_sessions.moc"

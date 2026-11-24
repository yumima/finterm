// The user's answer about hearth, and what connecting and disconnecting do to
// the chat's provider — on a real database, with v059's own migration.

#include "storage/repositories/EngineLinkRepository.h"
#include "storage/sqlite/Database.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using finterm::Database;
using finterm::EngineLinkRepository;
using finterm::engine::Link;

class TestEngineLink : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void init();
    void a_new_user_is_asked();
    void who_used_hearth_keeps_it_data();
    void who_used_hearth_keeps_it();
    void managing_it_became_starting_it();
    void connecting_for_everything_but_chat();
    void connecting_for_chat_and_back();
    void connecting_with_no_provider_gives_the_chat_one();
    void a_configured_row_keeps_its_model_and_address();
    void the_old_row_name_is_hearth();
    void disconnecting_leaves_another_chat_alone();
    void the_provider_it_had_may_be_gone();
    void connecting_twice_still_remembers_the_first();
    void saying_no();
    void nothing_is_linked_without_a_database();
    void the_seeded_row_is_nobodys_choice();
    void one_row_chats_after_the_migration_data();
    void one_row_chats_after_the_migration();
    void two_active_rows_do_not_hide_the_choice();

  private:
    void sql(const char* s) {
        auto r = Database::instance().execute(QString::fromUtf8(s));
        QVERIFY2(r.is_ok(), r.is_err() ? r.error().c_str() : "");
    }
    QString active() { return repo().chat_provider(); }
    EngineLinkRepository& repo() { return EngineLinkRepository::instance(); }
    void migrate_again() {
        sql("DELETE FROM schema_version WHERE version = 59");
        const QString p = Database::instance().path();
        QVERIFY(Database::instance().reopen(p).is_ok());
    }
    QTemporaryDir dir_;
    int n_ = 0;
};

void TestEngineLink::initTestCase() {
    QVERIFY(dir_.isValid());
    finterm::register_migration_v001();
    finterm::register_migration_v002();
    finterm::register_migration_v010();
    finterm::register_migration_v059();
}

void TestEngineLink::init() {
    auto r = Database::instance().reopen(dir_.filePath(QStringLiteral("%1.db").arg(n_++)));
    QVERIFY2(r.is_ok(), r.is_err() ? r.error().c_str() : "");
    sql("DELETE FROM llm_configs"); // v002 seeds one
}

void TestEngineLink::a_new_user_is_asked() {
    QCOMPARE(repo().link(), Link::Unasked);
    QVERIFY(!repo().linked() && !repo().start_with_app());
}

void TestEngineLink::who_used_hearth_keeps_it_data() {
    QTest::addColumn<QString>("had");
    QTest::addColumn<bool>("connected");
    const char* anthropic = "INSERT INTO llm_configs (provider, model, is_active) VALUES ('anthropic','m',1);";
    QTest::newRow("chats with it") << "INSERT INTO llm_configs (provider, model, is_active) VALUES ('hearth','m',1);" << true;
    QTest::newRow("under its old name") << "INSERT INTO llm_configs (provider, model, is_active) VALUES ('Ollama','m',1);" << true;
    QTest::newRow("no provider at all") << "" << true;
    QTest::newRow("none active, hearth first") << "INSERT INTO llm_configs (provider, model, is_active) VALUES ('hearth','m',0);" << true;
    QTest::newRow("none active, another first")
        << "INSERT INTO llm_configs (provider, model, is_active) VALUES ('anthropic','m',0);"
           "INSERT INTO llm_configs (provider, model, is_active) VALUES ('hearth','m',0);"
        << false;
    QTest::newRow("chats with another") << anthropic << false;
    QTest::newRow("another, with hearth's row beside it")
        << QString(anthropic) + "INSERT INTO llm_configs (provider, model, is_active) VALUES ('hearth','m',0);" << false;
    QTest::newRow("another, and had hearth managed")
        << QString(anthropic) + "INSERT INTO settings (key, value) VALUES ('hearth.manage','true');" << true;
    QTest::newRow("another, and had it unmanaged")
        << QString(anthropic) + "INSERT INTO settings (key, value) VALUES ('hearth.manage','false');" << false;
    QTest::newRow("another, with embedded memory")
        << QString(anthropic) + "CREATE TABLE memory_entries (embedding BLOB); INSERT INTO memory_entries VALUES (x'00');" << true;
    QTest::newRow("another, with memory never embedded")
        << QString(anthropic) + "CREATE TABLE memory_entries (embedding BLOB); INSERT INTO memory_entries VALUES (NULL);" << false;
}

void TestEngineLink::who_used_hearth_keeps_it() {
    QFETCH(QString, had);
    QFETCH(bool, connected);
    sql("INSERT INTO settings (key, value) VALUES ('anything', 'the user did')");
    for (const QString& s : had.split(';', Qt::SkipEmptyParts))
        sql(s.toUtf8().constData());
    migrate_again();
    QCOMPARE(repo().link(), connected ? Link::Connected : Link::Unasked);
}

void TestEngineLink::managing_it_became_starting_it() {
    sql("INSERT INTO settings (key, value) VALUES ('hearth.manage', 'true')");
    migrate_again();
    QVERIFY(repo().linked() && repo().start_with_app());
    auto r = Database::instance().execute("SELECT COUNT(*) FROM settings WHERE key = 'hearth.manage'");
    QVERIFY(r.is_ok() && r.value().next());
    QCOMPARE(r.value().value(0).toInt(), 0);
    // an answer given since is not overwritten by running it again
    QVERIFY(repo().decline().is_ok());
    migrate_again();
    QCOMPARE(repo().link(), Link::Declined);
}

void TestEngineLink::connecting_for_everything_but_chat() {
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('anthropic','m',1)");
    QVERIFY(repo().connect(false).is_ok());
    QVERIFY(repo().linked());
    QCOMPARE(active(), QString("anthropic"));
    QVERIFY(repo().disconnect().is_ok());
    QCOMPARE(active(), QString("anthropic"));
}

void TestEngineLink::connecting_for_chat_and_back() {
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('anthropic','m',1)");
    QVERIFY(repo().connect(true).is_ok());
    QCOMPARE(active(), QString("hearth"));
    auto r = Database::instance().execute("SELECT COUNT(*) FROM llm_configs WHERE is_active = 1");
    QVERIFY(r.is_ok() && r.value().next());
    QCOMPARE(r.value().value(0).toInt(), 1);
    QVERIFY(repo().disconnect().is_ok());
    QCOMPARE(active(), QString("anthropic"));
    QCOMPARE(repo().link(), Link::Declined);
}

void TestEngineLink::connecting_with_no_provider_gives_the_chat_one() {
    QVERIFY(repo().connect(false).is_ok());
    QCOMPARE(active(), QString("hearth"));
    QVERIFY(repo().disconnect().is_ok());
    QCOMPARE(active(), QString());
}

void TestEngineLink::a_configured_row_keeps_its_model_and_address() {
    sql("INSERT INTO llm_configs (provider, base_url, model, is_active) VALUES ('hearth','http://box:11435','mine',0)");
    QVERIFY(repo().connect(true).is_ok());
    auto r = Database::instance().execute("SELECT base_url, model FROM llm_configs WHERE provider = 'hearth' AND is_active = 1");
    QVERIFY(r.is_ok() && r.value().next());
    QCOMPARE(r.value().value(0).toString(), QString("http://box:11435"));
    QCOMPARE(r.value().value(1).toString(), QString("mine"));
}

void TestEngineLink::the_old_row_name_is_hearth() {
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('ollama','m',0)");
    QVERIFY(repo().connect(true).is_ok());
    QCOMPARE(active(), QString("ollama"));
    auto r = Database::instance().execute("SELECT COUNT(*) FROM llm_configs");
    QVERIFY(r.is_ok() && r.value().next());
    QCOMPARE(r.value().value(0).toInt(), 1); // no second row beside it
    QVERIFY(repo().disconnect().is_ok());
    QCOMPARE(active(), QString());
}

void TestEngineLink::disconnecting_leaves_another_chat_alone() {
    QVERIFY(repo().connect(true).is_ok());
    sql("UPDATE llm_configs SET is_active = 0");
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('groq','m',1)"); // chosen since
    QVERIFY(repo().disconnect().is_ok());
    QCOMPARE(active(), QString("groq"));
}

void TestEngineLink::the_provider_it_had_may_be_gone() {
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('anthropic','m',1)");
    QVERIFY(repo().connect(true).is_ok());
    sql("DELETE FROM llm_configs WHERE provider = 'anthropic'");
    QVERIFY(repo().disconnect().is_ok());
    QCOMPARE(active(), QString()); // none, rather than hearth again
}

void TestEngineLink::connecting_twice_still_remembers_the_first() {
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('anthropic','m',1)");
    QVERIFY(repo().connect(true).is_ok());
    QVERIFY(repo().connect(true).is_ok());
    QVERIFY(repo().disconnect().is_ok());
    QCOMPARE(active(), QString("anthropic"));
}

void TestEngineLink::saying_no() {
    QVERIFY(repo().decline().is_ok());
    QCOMPARE(repo().link(), Link::Declined);
    QVERIFY(repo().set_start_with_app(true).is_ok());
    QVERIFY(repo().start_with_app() && !repo().linked());
    QVERIFY(repo().connect(false).is_ok()); // and may change their mind
    QVERIFY(repo().linked());
}

void TestEngineLink::nothing_is_linked_without_a_database() {
    QVERIFY(repo().connect(true).is_ok());
    Database::instance().close();
    QCOMPARE(repo().link(), Link::Unasked);
    QVERIFY(repo().connect(true).is_err() && repo().decline().is_err());
}

void TestEngineLink::the_seeded_row_is_nobodys_choice() {
    // As v002–v051 leave a new database: hearth's row, active.
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('hearth','primary_chat',1)");
    migrate_again();
    QCOMPARE(repo().link(), Link::Unasked);
    QCOMPARE(active(), QString());
    sql("UPDATE llm_configs SET is_active = 1"); // even were it active
    QCOMPARE(active(), QString());
    sql("UPDATE llm_configs SET is_active = 0");
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('anthropic','m',1)");
    QVERIFY(repo().connect(false).is_ok()); // the chat was set up elsewhere: it stays there
    QCOMPARE(active(), QString("anthropic"));
    QVERIFY(repo().decline().is_ok());
    QCOMPARE(active(), QString("anthropic"));
}

void TestEngineLink::one_row_chats_after_the_migration_data() {
    QTest::addColumn<QString>("rows"); // provider:active
    QTest::addColumn<QString>("chats");
    QTest::newRow("none active, one") << "anthropic:0" << "anthropic";
    QTest::newRow("none active, two: the first, as before") << "openai:0 groq:0" << "groq";
    QTest::newRow("two active: the first, as before") << "openai:1 groq:1" << "groq";
    QTest::newRow("one of three") << "anthropic:0 openai:1 groq:0" << "openai";
    QTest::newRow("hearth active beside another") << "hearth:1 openai:1" << "hearth";
    QTest::newRow("another active, hearth not") << "hearth:0 openai:1" << "openai";
    QTest::newRow("none") << "" << "";
}

void TestEngineLink::one_row_chats_after_the_migration() {
    QFETCH(QString, rows);
    QFETCH(QString, chats);
    sql("INSERT INTO settings (key, value) VALUES ('anything', 'the user did')");
    for (const QString& r : rows.split(' ', Qt::SkipEmptyParts))
        sql(QString("INSERT INTO llm_configs (provider, model, is_active) VALUES ('%1','m',%2)")
                .arg(r.section(':', 0, 0), r.section(':', 1))
                .toUtf8()
                .constData());
    migrate_again();
    QCOMPARE(active(), chats);
    auto n = Database::instance().execute("SELECT COUNT(*) FROM llm_configs WHERE is_active = 1");
    QVERIFY(n.is_ok() && n.value().next());
    QCOMPARE(n.value().value(0).toInt(), chats.isEmpty() ? 0 : 1);
}

void TestEngineLink::two_active_rows_do_not_hide_the_choice() {
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('hearth','m',1)");
    sql("INSERT INTO llm_configs (provider, model, is_active) VALUES ('anthropic','m',1)");
    QVERIFY(repo().connect(true).is_ok());
    auto n = Database::instance().execute("SELECT group_concat(provider) FROM llm_configs WHERE is_active = 1");
    QVERIFY(n.is_ok() && n.value().next());
    QCOMPARE(n.value().value(0).toString(), QString("hearth"));
    QVERIFY(repo().disconnect().is_ok());
    QCOMPARE(active(), QString("anthropic"));
}

QTEST_MAIN(TestEngineLink)
#include "test_engine_link.moc"

// What finterm makes of hearth: each state, what is said about it, what is
// offered — and what another program's environment must not inherit.

#include "ai_chat/EngineText.h"
#include "core/util/ChildEnvironment.h"

#include <QJsonDocument>
#include <QtTest/QtTest>

using namespace finterm::engine;

namespace {
QJsonObject json(const char* text) {
    return QJsonDocument::fromJson(text).object();
}
} // namespace

class TestEngineState : public QObject {
    Q_OBJECT

  private slots:
    void contracts();
    void the_chat_model();
    void states();
    void whom_it_asks();
    void answers_survive_storage();
    void every_state_says_something_and_offers_the_right_thing();
    void a_cloud_model_is_not_called_local();
    void an_app_mount_is_not_handed_on();
};

void TestEngineState::contracts() {
    for (const char* ok : {"0.1", "0.2", "0.10", " 0.1 "})
        QVERIFY2(contract_supported(ok), ok);
    for (const char* no : {"", "0", "0.0", "1.0", "0.1.2", "x.1", "0.x", "-1.1", "00"})
        QVERIFY2(!contract_supported(no), no);
}

void TestEngineState::the_chat_model() {
    const auto roles = json(R"({"roles":{"primary_chat":{"model":"qwen3:8b","backend":"ollama","remote":false}}})");
    QVERIFY(chat_model_present(roles, json(R"({"data":[{"id":"qwen3:8b","owned_by":"ollama"}]})")));
    QVERIFY(!chat_model_present(roles, json(R"({"data":[{"id":"nomic-embed-text:latest"}]})")));
    // the alias is listed whether or not its model is there
    QVERIFY(!chat_model_present(roles, json(R"({"data":[{"id":"qwen3:8b","owned_by":"hearth-role"}]})")));
    const auto bare = json(R"({"roles":{"primary_chat":{"model":"mistral"}}})");
    QVERIFY(chat_model_present(bare, json(R"({"data":[{"id":"mistral:latest"}]})")));
    QVERIFY(!chat_model_present(bare, json(R"({"data":[{"id":"mistral:7b"}]})")));
    // a cloud model has nothing to install
    QVERIFY(chat_model_present(json(R"({"roles":{"primary_chat":{"model":"gemini-x","remote":true}}})"), {}));
    QVERIFY(!chat_model_present(json(R"({"roles":{}})"), json(R"({"data":[{"id":""}]})")));
    QVERIFY(!chat_model_present({}, {}));
}

void TestEngineState::states() {
    QCOMPARE(classify({}), State::Absent);
    QCOMPARE(classify({.installed = true}), State::Stopped);
    QCOMPARE(classify({.answered = true}), State::Foreign);
    QCOMPARE(classify({.answered = true, .installed = true}), State::Foreign);
    QCOMPARE(classify({.answered = true, .hearth = true, .contract = "0.2", .model = true}), State::Ready);
    QCOMPARE(classify({.answered = true, .hearth = true, .contract = "0.2"}), State::NoModel);
    QCOMPARE(classify({.answered = true, .hearth = true, .contract = "1.0", .model = true}), State::Unsupported);
    QCOMPARE(classify({.answered = true, .hearth = true}), State::Unsupported);
}

void TestEngineState::whom_it_asks() {
    const QList<State> here{State::Ready, State::NoModel, State::Stopped, State::Unsupported};
    for (auto s : {State::Unknown, State::Ready, State::NoModel, State::Stopped, State::Unsupported, State::Absent, State::Foreign}) {
        QCOMPARE(should_ask(s, Link::Unasked), here.contains(s));
        QVERIFY(!should_ask(s, Link::Connected));
        QVERIFY(!should_ask(s, Link::Declined)); // "no" stays no
    }
}

void TestEngineState::answers_survive_storage() {
    for (auto l : {Link::Unasked, Link::Connected, Link::Declined})
        QCOMPARE(link_from(to_stored(l)), l);
    QCOMPARE(link_from("true"), Link::Unasked); // nothing else counts as a yes
    QCOMPARE(link_from("Connected"), Link::Unasked);
}

void TestEngineState::every_state_says_something_and_offers_the_right_thing() {
    struct Case {
        State state;
        Action unasked, connected;
    };
    for (const Case& c : {Case{State::Unknown, Action::None, Action::None}, Case{State::Ready, Action::Connect, Action::None},
                          Case{State::NoModel, Action::FinishSetup, Action::FinishSetup},
                          Case{State::Stopped, Action::StartAndConnect, Action::Start},
                          Case{State::Unsupported, Action::HowToUpdate, Action::HowToUpdate},
                          Case{State::Absent, Action::Get, Action::Get}, Case{State::Foreign, Action::Recheck, Action::Recheck}}) {
        for (auto link : {Link::Unasked, Link::Declined, Link::Connected}) {
            const auto v = view_for({c.state, link, "0.2.0", "0.2", "http://127.0.0.1:11435", "qwen3:8b", false});
            QVERIFY(!v.headline.isEmpty() && !v.detail.isEmpty());
            QVERIFY(!v.headline.contains('%') && !v.detail.contains('%'));
            QCOMPARE(v.primary, link == Link::Connected ? c.connected : c.unasked);
            QCOMPARE(v.primary_text.isEmpty(), v.primary == Action::None);
            QCOMPARE(v.can_disconnect, link == Link::Connected); // whatever its state
            QCOMPARE(v.good, link == Link::Connected && c.state == State::Ready);
        }
    }
}

void TestEngineState::a_cloud_model_is_not_called_local() {
    const auto v = view_for({State::Ready, Link::Connected, "0.2.0", "0.2", {}, "gemini-x", true});
    QVERIFY(v.detail.contains("leaves this computer"));
    QVERIFY(!view_for({State::Ready, Link::Connected, "0.2.0", "0.2", {}, "qwen3:8b", false}).detail.contains("leaves"));
}

void TestEngineState::an_app_mount_is_not_handed_on() {
    QProcessEnvironment env;
    env.insert("APPDIR", "/tmp/.mount_finte");
    env.insert("APPIMAGE", "/home/u/finterm.AppImage");
    env.insert("LD_LIBRARY_PATH", "/tmp/.mount_finte/usr/lib:/opt/own");
    env.insert("QT_PLUGIN_PATH", "/tmp/.mount_finte/usr/plugins");
    env.insert("PATH", "/usr/bin:/tmp/.mount_finte/usr/bin");
    env.insert("HOME", "/home/u");
    const auto out = finterm::without_app_mount(env);
    QVERIFY(!out.contains("APPDIR") && !out.contains("APPIMAGE") && !out.contains("QT_PLUGIN_PATH"));
    QCOMPARE(out.value("LD_LIBRARY_PATH"), QString("/opt/own"));
    QCOMPARE(out.value("PATH"), QString("/usr/bin"));
    QCOMPARE(out.value("HOME"), QString("/home/u"));
    QProcessEnvironment plain;
    plain.insert("LD_LIBRARY_PATH", "/opt/own");
    QCOMPARE(finterm::without_app_mount(plain).value("LD_LIBRARY_PATH"), QString("/opt/own"));
}

QTEST_APPLESS_MAIN(TestEngineState)
#include "test_engine_state.moc"

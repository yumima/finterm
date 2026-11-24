// Base URLs, and how long a request to one may take.

#include "ai_chat/LlmUrl.h"

#include <QtTest/QtTest>

using namespace finterm;

class TestLlmUrl : public QObject {
    Q_OBJECT

  private slots:
    void bases_are_bare_roots();
    void what_is_this_computer();
    void a_local_engine_may_be_busy();
    void every_model_of_a_config_gets_its_base();
};

void TestLlmUrl::bases_are_bare_roots() {
    for (const char* b : {"http://h:1", "http://h:1/", "http://h:1/v1", "http://h:1/v1/", " http://h:1/v1/v1// "})
        QCOMPARE(normalize_llm_base(b), QString("http://h:1"));
    QCOMPARE(normalize_llm_base("http://h/openai"), QString("http://h/openai"));
}

void TestLlmUrl::what_is_this_computer() {
    for (const char* u : {"http://127.0.0.1:11435/v1/x", "http://localhost:1/", "http://LOCALHOST/", "http://127.9.9.9/", "http://[::1]:5/"})
        QVERIFY2(is_loopback_url(u), u);
    for (const char* u : {"https://api.openai.com/v1", "http://127.0.0.1.evil.example/", "http://127.evil.example/", "http://10.0.0.1/",
                          "http://localhost.evil.example/", "", "not a url", "http://user:localhost@evil.example/"})
        QVERIFY2(!is_loopback_url(u), u);
}

void TestLlmUrl::a_local_engine_may_be_busy() {
    QCOMPARE(chat_timeout_ms("http://127.0.0.1:11435/v1/chat/completions"), kLocalChatTimeoutMs);
    QCOMPARE(chat_timeout_ms("https://api.anthropic.com/v1/messages"), kChatTimeoutMs);
    QVERIFY(kLocalChatTimeoutMs > 2 * kChatTimeoutMs);
}

// An agent is saved with the address as stored; what it means is decided when it runs.
void TestLlmUrl::every_model_of_a_config_gets_its_base() {
    const auto base = [](const QString& provider, const QString& stored) {
        return provider == "hearth" ? QString("now:%1").arg(stored) : stored;
    };
    const QJsonObject model{{"provider", "hearth"}, {"model_id", "m"}};           // none stored
    const QJsonObject other{{"provider", "groq"}, {"base_url", "https://g"}, {"model_id", "m"}};
    const QJsonObject in{{"model", QJsonObject{{"provider", "hearth"}, {"base_url", "x"}, {"api_key", "k"}}},
                         {"team", QJsonObject{{"members", QJsonArray{QJsonObject{{"model", model}}, QJsonObject{{"model", other}}, "text"}}}},
                         {"memory", QJsonObject{{"provider", "hearth"}}}, // not a model
                         {"n", 1}};
    const QJsonObject out = with_model_bases(in, base);
    QCOMPARE(out["model"].toObject()["base_url"].toString(), QString("now:x"));
    QCOMPARE(out["model"].toObject()["api_key"].toString(), QString("k"));
    const QJsonArray members = out["team"].toObject()["members"].toArray();
    QCOMPARE(members[0].toObject()["model"].toObject()["base_url"].toString(), QString("now:"));
    QCOMPARE(members[1].toObject()["model"].toObject()["base_url"].toString(), QString("https://g"));
    QCOMPARE(members[2].toString(), QString("text"));
    QCOMPARE(out["memory"].toObject(), in["memory"].toObject());
    QCOMPARE(out["n"].toInt(), 1);
}

QTEST_APPLESS_MAIN(TestLlmUrl)
#include "test_llm_url.moc"

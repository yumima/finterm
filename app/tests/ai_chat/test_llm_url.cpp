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

QTEST_APPLESS_MAIN(TestLlmUrl)
#include "test_llm_url.moc"

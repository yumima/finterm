// test_sec_identity.cpp — the contact address the C++ side declares to SEC.
// Same case table as tests/scripts/test_identity.py (Python's valid_contact):
// the two sides must send the same identity for the same setting.

#include <QTest>

#include "services/markets/SecIdentity.h"

using fincept::services::sec_identity::header_safe_email;

class TestSecIdentity : public QObject {
    Q_OBJECT
  private slots:
    void cases_data() {
        QTest::addColumn<QString>("email");
        QTest::addColumn<bool>("ok");
        QTest::newRow("plain") << "me@example.com" << true;
        QTest::newRow("plus/sub") << "a.b+c@sub.example.co" << true;
        QTest::newRow("no domain") << "me@" << false;
        QTest::newRow("no local") << "@example.com" << false;
        QTest::newRow("no dot") << "me@example" << false;
        QTest::newRow("dot first") << "me@.com" << false;
        QTest::newRow("dot last") << "me@example.com." << false;
        QTest::newRow("paren )") << "a)b@c.com" << false;
        QTest::newRow("paren (") << "a(b@c.com" << false;
        QTest::newRow("space") << "a b@c.com" << false;
        QTest::newRow("non-ascii") << QString::fromUtf8("josé@example.de") << false;
        QTest::newRow("crlf") << "me@example.com\r\nX: y" << false;
        QTest::newRow("empty") << "" << false;
    }
    void cases() {
        QFETCH(QString, email);
        QFETCH(bool, ok);
        QCOMPARE(header_safe_email(email), ok);
    }
};

QTEST_GUILESS_MAIN(TestSecIdentity)
#include "test_sec_identity.moc"

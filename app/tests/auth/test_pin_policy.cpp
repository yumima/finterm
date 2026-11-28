// tests/auth/test_pin_policy.cpp — the PIN rules, the stored hash forms and
// the recovery key: what registration, the lock screen, Change PIN and the
// recovery-key reset all go through.
#include "auth/PinPolicy.h"
#include "core/util/SecureRandom.h"

#include <QCryptographicHash>
#include <QSet>
#include <QTest>

using namespace finterm::auth;

class TestPinPolicy : public QObject {
    Q_OBJECT
  private slots:
    void rules() {
        QVERIFY(pin_policy::problem(QStringLiteral("2580")).isEmpty());
        QVERIFY(pin_policy::problem(QStringLiteral("481937")).isEmpty());
        QVERIFY(!pin_policy::problem(QStringLiteral("123")).isEmpty());     // short
        QVERIFY(!pin_policy::problem(QStringLiteral("1234567")).isEmpty()); // long
        QVERIFY(!pin_policy::problem(QStringLiteral("12a4")).isEmpty());
        QVERIFY(!pin_policy::problem(QStringLiteral("0000")).isEmpty());
        QVERIFY(!pin_policy::problem(QStringLiteral("123456")).isEmpty());
        QVERIFY(!pin_policy::problem(QStringLiteral("9876")).isEmpty());
        QVERIFY(!pin_policy::problem(QString::fromUtf8("١٢٣٤")).isEmpty()); // non-ASCII digits
    }

    void hash_round_trip() {
        const QString stored = pin_policy::hash(QStringLiteral("2580"), 1000);
        QVERIFY(stored.startsWith(QStringLiteral("pbkdf2-sha256$1000$")));
        QVERIFY(!pin_policy::is_legacy(stored));
        QVERIFY(pin_policy::verify(QStringLiteral("2580"), stored));
        QVERIFY(!pin_policy::verify(QStringLiteral("2581"), stored));
        // Salted: the same PIN twice is two different records.
        QVERIFY(stored != pin_policy::hash(QStringLiteral("2580"), 1000));
        QVERIFY(!pin_policy::verify(QStringLiteral("2580"), QStringLiteral("pbkdf2-sha256$x$00$00")));
        QVERIFY(!pin_policy::verify(QStringLiteral("2580"), QString()));
    }

    void legacy_form_still_verifies() {
        // What accounts were created with before 4.2.2: salt_hex:sha256(salt||pin).
        const QByteArray salt = QByteArray::fromHex("00112233445566778899aabbccddeeff");
        const QString stored = QString::fromLatin1(salt.toHex()) + QLatin1Char(':') +
                               QString::fromLatin1(QCryptographicHash::hash(salt + "2580", QCryptographicHash::Sha256).toHex());
        QVERIFY(pin_policy::is_legacy(stored));
        QVERIFY(pin_policy::verify(QStringLiteral("2580"), stored));
        QVERIFY(!pin_policy::verify(QStringLiteral("0852"), stored));
    }

    void recovery_key() {
        QSet<QString> seen;
        for (int i = 0; i < 50; ++i) {
            const QString key = pin_policy::new_recovery_key();
            QCOMPARE(key.size(), 23); // 4 groups of 5 and 3 dashes
            QCOMPARE(key.count(QLatin1Char('-')), 3);
            QCOMPARE(pin_policy::normalize_recovery_key(key).size(), 20);
            seen.insert(key);
        }
        QCOMPARE(seen.size(), 50);
        // The source itself: no repeats in 64 blocks of 16 bytes.
        QSet<QByteArray> blocks;
        for (int i = 0; i < 64; ++i) {
            const QByteArray b = finterm::util::secure_random_bytes(16);
            QCOMPARE(b.size(), 16);
            blocks.insert(b);
        }
        QCOMPARE(blocks.size(), 64);
        // Past getentropy's 256 per call: every chunk filled, and its own.
        const QByteArray big = finterm::util::secure_random_bytes(1000);
        QCOMPARE(big.size(), 1000);
        QSet<QByteArray> chunks;
        for (int at = 0; at < 1000; at += 250) {
            QVERIFY(big.mid(at, 250) != QByteArray(250, '\0'));
            chunks.insert(big.mid(at, 250));
        }
        QCOMPARE(chunks.size(), 4);
        QCOMPARE(finterm::util::secure_token().size(), 64);
        // As a person types it back: lower case, spaces, the look-alike letters.
        QCOMPARE(pin_policy::normalize_recovery_key(QStringLiteral(" 7kq2m 9xd4t-b3n8v wq5zr ")),
                 QStringLiteral("7KQ2M9XD4TB3N8VWQ5ZR"));
        QCOMPARE(pin_policy::normalize_recovery_key(QStringLiteral("OOOOO-IIIII-LLLLL-00000")),
                 QStringLiteral("00000111111111100000"));
        QVERIFY(pin_policy::normalize_recovery_key(QStringLiteral("7KQ2M-9XD4T-B3N8V")).isEmpty()); // short
        QVERIFY(pin_policy::normalize_recovery_key(QStringLiteral("UUUUU-UUUUU-UUUUU-UUUUU")).isEmpty()); // U not used
        QVERIFY(pin_policy::normalize_recovery_key(QString::fromUtf8("7KQ2M-9XD4T-B3N8V-WQ5Zé")).isEmpty());
        // The stored hash is of the canonical form, so any typing of it matches.
        const QString key = pin_policy::new_recovery_key();
        const QString stored = pin_policy::hash(pin_policy::normalize_recovery_key(key), 1000);
        QVERIFY(pin_policy::verify(pin_policy::normalize_recovery_key(key.toLower()), stored));
    }
};

QTEST_GUILESS_MAIN(TestPinPolicy)
#include "test_pin_policy.moc"

// tests/screens/test_ipo_format.cpp
//
// The two IPO Watch helpers that have each shipped a wrong-but-plausible
// number.
//
// price_mid returned the LOW end of a range while reporting SUCCESS whenever
// the high end failed to parse. Nasdaq routinely decorates the high side
// ("$8.00-$10.00 per ADS", "$15.00 - $17.00*"), and QString::toDouble rejects
// the whole token on the first stray character — so deal size came out
// understated by the full width of the range, rendered identically to a
// correct value, with no unknown state. The same parse feeds final_price,
// which is the denominator of the pop %.
//
// count() exists because a share count was being formatted by the CURRENCY
// formatter: 12,000,000 unlocking shares rendered as "$12M" under a column
// header reading SHARES. A reader sizing lock-up supply in dollars was off by
// the share price.
//
// Neither failed loudly. That is precisely why they are pinned here.

#include "screens/pre_ipo/IpoFormat.h"

#include <QtTest/QtTest>

namespace fmt = fincept::pre_ipo::fmt;

class TestIpoFormat : public QObject {
    Q_OBJECT

  private slots:
    void plain_range_is_the_midpoint();
    void decorated_high_side_is_unknown_not_the_low_side();
    void decorated_low_side_still_parses();
    void single_price_parses();
    void unsplit_second_number_is_unknown();
    void garbage_is_unknown();
    void count_has_no_currency_symbol();
    void count_and_money_disagree_on_the_same_input();
};

void TestIpoFormat::plain_range_is_the_midpoint() {
    bool ok = false;
    QCOMPARE(fmt::price_mid("$15.00-$17.00", &ok), 16.0);
    QVERIFY(ok);
    QCOMPARE(fmt::price_mid("15-17", &ok), 16.0);
    QVERIFY(ok);
}

// THE regression. Previously returned 8.00 with ok == true.
void TestIpoFormat::decorated_high_side_is_unknown_not_the_low_side() {
    for (const char* s : {"$8.00-$10.00 per ADS",
                          "$15.00 - $17.00*",
                          "$15.00-$17.00 per share"}) {
        bool ok = true;
        const double v = fmt::price_mid(s, &ok);
        // These ARE resolvable — leading_number strips the suffix — so they
        // must come back as the true midpoint, never as the low end.
        QVERIFY2(ok, s);
        QVERIFY2(v > 8.5, qPrintable(QString("%1 -> %2 (looks like the low end)")
                                         .arg(s).arg(v)));
    }
    // And one that genuinely cannot be resolved must report failure rather
    // than silently becoming its low side.
    bool ok = true;
    QCOMPARE(fmt::price_mid("$8.00-TBD", &ok), 0.0);
    QVERIFY2(!ok, "an unresolvable high side must not report success");
}

void TestIpoFormat::decorated_low_side_still_parses() {
    bool ok = false;
    QCOMPARE(fmt::price_mid("8.00 USD-10.00 USD", &ok), 9.0);
    QVERIFY(ok);
}

void TestIpoFormat::single_price_parses() {
    bool ok = false;
    QCOMPARE(fmt::price_mid("$12.50", &ok), 12.5);
    QVERIFY(ok);
    QCOMPARE(fmt::price_mid("8", &ok), 8.0);
    QVERIFY(ok);
    QCOMPARE(fmt::price_mid(".50", &ok), 0.5);
    QVERIFY(ok);
}

// An en dash or the word "to" is not the ASCII '-' we split on, so the whole
// range arrives as ONE part. Returning its first number is the same
// confident-low-side failure.
void TestIpoFormat::unsplit_second_number_is_unknown() {
    for (const char* s : {"$15.00 – $17.00",   // en dash
                          "$15.00 to $17.00",
                          "15.00 or 17.00"}) {
        bool ok = true;
        const double v = fmt::price_mid(s, &ok);
        QVERIFY2(!ok, qPrintable(QString("%1 reported success").arg(s)));
        QCOMPARE(v, 0.0);
    }
}

void TestIpoFormat::garbage_is_unknown() {
    bool ok = true;
    QCOMPARE(fmt::price_mid("", &ok), 0.0);        QVERIFY(!ok);
    ok = true;
    QCOMPARE(fmt::price_mid("TBD", &ok), 0.0);     QVERIFY(!ok);
    ok = true;
    QCOMPARE(fmt::price_mid("-", &ok), 0.0);       QVERIFY(!ok);
    ok = true;
    QCOMPARE(fmt::price_mid("N/A", &ok), 0.0);     QVERIFY(!ok);
}

void TestIpoFormat::count_has_no_currency_symbol() {
    QVERIFY2(!fmt::count(12'000'000).contains('$'),
             "a share count must never render with a currency symbol");
    QCOMPARE(fmt::count(12'000'000), QStringLiteral("12.0M"));
    QCOMPARE(fmt::count(0), QStringLiteral("—"));
    QCOMPARE(fmt::count(750), QStringLiteral("750"));
}

// If these two ever agree, someone has aliased one to the other and the
// SHARES-as-dollars bug is back.
void TestIpoFormat::count_and_money_disagree_on_the_same_input() {
    const double shares = 12'000'000;
    QVERIFY(fmt::count(shares) != fmt::money(shares));
    QVERIFY(fmt::money(shares).startsWith('$'));
    QVERIFY(!fmt::count(shares).startsWith('$'));
}

QTEST_APPLESS_MAIN(TestIpoFormat)
#include "test_ipo_format.moc"

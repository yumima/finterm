// tests/services/test_portfolio_fx.cpp
//
// The FX conventions a multi-currency book is folded together with. Two
// invariants matter enough to pin:
//
//   - London pence. "GBp" (and the "GBX" alias) is 1/100 of a pound, while
//     "GBP" is the pound itself. Scaling the wrong one is a 100× error in the
//     portfolio total — bigger than any market move the number exists to show.
//   - Fetch key == lookup key. The summary downloads FX pair quotes under
//     names built by fx_pair_for and then finds them under names built by
//     fx_pair_for. If those two spellings ever drift, the rate is fetched and
//     never found, and every cross-currency total silently reverts to face
//     value. The round-trip test below is that invariant, stated once.

#include "services/portfolio/PortfolioFx.h"

#include <QtTest/QtTest>

using namespace fincept::portfolio;

class TestPortfolioFx : public QObject {
    Q_OBJECT

  private slots:
    void plain_codes_normalise_to_upper();
    void london_pence_scales_by_a_hundredth();
    void pound_is_not_pence();
    void unknown_currency_is_not_one_to_one();
    void same_currency_needs_no_pair();
    void cross_currency_builds_a_pair();
    void pence_holding_uses_the_pound_pair();
    void pair_naming_round_trips();
};

void TestPortfolioFx::plain_codes_normalise_to_upper() {
    QCOMPARE(fx_price_factor(QStringLiteral("usd")).first, QStringLiteral("USD"));
    QCOMPARE(fx_price_factor(QStringLiteral(" cad ")).first, QStringLiteral("CAD"));
    QCOMPARE(fx_price_factor(QStringLiteral("JPY")).second, 1.0);
}

void TestPortfolioFx::london_pence_scales_by_a_hundredth() {
    const auto gbp_pence = fx_price_factor(QStringLiteral("GBp"));
    QCOMPARE(gbp_pence.first, QStringLiteral("GBP"));
    QCOMPARE(gbp_pence.second, 0.01);

    // The "GBX" alias means the same thing, in any case.
    QCOMPARE(fx_price_factor(QStringLiteral("GBX")).second, 0.01);
    QCOMPARE(fx_price_factor(QStringLiteral("gbx")).second, 0.01);
}

void TestPortfolioFx::pound_is_not_pence() {
    // The whole reason fx_price_factor is case-sensitive on the way in.
    const auto pounds = fx_price_factor(QStringLiteral("GBP"));
    QCOMPARE(pounds.first, QStringLiteral("GBP"));
    QCOMPARE(pounds.second, 1.0);
}

void TestPortfolioFx::unknown_currency_is_not_one_to_one() {
    // An empty code means "not discovered yet". fx_pair_for reports no pair,
    // and callers must treat that as unconverted — NOT as a 1:1 rate.
    const auto [pair, factor] = fx_pair_for(QString(), QStringLiteral("USD"));
    QVERIFY(pair.isEmpty());
    QCOMPARE(factor, 1.0);
}

void TestPortfolioFx::same_currency_needs_no_pair() {
    const auto [pair, factor] = fx_pair_for(QStringLiteral("USD"), QStringLiteral("usd"));
    QVERIFY(pair.isEmpty());
    QCOMPARE(factor, 1.0);
}

void TestPortfolioFx::cross_currency_builds_a_pair() {
    const auto [pair, factor] = fx_pair_for(QStringLiteral("CAD"), QStringLiteral("USD"));
    QCOMPARE(pair, QStringLiteral("CADUSD=X"));
    QCOMPARE(factor, 1.0);
}

void TestPortfolioFx::pence_holding_uses_the_pound_pair() {
    // A London holding converts in two steps: pence → pounds (the factor),
    // pounds → portfolio currency (the pair). There is no "GBpUSD=X".
    const auto [pair, factor] = fx_pair_for(QStringLiteral("GBp"), QStringLiteral("USD"));
    QCOMPARE(pair, QStringLiteral("GBPUSD=X"));
    QCOMPARE(factor, 0.01);

    // A GBp holding inside a GBP-denominated portfolio needs no pair at all,
    // but still needs the pence scaling.
    const auto [same_pair, same_factor] = fx_pair_for(QStringLiteral("GBp"), QStringLiteral("GBP"));
    QVERIFY(same_pair.isEmpty());
    QCOMPARE(same_factor, 0.01);
}

void TestPortfolioFx::pair_naming_round_trips() {
    // The load-bearing invariant: the name used to FETCH a rate is the name
    // used to LOOK IT UP. Same inputs in any case spelling → same key.
    const QStringList instruments{"CAD", "cad", " Cad ", "EUR", "GBp", "GBX", "JPY"};
    for (const QString& raw : instruments) {
        const QString fetch_key = fx_pair_for(raw, QStringLiteral("USD")).first;
        const QString lookup_key = fx_pair_for(raw, QStringLiteral("usd")).first;
        QCOMPARE(fetch_key, lookup_key);
        if (!fetch_key.isEmpty()) {
            QVERIFY(fetch_key.endsWith(QStringLiteral("USD=X")));
            QCOMPARE(fetch_key, fetch_key.toUpper()); // never case-dependent
        }
    }
}

QTEST_GUILESS_MAIN(TestPortfolioFx)
#include "test_portfolio_fx.moc"

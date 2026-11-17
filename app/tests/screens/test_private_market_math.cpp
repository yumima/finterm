// tests/screens/test_private_market_math.cpp
//
// The Private Market screen's arithmetic. Each case is a way a plausible
// number goes wrong without failing loudly:
//  - an implied valuation built from a price and a round on different share
//    bases (or from a round that states only one of the two);
//  - a company bound to the wrong marketplace page by a near-miss name.

#include "screens/pre_ipo/PrivateMarketMath.h"

#include <QtTest/QtTest>

using namespace finterm::pre_ipo;

namespace {
FinancingRound round_(int y, int m, double post, double pps, bool tender = false) {
    FinancingRound r;
    r.month = QDate(y, m, 1);
    r.post_money_usd = post;
    r.pps = pps;
    r.tender = tender;
    return r;
}
} // namespace

class TestPrivateMarketMath : public QObject {
    Q_OBJECT
  private slots:
    void implied_valuation_uses_the_round_that_states_both() {
        // Stripe 2026-11-17: newest round (tender) states both; an older
        // round with only post-money must not supply the share count.
        QVector<FinancingRound> r{round_(2026, 2, 159e9, 63.0, true), round_(2025, 2, 91.5e9, pm::kNaN, true),
                                  round_(2023, 3, 50e9, 20.13)};
        MarketQuote q;
        q.price = 73.70;
        const double v = pm::implied_valuation(q, r);
        QVERIFY(std::abs(v - 73.70 * 159e9 / 63.0) < 1.0);
        QCOMPARE(pm::share_basis_round(r)->month, QDate(2026, 2, 1));
    }
    void a_newer_round_with_one_number_is_skipped_for_shares() {
        QVector<FinancingRound> r{round_(2026, 4, 350e9, pm::kNaN), round_(2026, 2, 380e9, 259.14)};
        QCOMPARE(pm::share_basis_round(r)->month, QDate(2026, 2, 1));
        QCOMPARE(pm::last_valued_round(r)->month, QDate(2026, 4, 1)); // headline post-money is still the newest
        QCOMPARE(pm::last_priced_round(r)->month, QDate(2026, 2, 1));
    }
    void no_price_or_no_basis_is_unknown_not_zero() {
        QVector<FinancingRound> r{round_(2026, 5, 965e9, 589.01)};
        MarketQuote no_price; // Anthropic on Clarity: rounds, no model price
        QVERIFY(std::isnan(pm::implied_valuation(no_price, r)));
        MarketQuote q;
        q.price = 10;
        QVERIFY(std::isnan(pm::implied_valuation(q, {round_(2020, 1, pm::kNaN, pm::kNaN)})));
        QVERIFY(std::isnan(pm::implied_valuation(q, {})));
    }
    void premium_needs_two_positive_numbers() {
        QCOMPARE(pm::premium_pct(73.70, 63.0), (73.70 / 63.0 - 1) * 100);
        QVERIFY(std::isnan(pm::premium_pct(73.70, 0)));
        QVERIFY(std::isnan(pm::premium_pct(pm::kNaN, 63)));
    }
    void legal_form_is_stripped_but_name_words_stay() {
        QCOMPARE(pm::strip_legal_form("Stripe, Inc."), QString("Stripe"));
        QCOMPARE(pm::strip_legal_form("Anthropic PBC"), QString("Anthropic"));
        QCOMPARE(pm::strip_legal_form("Ripple Labs Inc."), QString("Ripple Labs"));
        QCOMPARE(pm::strip_legal_form("Space Exploration Technologies Corp"),
                 QString("Space Exploration Technologies"));
        QCOMPARE(pm::strip_legal_form("Inc"), QString("Inc")); // never strips to nothing
    }
    void slug_match_is_exact() {
        const QSet<QString> u{"stripe", "ripple-labs", "spacex", "databricks", "mercury"};
        QCOMPARE(pm::match_slug({}, "stripe-inc", "Stripe, Inc.", {}, u), QString("stripe"));
        QCOMPARE(pm::match_slug({}, "x", "Ripple Labs Inc.", {}, u), QString("ripple-labs"));
        QCOMPARE(pm::match_slug({}, "space-exploration-technologies-corp", "Space Exploration Technologies Corp",
                                {"SpaceX"}, u),
                 QString("spacex"));
        // A fund named after a word that is also a company is not that company.
        QVERIFY(pm::match_slug({}, "mercury-fund-lp", "Mercury Fund LP", {}, u).isEmpty());
        QVERIFY(pm::match_slug({}, "databricks-spv", "Databricks SPV I", {}, u).isEmpty());
        // An already-bound slug wins even if the name drifted.
        QCOMPARE(pm::match_slug("databricks", "abc", "Something", {}, u), QString("databricks"));
        QVERIFY(pm::match_slug({}, "stripe", "Stripe", {}, {}).isEmpty());
    }
    void display_name_from_slug() {
        QCOMPARE(pm::display_from_slug("ripple-labs"), QString("Ripple Labs"));
        QCOMPARE(pm::display_from_slug("openai"), QString("Openai"));
    }
};

QTEST_MAIN(TestPrivateMarketMath)
#include "test_private_market_math.moc"

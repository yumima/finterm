// A portfolio total is the whole book, or it says what it leaves out.
//
// A holding with no price or no currency conversion is left out of every
// total (PortfolioService::build_summary). The displays used to mark such a
// total "≈" — and with the only holding left out, a $905 position showed
// "NAV ≈0.00". A sum over part of the book is not approximately the total:
// the rules here decide what every NAV / P&L / cost display shows.

#include "screens/portfolio/PortfolioTypes.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace fincept::portfolio;

namespace {

HoldingWithQuote holding(const QString& sym, bool price_known, bool fx_known) {
    HoldingWithQuote h;
    h.symbol = sym;
    h.price_known = price_known;
    h.fx_known = fx_known;
    return h;
}

} // namespace

class TestPortfolioSummaryTotals : public QObject {
    Q_OBJECT
  private slots:
    void whole_book_says_nothing() {
        PortfolioSummary s;
        s.holdings = {holding("MU", true, true), holding("PBF", true, true)};
        QVERIFY(!s.nothing_valued());
        QCOMPARE(s.exclusion_note(), QString());
    }
    void none_valued_is_unknown_not_zero() {
        PortfolioSummary s;
        s.holdings = {holding("MU", true, false)};
        s.fx_unknown_symbols = {"MU"};
        QVERIFY(s.nothing_valued());
        QCOMPARE(s.exclusion_note(), QStringLiteral("excl. MU"));
    }
    void partial_names_what_it_leaves_out() {
        PortfolioSummary s;
        s.holdings = {holding("MU", true, true), holding("PBF", false, true), holding("RNG", true, false)};
        s.unpriced_symbols = {"PBF"};
        s.fx_unknown_symbols = {"RNG"};
        QVERIFY(!s.nothing_valued());
        QCOMPARE(s.exclusion_note(), QStringLiteral("excl. PBF, RNG"));
    }
    void a_symbol_on_both_lists_counts_once() {
        PortfolioSummary s;
        s.holdings = {holding("MU", false, false), holding("PBF", true, true)};
        s.unpriced_symbols = {"MU"};
        s.fx_unknown_symbols = {"MU"};
        QCOMPARE(s.exclusion_note(), QStringLiteral("excl. MU"));
    }
    void many_left_out_are_counted() {
        PortfolioSummary s;
        s.holdings = {holding("A", false, true), holding("B", false, true), holding("C", false, true),
                      holding("D", true, true)};
        s.unpriced_symbols = {"A", "B", "C"};
        QCOMPARE(s.exclusion_note(), QStringLiteral("excl. 3"));
    }
    void stale_prices_are_an_estimate_not_an_exclusion() {
        PortfolioSummary s;
        s.holdings = {holding("MU", true, true)};
        s.holdings[0].price_stale = true;
        s.stale_symbols = {"MU"};
        QVERIFY(s.has_estimate());          // "≈": it is in the totals, at an old price
        QCOMPARE(s.exclusion_note(), QString());
    }
    void a_stale_holding_left_out_is_not_an_estimate() {
        // Stale AND no currency: excluded, so nothing in the totals is stale.
        PortfolioSummary s;
        s.holdings = {holding("MU", true, false), holding("PBF", true, true)};
        s.holdings[0].price_stale = true;
        s.stale_symbols = {"MU"};
        s.fx_unknown_symbols = {"MU"};
        QVERIFY(!s.has_estimate());
        QCOMPARE(s.exclusion_note(), QStringLiteral("excl. MU"));
    }
    void the_day_total_also_leaves_out_unknown_moves() {
        PortfolioSummary s;
        s.holdings = {holding("MU", true, true), holding("PBF", true, true), holding("RNG", false, true)};
        s.holdings[1].price_stale = true;
        s.unpriced_symbols = {"RNG"};
        s.day_change_unknown_symbols = {"RNG", "PBF"};
        QCOMPARE(PortfolioSummary::exclusion_note_for(s.excluded_symbols(s.day_change_unknown_symbols)),
                 QStringLiteral("excl. RNG, PBF"));
        // PBF's stale move is not in the day total, so the day is not an estimate
        QVERIFY(!s.has_estimate(s.day_change_unknown_symbols));
        QVERIFY(s.has_estimate());          // but its price is in the NAV
    }
    void an_empty_book_is_not_unknown() {
        PortfolioSummary s;
        QVERIFY(!s.nothing_valued()); // 0 positions: 0.00 is the true total
    }
};

QTEST_GUILESS_MAIN(TestPortfolioSummaryTotals)
#include "test_portfolio_summary_totals.moc"

// tests/screens/test_position_replay.cpp
//
// The arithmetic every return on the Power Trader screen rests on, which had
// no coverage at all because it lived inside a service singleton that needed a
// populated price history to run.
//
// The convention under test: a SELL removes shares at the position's AVERAGE
// COST and books proceeds minus that cost as realized P&L. The shipped code
// used to reduce cost basis by the sale PROCEEDS instead, so a position bought
// at $10k and sold at $30k left a $0 residual and dropped out of the
// leaderboard entirely — and since winners are the positions most likely to be
// closed, that biased the whole screen toward trades that hadn't worked yet.
//
// Congress discloses amount BANDS, never exact figures, so every dollar here
// is a midpoint and every share count is inferred from it via the close on the
// entry date. That is why "no real close" has to be a distinct state rather
// than a zero.

#include "screens/power_trader/PositionReplay.h"

#include <QtTest/QtTest>

using namespace fincept::power_trader;

class TestPositionReplay : public QObject {
    Q_OBJECT

  private slots:
    void buy_sets_shares_and_cost();
    void sell_books_realized_pnl_at_average_cost();
    void closed_winner_keeps_its_gain();
    void average_cost_across_multiple_buys();
    void oversized_sell_cannot_go_negative();
    void missing_close_flags_the_position();
    void missing_close_still_counts_the_dollars();
    void sell_with_no_prior_buy_is_harmless();
};

void TestPositionReplay::buy_sets_shares_and_cost() {
    const QVector<ReplayTrade> t{
        {"AAPL", TradeDirection::Buy, 10'000.0, 100.0},
    };
    const auto p = replay_positions(t).value("AAPL");
    QCOMPARE(p.shares, 100.0);
    QCOMPARE(p.cost_basis, 10'000.0);
    QCOMPARE(p.realized_pnl, 0.0);
    QVERIFY(!p.priced_gap);
}

void TestPositionReplay::sell_books_realized_pnl_at_average_cost() {
    // Buy 100 @ $100, sell half at $300 → 50 shares out at $100 cost,
    // $15,000 proceeds, $10,000 gain. Remaining: 50 shares, $5,000 cost.
    const QVector<ReplayTrade> t{
        {"AAPL", TradeDirection::Buy,  10'000.0, 100.0},
        {"AAPL", TradeDirection::Sell, 15'000.0, 300.0},
    };
    const auto p = replay_positions(t).value("AAPL");
    QCOMPARE(p.shares, 50.0);
    QCOMPARE(p.cost_basis, 5'000.0);
    QCOMPARE(p.realized_pnl, 10'000.0);
}

// THE regression: a fully-closed winner must not evaporate.
void TestPositionReplay::closed_winner_keeps_its_gain() {
    // Buy 100 @ $100 ($10k), sell all 100 @ $300 ($30k). The old rule did
    // cost_basis -= min(proceeds, cost_basis) → $0 residual, gain discarded.
    const QVector<ReplayTrade> t{
        {"AAPL", TradeDirection::Buy,  10'000.0, 100.0},
        {"AAPL", TradeDirection::Sell, 30'000.0, 300.0},
    };
    const auto p = replay_positions(t).value("AAPL");
    QCOMPARE(p.shares, 0.0);
    QCOMPARE(p.cost_basis, 0.0);
    QVERIFY2(qFuzzyCompare(p.realized_pnl, 20'000.0),
             qPrintable(QString("a closed winner must keep its $20k gain, got %1")
                            .arg(p.realized_pnl)));
}

void TestPositionReplay::average_cost_across_multiple_buys() {
    // 100 @ $100 then 100 @ $200 → 200 shares, $30k cost, $150 average.
    // Sell 100 @ $200 = $20k proceeds against $15k cost → $5k gain.
    const QVector<ReplayTrade> t{
        {"AAPL", TradeDirection::Buy,  10'000.0, 100.0},
        {"AAPL", TradeDirection::Buy,  20'000.0, 200.0},
        {"AAPL", TradeDirection::Sell, 20'000.0, 200.0},
    };
    const auto p = replay_positions(t).value("AAPL");
    QCOMPARE(p.shares, 100.0);
    QVERIFY2(qFuzzyCompare(p.cost_basis, 15'000.0),
             qPrintable(QString("expected $15k average cost left, got %1").arg(p.cost_basis)));
    QVERIFY2(qFuzzyCompare(p.realized_pnl, 5'000.0),
             qPrintable(QString("expected $5k realized, got %1").arg(p.realized_pnl)));
}

// A member can disclose a sale of a lot bought before our window opened.
// Letting that drive shares negative is what silently unpriced positions.
void TestPositionReplay::oversized_sell_cannot_go_negative() {
    const QVector<ReplayTrade> t{
        {"AAPL", TradeDirection::Buy,  10'000.0, 100.0},   // 100 shares
        {"AAPL", TradeDirection::Sell, 90'000.0, 300.0},   // implies 300 shares
    };
    const auto p = replay_positions(t).value("AAPL");
    QVERIFY2(p.shares >= 0.0, qPrintable(QString("shares went negative: %1").arg(p.shares)));
    QCOMPARE(p.shares, 0.0);
    QVERIFY2(p.cost_basis >= 0.0, "cost basis must not go negative");
    // Only the 100 shares actually held are realized: $30k out against $10k.
    QVERIFY2(qFuzzyCompare(p.realized_pnl, 20'000.0),
             qPrintable(QString("got %1").arg(p.realized_pnl)));
}

void TestPositionReplay::missing_close_flags_the_position() {
    const QVector<ReplayTrade> t{
        {"AAPL", TradeDirection::Buy, 10'000.0, 0.0},   // no real close
    };
    const auto p = replay_positions(t).value("AAPL");
    QVERIFY2(p.priced_gap, "a trade with no real close must flag the position");
    QCOMPARE(p.shares, 0.0);   // unknowable, never guessed
}

void TestPositionReplay::missing_close_still_counts_the_dollars() {
    // The disclosed dollar band is real even when the price history is not.
    const QVector<ReplayTrade> t{
        {"AAPL", TradeDirection::Buy, 10'000.0, 0.0},
    };
    const auto p = replay_positions(t).value("AAPL");
    QCOMPARE(p.cost_basis, 10'000.0);
    QCOMPARE(p.buy_count, 1);
}

void TestPositionReplay::sell_with_no_prior_buy_is_harmless() {
    const QVector<ReplayTrade> t{
        {"AAPL", TradeDirection::Sell, 30'000.0, 300.0},
    };
    const auto p = replay_positions(t).value("AAPL");
    QCOMPARE(p.shares, 0.0);
    QCOMPARE(p.cost_basis, 0.0);
    QCOMPARE(p.realized_pnl, 0.0);   // nothing held ⇒ nothing realized
}

QTEST_APPLESS_MAIN(TestPositionReplay)
#include "test_position_replay.moc"

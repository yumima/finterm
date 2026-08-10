// tests/services/test_portfolio_ledger.cpp
//
// Pins the single cost-basis convention. The bug that motivated this engine:
// two incompatible average-cost computations coexisted (add_asset's running
// upsert folded past sells in; edit_position recomputed over all BUYs
// ignoring sells), so the audit's worked example — Buy 100@10, Sell 50@20,
// Buy 50@30 — read avg 20.00 from one path and 16.67 from the other, and
// editing a transaction note silently moved cost basis by 17%. There is now
// exactly one answer, produced here, and this file states it.

#include "services/portfolio/PortfolioLedger.h"

#include <QtTest/QtTest>

using namespace fincept::portfolio;

namespace {

int seq = 0;

Transaction txn(const QString& type, double qty, double price, const QString& date) {
    Transaction t;
    t.id = QStringLiteral("t%1").arg(++seq, 4, 10, QLatin1Char('0'));
    t.transaction_type = type;
    t.quantity = qty;
    t.price = price;
    t.total_value = qty * price;
    t.transaction_date = date;
    t.created_at = t.id; // monotonic tie-break within a date
    return t;
}

} // namespace

class TestPortfolioLedger : public QObject {
    Q_OBJECT

  private slots:
    void single_buy();
    void multi_buy_average();
    void audit_worked_example();
    void partial_sell_keeps_avg();
    void full_close_records_realized_and_zeroes_qty();
    void reopen_after_close_resets_avg();
    void oversell_clamps_and_warns();
    void split_preserves_market_value();
    void reverse_split();
    void dividend_income_accumulates();
    void unsorted_input_replays_chronologically();
    void same_day_ties_replay_in_recorded_order();
    void bad_rows_warn_and_do_not_corrupt();
    void first_buy_date_anchors_entry();
    void cursor_states_position_per_date();
    void cursor_applies_split_on_its_date();
};

void TestPortfolioLedger::single_buy() {
    const auto pos = replay_transactions({txn("BUY", 10, 150.0, "2026-01-05")});
    QCOMPARE(pos.quantity, 10.0);
    QCOMPARE(pos.avg_cost, 150.0);
    QCOMPARE(pos.realized_pnl, 0.0);
    QVERIFY(pos.is_open());
    QVERIFY(pos.warnings.isEmpty());
}

void TestPortfolioLedger::multi_buy_average() {
    const auto pos = replay_transactions({
        txn("BUY", 10, 100.0, "2026-01-05"),
        txn("BUY", 30, 140.0, "2026-02-05"),
    });
    QCOMPARE(pos.quantity, 40.0);
    QCOMPARE(pos.avg_cost, 130.0); // (10*100 + 30*140) / 40
}

void TestPortfolioLedger::audit_worked_example() {
    // Buy 100@10 → Sell 50@20 → Buy 50@30.
    // Average-cost: after the sell, 50 shares remain at avg 10; the new buy
    // gives (50*10 + 50*30)/100 = 20. Realized on the sell: 50*(20-10) = 500.
    // The old edit_position path answered 16.67 for the same history.
    const auto pos = replay_transactions({
        txn("BUY", 100, 10.0, "2026-01-05"),
        txn("SELL", 50, 20.0, "2026-02-05"),
        txn("BUY", 50, 30.0, "2026-03-05"),
    });
    QCOMPARE(pos.quantity, 100.0);
    QCOMPARE(pos.avg_cost, 20.0);
    QCOMPARE(pos.realized_pnl, 500.0);
    QVERIFY(pos.warnings.isEmpty());
}

void TestPortfolioLedger::partial_sell_keeps_avg() {
    const auto pos = replay_transactions({
        txn("BUY", 100, 50.0, "2026-01-05"),
        txn("SELL", 40, 60.0, "2026-02-05"),
    });
    QCOMPARE(pos.quantity, 60.0);
    QCOMPARE(pos.avg_cost, 50.0);          // untouched by the sell
    QCOMPARE(pos.realized_pnl, 400.0);     // 40 * (60 - 50)
}

void TestPortfolioLedger::full_close_records_realized_and_zeroes_qty() {
    // The old sell path deleted the asset row and the gain vanished from every
    // total. The ledger keeps it: a closed position is realized P&L, not
    // nothing.
    const auto pos = replay_transactions({
        txn("BUY", 25, 80.0, "2026-01-05"),
        txn("SELL", 25, 100.0, "2026-03-05"),
    });
    QCOMPARE(pos.quantity, 0.0);
    QCOMPARE(pos.realized_pnl, 500.0); // 25 * (100 - 80)
    QVERIFY(!pos.is_open());
}

void TestPortfolioLedger::reopen_after_close_resets_avg() {
    const auto pos = replay_transactions({
        txn("BUY", 10, 100.0, "2026-01-05"),
        txn("SELL", 10, 120.0, "2026-02-05"),
        txn("BUY", 5, 200.0, "2026-03-05"),
    });
    QCOMPARE(pos.quantity, 5.0);
    QCOMPARE(pos.avg_cost, 200.0); // fresh era — no ghost of the closed lot
    QCOMPARE(pos.realized_pnl, 200.0);
}

void TestPortfolioLedger::oversell_clamps_and_warns() {
    // A data-entry error must be reported, not written into a negative
    // position (the old edit path once wrote -211 shares to the DB).
    const auto pos = replay_transactions({
        txn("BUY", 50, 10.0, "2026-01-05"),
        txn("SELL", 80, 12.0, "2026-02-05"),
    });
    QCOMPARE(pos.quantity, 0.0);
    QCOMPARE(pos.realized_pnl, 100.0); // only the 50 actually held: 50*(12-10)
    QCOMPARE(pos.warnings.size(), 1);
    QVERIFY(pos.warnings.first().contains("exceeds"));
}

void TestPortfolioLedger::split_preserves_market_value() {
    // 4-for-1: quantity ×4, avg ÷4 — cost basis and P&L invariant. This is
    // the transaction type the app accepted for years and never applied,
    // leaving P&L off by the split factor.
    const auto pos = replay_transactions({
        txn("BUY", 25, 400.0, "2026-01-05"),
        txn("SPLIT", 4, 0.0, "2026-02-05"),
    });
    QCOMPARE(pos.quantity, 100.0);
    QCOMPARE(pos.avg_cost, 100.0);
    QCOMPARE(pos.quantity * pos.avg_cost, 10000.0); // basis unchanged
}

void TestPortfolioLedger::reverse_split() {
    const auto pos = replay_transactions({
        txn("BUY", 100, 2.0, "2026-01-05"),
        txn("SPLIT", 0.1, 0.0, "2026-02-05"), // 1-for-10
    });
    QCOMPARE(pos.quantity, 10.0);
    QCOMPARE(pos.avg_cost, 20.0);
}

void TestPortfolioLedger::dividend_income_accumulates() {
    // record_dividend used to write the row and discard the amount
    // (Q_UNUSED(total)). Income is real return; the ledger keeps it, and
    // keeps it out of cost basis (cash dividend, not reinvestment).
    const auto pos = replay_transactions({
        txn("BUY", 100, 50.0, "2026-01-05"),
        txn("DIVIDEND", 100, 0.25, "2026-02-05"),
        txn("DIVIDEND", 100, 0.25, "2026-05-05"),
    });
    QCOMPARE(pos.dividend_income, 50.0);
    QCOMPARE(pos.avg_cost, 50.0);
}

void TestPortfolioLedger::unsorted_input_replays_chronologically() {
    // Repository queries return DESC; the ledger must not care.
    const auto pos = replay_transactions({
        txn("BUY", 50, 30.0, "2026-03-05"),
        txn("SELL", 50, 20.0, "2026-02-05"),
        txn("BUY", 100, 10.0, "2026-01-05"),
    });
    QCOMPARE(pos.avg_cost, 20.0); // same as audit_worked_example
    QCOMPARE(pos.realized_pnl, 500.0);
}

void TestPortfolioLedger::same_day_ties_replay_in_recorded_order() {
    // Buy and sell on the same date: created_at breaks the tie, so the buy
    // recorded first replays first and the sell finds shares to sell.
    auto buy = txn("BUY", 10, 100.0, "2026-01-05");
    auto sell = txn("SELL", 10, 110.0, "2026-01-05");
    const auto pos = replay_transactions({sell, buy});
    QCOMPARE(pos.quantity, 0.0);
    QCOMPARE(pos.realized_pnl, 100.0);
    QVERIFY(pos.warnings.isEmpty());
}

void TestPortfolioLedger::bad_rows_warn_and_do_not_corrupt() {
    const auto pos = replay_transactions({
        txn("BUY", 10, 100.0, "2026-01-05"),
        txn("BUY", 0, 100.0, "2026-01-06"),   // zero-qty guard (old code divided by it)
        txn("SPLIT", 0, 0.0, "2026-01-07"),   // zero ratio would erase the position
        txn("WITHDRAW", 5, 1.0, "2026-01-08"),// unknown type
    });
    QCOMPARE(pos.quantity, 10.0);
    QCOMPARE(pos.avg_cost, 100.0);
    QCOMPARE(pos.warnings.size(), 3);
}

void TestPortfolioLedger::first_buy_date_anchors_entry() {
    const auto pos = replay_transactions({
        txn("BUY", 5, 10.0, "2026-02-05"),
        txn("BUY", 5, 10.0, "2026-01-05"),
    });
    QCOMPARE(pos.first_buy_date, QStringLiteral("2026-01-05"));
}

void TestPortfolioLedger::cursor_states_position_per_date() {
    // The backfill walks a close calendar and must see the quantity the log
    // states FOR THAT DATE — not today's. Same history as the audit example.
    LedgerCursor c({
        txn("BUY", 100, 10.0, "2026-01-05"),
        txn("SELL", 50, 20.0, "2026-02-05"),
        txn("BUY", 50, 30.0, "2026-03-05"),
    });
    c.advance_to("2026-01-04");
    QCOMPARE(c.position().quantity, 0.0);
    c.advance_to("2026-01-05");
    QCOMPARE(c.position().quantity, 100.0);
    c.advance_to("2026-02-10");
    QCOMPARE(c.position().quantity, 50.0);
    QCOMPARE(c.position().avg_cost, 10.0);
    c.advance_to("2026-03-31");
    QCOMPARE(c.position().quantity, 100.0);
    QCOMPARE(c.position().avg_cost, 20.0);
    QCOMPARE(c.position().realized_pnl, 500.0);
}

void TestPortfolioLedger::cursor_applies_split_on_its_date() {
    LedgerCursor c({
        txn("BUY", 25, 400.0, "2026-01-05"),
        txn("SPLIT", 4, 0.0, "2026-02-05"),
    });
    c.advance_to("2026-02-04");
    QCOMPARE(c.position().quantity, 25.0);
    c.advance_to("2026-02-05");
    QCOMPARE(c.position().quantity, 100.0);
    QCOMPARE(c.position().avg_cost, 100.0);
}

QTEST_GUILESS_MAIN(TestPortfolioLedger)
#include "test_portfolio_ledger.moc"

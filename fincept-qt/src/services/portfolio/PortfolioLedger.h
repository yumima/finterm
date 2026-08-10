// src/services/portfolio/PortfolioLedger.h
//
// The one cost-basis engine. Every number the portfolio module states about
// a position — quantity, average cost, realized P&L, dividend income — is
// derived here by replaying the symbol's transaction log in chronological
// order. The transactions are the source of truth; the portfolio_assets row
// is a cache of this replay's output, never independently edited.
//
// Why replay instead of incremental updates: the module previously kept two
// incompatible average-cost conventions (a running upsert in add_asset, an
// all-BUYs-ignoring-SELLs recompute in edit_position), so editing a
// transaction's *note* could move a position's cost basis by double digits.
// Selling a full position deleted it, taking its realized gain out of every
// total. SPLIT rows were accepted and never applied. A single replay function
// makes all of those the same code path, and makes the answer to "where did
// this number come from" always "the transaction log".
//
// Conventions (documented because they are choices, not laws):
//   - Average cost, not FIFO. Matches what the app has always displayed as
//     AVG COST. Tax-lot reporting (FIFO/spec-ID) would need per-lot state.
//   - BUY:  avg' = (avg*qty + q*p) / (qty+q); qty' = qty+q.
//   - SELL: realized += q_sold * (p - avg); qty' = qty - q_sold. The average
//     cost of remaining shares is unchanged. Overselling clamps at zero and
//     records a warning — the ledger reports the inconsistency instead of
//     writing a negative position.
//   - DIVIDEND: income += quantity * price (quantity = shares held at record
//     time, price = cash per share). Cash dividends do not alter cost basis.
//   - SPLIT: quantity holds the ratio of new shares per old share (4-for-1
//     split → 4, 1-for-10 reverse split → 0.1); price is ignored. qty' =
//     qty*ratio, avg' = avg/ratio — market value and P&L are invariant.
//
// Pure functions, no Qt widgets, no DB, no network — testable in isolation.

#pragma once

#include "screens/portfolio/PortfolioTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::portfolio {

struct LedgerPosition {
    double quantity = 0;        // shares currently held
    double avg_cost = 0;        // average cost per share of those shares
    double realized_pnl = 0;    // proceeds minus average-cost basis over all SELLs
    double dividend_income = 0; // cash dividends received
    QString first_buy_date;     // date of the earliest BUY (entry anchor)
    QStringList warnings;       // inconsistencies found during replay (oversell, bad split)

    bool is_open(double epsilon = 1e-9) const { return quantity > epsilon; }
};

/// Replay one symbol's transactions in chronological order (the input need not
/// be sorted; ties on transaction_date break on created_at, then id, so two
/// same-day rows replay in the order they were recorded).
LedgerPosition replay_transactions(QVector<Transaction> txns);

/// Stepwise replay: the position as of successive dates. Built for the NAV
/// backfill, which walks a close-price calendar and needs the quantity and
/// cost basis that the transaction log states for each date — valuing history
/// with today's share counts is what used to put a phantom cliff in the NAV
/// series at every date where the position size has since changed.
class LedgerCursor {
  public:
    /// Takes the symbol's full transaction list (any order; sorted internally
    /// with the same tie-break as replay_transactions).
    explicit LedgerCursor(QVector<Transaction> txns);

    /// Apply every transaction dated on or before `date` (YYYY-MM-DD) that has
    /// not been applied yet. Dates must be fed in non-decreasing order.
    void advance_to(const QString& date);

    const LedgerPosition& position() const { return pos_; }

  private:
    QVector<Transaction> txns_;
    int next_ = 0;
    LedgerPosition pos_;
};

} // namespace fincept::portfolio

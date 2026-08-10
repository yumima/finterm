// src/services/portfolio/PortfolioLedger.cpp
#include "services/portfolio/PortfolioLedger.h"

#include <algorithm>

namespace fincept::portfolio {

namespace {

constexpr double kQtyEpsilon = 1e-9;

void sort_chronologically(QVector<Transaction>& txns) {
    std::sort(txns.begin(), txns.end(), [](const Transaction& a, const Transaction& b) {
        if (a.transaction_date != b.transaction_date)
            return a.transaction_date < b.transaction_date;
        if (a.created_at != b.created_at)
            return a.created_at < b.created_at;
        return a.id < b.id;
    });
}

// The single step both replay paths share — one convention, one place.
void apply_transaction(LedgerPosition& pos, const Transaction& t) {
    if (t.transaction_type == QLatin1String("BUY")) {
        if (t.quantity <= 0) {
            pos.warnings << QStringLiteral("BUY on %1 has non-positive quantity %2 — ignored")
                                .arg(t.transaction_date)
                                .arg(t.quantity);
            return;
        }
        const double new_qty = pos.quantity + t.quantity;
        pos.avg_cost = (pos.avg_cost * pos.quantity + t.price * t.quantity) / new_qty;
        pos.quantity = new_qty;
        if (pos.first_buy_date.isEmpty())
            pos.first_buy_date = t.transaction_date;
    } else if (t.transaction_type == QLatin1String("SELL")) {
        if (t.quantity <= 0) {
            pos.warnings << QStringLiteral("SELL on %1 has non-positive quantity %2 — ignored")
                                .arg(t.transaction_date)
                                .arg(t.quantity);
            return;
        }
        const double sold = std::min(t.quantity, pos.quantity);
        if (sold < t.quantity - kQtyEpsilon) {
            pos.warnings << QStringLiteral("SELL of %1 on %2 exceeds the %3 held — clamped")
                                .arg(t.quantity)
                                .arg(t.transaction_date)
                                .arg(pos.quantity);
        }
        pos.realized_pnl += sold * (t.price - pos.avg_cost);
        pos.quantity -= sold;
        if (pos.quantity <= kQtyEpsilon)
            pos.quantity = 0; // fully closed; avg_cost is stale until the next BUY resets it
    } else if (t.transaction_type == QLatin1String("DIVIDEND")) {
        pos.dividend_income += t.quantity * t.price;
    } else if (t.transaction_type == QLatin1String("SPLIT")) {
        // quantity carries the ratio: new shares per old share.
        if (t.quantity <= 0) {
            pos.warnings << QStringLiteral("SPLIT on %1 has non-positive ratio %2 — ignored")
                                .arg(t.transaction_date)
                                .arg(t.quantity);
            return;
        }
        pos.quantity *= t.quantity;
        pos.avg_cost /= t.quantity;
    } else {
        pos.warnings << QStringLiteral("Unknown transaction type '%1' on %2 — ignored")
                            .arg(t.transaction_type, t.transaction_date);
    }
}

} // namespace

LedgerPosition replay_transactions(QVector<Transaction> txns) {
    sort_chronologically(txns);
    LedgerPosition pos;
    for (const auto& t : txns)
        apply_transaction(pos, t);
    return pos;
}

LedgerCursor::LedgerCursor(QVector<Transaction> txns) : txns_(std::move(txns)) {
    sort_chronologically(txns_);
}

void LedgerCursor::advance_to(const QString& date) {
    while (next_ < txns_.size() && txns_[next_].transaction_date.left(10) <= date)
        apply_transaction(pos_, txns_[next_++]);
}

} // namespace fincept::portfolio

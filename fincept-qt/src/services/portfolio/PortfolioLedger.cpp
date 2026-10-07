// src/services/portfolio/PortfolioLedger.cpp
#include "services/portfolio/PortfolioLedger.h"

#include "services/portfolio/PortfolioDates.h"

#include <algorithm>

namespace fincept::portfolio {

namespace {

constexpr double kQtyEpsilon = 1e-9;

// Chronological by LOCAL trade date first (the calendar NAV snapshots and
// the return math use — PortfolioDates.h), then by the raw stamp, recording
// order and id. Returns the local date of each row in the sorted order so
// the cursor never re-parses a timestamp.
QStringList sort_chronologically(QVector<Transaction>& txns) {
    QVector<QPair<QString, int>> keyed;
    keyed.reserve(txns.size());
    for (int i = 0; i < txns.size(); ++i)
        keyed.append({transaction_local_date(txns[i]), i});
    std::sort(keyed.begin(), keyed.end(), [&txns](const auto& ka, const auto& kb) {
        if (ka.first != kb.first)
            return ka.first < kb.first;
        const Transaction& a = txns[ka.second];
        const Transaction& b = txns[kb.second];
        if (a.transaction_date != b.transaction_date)
            return a.transaction_date < b.transaction_date;
        if (a.created_at != b.created_at)
            return a.created_at < b.created_at;
        return a.id < b.id;
    });
    QVector<Transaction> sorted;
    QStringList dates;
    sorted.reserve(txns.size());
    dates.reserve(txns.size());
    for (const auto& k : keyed) {
        sorted.append(txns[k.second]);
        dates.append(k.first);
    }
    txns = std::move(sorted);
    return dates;
}

// The single step both replay paths share — one convention, one place.
void apply_transaction(LedgerPosition& pos, const Transaction& t, const QString& local_date) {
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
        const double realized = sold * (t.price - pos.avg_cost);
        pos.realized_pnl += realized;
        if (realized != 0.0)
            pos.realized_events.append({local_date, realized});
        pos.quantity -= sold;
        if (pos.quantity <= kQtyEpsilon) {
            // Fully closed; avg_cost is stale until the next BUY resets it.
            // The entry anchor resets too: a re-opened position's "peak
            // since entry" must not reach back into the previous holding
            // period's highs and report a drawdown the new position never had.
            pos.quantity = 0;
            pos.first_buy_date.clear();
        }
    } else if (t.transaction_type == QLatin1String("DIVIDEND")) {
        const double income = t.quantity * t.price;
        pos.dividend_income += income;
        if (income != 0.0)
            pos.dividend_events.append({local_date, income});
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
    const QStringList dates = sort_chronologically(txns);
    LedgerPosition pos;
    for (int i = 0; i < txns.size(); ++i)
        apply_transaction(pos, txns[i], dates[i]);
    return pos;
}

LedgerCursor::LedgerCursor(QVector<Transaction> txns) : txns_(std::move(txns)) {
    local_dates_ = sort_chronologically(txns_);
}

void LedgerCursor::advance_to(const QString& date) {
    // Local trade date, the same calendar the NAV dates and the return
    // math's flows use — a UTC evening stamp must not enter the NAV a day
    // after the flow that strips it.
    while (next_ < txns_.size() && local_dates_[next_] <= date) {
        apply_transaction(pos_, txns_[next_], local_dates_[next_]);
        ++next_;
    }
}

} // namespace fincept::portfolio

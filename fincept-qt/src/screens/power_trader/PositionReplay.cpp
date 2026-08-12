// src/screens/power_trader/PositionReplay.cpp
#include "screens/power_trader/PositionReplay.h"

namespace fincept::power_trader {

QHash<QString, ReplayPosition> replay_positions(const QVector<ReplayTrade>& trades,
                                                QVector<double>* cost_after) {
    QHash<QString, ReplayPosition> out;
    if (cost_after) {
        cost_after->clear();
        cost_after->reserve(trades.size());
    }
    const auto total_cost = [&out]() {
        double t = 0;
        for (const auto& p : out)
            t += p.cost_basis;
        return t;
    };

    for (const auto& t : trades) {
        if (t.ticker.isEmpty()) {
            if (cost_after) cost_after->append(total_cost());
            continue;
        }
        auto& p = out[t.ticker];

        // No real close on the entry date: we cannot convert the disclosed
        // dollar band into shares, so the position is flagged and left
        // unvalued rather than filled in with a guess.
        if (t.close <= 0.0) {
            p.priced_gap = true;
            // The dollar amounts are still real and still belong in the cost
            // basis — only the share count is unknowable.
            if (t.direction == TradeDirection::Buy) {
                p.cost_basis += t.amount_midpoint;
                p.buy_count++;
            } else if (t.direction == TradeDirection::Sell) {
                p.cost_basis = qMax(0.0, p.cost_basis - t.amount_midpoint);
                p.sell_count++;
            }
            if (cost_after) cost_after->append(total_cost());
            continue;
        }

        if (t.direction == TradeDirection::Buy) {
            p.shares     += t.amount_midpoint / t.close;
            p.cost_basis += t.amount_midpoint;
            p.buy_count++;
        } else if (t.direction == TradeDirection::Sell) {
            const double avg = p.shares > 0.0 ? p.cost_basis / p.shares : 0.0;
            // Cap at the shares actually held. A member can disclose a sale of
            // a lot bought before our window, and letting that drive the share
            // count negative is what silently unpriced whole positions.
            double sold = t.amount_midpoint / t.close;
            if (sold > p.shares)
                sold = p.shares;

            p.realized_pnl  += sold * t.close - sold * avg;
            p.realized_cost += sold * avg;
            p.cost_basis    -= sold * avg;
            p.shares       -= sold;
            if (p.cost_basis < 0.0)
                p.cost_basis = 0.0;
            p.sell_count++;
        }
        if (cost_after)
            cost_after->append(total_cost());
    }
    return out;
}

} // namespace fincept::power_trader

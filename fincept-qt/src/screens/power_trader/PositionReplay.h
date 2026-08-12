// src/screens/power_trader/PositionReplay.h
#pragma once

#include "screens/power_trader/PowerTraderTypes.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace fincept::power_trader {

/// One disclosed trade, already resolved to an entry date and the real close
/// on that date. `close <= 0` means no real close was available.
struct ReplayTrade {
    QString        ticker;
    TradeDirection direction = TradeDirection::Buy;
    /// Midpoint of the disclosed amount range. Congress discloses bands
    /// ($15,001–$50,000), never an exact figure.
    double         amount_midpoint = 0;
    /// Close on the ENTRY date — trade date or disclosure date depending on
    /// the basis the caller chose. Resolved by the caller because it needs the
    /// price history; this function stays pure.
    double         close = 0;
};

/// Reconstructed position for one ticker.
struct ReplayPosition {
    double shares       = 0;   ///< net shares still held
    double cost_basis   = 0;   ///< average-cost basis of those shares
    double realized_pnl = 0;   ///< booked on shares already sold
    int    buy_count    = 0;
    int    sell_count   = 0;
    /// Some trade in this ticker had no real close, so the position cannot be
    /// fully valued and must render as unknown rather than be guessed at.
    bool   priced_gap   = false;
};

/// Replay disclosed trades into positions, average-cost.
///
/// Extracted from PowerTraderService so it can be tested: the service version
/// needed the whole singleton plus a populated price history, so the arithmetic
/// that every return on the screen depends on had no coverage at all.
///
/// The convention that matters: a SELL removes shares at the position's
/// AVERAGE COST and books the difference against the proceeds as realized P&L.
/// The previous rule reduced cost basis by the sale proceeds instead, so a
/// position bought at $10k and sold at $30k left a $0 residual and vanished
/// from the leaderboard — and because winners are the positions most likely to
/// be closed, that systematically dropped the trades that had worked.
///
/// Trades must be passed in entry-date order; the caller sorts by whichever
/// basis it is pricing on.
QHash<QString, ReplayPosition> replay_positions(const QVector<ReplayTrade>& trades);

} // namespace fincept::power_trader

#pragma once
// Crypto Trading types — UI state, enums, constants
// Kept separate from trading engine types to avoid coupling.

#include "ui/theme/ThemeManager.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace finterm::screens::crypto {

// Order book view mode
enum class ObViewMode { Book, Volume, Imbalance, Signals };

// Tick snapshot for order book analytics
struct TickSnapshot {
    int64_t timestamp = 0;
    double best_bid = 0.0;
    double best_ask = 0.0;
    double imbalance = 0.0;
    double rise_ratio_60 = 0.0;
};

// Trade entry for Time & Sales feed
struct TradeEntry {
    QString id;
    QString side; // "buy" | "sell"
    double price = 0.0;
    double amount = 0.0;
    int64_t timestamp = 0;
};

// Market info data (funding rate, OI, fees)
struct MarketInfoData {
    double funding_rate = 0.0;
    double mark_price = 0.0;
    double index_price = 0.0;
    double open_interest = 0.0;
    double open_interest_value = 0.0;
    double maker_fee = 0.0;
    double taker_fee = 0.0;
    int64_t next_funding_time = 0;
    bool has_data = false;
};

// Trading mode
enum class TradingMode { Paper, Live };

// ── Constants ────────────────────────────────────────────────────────────────
inline constexpr int OHLCV_FETCH_COUNT = 200;
inline constexpr double DEFAULT_PAPER_BALANCE = 100000.0;
inline constexpr int OB_MAX_DISPLAY_LEVELS = 12;

// OB analytics thresholds
inline constexpr double OB_IMBALANCE_BUY_THRESHOLD = 0.30;
inline constexpr double OB_IMBALANCE_SELL_THRESHOLD = -0.30;
inline constexpr int MAX_TIME_SALES_TRADES = 200;
inline constexpr int CLOCK_UPDATE_MS = 1000;

// Design System Colors — live from ThemeManager
inline QColor COLOR_BUY() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().positive);
}
inline QColor COLOR_SELL() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().negative);
}

inline QColor BG_BASE() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().bg_base);
}
inline QColor BG_SURFACE() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().bg_surface);
}
inline QColor BG_RAISED() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().bg_raised);
}
inline QColor BG_HOVER() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().bg_hover);
}

inline QColor BORDER_DIM() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().border_dim);
}
inline QColor BORDER_MED() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().border_med);
}
inline QColor BORDER_BRIGHT() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().border_bright);
}

inline QColor TEXT_PRIMARY() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().text_primary);
}
inline QColor TEXT_SECONDARY() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().text_secondary);
}
inline QColor TEXT_TERTIARY() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().text_tertiary);
}
inline QColor TEXT_DIM() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().text_dim);
}

inline QColor ROW_ALT() {
    return QColor(finterm::ui::ThemeManager::instance().tokens().row_alt);
}

} // namespace finterm::screens::crypto

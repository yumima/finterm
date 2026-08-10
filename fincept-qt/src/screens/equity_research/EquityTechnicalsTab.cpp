// src/screens/equity_research/EquityTechnicalsTab.cpp
#include "screens/equity_research/EquityTechnicalsTab.h"

#include "services/equity/EquityResearchService.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

// ── Accent colors without a theme token ──────────────────────────────────────
static constexpr const char* LTGREEN = "#4ade80";
static constexpr const char* LTRED = "#f87171";
static constexpr const char* CYAN = "#22d3ee";
static constexpr const char* YELLOW = "#eab308";
static constexpr const char* BLUE = "#60a5fa";
static constexpr const char* GRAY = "#6b7280";

// ── Signal helpers ───────────────────────────────────────────────────────────

/// The verdict vocabulary.
///
/// Deliberately the conventional one. It briefly read STRONG UPTREND …
/// STRONG DOWNTREND, on the argument that the panel measurably describes the
/// past and measurably does not predict the future — currently 96% / 53%
/// under the trend-structure verdict; the accuracy label under the gauge is
/// the authoritative pair. The argument stands — but BUY/SELL is what every reader
/// already scans for, and a label nobody parses at a glance is its own kind of
/// wrong. The honesty moved to where it does not cost legibility: the line
/// under the gauge, its tooltip, and the MCP tool description all carry both
/// accuracies.
QString EquityTechnicalsTab::signal_text(services::equity::TechSignal s) {
    using S = services::equity::TechSignal;
    switch (s) {
        case S::StrongBuy:
            return "STRONG BUY";
        case S::Buy:
            return "BUY";
        case S::Sell:
            return "SELL";
        case S::StrongSell:
            return "STRONG SELL";
        default:
            return "NEUTRAL";
    }
}

/// Why a row is shown but not counted.
///
/// Every one of these would otherwise let a single indicator vote twice, or
/// contribute a Neutral it never actually expressed — both of which quietly
/// drag the composite toward the middle. Saying so on hover is cheaper than
/// leaving the user to wonder why the badge column and the tally disagree.
QString EquityTechnicalsTab::non_voting_note(const QString& col_key) {
    if (col_key == "macd_signal")
        return "Shown for reference. The MACD row already carries this pair's vote — "
               "it scores the gap between the two lines.";
    if (col_key == "stoch_d")
        return "Shown for reference. The Stoch %K row already carries this pair's vote — "
               "it scores %K against %D.";
    if (col_key == "aroon_down")
        return "Shown for reference. The Aroon Up row already carries this pair's vote — "
               "it scores Up against Down.";
    if (col_key == "bb_mavg")
        return "Shown for reference. This is the 20-period SMA under another name, and the "
               "SMA 20 row already votes on it.";
    if (col_key == "atr" || col_key == "bb_wband")
        return "Shown for reference. This measures how far price moves, not which way, so it "
               "has no bullish or bearish reading to contribute.";
    if (col_key == "bb_hband" || col_key == "bb_lband")
        return "Shown for reference. This is a price level, not a signal — the BB %B row scores "
               "where price sits between the bands.";
    return "Shown for reference — not counted toward the rating.";
}

const char* EquityTechnicalsTab::signal_color(services::equity::TechSignal s) {
    using S = services::equity::TechSignal;
    switch (s) {
        case S::StrongBuy:
            return ui::colors::POSITIVE;
        case S::Buy:
            return LTGREEN;
        case S::Sell:
            return LTRED;
        case S::StrongSell:
            return ui::colors::NEGATIVE;
        default:
            return GRAY;
    }
}

/// Context-aware interpretation for each indicator
QString EquityTechnicalsTab::interpretation(const QString& col_key, double value) {
    if (col_key == "rsi") {
        if (value <= 25)
            return "Deeply oversold — potential reversal zone";
        if (value <= 30)
            return "Oversold — watch for bullish divergence";
        if (value <= 40)
            return "Below midpoint — bearish bias weakening";
        if (value <= 60)
            return "Neutral zone — no strong momentum";
        if (value <= 70)
            return "Above midpoint — bullish bias building";
        if (value <= 80)
            return "Overbought — watch for bearish divergence";
        return "Deeply overbought — potential reversal zone";
    }
    if (col_key == "macd") {
        // Only the MACD line's own value is available here, which says where it
        // sits against zero — not against its signal line. The old text claimed
        // the crossover ("above signal line") off the sign of this number
        // alone, and read a bearish cross as bullish whenever both lines were
        // above zero. The crossover is scored in TechnicalRating, which has
        // both lines; the SIGNAL column carries it.
        if (value > 0)
            return "Above zero — 12-day EMA is above the 26-day";
        if (value < 0)
            return "Below zero — 12-day EMA is below the 26-day";
        return "At zero — the two EMAs have converged";
    }
    if (col_key == "stoch_k" || col_key == "stoch_d") {
        if (value <= 20)
            return "Oversold zone — potential bounce";
        if (value <= 40)
            return "Below midpoint — watch for crossover";
        if (value <= 60)
            return "Neutral — consolidation phase";
        if (value <= 80)
            return "Above midpoint — bullish momentum";
        return "Overbought zone — potential pullback";
    }
    if (col_key == "williams_r") {
        if (value <= -80)
            return "Oversold — potential reversal up";
        if (value <= -50)
            return "Bearish territory — selling pressure";
        if (value >= -20)
            return "Overbought — potential reversal down";
        return "Neutral zone";
    }
    if (col_key == "cci") {
        if (value <= -200)
            return "Extreme oversold — deep value territory";
        if (value <= -100)
            return "Oversold — watch for trend reversal";
        if (value >= 200)
            return "Extreme overbought — euphoria zone";
        if (value >= 100)
            return "Overbought — watch for profit taking";
        return "Neutral — no extreme conditions";
    }
    if (col_key == "mfi") {
        if (value <= 20)
            return "Oversold — money flowing out aggressively";
        if (value <= 40)
            return "Weak inflow — cautious sentiment";
        if (value >= 80)
            return "Overbought — heavy money inflow";
        if (value >= 60)
            return "Strong inflow — buying pressure";
        return "Balanced money flow";
    }
    if (col_key == "adx") {
        if (value >= 50)
            return "Very strong trend — trade with the trend";
        if (value >= 25)
            return "Trending — directional move in play";
        if (value >= 20)
            return "Weak trend — possible consolidation";
        return "No trend — range-bound market";
    }
    if (col_key == "bb_pband") {
        if (value < 0)
            return "Below lower band — extreme oversold";
        if (value < 0.2)
            return "Near lower band — oversold zone";
        if (value > 1.0)
            return "Above upper band — extreme overbought";
        if (value > 0.8)
            return "Near upper band — overbought zone";
        return "Within bands — normal range";
    }
    if (col_key == "bb_wband") {
        if (value < 0.05)
            return "Tight squeeze — breakout imminent";
        if (value < 0.1)
            return "Narrowing bands — volatility contracting";
        if (value > 0.3)
            return "Wide bands — high volatility";
        return "Normal bandwidth";
    }
    if (col_key == "atr")
        return "Average true range — use for stop-loss sizing";
    if (col_key == "roc") {
        if (value > 5)
            return "Strong upward momentum";
        if (value < -5)
            return "Strong downward momentum";
        return "Flat momentum — sideways movement";
    }
    if (col_key == "cmf") {
        if (value > 0.1)
            return "Buying pressure — accumulation";
        if (value < -0.1)
            return "Selling pressure — distribution";
        return "Balanced — no clear accumulation or distribution";
    }
    if (col_key == "aroon_up") {
        if (value >= 70)
            return "Strong uptrend — recent new highs";
        if (value <= 30)
            return "Weak upside — no recent highs";
        return "Moderate — watching for trend development";
    }
    if (col_key == "aroon_down") {
        if (value >= 70)
            return "Strong downtrend — recent new lows";
        if (value <= 30)
            return "Weak downside — no recent lows";
        return "Moderate — watching for trend development";
    }
    if (col_key == "obv")
        return "On-balance volume — confirms price trend with volume";
    if (col_key == "vwap")
        return "Rolling volume-weighted avg price — price above = bullish";
    if (col_key == "adi")
        return "Accumulation/distribution — confirms money flow";
    if (col_key == "ichimoku_base")
        return "Midpoint of the 26-period range — the equilibrium price";
    if (col_key.startsWith("sma_") || col_key.startsWith("ema_") || col_key.startsWith("wma_") || col_key == "kama")
        return "Moving average — price above = bullish, below = bearish";
    if (col_key == "macd_signal")
        return "Signal line — crossover with MACD triggers trade";
    if (col_key == "bb_mavg")
        return "Bollinger midline (20-SMA) — dynamic support/resistance";
    if (col_key == "bb_hband")
        return "Upper band — resistance level, overbought above";
    if (col_key == "bb_lband")
        return "Lower band — support level, oversold below";
    if (col_key == "ao")
        return value > 0 ? "Bullish — momentum above zero line" : "Bearish — momentum below zero line";
    return "";
}

/// Display name → the snake_case column key used by interpretation() and
/// indicator_help(). The generic rule (lowercase, spaces→underscores, drop %)
/// covers most names; the Bollinger/AO names need explicit mapping.
QString EquityTechnicalsTab::col_key_for(const QString& name) {
    if (name == "BB %B")
        return "bb_pband";
    if (name == "BB Width")
        return "bb_wband";
    if (name == "BB Mid")
        return "bb_mavg";
    if (name == "BB Upper")
        return "bb_hband";
    if (name == "BB Lower")
        return "bb_lband";
    if (name == "Awesome Osc")
        return "ao";
    // Displayed as "Rolling VWAP" because it is `ta`'s rolling volume-weighted
    // price, not the session VWAP the bare label implied.
    if (name == "Rolling VWAP")
        return "vwap";
    QString key = name.toLower();
    key.replace(QLatin1Char(' '), QLatin1Char('_'));
    key.remove(QLatin1Char('%'));
    return key;
}

/// What the indicator IS and how to read it — shown as a hover tooltip.
/// Complements interpretation(), which reads the current value.
QString EquityTechnicalsTab::indicator_help(const QString& col_key) {
    if (col_key == "rsi")
        return "Relative Strength Index (RSI)\n\n"
               "Momentum oscillator on a 0\xe2\x80\x93""100 scale comparing the size of recent gains to "
               "recent losses (typically over 14 periods). Above 70 is considered overbought, "
               "below 30 oversold. Divergence between RSI and price often precedes a reversal.";
    if (col_key == "macd")
        return "Moving Average Convergence Divergence (MACD)\n\n"
               "The gap between the 12- and 26-period exponential moving averages. Above zero, "
               "short-term momentum is bullish; below zero, bearish. Crossing its 9-period "
               "signal line is the classic trade trigger.";
    if (col_key == "macd_signal")
        return "MACD Signal Line\n\n"
               "A 9-period EMA of the MACD line that smooths it out. MACD crossing above the "
               "signal line is a bullish trigger; crossing below is bearish.";
    if (col_key == "stoch_k")
        return "Stochastic Oscillator %K\n\n"
               "Where the latest close sits within the recent high\xe2\x80\x93low range, 0\xe2\x80\x93""100. "
               "Above 80 is overbought, below 20 oversold. %K is the fast line; its crossover "
               "with the slower %D line is the trade trigger.";
    if (col_key == "stoch_d")
        return "Stochastic Oscillator %D\n\n"
               "A 3-period average of %K \xe2\x80\x94 the slow, smoother stochastic line. %K crossing "
               "above %D in oversold territory is a classic bullish trigger; crossing below in "
               "overbought territory, a bearish trigger.";
    if (col_key == "williams_r")
        return "Williams %R\n\n"
               "Where the close sits within the recent high\xe2\x80\x93low range, on an inverted 0 to "
               "\xe2\x88\x92""100 scale. Above \xe2\x88\x92""20 is overbought, below \xe2\x88\x92""80 oversold. Effectively a "
               "fast stochastic with a flipped axis.";
    if (col_key == "cci")
        return "Commodity Channel Index (CCI)\n\n"
               "How far price has stretched from its statistical average. Above +100 signals an "
               "unusually strong up-move (overbought), below \xe2\x88\x92""100 an unusually weak one "
               "(oversold); \xc2\xb1""200 marks extremes.";
    if (col_key == "adx")
        return "Average Directional Index (ADX)\n\n"
               "Trend strength, not direction, on a 0\xe2\x80\x93""100 scale. Below 20 the market is "
               "range-bound; above 25 a trend is in force; above 50 it is very strong. Use it "
               "to judge whether trend-following signals are trustworthy.";
    if (col_key == "aroon_up")
        return "Aroon Up\n\n"
               "How recently the period's highest high was set, 0\xe2\x80\x93""100. Above 70 means new "
               "highs keep being made (strong uptrend); below 30 the upside is stale.";
    if (col_key == "aroon_down")
        return "Aroon Down\n\n"
               "How recently the period's lowest low was set, 0\xe2\x80\x93""100. Above 70 means new lows "
               "keep being made (strong downtrend); below 30 the downside is stale.";
    if (col_key == "mfi")
        return "Money Flow Index (MFI)\n\n"
               "A volume-weighted RSI, 0\xe2\x80\x93""100: tracks whether money is flowing into or out of "
               "the stock. Above 80 is overbought, below 20 oversold; divergence from price "
               "warns the move lacks real flow behind it.";
    if (col_key == "roc")
        return "Rate of Change (ROC)\n\n"
               "Percentage change in price over the lookback period \xe2\x80\x94 raw momentum. Positive "
               "and rising means upside momentum is building; negative and falling, downside.";
    if (col_key == "ao")
        return "Awesome Oscillator (AO)\n\n"
               "Difference between the 5- and 34-period midpoint moving averages. Above zero, "
               "short-term momentum leads long-term (bullish); zero-line crossings and "
               "twin-peak patterns are its trade signals.";
    if (col_key == "kama")
        return "Kaufman Adaptive Moving Average (KAMA)\n\n"
               "A moving average that speeds up when the market trends and slows down in choppy "
               "ranges, filtering whipsaws. Price above a rising KAMA supports an uptrend.";
    if (col_key == "atr")
        return "Average True Range (ATR)\n\n"
               "The average size of a bar's full trading range, in price units \xe2\x80\x94 a pure "
               "volatility gauge with no direction. Commonly used to size stop-losses "
               "(e.g. 2\xc3\x97""ATR from entry) and positions.";
    if (col_key == "bb_mavg")
        return "Bollinger Band Midline\n\n"
               "The 20-period simple moving average at the center of the Bollinger Bands, with "
               "the bands two standard deviations either side. Acts as the trend baseline and "
               "a dynamic support/resistance level.";
    if (col_key == "bb_hband")
        return "Bollinger Upper Band\n\n"
               "Two standard deviations above the 20-period average. Price tagging or riding "
               "the upper band signals a strong \xe2\x80\x94 possibly stretched \xe2\x80\x94 up-move; in ranges "
               "it acts as resistance.";
    if (col_key == "bb_lband")
        return "Bollinger Lower Band\n\n"
               "Two standard deviations below the 20-period average. Price tagging or riding "
               "the lower band signals a strong \xe2\x80\x94 possibly stretched \xe2\x80\x94 down-move; in "
               "ranges it acts as support.";
    if (col_key == "bb_pband")
        return "Bollinger %B\n\n"
               "Where price sits within the Bollinger Bands: 1 = at the upper band, 0 = at the "
               "lower, 0.5 = at the midline. Readings outside 0\xe2\x80\x93""1 mean price closed beyond "
               "the bands \xe2\x80\x94 a statistically stretched move.";
    if (col_key == "bb_wband")
        return "Bollinger Band Width\n\n"
               "The distance between the bands relative to the midline \xe2\x80\x94 a direct read of "
               "volatility. A tight squeeze often precedes a sharp breakout; unusually wide "
               "bands mark volatility that tends to fade.";
    if (col_key == "obv")
        return "On-Balance Volume (OBV)\n\n"
               "Running total that adds the day's volume on up closes and subtracts it on down "
               "closes. Rising OBV confirms an uptrend has volume behind it; OBV falling while "
               "price rises warns the rally is thin.";
    if (col_key == "vwap")
        return "Rolling VWAP (14-period)\n\n"
               "The average traded price weighted by volume over the last 14 bars. This is a "
               "rolling average, not the single-session VWAP institutions benchmark execution "
               "against \xe2\x80\x94 it does not reset each day. Price above it means buyers have "
               "been in control over that window; below, sellers.";
    if (col_key == "cmf")
        return "Chaikin Money Flow (CMF)\n\n"
               "Volume-weighted buying versus selling pressure over the period, from \xe2\x88\x92""1 to "
               "+1. Above +0.1 indicates accumulation; below \xe2\x88\x92""0.1 distribution; near zero, "
               "balance.";
    if (col_key == "adi")
        return "Accumulation/Distribution Index (ADI)\n\n"
               "Cumulative volume weighted by where each close lands within its bar's range. "
               "Rising ADI shows accumulation confirming the trend; divergence from price "
               "warns of a hidden shift in flow.";
    if (col_key.startsWith("sma_"))
        return "Simple Moving Average (SMA)\n\n"
               "The arithmetic average close over the period \xe2\x80\x94 the plain trend baseline. "
               "Price above a rising SMA keeps the uptrend intact; short/long SMA crossovers "
               "(e.g. the golden cross) mark trend changes.";
    if (col_key.startsWith("ema_"))
        return "Exponential Moving Average (EMA)\n\n"
               "A moving average that weights recent prices more heavily, so it turns faster "
               "than an SMA. Price above a rising EMA supports the uptrend; crossovers signal "
               "shifts earlier, at the cost of more whipsaws.";
    if (col_key == "ichimoku_base")
        return "Ichimoku Base Line (Kijun-sen)\n\n"
               "The midpoint of the highest high and lowest low over the last 26 periods \xe2\x80\x94 "
               "not an average of closes but the centre of the recent range, which makes it a "
               "slower, steadier reference than a moving average of the same length. Price "
               "holding above it is bullish; losing it often marks the point a trend stalls.";
    if (col_key.startsWith("wma_"))
        return "Weighted Moving Average (WMA)\n\n"
               "A moving average with linearly increasing weight on the most recent prices \xe2\x80\x94 "
               "responsiveness between an SMA and an EMA. Price above a rising WMA is bullish.";
    return "";
}

// ── Constructor ──────────────────────────────────────────────────────────────

EquityTechnicalsTab::EquityTechnicalsTab(QWidget* parent) : QWidget(parent) {
    build_ui();
    // No service-signal connects — subscriptions bind per-(symbol, period)
    // in set_symbol() / switch_period() via QueryStore. The error path is
    // delivered alongside data on the same callback (State.error).
}

/// Point the subscription at the current (symbol, period, interval).
///
/// Dropping the old key before binding the new one matters: without it a late
/// resolve for the previous combination still delivers and overwrites panels
/// the user has already moved away from.
void EquityTechnicalsTab::rebind() {
    if (current_symbol_.isEmpty())
        return;
    loading_overlay_->show_loading("COMPUTING INDICATORS\xe2\x80\xa6");

    auto& store = services::query::QueryStore::instance();
    if (!current_technicals_key_.isEmpty())
        store.unsubscribe(this, current_technicals_key_);
    auto& svc = services::equity::EquityResearchService::instance();
    svc.subscribe_technicals(
        this, current_symbol_, current_period_, current_interval_,
        [this](const services::query::QueryStore::State& s) { apply_technicals_state(s); });
    current_technicals_key_ = services::equity::EquityResearchService::technicals_key(
        current_symbol_, current_period_, current_interval_);
}

void EquityTechnicalsTab::refresh() {
    if (current_symbol_.isEmpty())
        return;
    services::equity::EquityResearchService::instance().fetch_technicals(
        current_symbol_, current_period_, current_interval_);
}

void EquityTechnicalsTab::set_symbol(const QString& symbol) {
    if (symbol == current_symbol_)
        return;
    current_symbol_ = symbol;
    rebind();
}

void EquityTechnicalsTab::switch_period(QPushButton* btn, const QString& period) {
    if (period == current_period_ || current_symbol_.isEmpty())
        return;
    current_period_ = period;
    if (active_period_btn_)
        active_period_btn_->setStyleSheet(period_btn_style_inactive());
    btn->setStyleSheet(period_btn_style_active());
    active_period_btn_ = btn;
    rebind();
}

void EquityTechnicalsTab::switch_interval(QPushButton* btn, const QString& interval) {
    if (interval == current_interval_ || current_symbol_.isEmpty())
        return;
    current_interval_ = interval;
    if (active_interval_btn_)
        active_interval_btn_->setStyleSheet(period_btn_style_inactive());
    btn->setStyleSheet(period_btn_style_active());
    active_interval_btn_ = btn;
    rebind();
}

// static
QString EquityTechnicalsTab::period_btn_style_active() {
    return QString("QPushButton{background:%1;color:%2;border:1px solid %1;"
                   "border-radius:2px;padding:3px 10px;font-size:12px;font-weight:700;font-family:'Consolas',monospace;}")
        .arg(ui::colors::AMBER(), ui::colors::BG_BASE());
}

// static
QString EquityTechnicalsTab::period_btn_style_inactive() {
    return QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                   "border-radius:2px;padding:3px 10px;font-size:12px;font-weight:700;font-family:'Consolas',monospace;}"
                   "QPushButton:hover{border-color:%3;background:%4;}")
        .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER(), ui::colors::BG_RAISED());
}

// ── build_ui ─────────────────────────────────────────────────────────────────

void EquityTechnicalsTab::build_ui() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    loading_overlay_ = new ui::LoadingOverlay(this);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Period selector bar ───────────────────────────────────────────────────
    auto* period_bar = new QWidget;
    period_bar->setStyleSheet(
        QString("background:%1;border-bottom:1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* pb_hl = new QHBoxLayout(period_bar);
    pb_hl->setContentsMargins(12, 6, 12, 6);
    pb_hl->setSpacing(4);

    auto* period_lbl = new QLabel("PERIOD");
    // Be straight about what this control does. It selects how much history is
    // pulled, not the bar size the indicators are built from — and since every
    // reading is taken off the latest daily bar with a fixed lookback, the
    // selections that clear the warm-up floor land on the same verdict.
    period_lbl->setToolTip(
        "How much price history is fetched.\n\n"
        "Indicators are computed from daily bars and always from at least two years of "
        "them, so that the 50- and 200-day averages, MACD and ADX are warmed up and the "
        "rating has something to stand on. Because each indicator reads off the latest "
        "bar with a fixed lookback, the shorter selections produce the same rating.");
    period_lbl->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:600;background:transparent;border:0;")
            .arg(ui::colors::TEXT_SECONDARY()));
    pb_hl->addWidget(period_lbl);

    auto make_btn = [&](const QString& label, QPushButton*& out, const QString& period) {
        out = new QPushButton(label);
        out->setCursor(Qt::PointingHandCursor);
        out->setStyleSheet(period == current_period_ ? period_btn_style_active() : period_btn_style_inactive());
        connect(out, &QPushButton::clicked, this, [this, out, period]() { switch_period(out, period); });
        pb_hl->addWidget(out);
    };

    make_btn("1M", btn_1m_, "1mo");
    make_btn("3M", btn_3m_, "3mo");
    make_btn("6M", btn_6m_, "6mo");
    make_btn("1Y", btn_1y_, "1y");
    make_btn("5Y", btn_5y_, "5y");
    // Derive initial active button from current_period_ — stays correct if default ever changes.
    const auto period_to_btn = [&](const QString& p) -> QPushButton* {
        if (p == "1mo") return btn_1m_;
        if (p == "3mo") return btn_3m_;
        if (p == "6mo") return btn_6m_;
        if (p == "5y")  return btn_5y_;
        return btn_1y_;
    };
    active_period_btn_ = period_to_btn(current_period_);

    // ── Bar interval ─────────────────────────────────────────────────────────
    // Separate from PERIOD because it is the control that actually changes the
    // verdict. Every indicator is recomputed from weekly highs, lows and closes,
    // so the weekly rating describes the multi-month trend where the daily one
    // describes the multi-week one — they disagree often, and that disagreement
    // is the useful part.
    auto* bars_lbl = new QLabel("BARS");
    bars_lbl->setToolTip(
        "Which bars the indicators are built from.\n\n"
        "Daily reads the multi-week trend; weekly re-derives every indicator from weekly "
        "highs, lows and closes and reads the multi-month one. A stock can be neutral on "
        "the daily chart and strongly trending on the weekly, and vice versa \xe2\x80\x94 "
        "the two answer different questions.");
    bars_lbl->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:600;background:transparent;border:0;")
            .arg(ui::colors::TEXT_SECONDARY()));
    pb_hl->addSpacing(14);
    pb_hl->addWidget(bars_lbl);

    auto make_interval_btn = [&](const QString& label, QPushButton*& out, const QString& interval) {
        out = new QPushButton(label);
        out->setCursor(Qt::PointingHandCursor);
        out->setStyleSheet(interval == current_interval_ ? period_btn_style_active()
                                                          : period_btn_style_inactive());
        connect(out, &QPushButton::clicked, this,
                [this, out, interval]() { switch_interval(out, interval); });
        pb_hl->addWidget(out);
    };
    make_interval_btn("1D", btn_daily_, "1d");
    make_interval_btn("1W", btn_weekly_, "1wk");
    active_interval_btn_ = current_interval_ == "1wk" ? btn_weekly_ : btn_daily_;

    pb_hl->addStretch();
    outer->addWidget(period_bar);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background:transparent;border:0;");

    auto* content = new QWidget(this);
    auto* vl = new QVBoxLayout(content);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(8);

    // ── Top row: RATING + KEY INDICATORS ─────────────────────────────────────
    auto* top_row = new QHBoxLayout;
    top_row->setSpacing(8);

    // === RATING PANEL ===
    auto* rating_panel = new QFrame;
    rating_panel->setFixedWidth(260);
    rating_panel->setStyleSheet(QString("QFrame{background:%1;border:1px solid %2;border-radius:2px;}")
                                    .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* rp_vl = new QVBoxLayout(rating_panel);
    rp_vl->setContentsMargins(14, 10, 14, 10);
    rp_vl->setSpacing(10);

    auto* rp_title = new QLabel("TECHNICAL RATING");
    rp_title->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;letter-spacing:1px;background:transparent;border:0;")
            .arg(ui::colors::AMBER()));
    rp_vl->addWidget(rp_title);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QString("background:%1;border:0;").arg(ui::colors::BORDER_DIM()));
    rp_vl->addWidget(sep);

    rating_label_ = new QLabel("\xe2\x80\x94");
    rating_label_->setAlignment(Qt::AlignCenter);
    rating_label_->setStyleSheet(
        QString("color:%1;font-size:22px;font-weight:700;letter-spacing:2px;background:transparent;border:0;")
            .arg(GRAY));
    rp_vl->addWidget(rating_label_);

    // Gauge bar
    gauge_bar_ = new QProgressBar;
    gauge_bar_->setRange(0, 100);
    gauge_bar_->setValue(50);
    gauge_bar_->setFixedHeight(6);
    gauge_bar_->setTextVisible(false);
    gauge_bar_->setStyleSheet(QString("QProgressBar{background:%1;border:1px solid %2;border-radius:0;}"
                                      "QProgressBar::chunk{background:%3;}")
                                  .arg(ui::colors::NEGATIVE(), ui::colors::BORDER_DIM(), ui::colors::POSITIVE()));
    rp_vl->addWidget(gauge_bar_);

    // Signal counts — 5 columns
    auto* counts = new QHBoxLayout;
    counts->setSpacing(4);

    auto make_count = [&](const char* color, const QString& label, QLabel*& out) {
        auto* w = new QWidget(this);
        w->setStyleSheet(QString("background:%1;border:0;").arg(ui::colors::BG_RAISED()));
        auto* cvl = new QVBoxLayout(w);
        cvl->setContentsMargins(4, 4, 4, 4);
        cvl->setSpacing(1);
        cvl->setAlignment(Qt::AlignCenter);
        out = new QLabel("0");
        out->setAlignment(Qt::AlignCenter);
        out->setStyleSheet(
            QString("color:%1;font-size:16px;font-weight:700;background:transparent;border:0;").arg(color));
        auto* lbl = new QLabel(label);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:600;letter-spacing:0.5px;background:transparent;border:0;")
                .arg(ui::colors::TEXT_SECONDARY()));
        cvl->addWidget(out);
        cvl->addWidget(lbl);
        counts->addWidget(w, 1);
    };

    make_count(ui::colors::POSITIVE, "STR.BUY", strong_buy_count_);
    make_count(LTGREEN, "BUY", buy_count_);
    make_count(GRAY, "NEUTRAL", neutral_count_);
    make_count(LTRED, "SELL", sell_count_);
    make_count(ui::colors::NEGATIVE, "STR.SELL", strong_sell_count_);
    rp_vl->addLayout(counts);

    total_label_ = new QLabel("0 INDICATORS");
    total_label_->setAlignment(Qt::AlignCenter);
    total_label_->setStyleSheet(QString("color:%1;font-size:12px;letter-spacing:1px;background:transparent;border:0;")
                                    .arg(ui::colors::TEXT_SECONDARY()));
    rp_vl->addWidget(total_label_);

    // The four bucket scores the verdict was averaged from. A rating the user
    // cannot argue with is a rating they cannot trust.
    basis_label_ = new QLabel;
    basis_label_->setAlignment(Qt::AlignCenter);
    basis_label_->setWordWrap(true);
    basis_label_->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;border:0;").arg(ui::colors::TEXT_SECONDARY()));
    rp_vl->addWidget(basis_label_);

    // What this panel is, stated on the panel. Measured over 178 large caps
    // across twelve years with the shipped scorer: the verdict agrees with the
    // trend already in place 96% of the time, and with the direction of the
    // next 40 days 53%. Both numbers belong in front of the user, because only
    // the first one supports the words above it.
    accuracy_label_ = new QLabel("96% on the trend in place \xe2\x80\xb7 53% on the next 40 days");
    accuracy_label_->setAlignment(Qt::AlignCenter);
    accuracy_label_->setWordWrap(true);
    accuracy_label_->setToolTip(
        "This panel reports the trend that has already happened, and it is good at that: "
        "scored against a 40-day regression label over 178 large caps and twelve years of "
        "daily bars, the verdict agrees with the established trend 96% of the time.\n\n"
        "The verdict comes from how the 10/20/50/100/200-day averages are stacked and how far "
        "price sits from the 50-day, in ATRs \xe2\x80\x94 not from counting the table below. "
        "Averaging ~20 correlated indicator votes scored 80%; these two readings score 96% on "
        "held-out years. The table is shown because the detail is useful, not because the "
        "headline is a tally of it.\n\n"
        "It is not a forecast. The same evidence scored against the direction of the *next* "
        "40 days lands at 53% \xe2\x80\x94 barely off a coin flip \xe2\x80\x94 and its "
        "rank correlation with forward returns is indistinguishable from zero. Read the "
        "verdict as a description of the move already in place, not as a forecast of the "
        "next one.\n\n"
        "Re-run the measurement yourself: python -m factor_rating --describe");
    accuracy_label_->setStyleSheet(
        QString("color:%1;font-size:12px;font-style:italic;background:transparent;border:0;")
            .arg(ui::colors::TEXT_SECONDARY()));
    rp_vl->addWidget(accuracy_label_);

    warning_label_ = new QLabel;
    warning_label_->setAlignment(Qt::AlignCenter);
    warning_label_->setWordWrap(true);
    warning_label_->setVisible(false);
    warning_label_->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:600;background:transparent;border:0;").arg(ui::colors::AMBER()));
    rp_vl->addWidget(warning_label_);
    rp_vl->addStretch();

    top_row->addWidget(rating_panel);

    // === KEY INDICATORS PANEL ===
    auto* key_panel = new QFrame;
    key_panel->setStyleSheet(QString("QFrame{background:%1;border:1px solid %2;border-radius:2px;}")
                                 .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* kp_vl = new QVBoxLayout(key_panel);
    kp_vl->setContentsMargins(10, 10, 10, 10);
    kp_vl->setSpacing(6);

    auto* kp_title = new QLabel("KEY INDICATORS");
    kp_title->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;letter-spacing:1px;background:transparent;border:0;")
            .arg(CYAN));
    kp_vl->addWidget(kp_title);

    auto* ksep = new QFrame;
    ksep->setFrameShape(QFrame::HLine);
    ksep->setFixedHeight(1);
    ksep->setStyleSheet(QString("background:%1;border:0;").arg(ui::colors::BORDER_DIM()));
    kp_vl->addWidget(ksep);

    key_container_ = new QWidget(this);
    key_container_->setStyleSheet("background:transparent;border:0;");
    auto* kc_layout = new QGridLayout(key_container_);
    kc_layout->setContentsMargins(0, 0, 0, 0);
    kc_layout->setSpacing(6);
    kp_vl->addWidget(key_container_, 1);

    top_row->addWidget(key_panel, 1);
    vl->addLayout(top_row);

    // ── Category sections ────────────────────────────────────────────────────
    sections_container_ = new QWidget(this);
    sections_container_->setStyleSheet("background:transparent;border:0;");
    auto* sc_vl = new QVBoxLayout(sections_container_);
    sc_vl->setContentsMargins(0, 0, 0, 0);
    sc_vl->setSpacing(6);
    vl->addWidget(sections_container_);
    vl->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

// ── clear_sections ───────────────────────────────────────────────────────────

void EquityTechnicalsTab::clear_sections() {
    // Clear key indicators grid
    if (auto* gl = qobject_cast<QGridLayout*>(key_container_->layout())) {
        while (gl->count() > 0) {
            auto* item = gl->takeAt(0);
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }
    // Clear category sections
    if (auto* vl = qobject_cast<QVBoxLayout*>(sections_container_->layout())) {
        while (vl->count() > 0) {
            auto* item = vl->takeAt(0);
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }
}

// ── populate ─────────────────────────────────────────────────────────────────

void EquityTechnicalsTab::populate(const services::equity::TechnicalsData& payload) {
    clear_sections();

    // Flatten all
    QVector<services::equity::TechIndicator> all;
    all << payload.trend << payload.momentum << payload.volatility << payload.volume;

    // ── Rating ───────────────────────────────────────────────────────────────
    const int sb = payload.strong_buy, b = payload.buy, n = payload.neutral;
    const int s = payload.sell, ss = payload.strong_sell;

    rating_label_->setText(signal_text(payload.overall_signal));
    rating_label_->setStyleSheet(
        QString("color:%1;font-size:22px;font-weight:700;letter-spacing:2px;background:transparent;border:0;")
            .arg(signal_color(payload.overall_signal)));

    // The gauge is drawn off the same weighted score as the verdict, so the bar
    // and the words cannot disagree. It used to be bulls/total *including*
    // neutrals, which pinned it near the bottom of its range and left it
    // reading bearish next to the word STRONG BUY.
    gauge_bar_->setValue(static_cast<int>(std::lround(50.0 + 50.0 * payload.net_score)));

    strong_buy_count_->setText(QString::number(sb));
    buy_count_->setText(QString::number(b));
    neutral_count_->setText(QString::number(n));
    sell_count_->setText(QString::number(s));
    strong_sell_count_->setText(QString::number(ss));
    // Counts cover the indicators that actually vote; `all` also holds the
    // reference-only rows, and conflating the two overstated the evidence.
    total_label_->setText(QString("%1 OF %2 INDICATORS SCORED")
                              .arg(payload.voting_count)
                              .arg(all.size()));
    basis_label_->setText(payload.rating_basis);
    warning_label_->setText(payload.data_warning);
    warning_label_->setVisible(!payload.data_warning.isEmpty());

    // ── Key indicators — pick the most decision-relevant ones ────────────────
    // These are column keys we look for in the flattened indicators
    static const QStringList key_names = {"RSI", "MACD",  "Stoch %K",    "ADX", "CCI",
                                          "MFI", "BB %B", "Williams %R", "ATR", "CMF"};

    QMap<QString, services::equity::TechIndicator> name_map;
    for (const auto& ti : all)
        name_map[ti.name] = ti;

    auto* kg = qobject_cast<QGridLayout*>(key_container_->layout());
    int ki_row = 0, ki_col = 0;

    for (const auto& key : key_names) {
        auto it = name_map.find(key);
        if (it == name_map.end())
            continue;
        const auto& ti = it.value();

        const QString col_key = col_key_for(ti.name);

        auto* card = new QFrame;
        const char* sc = signal_color(ti.signal);
        const QString help = indicator_help(col_key);
        if (!help.isEmpty())
            card->setToolTip(help);
        card->setStyleSheet(
            QString("QFrame{background:%1;border:1px solid %2;border-left:3px solid %3;border-radius:2px;}")
                .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM(), sc));
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(10, 6, 10, 6);
        cl->setSpacing(2);

        // Name
        auto* nm = new QLabel(ti.name.toUpper());
        nm->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:600;letter-spacing:0.5px;background:transparent;border:0;")
                .arg(ui::colors::TEXT_SECONDARY()));
        cl->addWidget(nm);

        // Value + signal row
        auto* vr = new QHBoxLayout;
        vr->setSpacing(8);
        auto* val = new QLabel(QString::number(ti.value, 'f', 2));
        val->setStyleSheet(QString("color:%1;font-size:16px;font-weight:700;font-family:'Consolas',monospace;"
                                   "background:transparent;border:0;")
                               .arg(ui::colors::TEXT_PRIMARY()));
        auto* sig = new QLabel(ti.votes ? signal_text(ti.signal) : QStringLiteral("REFERENCE"));
        sig->setStyleSheet(QString("color:%1;font-size:12px;font-weight:700;background:transparent;border:0;")
                               .arg(ti.votes ? sc : GRAY));
        sig->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        if (!ti.votes)
            sig->setToolTip(non_voting_note(col_key));
        vr->addWidget(val);
        vr->addStretch();
        vr->addWidget(sig);
        cl->addLayout(vr);

        // Interpretation
        QString interp = interpretation(col_key, ti.value);
        if (!interp.isEmpty()) {
            auto* desc = new QLabel(interp);
            desc->setWordWrap(true);
            desc->setStyleSheet(
                QString("color:%1;font-size:12px;background:transparent;border:0;").arg(ui::colors::TEXT_SECONDARY()));
            cl->addWidget(desc);
        }

        kg->addWidget(card, ki_row, ki_col);
        ki_col++;
        if (ki_col == 5) {
            ki_col = 0;
            ki_row++;
        }
    }

    // ── Category sections ────────────────────────────────────────────────────
    auto* sc_vl = qobject_cast<QVBoxLayout*>(sections_container_->layout());

    struct CatDef {
        QString title;
        const QVector<services::equity::TechIndicator>* inds;
        const char* color;
    };
    QList<CatDef> cats = {
        {"TREND INDICATORS", &payload.trend, CYAN},
        {"MOMENTUM INDICATORS", &payload.momentum, ui::colors::POSITIVE},
        {"VOLATILITY INDICATORS", &payload.volatility, YELLOW},
        {"VOLUME INDICATORS", &payload.volume, BLUE},
    };

    for (const auto& cat : cats) {
        if (!cat.inds || cat.inds->isEmpty())
            continue;

        // Count per category — voting rows only, to match the composite.
        int cb = 0, cs = 0, cn = 0, cvoting = 0;
        for (const auto& ti : *cat.inds) {
            using S = services::equity::TechSignal;
            if (!ti.votes)
                continue;
            cvoting++;
            if (ti.signal == S::StrongBuy || ti.signal == S::Buy)
                cb++;
            else if (ti.signal == S::StrongSell || ti.signal == S::Sell)
                cs++;
            else
                cn++;
        }

        auto* section = new QFrame;
        section->setStyleSheet(QString("QFrame{background:%1;border:1px solid %2;border-radius:2px;}")
                                   .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        auto* svl = new QVBoxLayout(section);
        svl->setContentsMargins(0, 0, 0, 0);
        svl->setSpacing(0);

        // Header
        auto* hdr = new QWidget(this);
        hdr->setStyleSheet(QString("background:%1;border:0;border-bottom:1px solid %2;")
                               .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(12, 8, 12, 8);
        hl->setSpacing(10);

        auto* htitle = new QLabel(cat.title);
        htitle->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;letter-spacing:1px;background:transparent;border:0;")
                .arg(cat.color));
        hl->addWidget(htitle);

        auto* cnt = new QLabel(QString("%1 indicators · %2 scored").arg(cat.inds->size()).arg(cvoting));
        cnt->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;border:0;").arg(ui::colors::TEXT_SECONDARY()));
        hl->addWidget(cnt);
        hl->addStretch();

        auto add_badge = [&](int count, const QString& label, const char* color) {
            if (count == 0)
                return;
            auto* badge = new QLabel(QString("%1 %2").arg(count).arg(label));
            badge->setStyleSheet(
                QString("color:%1;font-size:12px;font-weight:700;padding:2px 6px;background:transparent;border:0;")
                    .arg(color));
            hl->addWidget(badge);
        };
        add_badge(cb, "BUY", ui::colors::POSITIVE);
        add_badge(cn, "HOLD", GRAY);
        add_badge(cs, "SELL", ui::colors::NEGATIVE);

        svl->addWidget(hdr);

        // Table header
        auto* tbl_hdr = new QWidget(this);
        tbl_hdr->setStyleSheet(QString("background:%1;border:0;border-bottom:1px solid %2;")
                                   .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
        auto* thl = new QHBoxLayout(tbl_hdr);
        thl->setContentsMargins(12, 4, 12, 4);
        thl->setSpacing(0);

        auto hdr_lbl = [&](const QString& text, int stretch, Qt::Alignment align = Qt::AlignLeft) {
            auto* l = new QLabel(text);
            l->setAlignment(align | Qt::AlignVCenter);
            l->setStyleSheet(
                QString("color:%1;font-size:12px;font-weight:600;letter-spacing:0.5px;background:transparent;border:0;")
                    .arg(ui::colors::TEXT_SECONDARY()));
            thl->addWidget(l, stretch);
        };
        hdr_lbl("INDICATOR", 2);
        hdr_lbl("VALUE", 1, Qt::AlignRight);
        hdr_lbl("SIGNAL", 1, Qt::AlignCenter);
        hdr_lbl("INTERPRETATION", 3);
        svl->addWidget(tbl_hdr);

        // Rows
        bool alt = false;
        for (const auto& ti : *cat.inds) {
            const char* sc = signal_color(ti.signal);

            auto* row = new QWidget(this);
            row->setStyleSheet(QString("background:%1;border:0;border-bottom:1px solid %2;")
                                   .arg(alt ? ui::colors::BG_RAISED() : ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(12, 5, 12, 5);
            rl->setSpacing(0);

            const QString col_key = col_key_for(ti.name);

            // Name — hover for what the indicator is and how to read it
            auto* name_lbl = new QLabel(ti.name);
            name_lbl->setStyleSheet(QString("color:%1;font-size:12px;font-weight:600;background:transparent;border:0;")
                                        .arg(ui::colors::TEXT_PRIMARY()));
            const QString help = indicator_help(col_key);
            if (!help.isEmpty())
                name_lbl->setToolTip(help);
            rl->addWidget(name_lbl, 2);

            // Value
            auto* val_lbl = new QLabel(QString::number(ti.value, 'f', 4));
            val_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            val_lbl->setStyleSheet(
                QString("color:%1;font-size:12px;font-family:'Consolas',monospace;background:transparent;border:0;")
                    .arg(ui::colors::TEXT_SECONDARY()));
            rl->addWidget(val_lbl, 1);

            // Signal badge
            auto* sig_w = new QWidget(this);
            sig_w->setStyleSheet("background:transparent;border:0;");
            auto* sig_hl = new QHBoxLayout(sig_w);
            sig_hl->setContentsMargins(0, 0, 0, 0);
            sig_hl->setAlignment(Qt::AlignCenter);
            // Reference rows say so instead of showing a verdict they did not
            // cast — a "NEUTRAL" badge on ATR reads as an assessment of ATR.
            auto* sig_lbl = new QLabel(ti.votes ? signal_text(ti.signal) : QStringLiteral("REFERENCE"));
            sig_lbl->setAlignment(Qt::AlignCenter);
            sig_lbl->setFixedWidth(90);
            sig_lbl->setStyleSheet(QString("color:%1;font-size:12px;font-weight:700;background:%2;"
                                           "border-radius:2px;padding:2px 6px;border:0;")
                                       .arg(ti.votes ? sc : GRAY, ui::colors::BG_RAISED()));
            if (!ti.votes)
                sig_lbl->setToolTip(non_voting_note(col_key));
            sig_hl->addWidget(sig_lbl);
            rl->addWidget(sig_w, 1);

            // Interpretation
            QString interp = interpretation(col_key, ti.value);
            auto* interp_lbl = new QLabel(interp.isEmpty() ? "\xe2\x80\x94" : interp);
            interp_lbl->setWordWrap(true);
            interp_lbl->setStyleSheet(
                QString("color:%1;font-size:12px;background:transparent;border:0;").arg(ui::colors::TEXT_SECONDARY()));
            rl->addWidget(interp_lbl, 3);

            svl->addWidget(row);
            alt = !alt;
        }

        sc_vl->addWidget(section);
    }
}

// ── apply_technicals_state ───────────────────────────────────────────────────

void EquityTechnicalsTab::apply_technicals_state(const services::query::QueryStore::State& s) {
    // Error: hide overlay so the user isn't stuck. No populate() on error —
    // the previous panel contents stay until the next successful fetch.
    if (!s.error.isEmpty()) {
        if (loading_overlay_) loading_overlay_->hide_loading();
        return;
    }
    if (!s.data.isValid() || s.data.isNull()) {
        // Still loading — overlay was already shown in set_symbol/switch_period.
        return;
    }
    const auto payload = s.data.value<services::equity::TechnicalsData>();
    if (loading_overlay_) loading_overlay_->hide_loading();
    populate(payload);
}

} // namespace fincept::screens

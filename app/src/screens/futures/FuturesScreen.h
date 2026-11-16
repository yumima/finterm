// src/screens/futures/FuturesScreen.h
#pragma once
#include "screens/IStatefulScreen.h"
#include "services/equity/EquityResearchModels.h"
#include "services/futures/FuturesMath.h"
#include "services/markets/MarketDataService.h"
#include "ui/RenderCoalescer.h"

#include <QHash>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTabWidget;
class QTableWidget;
class QTimer;

namespace finterm::ui {
class StatusPill;
class LoadingOverlay;
} // namespace finterm::ui

namespace finterm::screens {

class ResearchCandleCanvas;

namespace futures {
class CurveChart;
class CotChart;
class SpreadChart;
} // namespace futures

/// FUTURES — one market at a time, the way a futures desk reads it.
///
///   board      every root of the asset class, live (the continuous front
///              month from the shared quote feed); click one to load it
///   strip      the selected market: last, change, which month is the front,
///              the curve's shape and what rolling it costs or earns
///   chart      price history — the continuous series, or any one delivery
///              month picked off the curve (no roll gaps)
///   curve      every listed month now, against the previous close and where
///              the same months settled a week and a month ago
///   months     the contract table behind the curve: last, change, calendar
///              spread to the front, annualised carry, volume, open interest,
///              last trading day
///   spread     six months of the calendar spread between the front month and
///              the one picked: where it is in its own range
///   positioning  the CFTC's weekly Commitments of Traders, three years
///   ticket     size a trade in dollars (notional, tick value, risk, reward)
///              and fill it in the paper account; open positions marked live
///   contract   the terms: size, quote unit, tick and its dollar value
///
/// Quotes are Yahoo's, about ten minutes behind the exchange. Margin is not
/// shown anywhere: there is no feed for it.
class FuturesScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit FuturesScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("futures"); }

  protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

  private:
    void build_ui();
    QWidget* build_board();
    QWidget* build_chart_panel();
    QWidget* build_curve_panel();
    QWidget* build_months_panel();
    QWidget* build_side_tabs();
    void apply_theme();

    void set_group(const QString& group);
    void set_root(const QString& root);
    void subscribe_board();
    void unsubscribe_board();
    void render_board();
    void render_strip();

    void fetch_curve();
    void fetch_curve_history();
    void render_curve();
    void fetch_spread();
    void on_front_tick(const services::StreamTick& tick);
    void fetch_cot();
    void render_cot(const services::futures::CotReport& report);

    void set_chart_symbol(const QString& symbol);
    void load_chart();

    void render_contract_terms();
    void render_ticket();
    void place_order();
    void close_position(const QString& symbol);
    void render_positions();
    void sync_stream();
    QString paper_portfolio_id();
    double last_price(const QString& contract_symbol) const;
    const services::futures::CurveContract* ticket_contract() const;

    const services::futures::ContractSpec* spec() const;
    QString price_text(double v) const;

    QString group_;
    QString root_;
    bool shown_ = false;
    bool board_subscribed_ = false;

    // Board
    QHash<QString, services::QuoteData> quotes_; ///< by continuous symbol
    QTableWidget* board_ = nullptr;
    QButtonGroup* group_buttons_ = nullptr;
    ui::RenderCoalescer board_render_{this, [this]() { render_board(); }};
    ui::RenderCoalescer strip_render_{this, [this]() {
                                          render_strip();
                                          render_curve(); // the table and chart show the same front price as the strip
                                          render_ticket();
                                      }};

    // Selected market
    services::futures::Curve curve_;
    QVector<services::futures::PastCurve> past_;
    QString past_for_; ///< the contract set `past_` was fetched for
    qint64 past_failed_at_ = 0; ///< when the comparison curves were last refused
    int curve_seq_ = 0;
    bool curve_in_flight_ = false;
    QTimer* curve_timer_ = nullptr;

    QLabel* strip_name_ = nullptr;
    QLabel* strip_price_ = nullptr;
    QLabel* strip_change_ = nullptr;
    QLabel* strip_facts_ = nullptr;
    ui::StatusPill* status_ = nullptr;

    // Chart
    ResearchCandleCanvas* canvas_ = nullptr;
    ui::LoadingOverlay* chart_overlay_ = nullptr;
    QLabel* chart_title_ = nullptr;
    QPushButton* continuous_btn_ = nullptr;
    QButtonGroup* period_buttons_ = nullptr;
    QString chart_symbol_; ///< empty = the continuous series
    QString period_ = QStringLiteral("6M");
    int chart_seq_ = 0;

    // Curve + months
    futures::CurveChart* curve_chart_ = nullptr;
    QLabel* curve_title_ = nullptr;
    QTableWidget* months_ = nullptr;

    // Side tabs
    QTabWidget* tabs_ = nullptr;
    futures::CotChart* cot_chart_ = nullptr;
    QLabel* cot_summary_ = nullptr;
    QString cot_for_;
    futures::SpreadChart* spread_chart_ = nullptr;
    QLabel* spread_summary_ = nullptr;
    QString spread_for_; ///< "near|far" the spread view shows or is loading
    bool spread_failed_ = false;
    services::futures::CurveContract front_live_; ///< the front month's newest stream print
    QLabel* terms_ = nullptr;

    // Ticket
    QPushButton* buy_btn_ = nullptr;
    QPushButton* sell_btn_ = nullptr;
    QSpinBox* qty_ = nullptr;
    QLineEdit* stop_ = nullptr;
    QLineEdit* target_ = nullptr;
    QLabel* ticket_contract_ = nullptr;
    QLabel* ticket_math_ = nullptr;
    QLabel* ticket_status_ = nullptr;
    QLabel* account_ = nullptr;
    QPushButton* place_btn_ = nullptr;
    QTableWidget* positions_ = nullptr;
    QString ticket_symbol_; ///< empty = the front month
    QString portfolio_id_;
    QHash<QString, double> marks_; ///< last print seen this session, by contract symbol
};

} // namespace finterm::screens

#pragma once
// FuturesWidgets.h — the futures desk as widgets (the old FUTURES screen,
// taken apart so its pieces sit on the MARKETS FUTURES screen — or any
// screen, or the dashboard):
//
//   FuturesBoardWidget   the board: group tabs (MAJORS · INDEX · RATES · ENERGY
//                        · METALS · GRAINS · SOFTS · LIVESTOCK · FX · CRYPTO),
//                        every root's continuous front month, live
//   FuturesCurveWidget   the market's strip (front month, shape, roll yield /
//                        implied rate, last day, open interest) and its
//                        forward curve now vs prev close, 1W and 1M ago
//   FuturesMonthsWidget  the contract table behind the curve
//   FuturesDetailWidget  tabs: SPREAD (calendar spread to the picked month) ·
//                        POSITIONING (CFTC COT) · TICKET (paper account, stops
//                        and targets) · CONTRACT (the terms)
//
// They follow the canvas's selection (SelectionLink): a board row selects its
// continuous contract, a month on the curve or table selects that delivery
// month — the screen's chart draws it, the others re-root to its market. A
// non-futures symbol (an index) selects its future when it has one.
// Data: FuturesHub's fut:* topics — one fetch however many widgets show it.
// Config: {"root": "CL"} (the board: {"group": "MAJORS"}).

#include "screens/dashboard/widgets/BaseWidget.h"
#include "services/futures/FuturesCatalog.h"
#include "services/futures/FuturesMath.h"
#include "services/markets/MarketDataService.h"
#include "trading/TradingTypes.h"
#include "ui/RenderCoalescer.h"

#include <QHash>
#include <QJsonObject>
#include <QPointer>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

namespace finterm::screens {
class SelectionLink;
namespace futures {
class CurveChart;
class SpreadChart;
class CotChart;
} // namespace futures
} // namespace finterm::screens

namespace finterm::screens::widgets {

/// The futures widgets' common part: a root, its curve (fut:curve:<root>),
/// the month picked, and following the canvas's selection.
class FuturesWidgetBase : public BaseWidget {
    Q_OBJECT
  public:
    FuturesWidgetBase(const QString& title, const QJsonObject& cfg, QWidget* parent);
    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void on_theme_changed() override;

    const services::futures::ContractSpec* spec() const;
    int price_dp() const;
    /// Select a delivery month on the canvas (the chart draws it).
    void pick_contract(const QString& contract_symbol);
    virtual void root_changed() {}
    virtual void curve_changed() {}
    virtual void contract_changed() {}
    /// Topics beyond the curve this widget follows while shown.
    virtual void subscribe_more() {}

    QString root_ = QStringLiteral("CL");
    QString contract_; ///< the month picked ("" = the front)
    services::futures::Curve curve_;
    QVector<services::futures::PastCurve> past_;
    QString curve_error_;
    bool subscribed_ = false;
    bool wants_past_ = false;

  private:
    void set_root(const QString& root);
    void subscribe();
    void unsubscribe();
    void on_selected(const QString& symbol);
    void retitle();
    QString base_title_;
    QPointer<SelectionLink> link_;
};

class FuturesCurveWidget : public FuturesWidgetBase {
    Q_OBJECT
  public:
    explicit FuturesCurveWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);

  protected:
    void root_changed() override;
    void curve_changed() override;
    void contract_changed() override { curve_changed(); }

  private:
    void render_strip();
    QLabel* name_ = nullptr;
    QLabel* price_ = nullptr;
    QLabel* change_ = nullptr;
    QLabel* facts_ = nullptr;
    futures::CurveChart* chart_ = nullptr;
};

class FuturesMonthsWidget : public FuturesWidgetBase {
    Q_OBJECT
  public:
    explicit FuturesMonthsWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);

  protected:
    void root_changed() override;
    void curve_changed() override;
    void contract_changed() override { curve_changed(); }

  private:
    QTableWidget* table_ = nullptr;
};

class FuturesDetailWidget : public FuturesWidgetBase {
    Q_OBJECT
  public:
    explicit FuturesDetailWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);

  protected:
    void root_changed() override;
    void curve_changed() override;
    void contract_changed() override;
    void subscribe_more() override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void build_ticket(QWidget* tk);
    void follow_spread();
    void follow_cot();
    void render_spread(const services::futures::SpreadHistory& h);
    void render_cot(const services::futures::CotReport& r);
    void render_terms();
    // ticket / paper account (unchanged from the FUTURES screen)
    const services::futures::CurveContract* ticket_contract() const;
    double last_price(const QString& contract_symbol) const;
    void render_ticket();
    QString paper_portfolio_id();
    void place_order();
    static double ticket_level(const QLineEdit* field);
    void load_brackets();
    void save_brackets();
    void reconcile_brackets();
    void check_brackets();
    bool close_position(const QString& symbol, bool report);
    void reload_positions();
    void render_positions();
    void sync_stream();
    void on_ticks(const QVector<services::StreamTick>& ticks);

    QTabWidget* tabs_ = nullptr;
    QLabel* spread_summary_ = nullptr;
    futures::SpreadChart* spread_chart_ = nullptr;
    QString spread_topic_;
    QString spread_title_;
    QLabel* cot_summary_ = nullptr;
    futures::CotChart* cot_chart_ = nullptr;
    QString cot_topic_;
    QLabel* terms_ = nullptr;

    QLabel* ticket_contract_ = nullptr;
    QPushButton* buy_btn_ = nullptr;
    QPushButton* sell_btn_ = nullptr;
    QSpinBox* qty_ = nullptr;
    QLineEdit* stop_ = nullptr;
    QLineEdit* target_ = nullptr;
    QLabel* ticket_math_ = nullptr;
    QPushButton* place_btn_ = nullptr;
    QLabel* ticket_status_ = nullptr;
    QLabel* account_ = nullptr;
    QTableWidget* positions_ = nullptr;
    QString portfolio_id_;
    QHash<QString, double> marks_;        ///< newest print known this session, by contract
    QHash<QString, qint64> mark_seen_ms_; ///< when each mark arrived (wall clock)
    QHash<QString, qint64> mark_ts_;      ///< when that print was, unix seconds
    QVector<trading::PtPosition> open_;
    double balance_ = 0.0;
    QString account_stats_;
    QString book_error_;
    bool book_ok_ = false;
    ui::RenderCoalescer positions_render_{this, [this]() { render_positions(); }};
};

class FuturesBoardWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit FuturesBoardWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);
    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void on_theme_changed() override;

  private:
    void set_group(const QString& group);
    void subscribe();
    void render();
    QString group_;
    bool fixed_ = false;        ///< the group is set by the page (no group tabs)
    QWidget* bar_ = nullptr;     ///< the group tabs
    QString selected_root_;
    QButtonGroup* groups_ = nullptr;
    QTableWidget* table_ = nullptr;
    QHash<QString, services::QuoteData> quotes_;
    bool subscribed_ = false;
    QPointer<SelectionLink> link_;
    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::screens::widgets

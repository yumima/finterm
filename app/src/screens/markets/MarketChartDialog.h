// src/screens/markets/MarketChartDialog.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "services/markets/IndexFutures.h"

#include <QDialog>
#include <QWidget>
#include <QHash>
#include <QJsonObject>
#include <QPointer>

#include <optional>

class QLabel;
class QPushButton;
class QTimer;

namespace finterm::ui {
class LoadingOverlay;
}

namespace finterm::screens {

class ResearchCandleCanvas;

/// A price chart for any market data point — an index, a future, a currency
/// pair, a coin, a stock — opened by clicking it wherever quotes are listed
/// (Markets panels, the dashboard quote tables, the ticker bar).
///
/// Centred over the main window and non-modal, so the reader can keep it open
/// while they click through a list: a second click retargets the same window
/// rather than stacking another. Period, style and overlays persist for the
/// session, so the reader sets their preference once.
///
/// The chart is ER's QPainter canvas (ResearchCandleCanvas) — crosshair, log
/// scale, volume, moving averages — with a line/area style and an intraday
/// axis added for 1D/5D.
class MarketChartView : public QWidget {
    Q_OBJECT
  public:
    explicit MarketChartView(QWidget* parent = nullptr);
    /// Show `symbol` ("DOW JONES" as `label`; empty = the symbol). A Yahoo
    /// symbol, or a derived series (ConditionService::is_derived).
    void set_symbol(const QString& symbol, const QString& label);
    QString symbol() const;
    /// Hide the ✕ (a host with its own close, e.g. a dashboard tile).
    void set_closable(bool on);

  signals:
    void close_requested();
    void title_changed(const QString& title);
    /// set_symbol() moved the view to another symbol.
    void symbol_changed(const QString& symbol, const QString& label);

  protected:
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void load();
    void apply(const QVector<services::equity::Candle>& candles);
    void update_header(int hover_idx);
    void sync_buttons();
    /// The FUT readout: the index's futures quote (contract, last, change
    /// since settle), shown when the reader turns FUT on and the index has a
    /// future on the feed.
    void fetch_futures();
    void update_fut_visibility();
    /// The FUT readout from fut_quote_ (a fetch, then live prints of its contract).
    void paint_fut();
    /// Stream fut_quote_'s contract while the readout is on screen.
    void sync_fut_stream();
    /// Follow cond:stats:<symbol> while shown (not for derived series).
    void sync_conditions();
    void paint_conditions();

    QString symbol_;
    QString label_;
    // Bumped per load: a later click (another period or symbol) supersedes an
    // answer still out. Bars come from MarketDataService::fetch_bars, whose
    // shared cache answers a repeat of a fresh series without a fetch.
    int request_seq_ = 0;

    ResearchCandleCanvas* canvas_ = nullptr;
    ui::LoadingOverlay* overlay_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* last_ = nullptr;
    QLabel* change_ = nullptr;
    QLabel* readout_ = nullptr;
    QLabel* fut_ = nullptr;
    QLabel* cond_ = nullptr;
    QString cond_topic_;
    QJsonObject cond_data_;
    QString cond_quote_topic_;
    QString cond_error_; ///< the reading's last refresh error, shown while there is no reading
    double cond_live_ = 0; ///< live price for the condition strip; NaN = none (set in sync_conditions)
    QTimer* fut_timer_ = nullptr;
    int fut_seq_ = 0;
    std::optional<services::IndexFutureQuote> fut_quote_;
    QHash<QString, QPushButton*> period_btns_;
    QHash<int, QPushButton*> style_btns_;
    QHash<QString, QPushButton*> toggle_btns_;
};


/// The chart as a window: opened by clicking a market data point wherever
/// quotes are listed (Markets, the dashboard, the ticker bar …). Centred over
/// the main window and non-modal; a second click retargets it.
///
/// Security linking: a canvas (or page) that holds a Chart widget registers it
/// with link_chart(); a click inside that canvas then loads the symbol into
/// the linked chart instead of opening this window.
class MarketChartDialog : public QDialog {
    Q_OBJECT
  public:
    /// Open (or retarget) the chart for `symbol` — or the linked chart of the
    /// canvas `from` sits on, when one is on screen.
    static void show_for(const QString& symbol, const QString& label, QWidget* from);

    /// Make a label-built row clickable: a left click on `w` (or on a child
    /// that passes its clicks up, as a plain QLabel does) opens the chart for
    /// the symbol `w` carries at that moment. Rows that change symbol in place
    /// just call set_target() again — nothing is captured at install time.
    static void make_clickable(QWidget* w);
    static void set_target(QWidget* w, const QString& symbol, const QString& label = {});

    /// `view` follows clicks made inside `scope` (the newest link wins).
    static void link_chart(QWidget* scope, MarketChartView* view);
    static void unlink_chart(QWidget* scope, MarketChartView* view);

  private:
    explicit MarketChartDialog(QWidget* parent);
    static bool route_to_linked(const QString& symbol, const QString& label, QWidget* from);
    MarketChartView* view_ = nullptr;
    static QPointer<MarketChartDialog> instance_;
};

} // namespace finterm::screens

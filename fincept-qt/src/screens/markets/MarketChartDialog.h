// src/screens/markets/MarketChartDialog.h
#pragma once
#include "services/equity/EquityResearchModels.h"

#include <QDialog>
#include <QHash>
#include <QPointer>

class QLabel;
class QPushButton;

namespace fincept::ui {
class LoadingOverlay;
}

namespace fincept::screens {

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
class MarketChartDialog : public QDialog {
    Q_OBJECT
  public:
    /// Open (or retarget) the chart for `symbol`. `label` is the display name
    /// shown in the title ("DOW JONES"); empty falls back to the symbol.
    static void show_for(const QString& symbol, const QString& label, QWidget* from);

    /// Make a label-built row clickable: a left click on `w` (or on a child
    /// that passes its clicks up, as a plain QLabel does) opens the chart for
    /// the symbol `w` carries at that moment. Rows that change symbol in place
    /// just call set_target() again — nothing is captured at install time.
    static void make_clickable(QWidget* w);
    static void set_target(QWidget* w, const QString& symbol, const QString& label = {});

  protected:
    void keyPressEvent(QKeyEvent* e) override;

  private:
    explicit MarketChartDialog(QWidget* parent);
    void set_symbol(const QString& symbol, const QString& label);
    void load();
    void apply(const QVector<services::equity::Candle>& candles);
    void update_header(int hover_idx);
    void sync_buttons();

    QString symbol_;
    QString label_;
    // Bars per symbol+period for this window's life: flipping between periods
    // is the main thing anyone does here, and refetching each time is slow.
    QHash<QString, QVector<services::equity::Candle>> cache_;
    int request_seq_ = 0;

    ResearchCandleCanvas* canvas_ = nullptr;
    ui::LoadingOverlay* overlay_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* last_ = nullptr;
    QLabel* change_ = nullptr;
    QLabel* readout_ = nullptr;
    QHash<QString, QPushButton*> period_btns_;
    QHash<int, QPushButton*> style_btns_;
    QHash<QString, QPushButton*> toggle_btns_;

    static QPointer<MarketChartDialog> instance_;
};

} // namespace fincept::screens

// src/screens/equity_research/EarningsReactionChart.h
#pragma once
#include "services/equity/EarningsSignal.h"
#include "services/equity/EquityResearchModels.h"

#include <QVector>
#include <QWidget>

namespace fincept::screens {

/// Earnings change plotted against what the stock actually did on the print.
///
/// Bars are the selected earnings metric (surprise vs consensus by default,
/// or sequential / year-ago EPS change); the line is the close-to-close move
/// over the report. Each series has its own scale — a 700% sequential swing and
/// a 7% price move share no natural axis — so the chart shows *co-movement*,
/// not magnitude between series. Both are labelled with their own range.
///
/// Behind the line sits the size forecast that stood before each print, as a
/// ±band on the price axis: the expected move, rebuilt walk-forward from the
/// quarters before it, and carried onto the upcoming column. It is the one
/// forecast here with a measured record, and the band makes that record
/// readable at a glance — about half the dots should land inside it.
///
/// Deliberately QPainter, like every other chart in this app: QOpenGLWidget
/// spawns duplicate xdg_toplevels under Mutter.
class EarningsReactionChart : public QWidget {
    Q_OBJECT
  public:
    explicit EarningsReactionChart(QWidget* parent = nullptr);

    /// `history` is newest-first (as the service delivers it); the chart plots
    /// oldest → newest, left to right.
    void set_history(const QVector<services::equity::EarningsPoint>& history);
    void set_metric(services::equity::ReactionMetric m);
    /// The size forecast before each past print (oldest first, as
    /// forecast_record() returns them) and the one standing for the next.
    void set_forecasts(const QVector<services::equity::PrintForecast>& prints,
                       std::optional<double> next_expected_pct);
    services::equity::ReactionMetric metric() const { return metric_; }

  protected:
    void paintEvent(QPaintEvent* e) override;
    /// Hovering a column explains that quarter rather than the chart in
    /// general: which line is which, what each one read, and — for the dotted
    /// point — whether it was recorded before the print or rebuilt afterwards.
    /// A legend can only say what the styles mean; this says what they mean
    /// HERE, which is the question someone squinting at two lines is asking.
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;

  private:
    struct Column {
        qint64 timestamp = 0;
        std::optional<double> metric;    // the selected earnings metric
        std::optional<double> reaction;
        // The trailing column: a consensus bar (when one is published) and,
        // in place of a print reaction, where the price sits right now
        // against the last completed print. Drawn provisionally — dashed
        // outline, hollow marker — so a forecast and a live price are never
        // mistaken for settled history.
        bool projected = false;
        std::optional<double> live_move;
        // The ±size forecast that stood before this print.
        std::optional<double> expected;
        std::optional<double> p_beat;
        std::optional<bool> beat;
    };

    QVector<Column> columns() const;
    /// The data area, shared by painting and hit-testing so a hover can never
    /// resolve to a different column than the one drawn under the cursor.
    QRect plot_rect() const;
    /// Column index under `x`, or -1 outside the plot.
    int column_at(double x, const QVector<Column>& cols) const;
    QString tooltip_for(const Column& c) const;
    /// Axis half-range for a series: the 80th percentile of |value|, so one
    /// outlier (a +745% quarter off a depressed base) can't flatten the rest.
    /// Bars beyond it are clamped and marked with a chevron.
    static double axis_extent(const QVector<double>& values);

    QVector<services::equity::EarningsPoint> history_;   // oldest → newest
    QVector<services::equity::PrintForecast> forecasts_;
    std::optional<double> next_expected_;
    services::equity::ReactionMetric metric_ = services::equity::ReactionMetric::Surprise;
};

} // namespace fincept::screens

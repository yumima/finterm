// src/screens/equity_research/EarningsReactionChart.h
#pragma once
#include "services/equity/EarningsSignal.h"
#include "services/equity/EquityResearchModels.h"

#include <QVector>
#include <QWidget>

namespace fincept::screens {

/// Earnings change plotted against what the stock actually did on the print.
///
/// Bars are the selected earnings metric (sequential EPS change, year-ago
/// change, or surprise vs consensus); the line is the close-to-close move over
/// the report. Each series has its own scale — a 700% sequential swing and a
/// 7% price move share no natural axis — so the chart shows *co-movement*, not
/// magnitude between series. Both are labelled with their own range.
///
/// A second, dotted line carries what the pre-earnings signal predicted for
/// each print. It shares the price axis with the reaction line — both measure
/// the same session's move — so the gap between them is the error, read
/// without a second chart. It lives here rather than in a panel of its own
/// precisely because the realised move it is compared against is already this
/// chart's line; drawing that series twice to compare it with itself was the
/// whole problem with doing it separately.
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
    /// The signal's own estimate for each print, drawn against the realised
    /// move on the SAME axis — both are a percentage move on the same session,
    /// so the vertical gap between the two lines is the error and reads
    /// directly. Matched to columns by timestamp; quarters without an estimate
    /// simply have no point. Dotted where the estimate was rebuilt after the
    /// fact, solid between two that were recorded before their prints.
    void set_predictions(const QVector<services::equity::QuarterPrediction>& predictions);
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
        // The signal's estimate for this print, and whether it was rebuilt
        // afterwards rather than recorded before.
        std::optional<double> predicted;
        bool predicted_reconstructed = true;
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
    QVector<services::equity::QuarterPrediction> predictions_;
    services::equity::ReactionMetric metric_ = services::equity::ReactionMetric::QoQ;
};

} // namespace fincept::screens

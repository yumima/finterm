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
    services::equity::ReactionMetric metric() const { return metric_; }

  protected:
    void paintEvent(QPaintEvent* e) override;

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
    };

    QVector<Column> columns() const;
    /// Axis half-range for a series: the 80th percentile of |value|, so one
    /// outlier (a +745% quarter off a depressed base) can't flatten the rest.
    /// Bars beyond it are clamped and marked with a chevron.
    static double axis_extent(const QVector<double>& values);

    QVector<services::equity::EarningsPoint> history_;   // oldest → newest
    services::equity::ReactionMetric metric_ = services::equity::ReactionMetric::QoQ;
};

} // namespace fincept::screens

#pragma once
#include <QVector>
#include <QWidget>

namespace fincept::screens {

/// A small line chart for the SCREENER tab: a strategy's growth of $1 against
/// the index, or a stock's closes with its averages and a trade plan's levels.
/// Painted directly (no QtCharts scene) so dozens of redraws as the reader
/// clicks down a list stay cheap. NaN points are gaps, never zeros.
class ScreenerChart : public QWidget {
    Q_OBJECT
  public:
    struct Series {
        QVector<double> y;
        QString colour;
        QString label;
        double width = 1.4;
        bool dashed = false;
    };
    struct Level {
        double y = 0;
        QString colour;
        QString label;
        bool label_below = false; // stop under its line, entry over: 2-3% apart they collide
    };

    explicit ScreenerChart(QWidget* parent = nullptr);

    void set_title(const QString& t) { title_ = t; update(); }
    void set_series(const QVector<Series>& s) { series_ = s; update(); }
    void set_levels(const QVector<Level>& l) { levels_ = l; update(); }
    /// x-axis labels at the first, middle and last point
    void set_x_labels(const QStringList& l) { x_labels_ = l; update(); }
    void set_log_scale(bool on) { log_ = on; update(); }
    void set_placeholder(const QString& text) { placeholder_ = text; update(); }
    void clear_all();

    QSize sizeHint() const override { return {360, 200}; }

  protected:
    void paintEvent(QPaintEvent*) override;

  private:
    QString title_, placeholder_;
    QVector<Series> series_;
    QVector<Level> levels_;
    QStringList x_labels_;
    bool log_ = false;
};

} // namespace fincept::screens

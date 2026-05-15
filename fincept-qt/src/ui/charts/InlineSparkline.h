#pragma once
#include <QVector>
#include <QWidget>

namespace fincept::ui {

/// Tiny in-row sparkline. Renders a polyline normalised to the widget's bounds
/// and colours it green if the last value >= first, red otherwise. Designed to
/// live inside dashboard table rows (PortfolioSummary, Watchlist, TopMovers, …)
/// so trend direction and volatility read pre-attentively next to the numbers.
///
/// Data: feed it with a QVector<double> of recent prices via `set_points()`.
/// The DataHub topic `market:sparkline:<symbol>` already publishes this vector
/// for any subscribed symbol; widgets can just `subscribe(this, topic, …)`.
class InlineSparkline : public QWidget {
    Q_OBJECT
  public:
    explicit InlineSparkline(QWidget* parent = nullptr);

    void set_points(const QVector<double>& pts);
    void clear() { pts_.clear(); update(); }

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QVector<double> pts_;
};

} // namespace fincept::ui

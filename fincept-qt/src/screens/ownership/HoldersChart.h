#pragma once
// Small painted charts for the OWNERSHIP tiles.
//
// Hand-painted rather than QtCharts: these are one-dimensional (a ranked bar
// list, a diverging bar list, a timeline of dated events), and a QChartView per
// tile brings a scene graph, axes and legends that would each need styling back
// down to nothing. Painting them keeps the tiles readable at small sizes, which
// is the whole point of a tiled layout.
//
// Colour is semantic and consistent across every tile: green for added or
// bought, red for trimmed, sold or exited, amber for the reader's attention.

#include <QColor>
#include <QDate>
#include <QString>
#include <QVector>
#include <QWidget>

namespace fincept::screens {

/// One ranked horizontal bar — a holder's weight, a position's share of a book.
struct RankedBar {
    QString label;
    QString value_text;   ///< right-aligned, already formatted
    double  fraction = 0; ///< 0..1, bar length
    QColor  colour;
    QString tooltip;
};

/// A ranked horizontal bar list. Reads top-down, largest first, with the label
/// inside the bar so no separate legend column is needed.
class RankedBarChart : public QWidget {
    Q_OBJECT
  public:
    explicit RankedBarChart(QWidget* parent = nullptr);
    void set_bars(const QVector<RankedBar>& bars);
    void set_empty_text(const QString& text);
    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;

  private:
    QVector<RankedBar> bars_;
    QString empty_text_;
    int row_height_ = 22;
};

/// One dated event on a timeline — an insider transaction.
struct TimelineEvent {
    QDate   date;
    double  magnitude = 0;  ///< 0..1 relative to the largest in the set
    bool    positive = true;
    QString tooltip;
};

/// A dated event timeline: bars up from the axis for buys, down for sells,
/// height by value. Makes a cluster visible as a cluster, which a date-sorted
/// table cannot do.
class EventTimeline : public QWidget {
    Q_OBJECT
  public:
    explicit EventTimeline(QWidget* parent = nullptr);
    void set_events(const QVector<TimelineEvent>& events);
    void set_empty_text(const QString& text);
    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;

  private:
    QVector<TimelineEvent> events_;
    QString empty_text_;
    QDate   first_, last_;
};

} // namespace fincept::screens

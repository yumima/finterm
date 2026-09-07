#pragma once
// The insider timeline: open-market buys above the axis, sells below, height
// by value, a cluster shaded as a band.
//
// Hand-painted rather than QtCharts: it is one-dimensional, and a chart
// framework brings axes and legends that would each need styling back down to
// this. Painting it keeps the labels at a size a reader can see — the one
// rule this widget exists to keep is that nothing on it is smaller than 12px.

#include "screens/ownership/OwnershipTypes.h"

#include <QDate>
#include <QWidget>

namespace fincept::screens {

class InsiderTimeline : public QWidget {
    Q_OBJECT
  public:
    explicit InsiderTimeline(QWidget* parent = nullptr);

    /// Open-market rows only are drawn; the clusters are shaded behind them.
    void set_transactions(const QVector<ownership::InsiderTransaction>& tx,
                          const QVector<ownership::BuyCluster>& clusters);
    void set_empty_text(const QString& text);
    QSize minimumSizeHint() const override { return {320, 64}; }

  protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;

  private:
    struct Mark {
        QDate  date;
        bool   buy = true;
        bool   scored = true;   ///< false for 10% owners and plan trades
        double magnitude = 0.0; ///< 0..1 of the largest value in the set
        QString tooltip;
    };
    QVector<Mark> marks_;
    QVector<ownership::BuyCluster> clusters_;
    QString empty_text_;
    QDate first_, last_;
    int x_for(const QDate& d, const QRect& plot) const;
};

} // namespace fincept::screens

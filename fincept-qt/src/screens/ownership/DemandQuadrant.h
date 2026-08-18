#pragma once
// The institutional demand scatter: every discretionary holder placed on
// conviction against direction, with the ten largest named.
//
// A single verdict badge hid the answer. AAPL last quarter gained 91 holders
// and 14.2m shares — "accumulation" by any one-number measure — while the funds
// holding the LARGEST stakes were net sellers, 1,442 trimming against 1,113
// adding. The aggregate and the conviction-weighted read pointed opposite ways,
// and a label can only ever show one of them.
//
// x — share of the holder's own book, so "conviction" is measured against the
//     other holders of this security rather than an absolute cut. 3% of a
//     twenty-name book is a different statement from 3% of a nine-hundred-name
//     one, and the median holder is the only honest reference available.
// y — change in shares since the prior quarter.
//
// Painted rather than QtCharts: five thousand points with four quadrant
// backgrounds and ten callouts is less code by hand than it is styling a chart
// framework back down to this.

#include "screens/ownership/OwnershipTypes.h"

#include <QWidget>

namespace fincept::screens {

class DemandQuadrant : public QWidget {
    Q_OBJECT
  public:
    explicit DemandQuadrant(QWidget* parent = nullptr);

    void set_demand(const ownership::InstitutionalDemand& d);
    void clear();
    void set_empty_text(const QString& t);

    QSize minimumSizeHint() const override;

  signals:
    /// A named holder was clicked — carries the manager name.
    void holder_activated(const QString& manager);

  protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

  private:
    /// Point geometry is computed once per repaint and reused for hit-testing,
    /// so a tooltip can never disagree with what was drawn.
    struct Plotted { QPointF at; const ownership::DemandPoint* p; };
    QVector<Plotted> layout_points(const QRect& plot) const;

    ownership::InstitutionalDemand d_;
    QString empty_text_;
    mutable QVector<Plotted> hit_;
    mutable QRect plot_;
};

} // namespace fincept::screens

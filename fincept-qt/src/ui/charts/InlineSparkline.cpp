#include "ui/charts/InlineSparkline.h"

#include "ui/theme/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace fincept::ui {

InlineSparkline::InlineSparkline(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(16);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void InlineSparkline::set_points(const QVector<double>& pts) {
    pts_ = pts;
    update();
}

void InlineSparkline::paintEvent(QPaintEvent* /*event*/) {
    if (pts_.size() < 2)
        return;

    QPainter p(this);
    const QRectF r = rect();

    double lo = pts_.first(), hi = pts_.first();
    for (double v : pts_) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    const double span = std::max(1e-9, hi - lo);
    const double dx = r.width() / (pts_.size() - 1);

    QPainterPath path;
    for (int i = 0; i < pts_.size(); ++i) {
        const double x = r.left() + i * dx;
        const double y = r.bottom() - ((pts_[i] - lo) / span) * r.height();
        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }

    const QColor col = (pts_.last() >= pts_.first())
                           ? QColor(colors::POSITIVE())
                           : QColor(colors::NEGATIVE());
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(col, 1.2));
    p.drawPath(path);
}

} // namespace fincept::ui

#include "screens/screener/ScreenerChart.h"

#include "ui/theme/Theme.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace fincept::screens {

ScreenerChart::ScreenerChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(150);
}

void ScreenerChart::clear_all() {
    series_.clear();
    levels_.clear();
    x_labels_.clear();
    title_.clear();
    update();
}

void ScreenerChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));
    p.setPen(QColor(ui::colors::BORDER_DIM()));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
    p.setFont(f);
    const int fh = p.fontMetrics().height();

    if (!title_.isEmpty()) {
        p.setPen(QColor(ui::colors::AMBER()));
        p.drawText(QRect(8, 4, width() - 16, fh), Qt::AlignLeft | Qt::AlignVCenter, title_);
    }

    auto tx = [&](double v) { return log_ ? std::log(v) : v; };
    double lo = INFINITY, hi = -INFINITY;
    int n = 0;
    for (const auto& s : series_) {
        n = std::max(n, int(s.y.size()));
        for (double v : s.y)
            if (std::isfinite(v) && (!log_ || v > 0)) {
                lo = std::min(lo, tx(v));
                hi = std::max(hi, tx(v));
            }
    }
    for (const auto& l : levels_)
        if (std::isfinite(l.y) && (!log_ || l.y > 0)) {
            lo = std::min(lo, tx(l.y));
            hi = std::max(hi, tx(l.y));
        }
    if (n < 2 || !std::isfinite(lo) || !std::isfinite(hi)) {
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                   placeholder_.isEmpty() ? QStringLiteral("No data") : placeholder_);
        return;
    }
    if (hi - lo < 1e-12) {
        hi += 0.5;
        lo -= 0.5;
    }
    const double pad = (hi - lo) * 0.06;
    lo -= pad;
    hi += pad;

    const QRectF plot(8, 8 + fh + 4, width() - 8 - 56, height() - (8 + fh + 4) - fh - 8);
    auto X = [&](int i) { return plot.left() + plot.width() * i / double(n - 1); };
    auto Y = [&](double v) { return plot.bottom() - plot.height() * (tx(v) - lo) / (hi - lo); };

    // grid + right-side value labels
    p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1, Qt::DotLine));
    for (int k = 0; k <= 3; ++k) {
        const double tv = lo + (hi - lo) * k / 3.0;
        const double v = log_ ? std::exp(tv) : tv;
        const double y = plot.bottom() - plot.height() * k / 3.0;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        const QString lab = std::abs(v) >= 1000 ? QString::number(v, 'f', 0)
                                                : QString::number(v, 'f', std::abs(v) >= 10 ? 1 : 2);
        p.drawText(QRectF(plot.right() + 4, y - fh / 2.0, 52, fh), Qt::AlignLeft | Qt::AlignVCenter, lab);
        p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1, Qt::DotLine));
    }
    // x labels
    p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    if (!x_labels_.isEmpty()) {
        const QRectF xb(plot.left(), plot.bottom() + 2, plot.width(), fh);
        p.drawText(xb, Qt::AlignLeft | Qt::AlignVCenter, x_labels_.value(0));
        if (x_labels_.size() > 2)
            p.drawText(xb, Qt::AlignHCenter | Qt::AlignVCenter, x_labels_.value(1));
        p.drawText(xb, Qt::AlignRight | Qt::AlignVCenter, x_labels_.last());
    }
    // levels (a trade plan)
    for (const auto& l : levels_) {
        if (!std::isfinite(l.y) || (log_ && l.y <= 0))
            continue;
        const double y = Y(l.y);
        p.setPen(QPen(QColor(l.colour), 1, Qt::DashLine));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(l.colour));
        if (l.label_below)
            p.drawText(QRectF(plot.left() + 4, y, 160, fh), Qt::AlignLeft | Qt::AlignTop, l.label);
        else
            p.drawText(QRectF(plot.left() + 4, y - fh, 160, fh), Qt::AlignLeft | Qt::AlignBottom, l.label);
    }
    // series: NaN breaks the line
    for (const auto& s : series_) {
        QPainterPath path;
        bool pen_down = false;
        for (int i = 0; i < s.y.size(); ++i) {
            const double v = s.y[i];
            if (!std::isfinite(v) || (log_ && v <= 0)) {
                pen_down = false;
                continue;
            }
            const QPointF pt(X(i), Y(v));
            if (pen_down)
                path.lineTo(pt);
            else
                path.moveTo(pt);
            pen_down = true;
        }
        QPen pen(QColor(s.colour), s.width);
        if (s.dashed)
            pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    // legend, top right
    int lx = int(plot.right());
    for (int k = int(series_.size()) - 1; k >= 0; --k) {
        const auto& s = series_[k];
        if (s.label.isEmpty())
            continue;
        const int w = p.fontMetrics().horizontalAdvance(s.label);
        lx -= w + 22;
        p.setPen(QPen(QColor(s.colour), 2));
        p.drawLine(lx, 4 + fh / 2, lx + 12, 4 + fh / 2);
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(QRect(lx + 15, 4, w + 4, fh), Qt::AlignLeft | Qt::AlignVCenter, s.label);
    }
}

} // namespace fincept::screens

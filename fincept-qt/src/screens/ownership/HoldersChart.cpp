#include "screens/ownership/HoldersChart.h"

#include "ui/theme/Theme.h"

#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

namespace fincept::screens {

namespace {
constexpr int kPadX = 6;
constexpr int kPadY = 6;
constexpr int kGap = 3;
} // namespace

// ── RankedBarChart ──────────────────────────────────────────────────────────

RankedBarChart::RankedBarChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void RankedBarChart::set_bars(const QVector<RankedBar>& bars) {
    bars_ = bars;
    updateGeometry();
    update();
}

void RankedBarChart::set_empty_text(const QString& text) {
    empty_text_ = text;
    update();
}

QSize RankedBarChart::minimumSizeHint() const {
    const int shown = std::max(1, std::min<int>(static_cast<int>(bars_.size()), 4));
    return {220, kPadY * 2 + row_height_ * shown};
}

void RankedBarChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    if (bars_.isEmpty()) {
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(rect().adjusted(kPadX, kPadY, -kPadX, -kPadY),
                   Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, empty_text_);
        return;
    }

    // Fit as many rows as the tile has room for; the caller ranks them, so a
    // truncated view is still the most important rows.
    const int usable = height() - kPadY * 2;
    const int rows = std::max(1, std::min<int>(static_cast<int>(bars_.size()),
                                               usable / (row_height_ + kGap)));
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    const QFontMetrics fm(f);

    // Value column is right-aligned and sized to its widest entry, so the bars
    // all start and end at the same place and can be compared by eye.
    int value_w = 0;
    for (int i = 0; i < rows; ++i)
        value_w = std::max(value_w, fm.horizontalAdvance(bars_[i].value_text));
    value_w += 10;

    const int bar_x = kPadX;
    const int bar_w = std::max(40, width() - kPadX * 2 - value_w);

    for (int i = 0; i < rows; ++i) {
        const auto& b = bars_[i];
        const int y = kPadY + i * (row_height_ + kGap);
        const QRect track(bar_x, y, bar_w, row_height_);

        p.fillRect(track, QColor(ui::colors::BG_RAISED()));
        const int fill_w = static_cast<int>(bar_w * std::clamp(b.fraction, 0.0, 1.0));
        QColor c = b.colour.isValid() ? b.colour : QColor(ui::colors::CYAN());
        QColor fill = c;
        fill.setAlpha(90);
        p.fillRect(QRect(bar_x, y, fill_w, row_height_), fill);
        p.setPen(c);
        p.drawLine(bar_x, y, bar_x, y + row_height_ - 1);

        // Label sits inside the bar in the primary text colour — a label drawn
        // in the bar's own hue on a tinted bar is the classic unreadable case.
        p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
        p.drawText(track.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(b.label, Qt::ElideRight, bar_w - 12));

        p.setPen(c);
        p.drawText(QRect(bar_x + bar_w, y, value_w - 4, row_height_),
                   Qt::AlignVCenter | Qt::AlignRight, b.value_text);
    }

    if (bars_.size() > rows) {
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(rect().adjusted(kPadX, 0, -kPadX, -2), Qt::AlignBottom | Qt::AlignRight,
                   QStringLiteral("+%1 more").arg(bars_.size() - rows));
    }
}

bool RankedBarChart::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const int idx = (he->pos().y() - kPadY) / (row_height_ + kGap);
        if (idx >= 0 && idx < bars_.size() && !bars_[idx].tooltip.isEmpty()) {
            QToolTip::showText(he->globalPos(), bars_[idx].tooltip, this);
            return true;
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

// ── EventTimeline ───────────────────────────────────────────────────────────

EventTimeline::EventTimeline(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void EventTimeline::set_events(const QVector<TimelineEvent>& events) {
    events_ = events;
    first_ = last_ = {};
    for (const auto& e : events_) {
        if (!e.date.isValid())
            continue;
        if (!first_.isValid() || e.date < first_) first_ = e.date;
        if (!last_.isValid() || e.date > last_) last_ = e.date;
    }
    update();
}

void EventTimeline::set_empty_text(const QString& text) {
    empty_text_ = text;
    update();
}

QSize EventTimeline::minimumSizeHint() const { return {240, 90}; }

void EventTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);

    if (events_.isEmpty() || !first_.isValid()) {
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(rect().adjusted(kPadX, kPadY, -kPadX, -kPadY),
                   Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, empty_text_);
        return;
    }

    const int label_h = 14;
    const QRect plot = rect().adjusted(kPadX, kPadY, -kPadX, -(kPadY + label_h));
    const int mid = plot.center().y();
    const int half = plot.height() / 2 - 2;

    p.setPen(QColor(ui::colors::BORDER_DIM()));
    p.drawLine(plot.left(), mid, plot.right(), mid);

    // A single-day span would divide by zero and stack every bar on the left
    // edge; give it the whole width instead.
    const qint64 span = std::max<qint64>(1, first_.daysTo(last_));
    const int bar_w = std::max(2, std::min<int>(9,
        plot.width() / std::max<int>(1, static_cast<int>(events_.size()) * 2)));

    for (const auto& e : events_) {
        if (!e.date.isValid())
            continue;
        const double t = static_cast<double>(first_.daysTo(e.date)) / span;
        const int x = plot.left() + static_cast<int>(t * (plot.width() - bar_w));
        const int h = std::max(2, static_cast<int>(half * std::clamp(e.magnitude, 0.05, 1.0)));
        const QColor c(e.positive ? ui::colors::GREEN() : ui::colors::RED());
        QColor fill = c;
        fill.setAlpha(170);
        p.fillRect(QRect(x, e.positive ? mid - h : mid, bar_w, h), fill);
    }

    p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    const QRect labels(plot.left(), plot.bottom() + 2, plot.width(), label_h);
    p.drawText(labels, Qt::AlignLeft | Qt::AlignVCenter,
               first_.toString(QStringLiteral("MMM yyyy")));
    p.drawText(labels, Qt::AlignRight | Qt::AlignVCenter,
               last_.toString(QStringLiteral("MMM yyyy")));
    p.setPen(QColor(ui::colors::GREEN()));
    p.drawText(labels, Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("bought ↑   sold ↓"));
}

bool EventTimeline::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        // Nearest event by x, so a thin bar is still reachable.
        const QRect plot = rect().adjusted(kPadX, kPadY, -kPadX, -20);
        const qint64 span = std::max<qint64>(1, first_.daysTo(last_));
        int best = -1, best_dx = 1 << 30;
        for (int i = 0; i < events_.size(); ++i) {
            if (!events_[i].date.isValid())
                continue;
            const double t = static_cast<double>(first_.daysTo(events_[i].date)) / span;
            const int x = plot.left() + static_cast<int>(t * plot.width());
            const int dx = std::abs(x - he->pos().x());
            if (dx < best_dx) { best_dx = dx; best = i; }
        }
        if (best >= 0 && best_dx < 12 && !events_[best].tooltip.isEmpty()) {
            QToolTip::showText(he->globalPos(), events_[best].tooltip, this);
            return true;
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

} // namespace fincept::screens

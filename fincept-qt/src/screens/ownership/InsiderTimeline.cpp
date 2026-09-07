#include "screens/ownership/InsiderTimeline.h"

#include "ui/components/TooltipText.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace fmt = fincept::ui::formatting;

namespace {
constexpr int kPadX = 8;
constexpr int kPadY = 6;
constexpr int kLabelH = 16;
constexpr int kFontPx = 12;
} // namespace

InsiderTimeline::InsiderTimeline(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void InsiderTimeline::set_transactions(const QVector<ownership::InsiderTransaction>& tx,
                                       const QVector<ownership::BuyCluster>& clusters) {
    marks_.clear();
    clusters_ = clusters;
    first_ = last_ = {};
    double biggest = 0.0;
    for (const auto& t : tx)
        if (t.open_market && !t.derivative && t.value)
            biggest = std::max(biggest, std::abs(*t.value));
    for (const auto& t : tx) {
        if (!t.open_market || t.derivative || !t.date.isValid())
            continue;
        Mark m;
        m.date = t.date;
        m.buy = t.acquired;
        m.scored = !t.acquired || t.scorable_buy();
        m.magnitude = (biggest > 0 && t.value) ? std::abs(*t.value) / biggest : 0.3;
        QString why;
        if (t.acquired && t.ten_percent_owner)
            why = QStringLiteral("\n10% owner — shown, not scored");
        else if (t.acquired && t.plan_10b5_1.value_or(false))
            why = QStringLiteral("\n10b5-1 plan trade — decided in advance, not scored");
        m.tooltip = QStringLiteral("%1\n%2 — %3\n%4 shares at %5%6")
                        .arg(t.date.toString(QStringLiteral("d MMM yyyy")), t.insider,
                             t.acquired ? QStringLiteral("bought") : QStringLiteral("sold"),
                             t.shares ? fmt::format_compact(*t.shares) : fmt::placeholder(),
                             t.price ? fmt::format_money(*t.price) : fmt::placeholder(), why);
        marks_.push_back(m);
        if (!first_.isValid() || t.date < first_) first_ = t.date;
        if (!last_.isValid() || t.date > last_) last_ = t.date;
    }
    // Room on the right so the newest bar is not on the edge, and a floor on
    // the span so a single week does not stretch across the whole widget.
    if (first_.isValid()) {
        if (first_.daysTo(last_) < 60)
            first_ = last_.addDays(-60);
        last_ = std::max(last_, QDate::currentDate());
    }
    update();
}

void InsiderTimeline::set_empty_text(const QString& text) {
    empty_text_ = text;
    update();
}

int InsiderTimeline::x_for(const QDate& d, const QRect& plot) const {
    const qint64 span = std::max<qint64>(1, first_.daysTo(last_));
    const double t = static_cast<double>(first_.daysTo(d)) / span;
    return plot.left() + static_cast<int>(t * (plot.width() - 6));
}

void InsiderTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QFont f = font();
    f.setPixelSize(kFontPx);
    p.setFont(f);

    if (marks_.isEmpty() || !first_.isValid()) {
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(rect().adjusted(kPadX, kPadY, -kPadX, -kPadY),
                   Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, empty_text_);
        return;
    }

    const QRect plot = rect().adjusted(kPadX, kPadY, -kPadX, -(kPadY + kLabelH));
    const int mid = plot.center().y();
    const int half = plot.height() / 2 - 3;

    // Clusters first, as a band under everything: several buyers inside a
    // month is the pattern with the evidence behind it, and a band makes it
    // visible as one event rather than as bars that happen to be near.
    for (const auto& c : clusters_) {
        const int x0 = x_for(c.start, plot) - 3;
        const int x1 = x_for(c.end, plot) + 9;
        QColor band(ui::colors::AMBER());
        band.setAlpha(40);
        p.fillRect(QRect(x0, plot.top(), std::max(6, x1 - x0), plot.height()), band);
    }

    p.setPen(QColor(ui::colors::BORDER_DIM()));
    p.drawLine(plot.left(), mid, plot.right(), mid);

    const int bar_w = std::max(3, std::min(9, plot.width() / std::max(1, static_cast<int>(marks_.size()) * 2)));
    for (const auto& m : marks_) {
        const int x = x_for(m.date, plot);
        const int h = std::max(3, static_cast<int>(half * std::clamp(m.magnitude, 0.06, 1.0)));
        QColor c(m.buy ? ui::colors::GREEN() : ui::colors::RED());
        c.setAlpha(m.scored ? 190 : 80);
        p.fillRect(QRect(x, m.buy ? mid - h : mid + 1, bar_w, h), c);
    }

    p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    const QRect labels(plot.left(), plot.bottom() + 2, plot.width(), kLabelH);
    p.drawText(labels, Qt::AlignLeft | Qt::AlignVCenter, first_.toString(QStringLiteral("MMM yyyy")));
    p.drawText(labels, Qt::AlignRight | Qt::AlignVCenter, last_.toString(QStringLiteral("MMM yyyy")));
    p.setPen(QColor(ui::colors::GREEN()));
    p.drawText(labels, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("bought ↑"));
    const int cx = labels.center().x();
    p.setPen(QColor(ui::colors::RED()));
    p.drawText(QRect(cx + 40, labels.top(), 80, labels.height()), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("sold ↓"));
    if (!clusters_.isEmpty()) {
        p.setPen(QColor(ui::colors::AMBER()));
        p.drawText(QRect(cx - 160, labels.top(), 110, labels.height()), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("▮ cluster"));
    }
}

bool InsiderTimeline::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const QRect plot = rect().adjusted(kPadX, kPadY, -kPadX, -(kPadY + kLabelH));
        int best = -1, best_dx = 1 << 30;
        for (int i = 0; i < marks_.size(); ++i) {
            const int dx = std::abs(x_for(marks_[i].date, plot) + 3 - he->pos().x());
            if (dx < best_dx) { best_dx = dx; best = i; }
        }
        if (best >= 0 && best_dx < 14) {
            QToolTip::showText(he->globalPos(), ui::tooltip_wrap(marks_[best].tooltip), this);
            return true;
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

} // namespace fincept::screens

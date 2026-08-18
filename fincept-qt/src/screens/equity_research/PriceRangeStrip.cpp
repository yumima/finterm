#include "screens/equity_research/PriceRangeStrip.h"

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace fmt = fincept::ui::formatting;

namespace {
constexpr int kPad = 10;
constexpr int kBarH = 16;

/// Simple mean of the last @n closes. Returns false rather than averaging a
/// shorter window: a "200-day average" computed over 60 days is a different
/// statistic wearing the same label.
bool sma(const QVector<services::equity::Candle>& c, int n, double* out) {
    if (c.size() < n || n <= 0)
        return false;
    double s = 0.0;
    for (int i = c.size() - n; i < c.size(); ++i)
        s += c[i].close;
    *out = s / n;
    return true;
}
} // namespace

PriceRangeStrip::PriceRangeStrip(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize PriceRangeStrip::minimumSizeHint() const {
    // Must cover, top to bottom: the endpoint labels, the bar, the tick labels
    // hanging below it, and the caption row. Derived rather than guessed so the
    // caption cannot be clipped by a few pixels.
    return {320, kPad + 12 + kBarH + 18 + 14 + 6};
}

void PriceRangeStrip::clear() {
    stats_ = Stats{};
    update();
}

void PriceRangeStrip::set_candles(const QVector<services::equity::Candle>& candles,
                                  const QString& currency_sym) {
    ccy_ = currency_sym;
    stats_ = Stats{};
    if (candles.size() < 30) {   // too little history to say anything about range
        update();
        return;
    }

    // The 52-week window is taken from the candles rather than from a vendor
    // field so the range and the price on the chart share one definition —
    // mixing the two produced a "52w high" the visible series never reached.
    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 365LL * 24 * 3600;
    double hi = 0.0, lo = 0.0;
    int in_window = 0;
    for (const auto& c : candles) {
        if (c.timestamp < cutoff || c.high <= 0.0 || c.low <= 0.0)
            continue;
        hi = hi > 0 ? std::max(hi, c.high) : c.high;
        lo = lo > 0 ? std::min(lo, c.low) : c.low;
        ++in_window;
    }
    if (in_window < 30 || hi <= lo)
        return;   // stats_ stays invalid; paint shows why

    stats_.last  = candles.last().close;
    stats_.hi52  = hi;
    stats_.lo52  = lo;
    stats_.days  = in_window;
    stats_.pct_of_range = std::clamp((stats_.last - lo) / (hi - lo), 0.0, 1.0);
    stats_.has50  = sma(candles, 50, &stats_.sma50);
    stats_.has200 = sma(candles, 200, &stats_.sma200);

    // Daily-return volatility, so distance from an average can be stated in
    // units of this security's own typical move rather than as a bare
    // percentage that means something different for every stock.
    double sum = 0.0, sumsq = 0.0;
    int n = 0;
    for (qsizetype i = std::max<qsizetype>(1, candles.size() - 252); i < candles.size(); ++i) {
        const double p0 = candles[i - 1].close, p1 = candles[i].close;
        if (p0 <= 0.0 || p1 <= 0.0)
            continue;
        const double r = std::log(p1 / p0);
        sum += r;
        sumsq += r * r;
        ++n;
    }
    if (n > 20) {
        const double mean = sum / n;
        stats_.daily_vol = std::sqrt(std::max(0.0, sumsq / n - mean * mean));
    }
    if (stats_.daily_vol > 1e-9) {
        if (stats_.has50)
            stats_.z50 = std::log(stats_.last / stats_.sma50) / stats_.daily_vol;
        if (stats_.has200)
            stats_.z200 = std::log(stats_.last / stats_.sma200) / stats_.daily_vol;
    }
    stats_.valid = true;
    update();
}

void PriceRangeStrip::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.fillRect(rect(), QColor(ui::colors::BG_SURFACE()));
    QFont f = font();
    f.setPixelSize(11);
    g.setFont(f);
    const QFontMetrics fm(f);

    if (!stats_.valid) {
        g.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        g.drawText(rect().adjusted(kPad, 0, -kPad, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Not enough price history to place this price in its range."));
        return;
    }

    const int bar_y = kPad + 12;
    const QRect bar(kPad, bar_y, std::max(40, width() - kPad * 2), kBarH);

    // The track is a cool-to-warm gradient low→high. It encodes POSITION, not
    // desirability: the research says nearness to the 52-week high predicts
    // higher returns, so colouring the high end red as "expensive" would state
    // the opposite of the evidence.
    QLinearGradient grad(bar.topLeft(), bar.topRight());
    QColor lo_c(ui::colors::CYAN());
    QColor hi_c(ui::colors::GREEN());
    lo_c.setAlpha(80);
    hi_c.setAlpha(80);
    grad.setColorAt(0.0, lo_c);
    grad.setColorAt(1.0, hi_c);
    g.fillRect(bar, grad);

    auto x_for = [&](double price) {
        const double t = std::clamp((price - stats_.lo52) / (stats_.hi52 - stats_.lo52), 0.0, 1.0);
        return bar.left() + static_cast<int>(t * bar.width());
    };

    // Moving-average reference marks. Drawn as thin ticks, deliberately not as
    // thresholds — see the header for why "below the average" is not a signal
    // at these horizons.
    auto tick = [&](double v, const QString& label) {
        const int x = x_for(v);
        g.setPen(QPen(QColor(ui::colors::TEXT_SECONDARY()), 1, Qt::DashLine));
        g.drawLine(x, bar.top() - 4, x, bar.bottom() + 4);
        g.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        g.drawText(QRect(x - 24, bar.bottom() + 5, 48, 13), Qt::AlignHCenter | Qt::AlignTop,
                   label);
    };
    if (stats_.has200) tick(stats_.sma200, QStringLiteral("200d"));
    if (stats_.has50)  tick(stats_.sma50,  QStringLiteral("50d"));

    // Today.
    const int px = x_for(stats_.last);
    g.setPen(QPen(QColor(ui::colors::AMBER()), 2));
    g.drawLine(px, bar.top() - 6, px, bar.bottom() + 6);
    g.setBrush(QColor(ui::colors::AMBER()));
    g.setPen(Qt::NoPen);
    g.drawEllipse(QPoint(px, bar.center().y()), 4, 4);

    // Endpoints, and the one number that carries the finding worth knowing.
    g.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    g.drawText(QRect(kPad, kPad - 4, bar.width() / 2, 13), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("52w low %1").arg(fmt::format_money(stats_.lo52, ccy_)));
    g.drawText(QRect(kPad + bar.width() / 2, kPad - 4, bar.width() / 2, 13),
               Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 52w high").arg(fmt::format_money(stats_.hi52, ccy_)));

    g.setPen(QColor(ui::colors::TEXT_PRIMARY()));
    QString caption = QStringLiteral("%1 of its 52-week range")
                          .arg(fmt::format_percent(stats_.pct_of_range * 100.0, 0));
    if (stats_.has200 && stats_.daily_vol > 1e-9) {
        // Distance in this stock's own daily sigma. A bare percentage cannot be
        // compared across securities; this can.
        caption += QStringLiteral("   ·   %1σ from its 200-day")
                       .arg(QString::number(stats_.z200, 'f', 1));
    }
    g.drawText(QRect(kPad, bar.bottom() + 18, width() - kPad * 2, 14),
               Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(caption, Qt::ElideRight, width() - kPad * 2));
}

bool PriceRangeStrip::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        if (!stats_.valid) {
            QToolTip::hideText();
            return true;
        }
        QStringList t;
        t << QStringLiteral("Last %1 · %2 of the 52-week range (%3 sessions)")
                 .arg(fmt::format_money(stats_.last, ccy_),
                      fmt::format_percent(stats_.pct_of_range * 100.0, 0))
                 .arg(stats_.days);
        if (stats_.has50)
            t << QStringLiteral("50-day %1 · %2σ away")
                     .arg(fmt::format_money(stats_.sma50, ccy_),
                          QString::number(stats_.z50, 'f', 1));
        if (stats_.has200)
            t << QStringLiteral("200-day %1 · %2σ away")
                     .arg(fmt::format_money(stats_.sma200, ccy_),
                          QString::number(stats_.z200, 'f', 1));
        t << QString();
        // The honest reading, stated once, where someone is about to draw the
        // wrong conclusion from the picture.
        t << QStringLiteral(
            "Position, not a recommendation. Trading below an average is not evidence of a "
            "good entry at these horizons: nearness to the 52-week HIGH is what predicts "
            "higher subsequent returns (George & Hwang, 2004), reversal of past losers takes "
            "three to five years (De Bondt & Thaler, 1985), and short-horizon mean reversion "
            "plays out inside about two weeks. The averages are marked because they are the "
            "levels most capital watches, not because crossing one is a signal.");
        QToolTip::showText(he->globalPos(), t.join(QStringLiteral("\n")), this);
        return true;
    }
    return QWidget::event(e);
}

} // namespace fincept::screens

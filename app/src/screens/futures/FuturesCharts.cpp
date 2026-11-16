// src/screens/futures/FuturesCharts.cpp
#include "screens/futures/FuturesCharts.h"

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QFontMetrics>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace finterm::screens::futures {

using namespace services::futures;

namespace {

constexpr int kLeft = 10;
constexpr int kRight = 66;
constexpr int kTop = 26;
constexpr int kBottom = 22;

QFont data_font(int offset = -2) {
    QFont f{QString(ui::fonts::DATA_FAMILY)};
    f.setPixelSize(ui::fonts::font_px(offset));
    return f;
}

struct Range {
    double lo = 0, hi = 1;
    bool valid = false;
    void add(double v) {
        if (!std::isfinite(v))
            return;
        lo = valid ? std::min(lo, v) : v;
        hi = valid ? std::max(hi, v) : v;
        valid = true;
    }
    /// Pad so the extremes do not sit on the frame; a flat series still gets height.
    void pad() {
        const double span = hi - lo;
        const double m = span > 0 ? span * 0.08 : std::max(std::abs(hi) * 0.01, 1e-6);
        lo -= m;
        hi += m;
    }
    double y(double v, const QRectF& plot) const { return plot.bottom() - (v - lo) / (hi - lo) * plot.height(); }
};

/// A step that puts about `target` round gridlines across [lo, hi].
double nice_step(double span, int target) {
    if (!(span > 0))
        return 1.0;
    const double raw = span / std::max(1, target);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double n = raw / mag;
    return (n < 1.5 ? 1.0 : n < 3.5 ? 2.0 : n < 7.5 ? 5.0 : 10.0) * mag;
}

void draw_message(QPainter& p, const QRect& r, const QString& text) {
    p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
    p.setFont(data_font());
    p.drawText(r.adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, text);
}

void draw_legend(QPainter& p, double x, double y, const QVector<QPair<QString, QColor>>& items) {
    p.setFont(data_font());
    const QFontMetrics fm(p.font());
    for (const auto& it : items) {
        p.setPen(QPen(it.second, 2));
        p.drawLine(QPointF(x, y), QPointF(x + 14, y));
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(QPointF(x + 18, y + fm.ascent() / 2.0 - 1), it.first);
        x += 18 + fm.horizontalAdvance(it.first) + 14;
    }
}

/// A readout box near the cursor, kept inside the widget.
void draw_readout(QPainter& p, const QRect& bounds, QPointF anchor, const QStringList& lines,
                  const QVector<QColor>& colors) {
    p.setFont(data_font());
    const QFontMetrics fm(p.font());
    int w = 0;
    for (const auto& l : lines)
        w = std::max(w, fm.horizontalAdvance(l));
    const int lh = fm.height() + 1;
    QRectF box(anchor.x() + 12, anchor.y() - 8, w + 16, lh * lines.size() + 10);
    if (box.right() > bounds.right() - 4)
        box.moveRight(anchor.x() - 12);
    if (box.bottom() > bounds.bottom() - 4)
        box.moveBottom(bounds.bottom() - 4);
    if (box.top() < bounds.top() + 4)
        box.moveTop(bounds.top() + 4);
    p.setPen(QColor(ui::colors::BORDER_MED()));
    p.setBrush(QColor(ui::colors::BG_RAISED()));
    p.drawRect(box);
    for (int i = 0; i < lines.size(); ++i) {
        p.setPen(i < colors.size() ? colors[i] : QColor(ui::colors::TEXT_PRIMARY()));
        p.drawText(QPointF(box.left() + 8, box.top() + 5 + fm.ascent() + i * lh), lines[i]);
    }
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════
// CurveChart
// ════════════════════════════════════════════════════════════════════════════

CurveChart::CurveChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(150);
}

void CurveChart::set_curve(const Curve& curve, const QVector<PastCurve>& past, bool as_rate, int decimals) {
    curve_ = curve;
    past_ = past;
    as_rate_ = as_rate;
    decimals_ = decimals;
    message_.clear();
    if (hover_ >= curve_.contracts.size())
        hover_ = -1;
    update();
}

void CurveChart::set_message(const QString& message) {
    curve_ = {};
    past_.clear();
    message_ = message;
    hover_ = -1;
    update();
}

double CurveChart::shown(double price) const {
    return as_rate_ ? implied_rate(price) : price;
}

QRectF CurveChart::plot_rect() const {
    return QRectF(kLeft, kTop, width() - kLeft - kRight, height() - kTop - kBottom);
}

double CurveChart::x_at(int i, const QRectF& plot) const {
    const int n = curve_.contracts.size();
    if (n <= 1)
        return plot.center().x();
    const double inset = 14;
    return plot.left() + inset + (plot.width() - 2 * inset) * i / (n - 1);
}

int CurveChart::index_at(double x) const {
    const int n = curve_.contracts.size();
    if (n == 0)
        return -1;
    const QRectF plot = plot_rect();
    int best = 0;
    for (int i = 1; i < n; ++i)
        if (std::abs(x_at(i, plot) - x) < std::abs(x_at(best, plot) - x))
            best = i;
    return best;
}

QVector<CurveChart::Series> CurveChart::build_series() const {
    QVector<Series> out;
    const auto& k = curve_.contracts;
    // Oldest first so today's curve is drawn on top.
    const QColor past_colors[] = {QColor(ui::colors::INFO()), QColor(ui::colors::CYAN())};
    for (int pi = past_.size() - 1; pi >= 0; --pi) {
        Series s;
        s.label = QStringLiteral("%1 ago · %2").arg(past_[pi].label, past_[pi].date.toString(QStringLiteral("d MMM")));
        s.color = past_colors[pi % 2];
        s.style = Qt::DashLine;
        for (const auto& c : k)
            s.values.append(shown(past_[pi].closes.value(c.symbol, kNaN)));
        out.append(s);
    }
    Series prev;
    prev.label = QStringLiteral("Prev close");
    prev.color = QColor(ui::colors::TEXT_TERTIARY());
    prev.style = Qt::DotLine;
    for (const auto& c : k)
        prev.values.append(shown(c.prev_close));
    out.append(prev);
    Series now;
    now.label = QStringLiteral("Last");
    now.color = QColor(ui::colors::AMBER());
    for (const auto& c : k)
        now.values.append(shown(c.price));
    out.append(now);
    return out;
}

void CurveChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));
    if (curve_.contracts.isEmpty()) {
        draw_message(p, rect(), message_);
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = plot_rect();
    const auto series = build_series();
    const auto& k = curve_.contracts;

    Range range;
    for (const auto& s : series)
        for (double v : s.values)
            range.add(v);
    if (!range.valid)
        return;
    range.pad();

    // Grid and the value axis.
    p.setFont(data_font());
    const QFontMetrics fm(p.font());
    const double step = nice_step(range.hi - range.lo, std::max(2, static_cast<int>(plot.height() / 44)));
    const int axis_dp = as_rate_ ? 3 : std::min(decimals_, step < 1 ? decimals_ : 2);
    for (double v = std::ceil(range.lo / step) * step; v <= range.hi; v += step) {
        const double y = range.y(v, plot);
        p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
        p.drawText(QRectF(plot.right() + 6, y - 8, kRight - 8, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QLocale::c().toString(v, 'f', axis_dp) + (as_rate_ ? QStringLiteral("%") : QString()));
    }

    // Month labels, thinned so they never collide; a year tick under each January.
    const int label_w = fm.horizontalAdvance(QStringLiteral("Mmm 00")) + 10;
    const int n = k.size();
    const double spacing = n > 1 ? x_at(1, plot) - x_at(0, plot) : plot.width();
    const int every = std::max(1, static_cast<int>(std::ceil(label_w / std::max(1.0, spacing))));
    for (int i = 0; i < n; i += every) {
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
        p.drawText(QRectF(x_at(i, plot) - label_w / 2.0, plot.bottom() + 3, label_w, kBottom - 4),
                   Qt::AlignHCenter | Qt::AlignTop, k[i].month_label());
    }

    for (int si = 0; si < series.size(); ++si) {
        const auto& s = series[si];
        const bool is_last = si == series.size() - 1;
        QPainterPath path;
        bool open = false;
        for (int i = 0; i < n; ++i) {
            // A month with no value breaks the line; nothing is interpolated.
            // Nor is today's curve drawn through a month that has not printed
            // lately — its old price beside live ones would bend the curve.
            if (!std::isfinite(s.values[i]) || (is_last && k[i].stale)) {
                open = false;
                continue;
            }
            const QPointF pt(x_at(i, plot), range.y(s.values[i], plot));
            if (open)
                path.lineTo(pt);
            else
                path.moveTo(pt);
            open = true;
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(s.color, s.style == Qt::SolidLine ? 2.0 : 1.3, s.style));
        p.drawPath(path);
    }
    // Markers on today's curve: hollow where the month has not printed lately.
    const auto& now = series.last();
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(now.values[i]))
            continue;
        const QPointF pt(x_at(i, plot), range.y(now.values[i], plot));
        p.setPen(QPen(now.color, 1.5));
        p.setBrush(k[i].stale ? QColor(ui::colors::BG_BASE()) : now.color);
        const double r = k[i].symbol == curve_.front ? 4.5 : 3.0;
        p.drawEllipse(pt, r, r);
    }

    QVector<QPair<QString, QColor>> legend;
    for (int i = series.size() - 1; i >= 0; --i)
        legend.append({series[i].label, series[i].color});
    draw_legend(p, plot.left() + 4, 12, legend);

    if (hover_ >= 0 && hover_ < n) {
        const double x = x_at(hover_, plot);
        p.setPen(QPen(QColor(ui::colors::BORDER_BRIGHT()), 1, Qt::DashLine));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const auto& c = k[hover_];
        QStringList lines{QStringLiteral("%1  %2%3").arg(c.month_label(), c.code(),
                                                         c.stale ? QStringLiteral("  (no recent print)") : QString())};
        QVector<QColor> colors{QColor(ui::colors::TEXT_PRIMARY())};
        const int dp = as_rate_ ? 4 : decimals_;
        for (int i = series.size() - 1; i >= 0; --i) {
            if (!std::isfinite(series[i].values[hover_]))
                continue;
            lines << QStringLiteral("%1  %2%3").arg(series[i].label, -12)
                         .arg(QLocale::c().toString(series[i].values[hover_], 'f', dp),
                              as_rate_ ? QStringLiteral("%") : QString());
            colors << series[i].color;
        }
        const double y = std::isfinite(now.values[hover_]) ? range.y(now.values[hover_], plot) : plot.center().y();
        draw_readout(p, rect(), QPointF(x, y), lines, colors);
    }
}

void CurveChart::mouseMoveEvent(QMouseEvent* e) {
    const int i = plot_rect().adjusted(-kLeft, 0, kRight, 0).contains(e->position()) ? index_at(e->position().x()) : -1;
    if (i != hover_) {
        hover_ = i;
        update();
    }
    setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void CurveChart::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const int i = index_at(e->position().x());
        if (i >= 0)
            emit contract_clicked(curve_.contracts[i].symbol);
    }
    QWidget::mousePressEvent(e);
}

void CurveChart::leaveEvent(QEvent*) {
    if (hover_ != -1) {
        hover_ = -1;
        update();
    }
}

// ════════════════════════════════════════════════════════════════════════════
// SpreadChart
// ════════════════════════════════════════════════════════════════════════════

SpreadChart::SpreadChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(120);
}

void SpreadChart::set_history(const SpreadHistory& history, int decimals) {
    history_ = history;
    decimals_ = decimals;
    message_.clear();
    hover_ = -1;
    update();
}

void SpreadChart::set_message(const QString& message) {
    history_ = {};
    message_ = message;
    hover_ = -1;
    update();
}

QRectF SpreadChart::plot_rect() const {
    return QRectF(kLeft, 10, width() - kLeft - kRight, height() - 10 - kBottom);
}

void SpreadChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));
    const auto& pts = history_.points;
    const int n = pts.size();
    if (n < 2) {
        draw_message(p, rect(), message_);
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = plot_rect();
    Range range;
    for (const auto& pt : pts)
        range.add(pt.spread);
    // Zero is where contango turns to backwardation, so it is kept in view
    // when the spread has been anywhere near it — but not when that would
    // flatten the line against one edge (an index spread sits 70 points up).
    if (range.valid && (range.lo <= 0 || range.hi >= 0) && std::min(std::abs(range.lo), std::abs(range.hi)) <= (range.hi - range.lo))
        range.add(0.0);
    range.pad();
    auto x_at = [&](int i) { return plot.left() + plot.width() * i / (n - 1); };

    p.setFont(data_font());
    const double step = nice_step(range.hi - range.lo, std::max(2, static_cast<int>(plot.height() / 36)));
    const int dp = std::min(decimals_, 4);
    for (double v = std::ceil(range.lo / step) * step; v <= range.hi; v += step) {
        const double y = range.y(v, plot);
        const bool zero = std::abs(v) < step * 1e-6;
        p.setPen(QPen(QColor(zero ? ui::colors::BORDER_BRIGHT() : ui::colors::BORDER_DIM()), 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
        p.drawText(QRectF(plot.right() + 6, y - 8, kRight - 8, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   zero ? QStringLiteral("0") : QLocale::c().toString(v, 'f', dp));
    }
    // A label at each new month.
    double last_label = -1e9;
    for (int i = 1; i < n; ++i)
        if (pts[i].date.month() != pts[i - 1].date.month() && x_at(i) - last_label > 46) {
            p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
            p.drawText(QPointF(x_at(i) + 2, plot.bottom() + 14), pts[i].date.toString(QStringLiteral("MMM")));
            last_label = x_at(i);
        }
    QPainterPath path;
    for (int i = 0; i < n; ++i) {
        const QPointF pt(x_at(i), range.y(pts[i].spread, plot));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(ui::colors::AMBER()), 2));
    p.drawPath(path);

    if (hover_ >= 0 && hover_ < n) {
        const double x = x_at(hover_);
        p.setPen(QPen(QColor(ui::colors::BORDER_BRIGHT()), 1, Qt::DashLine));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const auto& pt = pts[hover_];
        draw_readout(p, rect(), QPointF(x, range.y(pt.spread, plot)),
                     {pt.date.toString(QStringLiteral("d MMM yyyy")),
                      QStringLiteral("Spread  %1").arg(QLocale::c().toString(pt.spread, 'f', dp)),
                      QStringLiteral("Far     %1").arg(QLocale::c().toString(pt.far_px, 'f', dp)),
                      QStringLiteral("Near    %1").arg(QLocale::c().toString(pt.near_px, 'f', dp))},
                     {QColor(ui::colors::TEXT_PRIMARY()), QColor(ui::colors::AMBER()),
                      QColor(ui::colors::TEXT_SECONDARY()), QColor(ui::colors::TEXT_SECONDARY())});
    }
}

void SpreadChart::mouseMoveEvent(QMouseEvent* e) {
    const int n = history_.points.size();
    const QRectF plot = plot_rect();
    int i = -1;
    if (n >= 2 && plot.adjusted(-kLeft, 0, kRight, 0).contains(e->position()))
        i = std::clamp(static_cast<int>(std::lround((e->position().x() - plot.left()) / plot.width() * (n - 1))), 0,
                       n - 1);
    if (i != hover_) {
        hover_ = i;
        update();
    }
}

void SpreadChart::leaveEvent(QEvent*) {
    if (hover_ != -1) {
        hover_ = -1;
        update();
    }
}

// ════════════════════════════════════════════════════════════════════════════
// CotChart
// ════════════════════════════════════════════════════════════════════════════

CotChart::CotChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(130);
}

void CotChart::set_report(const CotReport& report) {
    report_ = report;
    message_.clear();
    hover_ = -1;
    update();
}

void CotChart::set_message(const QString& message) {
    report_ = {};
    message_ = message;
    hover_ = -1;
    update();
}

QRectF CotChart::plot_rect() const {
    return QRectF(kLeft, kTop, width() - kLeft - kRight, height() - kTop - kBottom);
}

void CotChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));
    const auto& rows = report_.rows;
    if (rows.size() < 2) {
        draw_message(p, rect(), message_);
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = plot_rect();
    const int n = rows.size();
    const QColor spec(ui::colors::AMBER()), hedger(ui::colors::CYAN()), small(ui::colors::TEXT_TERTIARY());

    Range range;
    range.add(0.0);
    for (const auto& r : rows) {
        range.add(r.nc_net());
        range.add(r.c_net());
        range.add(r.nr_net());
    }
    range.pad();
    auto x_at = [&](int i) { return plot.left() + plot.width() * i / (n - 1); };

    p.setFont(data_font());
    const double step = nice_step(range.hi - range.lo, std::max(2, static_cast<int>(plot.height() / 40)));
    for (double v = std::ceil(range.lo / step) * step; v <= range.hi; v += step) {
        const double y = range.y(v, plot);
        const bool zero = std::abs(v) < step * 1e-6;
        p.setPen(QPen(QColor(zero ? ui::colors::BORDER_BRIGHT() : ui::colors::BORDER_DIM()), 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
        p.drawText(QRectF(plot.right() + 6, y - 8, kRight - 8, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   zero ? QStringLiteral("0") : ui::formatting::format_compact(v, 0));
    }
    // A label at each January.
    for (int i = 1; i < n; ++i)
        if (rows[i].date.year() != rows[i - 1].date.year()) {
            const double x = x_at(i);
            p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
            p.drawText(QPointF(x + 4, plot.bottom() + 14), QString::number(rows[i].date.year()));
        }

    auto draw = [&](const QColor& color, double width, double (CotRow::*net)() const) {
        QPainterPath path;
        bool open = false;
        for (int i = 0; i < n; ++i) {
            const double v = (rows[i].*net)();
            if (!std::isfinite(v)) {
                open = false;
                continue;
            }
            const QPointF pt(x_at(i), range.y(v, plot));
            if (open)
                path.lineTo(pt);
            else
                path.moveTo(pt);
            open = true;
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color, width));
        p.drawPath(path);
    };
    draw(small, 1.0, &CotRow::nr_net);
    draw(hedger, 1.5, &CotRow::c_net);
    draw(spec, 2.0, &CotRow::nc_net);

    draw_legend(p, plot.left() + 4, 12,
                {{QStringLiteral("Speculators"), spec},
                 {QStringLiteral("Commercials"), hedger},
                 {QStringLiteral("Small traders"), small}});

    if (hover_ >= 0 && hover_ < n) {
        const double x = x_at(hover_);
        p.setPen(QPen(QColor(ui::colors::BORDER_BRIGHT()), 1, Qt::DashLine));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const auto& r = rows[hover_];
        auto num = [](double v) {
            return std::isfinite(v) ? QLocale(QLocale::English).toString(static_cast<qlonglong>(std::llround(v)))
                                    : ui::formatting::placeholder();
        };
        draw_readout(p, rect(), QPointF(x, range.y(std::isfinite(r.nc_net()) ? r.nc_net() : 0.0, plot)),
                     {r.date.toString(QStringLiteral("d MMM yyyy")),
                      QStringLiteral("Speculators  %1").arg(num(r.nc_net())),
                      QStringLiteral("Commercials  %1").arg(num(r.c_net())),
                      QStringLiteral("Small        %1").arg(num(r.nr_net())),
                      QStringLiteral("Open int.    %1").arg(num(r.oi))},
                     {QColor(ui::colors::TEXT_PRIMARY()), spec, hedger, small, QColor(ui::colors::TEXT_SECONDARY())});
    }
}

void CotChart::mouseMoveEvent(QMouseEvent* e) {
    const int n = report_.rows.size();
    int i = -1;
    const QRectF plot = plot_rect();
    if (n >= 2 && plot.adjusted(-kLeft, 0, kRight, 0).contains(e->position()))
        i = std::clamp(static_cast<int>(std::lround((e->position().x() - plot.left()) / plot.width() * (n - 1))), 0,
                       n - 1);
    if (i != hover_) {
        hover_ = i;
        update();
    }
}

void CotChart::leaveEvent(QEvent*) {
    if (hover_ != -1) {
        hover_ = -1;
        update();
    }
}

} // namespace finterm::screens::futures

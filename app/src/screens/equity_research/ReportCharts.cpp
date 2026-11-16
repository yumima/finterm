// src/screens/equity_research/ReportCharts.cpp
#include "screens/equity_research/ReportCharts.h"

#include "ui/theme/Theme.h"

#include <QFontMetrics>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace finterm::screens::report {

namespace {

constexpr int kPad = 12;
constexpr int kTitleH = 20;
constexpr int kNoteH = 16;
constexpr int kLegendH = 18;

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
    void pad(double frac) {
        const double span = hi - lo;
        const double m = span > 0 ? span * frac : std::max(std::abs(hi) * 0.05, 1e-9);
        if (lo != 0.0 || hi < 0)
            lo -= m;
        if (hi != 0.0 || lo > 0)
            hi += m;
    }
    double at(double v, double top, double bottom) const { return bottom - (v - lo) / (hi - lo) * (bottom - top); }
};

double nice_step(double span, int target) {
    if (!(span > 0))
        return 1.0;
    const double raw = span / std::max(1, target);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double n = raw / mag;
    return (n < 1.5 ? 1.0 : n < 3.5 ? 2.0 : n < 7.5 ? 5.0 : 10.0) * mag;
}

QFont font(const Palette& pal, int delta = 0, bool bold = false) {
    QFont f(pal.font_family);
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(std::max(10, pal.font_px + delta));
    f.setBold(bold);
    return f;
}

QImage canvas(QSize logical, qreal dpr, const Palette& pal) {
    QImage img(logical * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(pal.panel);
    return img;
}

/// Title and note; returns the y where the body starts.
int header(QPainter& p, const Palette& pal, int width, const QString& title, const QString& note) {
    int y = kPad;
    p.setPen(pal.text);
    p.setFont(font(pal, 1, true));
    p.drawText(QRect(kPad, y, width - 2 * kPad, kTitleH), Qt::AlignLeft | Qt::AlignVCenter, title);
    y += kTitleH;
    if (!note.isEmpty()) {
        p.setPen(pal.text_dim);
        p.setFont(font(pal, -1));
        p.drawText(QRect(kPad, y, width - 2 * kPad, kNoteH), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(p.font()).elidedText(note, Qt::ElideRight, width - 2 * kPad));
        y += kNoteH;
    }
    return y + 4;
}

void legend(QPainter& p, const Palette& pal, int x, int y, const QVector<QPair<QString, QColor>>& items, bool line) {
    p.setFont(font(pal, -1));
    const QFontMetrics fm(p.font());
    for (const auto& it : items) {
        if (line) {
            p.setPen(QPen(it.second, 2));
            p.drawLine(x, y + kLegendH / 2, x + 12, y + kLegendH / 2);
        } else {
            p.fillRect(QRect(x, y + kLegendH / 2 - 4, 10, 9), it.second);
        }
        p.setPen(pal.text_dim);
        p.drawText(QRect(x + 16, y, 400, kLegendH), Qt::AlignLeft | Qt::AlignVCenter, it.first);
        x += 16 + fm.horizontalAdvance(it.first) + 14;
    }
}

void empty_note(QPainter& p, const Palette& pal, const QRect& r) {
    p.setPen(pal.text_dim);
    p.setFont(font(pal));
    p.drawText(r, Qt::AlignCenter, QStringLiteral("No data reported"));
}

QString fmt_or(const Format& f, double v) {
    return f ? f(v) : compact(v);
}

} // namespace

Palette Palette::from_theme() {
    Palette p;
    p.bg = QColor(ui::colors::BG_BASE());
    p.panel = QColor(ui::colors::BG_SURFACE());
    p.border = QColor(ui::colors::BORDER_DIM());
    p.text = QColor(ui::colors::TEXT_PRIMARY());
    p.text_dim = QColor(ui::colors::TEXT_TERTIARY());
    p.grid = QColor(ui::colors::BORDER_DIM());
    p.accent = QColor(ui::colors::AMBER());
    p.positive = QColor(ui::colors::POSITIVE());
    p.negative = QColor(ui::colors::NEGATIVE());
    p.series = {QColor(ui::colors::AMBER()), QColor(ui::colors::CYAN()), QColor(ui::colors::INFO()),
                QColor(ui::colors::POSITIVE()), QColor(0xB0, 0x7A, 0xD9), QColor(ui::colors::TEXT_SECONDARY())};
    // A report is read, not scanned: the system's proportional face rather
    // than the terminal's monospace, one size up from the data tables.
    p.font_family = QStringLiteral("sans-serif");
    p.font_px = ui::fonts::font_px(-1);
    return p;
}

Palette Palette::paper() {
    Palette p;
    p.bg = QColor(0xFF, 0xFF, 0xFF);
    p.panel = QColor(0xFF, 0xFF, 0xFF);
    p.border = QColor(0xD5, 0xD8, 0xDD);
    p.text = QColor(0x1B, 0x1F, 0x24);
    p.text_dim = QColor(0x5B, 0x63, 0x6E);
    p.grid = QColor(0xE6, 0xE8, 0xEB);
    p.accent = QColor(0xB3, 0x5C, 0x00);
    p.positive = QColor(0x12, 0x7A, 0x3E);
    p.negative = QColor(0xB4, 0x23, 0x18);
    p.series = {QColor(0xB3, 0x5C, 0x00), QColor(0x0B, 0x6E, 0x99), QColor(0x3D, 0x5A, 0xB8),
                QColor(0x12, 0x7A, 0x3E), QColor(0x7A, 0x3E, 0xA8), QColor(0x5B, 0x63, 0x6E)};
    p.font_family = QStringLiteral("sans-serif");
    p.font_px = 12;
    return p;
}

// ── bars and lines over categories ─────────────────────────────────────────

QImage render_chart(const ChartSpec& spec, const Palette& pal, QSize logical, qreal dpr) {
    QImage img = canvas(logical, dpr, pal);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = logical.width(), H = logical.height();
    int top = header(p, pal, W, spec.title, spec.note);

    QVector<QPair<QString, QColor>> bar_legend, line_legend;
    auto color_of = [&pal](int i) { return pal.series[((i % pal.series.size()) + pal.series.size()) % pal.series.size()]; };
    for (const auto& s : spec.bars)
        bar_legend.append({s.name, color_of(s.color)});
    for (const auto& s : spec.lines)
        line_legend.append({s.name, color_of(s.color)});
    const bool show_legend = bar_legend.size() + line_legend.size() > 1;
    if (show_legend) {
        legend(p, pal, kPad, top, bar_legend, false);
        int x = kPad;
        p.setFont(font(pal, -1));
        for (const auto& it : bar_legend)
            x += 16 + QFontMetrics(p.font()).horizontalAdvance(it.first) + 14;
        legend(p, pal, x, top, line_legend, true);
        top += kLegendH + 2;
    }

    const int n = spec.categories.size();
    const bool right_axis = spec.lines_on_right && !spec.lines.isEmpty();
    p.setFont(font(pal, -1));
    const QFontMetrics fm(p.font());
    const int axis_w = 52;
    const QRectF plot(kPad + axis_w, top + 6, W - 2 * kPad - axis_w - (right_axis ? axis_w : 0), H - top - 6 - 22 - kPad);

    Range left, right;
    if (spec.stacked) {
        for (int i = 0; i < n; ++i) {
            double pos = 0, neg = 0;
            for (const auto& s : spec.bars) {
                const double v = i < s.values.size() ? s.values[i] : qQNaN();
                if (std::isfinite(v))
                    (v >= 0 ? pos : neg) += v;
            }
            left.add(pos);
            left.add(neg);
        }
    } else {
        for (const auto& s : spec.bars)
            for (double v : s.values)
                left.add(v);
    }
    for (const auto& s : spec.lines)
        for (double v : s.values)
            (right_axis ? right : left).add(v);
    if (std::isfinite(spec.rule))
        left.add(spec.rule);
    if (!left.valid && !right.valid) {
        empty_note(p, pal, plot.toRect());
        return img;
    }
    if (!spec.free_baseline && left.valid)
        left.add(0.0);
    if (left.valid)
        left.pad(spec.bar_labels ? 0.14 : 0.08);
    if (right.valid) {
        right.pad(0.12);
    }
    if (!left.valid)
        left = right;

    // Grid off the left axis.
    const double step = nice_step(left.hi - left.lo, std::max(2, static_cast<int>(plot.height() / 38)));
    for (double v = std::ceil(left.lo / step) * step; v <= left.hi + step * 1e-9; v += step) {
        const double yy = left.at(v, plot.top(), plot.bottom());
        const bool zero = std::abs(v) < step * 1e-6;
        p.setPen(QPen(zero ? pal.text_dim : pal.grid, 1));
        p.drawLine(QPointF(plot.left(), yy), QPointF(plot.right(), yy));
        p.setPen(pal.text_dim);
        p.drawText(QRectF(kPad, yy - 8, axis_w - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   fmt_or(spec.fmt, zero ? 0.0 : v));
    }
    if (right_axis && right.valid) {
        const double rstep = nice_step(right.hi - right.lo, 4);
        for (double v = std::ceil(right.lo / rstep) * rstep; v <= right.hi + rstep * 1e-9; v += rstep) {
            const double yy = right.at(v, plot.top(), plot.bottom());
            p.setPen(pal.text_dim);
            p.drawText(QRectF(plot.right() + 6, yy - 8, axis_w - 6, 16), Qt::AlignLeft | Qt::AlignVCenter,
                       fmt_or(spec.fmt_right, std::abs(v) < rstep * 1e-6 ? 0.0 : v));
        }
    }
    if (std::isfinite(spec.rule)) {
        const double yy = left.at(spec.rule, plot.top(), plot.bottom());
        p.setPen(QPen(pal.text_dim, 1, Qt::DashLine));
        p.drawLine(QPointF(plot.left(), yy), QPointF(plot.right(), yy));
    }

    if (n == 0)
        return img;
    const double slot = plot.width() / n;
    auto cx = [&](int i) { return plot.left() + slot * (i + 0.5); };

    // Category labels: each drawn only if it clears the one before it and
    // fits inside the plot. An empty label marks a category that has none.
    p.setPen(pal.text_dim);
    double last_right = -1e9;
    for (int i = 0; i < n; ++i) {
        const QString& text = spec.categories[i];
        if (text.isEmpty())
            continue;
        const double w = fm.horizontalAdvance(text);
        const double left_x = cx(i) - w / 2;
        if (left_x < last_right + 10 || left_x < plot.left() - axis_w / 2.0 || left_x + w > W - 2)
            continue;
        p.drawText(QRectF(left_x, plot.bottom() + 4, w + 2, 16), Qt::AlignLeft | Qt::AlignTop, text);
        last_right = left_x + w;
    }

    // Bars.
    const int groups = spec.stacked ? 1 : std::max<int>(1, spec.bars.size());
    const double group_w = std::min(slot * 0.72, 64.0 * groups);
    const double bar_w = group_w / groups;
    const double zero_y = left.at(std::clamp(0.0, left.lo, left.hi), plot.top(), plot.bottom());
    for (int i = 0; i < n; ++i) {
        double pos = 0, neg = 0;
        for (int si = 0; si < spec.bars.size(); ++si) {
            const auto& s = spec.bars[si];
            const double v = i < s.values.size() ? s.values[i] : qQNaN();
            if (!std::isfinite(v))
                continue;
            double y0, y1, x0;
            if (spec.stacked) {
                double& base = v >= 0 ? pos : neg;
                y0 = left.at(base, plot.top(), plot.bottom());
                base += v;
                y1 = left.at(base, plot.top(), plot.bottom());
                x0 = cx(i) - group_w / 2;
            } else {
                y0 = zero_y;
                y1 = left.at(v, plot.top(), plot.bottom());
                x0 = cx(i) - group_w / 2 + si * bar_w;
            }
            const QRectF r(x0 + 1, std::min(y0, y1), bar_w - 2, std::max(1.0, std::abs(y1 - y0)));
            p.fillRect(r, color_of(s.color));
            if (spec.bar_labels && !spec.stacked && bar_w >= 26) {
                p.setPen(pal.text);
                p.setFont(font(pal, -2));
                const QString t = fmt_or(spec.fmt, v);
                p.drawText(QRectF(r.center().x() - 50, v >= 0 ? r.top() - 15 : r.bottom() + 1, 100, 14),
                           Qt::AlignHCenter | Qt::AlignVCenter, t);
            }
        }
    }

    // Lines.
    for (const auto& s : spec.lines) {
        const Range& rg = right_axis && right.valid ? right : left;
        QPainterPath path;
        bool open = false;
        QVector<QPointF> pts;
        for (int i = 0; i < n; ++i) {
            const double v = i < s.values.size() ? s.values[i] : qQNaN();
            if (!std::isfinite(v)) {
                open = false;
                continue;
            }
            const QPointF pt(cx(i), rg.at(v, plot.top(), plot.bottom()));
            if (open)
                path.lineTo(pt);
            else
                path.moveTo(pt);
            open = true;
            pts.append(pt);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color_of(s.color), 2));
        p.drawPath(path);
        if (n <= 16) { // markers only where points are far enough apart to read
            p.setBrush(pal.panel);
            for (const auto& pt : pts)
                p.drawEllipse(pt, 3, 3);
        }
    }
    return img;
}

// ── horizontal bars ────────────────────────────────────────────────────────

QImage render_hbars(const HBarSpec& spec, const Palette& pal, QSize logical, qreal dpr) {
    QImage img = canvas(logical, dpr, pal);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = logical.width(), H = logical.height();
    const int top = header(p, pal, W, spec.title, spec.note);
    const int n = spec.labels.size();
    Range rg;
    rg.add(0.0);
    for (double v : spec.values)
        rg.add(v);
    rg.add(spec.reference);
    const QRect body(kPad, top, W - 2 * kPad, H - top - kPad);
    if (n == 0 || rg.hi == rg.lo) {
        empty_note(p, pal, body);
        return img;
    }
    p.setFont(font(pal, -1));
    const QFontMetrics fm(p.font());
    int label_w = 0;
    for (const auto& l : spec.labels)
        label_w = std::max(label_w, fm.horizontalAdvance(l));
    label_w += 8;
    const int value_w = 58;
    const double x0 = body.left() + label_w, x1 = body.right() - value_w;
    auto x_at = [&](double v) { return x0 + (v - rg.lo) / (rg.hi - rg.lo) * (x1 - x0); };
    const double ref_h = std::isfinite(spec.reference) ? 16 : 0;
    const double row_h = std::min(26.0, (body.height() - ref_h) / n);
    const double zero_x = x_at(0.0);
    if (std::isfinite(spec.reference)) {
        const double xr = x_at(spec.reference);
        p.setPen(QPen(pal.text, 1, Qt::DashLine));
        p.drawLine(QPointF(xr, body.top() + ref_h - 2), QPointF(xr, body.top() + ref_h + row_h * n));
        p.setFont(font(pal, -2));
        p.setPen(pal.text_dim);
        const QString t = spec.reference_label + QLatin1Char(' ') + fmt_or(spec.fmt, spec.reference);
        const int tw = QFontMetrics(p.font()).horizontalAdvance(t);
        p.drawText(QRectF(std::clamp(xr - tw / 2.0, double(body.left()), double(body.right() - tw)), body.top(), tw + 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, t);
    }
    for (int i = 0; i < n; ++i) {
        const double y = body.top() + ref_h + row_h * i;
        const bool hi = i == spec.highlight;
        p.setPen(hi ? pal.text : pal.text_dim);
        p.setFont(font(pal, -1, hi));
        p.drawText(QRectF(body.left(), y, label_w - 6, row_h), Qt::AlignRight | Qt::AlignVCenter, spec.labels[i]);
        const double v = i < spec.values.size() ? spec.values[i] : qQNaN();
        if (!std::isfinite(v)) {
            p.setPen(pal.text_dim);
            p.drawText(QRectF(x0 + 4, y, 120, row_h), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("—"));
            continue;
        }
        const double xv = x_at(v);
        QColor c = hi ? pal.accent : pal.text_dim;
        if (!hi)
            c.setAlphaF(0.55);
        p.fillRect(QRectF(std::min(zero_x, xv), y + row_h * 0.22, std::max(1.0, std::abs(xv - zero_x)), row_h * 0.56), c);
        // On a plate, so the median rule never runs through a figure.
        const QString text = fmt_or(spec.fmt, v);
        const QRectF plate(std::max(zero_x, xv) + 3, y + 3, fm.horizontalAdvance(text) + 6, row_h - 6);
        p.fillRect(plate, pal.panel);
        p.setPen(hi ? pal.text : pal.text_dim);
        p.drawText(plate, Qt::AlignCenter, text);
    }
    return img;
}

// ── range strip ────────────────────────────────────────────────────────────

QImage render_range(const RangeSpec& spec, const Palette& pal, QSize logical, qreal dpr) {
    QImage img = canvas(logical, dpr, pal);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = logical.width(), H = logical.height();
    const int top = header(p, pal, W, spec.title, spec.note);
    const QRect body(kPad, top, W - 2 * kPad, H - top - kPad);
    if (!std::isfinite(spec.low) || !std::isfinite(spec.high) || !(spec.high > spec.low)) {
        empty_note(p, pal, body);
        return img;
    }
    Range rg;
    rg.add(spec.low);
    rg.add(spec.high);
    rg.add(spec.current);
    rg.pad(0.12);
    const double x0 = body.left() + 10, x1 = body.right() - 10;
    auto x_at = [&](double v) { return x0 + (v - rg.lo) / (rg.hi - rg.lo) * (x1 - x0); };
    const double mid = body.top() + body.height() * 0.52;
    QColor band = pal.accent;
    band.setAlphaF(0.30);
    p.fillRect(QRectF(x_at(spec.low), mid - 7, x_at(spec.high) - x_at(spec.low), 14), band);
    p.setFont(font(pal, -1));
    p.setPen(pal.text_dim);
    p.drawText(QRectF(x_at(spec.low) - 60, mid + 10, 120, 16), Qt::AlignHCenter, QStringLiteral("low ") + fmt_or(spec.fmt, spec.low));
    p.drawText(QRectF(x_at(spec.high) - 60, mid + 10, 120, 16), Qt::AlignHCenter, QStringLiteral("high ") + fmt_or(spec.fmt, spec.high));
    if (std::isfinite(spec.mean)) {
        p.setPen(QPen(pal.accent, 3));
        p.drawLine(QPointF(x_at(spec.mean), mid - 11), QPointF(x_at(spec.mean), mid + 11));
        p.setPen(pal.text);
        p.setFont(font(pal, -1, true));
        p.drawText(QRectF(x_at(spec.mean) - 90, mid + 26, 180, 16), Qt::AlignHCenter,
                   spec.mean_label + QLatin1Char(' ') + fmt_or(spec.fmt, spec.mean));
    }
    if (std::isfinite(spec.current)) {
        const double xc = x_at(spec.current);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.text);
        QPainterPath tri;
        tri.moveTo(xc, mid - 9);
        tri.lineTo(xc - 6, mid - 20);
        tri.lineTo(xc + 6, mid - 20);
        tri.closeSubpath();
        p.drawPath(tri);
        p.setPen(pal.text);
        p.setFont(font(pal, -1, true));
        const QString t = spec.current_label + QLatin1Char(' ') + fmt_or(spec.fmt, spec.current);
        const int tw = QFontMetrics(p.font()).horizontalAdvance(t);
        p.drawText(QRectF(std::clamp(xc - tw / 2.0, double(body.left()), double(body.right() - tw)), mid - 38, tw + 4, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, t);
    }
    return img;
}

// ── stacked rows ───────────────────────────────────────────────────────────

QImage render_stack(const StackSpec& spec, const Palette& pal, QSize logical, qreal dpr) {
    QImage img = canvas(logical, dpr, pal);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = logical.width(), H = logical.height();
    int top = header(p, pal, W, spec.title, spec.note);
    QVector<QPair<QString, QColor>> items;
    for (int i = 0; i < spec.part_names.size() && i < spec.part_colors.size(); ++i)
        items.append({spec.part_names[i], spec.part_colors[i]});
    legend(p, pal, kPad, top, items, false);
    top += kLegendH + 4;
    const QRect body(kPad, top, W - 2 * kPad, H - top - kPad);
    const int n = spec.rows.size();
    if (n == 0) {
        empty_note(p, pal, body);
        return img;
    }
    p.setFont(font(pal, -1));
    const QFontMetrics fm(p.font());
    int label_w = 0;
    for (const auto& l : spec.row_labels)
        label_w = std::max(label_w, fm.horizontalAdvance(l));
    label_w += 10;
    const double row_h = std::min(30.0, double(body.height()) / n);
    for (int r = 0; r < n; ++r) {
        const double y = body.top() + row_h * r;
        p.setPen(pal.text_dim);
        p.setFont(font(pal, -1));
        p.drawText(QRectF(body.left(), y, label_w - 6, row_h), Qt::AlignRight | Qt::AlignVCenter,
                   r < spec.row_labels.size() ? spec.row_labels[r] : QString());
        double total = 0;
        for (double v : spec.rows[r])
            if (std::isfinite(v) && v > 0)
                total += v;
        if (!(total > 0))
            continue;
        double x = body.left() + label_w;
        const double w = body.width() - label_w;
        for (int i = 0; i < spec.rows[r].size() && i < spec.part_colors.size(); ++i) {
            const double v = spec.rows[r][i];
            if (!std::isfinite(v) || v <= 0)
                continue;
            const double seg = w * v / total;
            p.fillRect(QRectF(x, y + row_h * 0.18, seg - 1, row_h * 0.64), spec.part_colors[i]);
            if (seg >= 20) {
                p.setPen(pal.panel);
                p.setFont(font(pal, -1, true));
                p.drawText(QRectF(x, y, seg - 1, row_h), Qt::AlignCenter, QString::number(static_cast<int>(std::lround(v))));
            }
            x += seg;
        }
    }
    return img;
}

} // namespace finterm::screens::report

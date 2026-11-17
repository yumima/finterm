// src/screens/pre_ipo/PrivateMarketCharts.cpp
#include "screens/pre_ipo/PrivateMarketCharts.h"

#include "screens/pre_ipo/PrivateMarketMath.h"
#include "ui/charts/ChartFactory.h"
#include "ui/theme/Theme.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QDateTime>
#include <QMap>
#include <QtCharts/QDateTimeAxis>
#include <QHBoxLayout>
#include <QLabel>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QPushButton>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace finterm::screens::widgets {

using pre_ipo::FinancingRound;
using pre_ipo::PricePoint;
using pre_ipo::PrivateCompany;

// ── RangeChart ───────────────────────────────────────────────────────────────

RangeChart::RangeChart(Builder build, int initial_days, QWidget* parent) : QWidget(parent), build_(std::move(build)) {
    lay_ = new QVBoxLayout(this);
    lay_->setContentsMargins(0, 0, 0, 0);
    lay_->setSpacing(2);
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    row->addStretch();
    const QVector<QPair<int, QString>> ranges{{182, "6M"}, {365, "1Y"}, {730, "2Y"}, {0, "MAX"}};
    for (const auto& [days, label] : ranges) {
        auto* b = new QPushButton(label);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(20);
        b->setStyleSheet(QString("QPushButton{color:%1;background:transparent;border:1px solid %2;padding:0 8px;"
                                 "font-size:10px;}QPushButton:checked{color:%3;border-color:%3;}")
                             .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER()));
        connect(b, &QPushButton::clicked, this, [this, d = days]() { show_range(d); });
        row->addWidget(b);
        buttons_.append({days, b});
    }
    lay_->addLayout(row);
    note_ = new QLabel;
    note_->setWordWrap(true);
    note_->setStyleSheet(QString("color:%1;font-size:10px;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    note_->setVisible(false);
    show_range(initial_days);
}

void RangeChart::show_range(int days) {
    for (const auto& [d, b] : buttons_)
        b->setChecked(d == days);
    if (view_) {
        lay_->removeWidget(view_);
        view_->deleteLater();
        view_ = nullptr;
    }
    lay_->removeWidget(note_);
    QString note;
    view_ = build_ ? build_(days, &note) : nullptr;
    if (view_)
        lay_->addWidget(view_, 1);
    lay_->addWidget(note_);
    note_->setText(note);
    note_->setVisible(!note.isEmpty());
}

// ── Charts ───────────────────────────────────────────────────────────────────

namespace pm_charts {

namespace {

qint64 ms(const QDate& d) {
    return QDateTime(d, QTime(12, 0)).toMSecsSinceEpoch();
}

/// Model price on or before `d` (NaN before the series starts).
double price_at(const QVector<PricePoint>& pts, const QDate& d) {
    double v = pre_ipo::pm::kNaN;
    for (const auto& p : pts) {
        if (p.day > d)
            break;
        v = p.value;
    }
    return v;
}

/// Price per share of the newest priced round closed on or before `d`.
double round_pps_at(const QVector<FinancingRound>& rounds, const QDate& d) {
    for (const auto& r : rounds) // newest first
        if (r.month.isValid() && r.month <= d && pre_ipo::pm::usable(r.pps))
            return r.pps;
    return pre_ipo::pm::kNaN;
}

struct MarkPick {
    QVector<QPair<QDate, double>> shown; // consensus per as-of date
    int dropped = 0;                     // on another share basis
};

/// One dot per reporting date (the funds' average), kept only when it sits
/// on the chart's share basis. A fund marks per ITS share count, which a
/// split or a different class can put at many times the marketplace price;
/// plotted together, a 5x gap reads as a 5x discount.
MarkPick pick_marks(const PrivateCompany& c) {
    QMap<QDate, QPair<double, int>> by_date;
    for (const auto& m : c.fund_marks)
        if (m.as_of.isValid() && m.mark_pps > 0) {
            auto& acc = by_date[m.as_of];
            acc.first += m.mark_pps;
            acc.second += 1;
        }
    MarkPick out;
    for (auto it = by_date.cbegin(); it != by_date.cend(); ++it) {
        const double v = it->first / it->second;
        double ref = price_at(c.market.points, it.key());
        if (!pre_ipo::pm::usable(ref))
            ref = round_pps_at(c.financing, it.key());
        if (pre_ipo::pm::usable(ref) && (v / ref > 3.0 || v / ref < 1.0 / 3.0)) {
            ++out.dropped;
            continue;
        }
        out.shown.append({it.key(), v});
    }
    return out;
}

int priced_rounds(const QVector<FinancingRound>& rounds) {
    int n = 0;
    for (const auto& r : rounds)
        if (r.month.isValid() && pre_ipo::pm::usable(r.pps))
            ++n;
    return n;
}

QDateTimeAxis* date_axis(const QDate& lo, const QDate& hi) {
    auto* x = new QDateTimeAxis;
    x->setFormat(lo.daysTo(hi) > 800 ? QStringLiteral("yyyy") : QStringLiteral("MMM yy"));
    x->setTickCount(6);
    x->setRange(QDateTime(lo, QTime(0, 0)), QDateTime(hi, QTime(23, 59)));
    return x;
}

QChartView* finish(QChart* chart) {
    ui::ChartFactory::apply_theme(chart);
    auto* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setStyleSheet("background: transparent; border: none;");
    view->setMinimumHeight(160);
    return view;
}

} // namespace

bool has_price_history(const PrivateCompany& c) {
    return c.market.points.size() >= 2 || priced_rounds(c.financing) >= 2 || !pick_marks(c).shown.isEmpty();
}

QChartView* price_history(const PrivateCompany& c, int days, QString* note) {
    if (!has_price_history(c))
        return nullptr;
    const QDate today = QDate::currentDate();
    QDate hi = c.market.points.isEmpty() ? today : std::max(c.market.points.last().day, today.addDays(-1));
    // MAX = from the earliest thing on the chart: the series, a priced round,
    // or a mark.
    QDate lo = hi.addDays(-(days > 0 ? days : 365 * 15));
    if (days <= 0) {
        QDate first = c.market.points.isEmpty() ? QDate() : c.market.points.first().day;
        for (const auto& r : c.financing)
            if (r.month.isValid() && pre_ipo::pm::usable(r.pps) && (!first.isValid() || r.month < first))
                first = r.month;
        for (const auto& m : c.fund_marks)
            if (m.as_of.isValid() && m.mark_pps > 0 && (!first.isValid() || m.as_of < first))
                first = m.as_of;
        if (first.isValid())
            lo = first.addDays(-15);
    }

    auto* chart = new QChart;
    double ymin = 1e300, ymax = -1e300;
    auto track = [&](double v) {
        ymin = std::min(ymin, v);
        ymax = std::max(ymax, v);
    };

    // Model price.
    if (c.market.points.size() >= 2) {
        auto* line = new QLineSeries;
        line->setName(QString("%1 price (model)").arg(c.market.source.isEmpty() ? "Market" : c.market.source));
        line->setPen(QPen(QColor(ui::colors::CYAN()), 1.6));
        for (const auto& p : c.market.points)
            if (p.day >= lo && p.day <= hi) {
                line->append(ms(p.day), p.value);
                track(p.value);
            }
        chart->addSeries(line);
    }

    // Priced rounds as a step: each round's PPS from its month until the next
    // priced round (or today). Oldest → newest.
    QVector<const FinancingRound*> priced;
    for (const auto& r : c.financing)
        if (r.month.isValid() && pre_ipo::pm::usable(r.pps))
            priced.prepend(&r);
    if (!priced.isEmpty()) {
        auto* step = new QLineSeries;
        step->setName(QStringLiteral("Last round $/sh"));
        QPen pen(QColor(ui::colors::AMBER()), 1.2);
        pen.setStyle(Qt::DashLine);
        step->setPen(pen);
        auto* dots = new QScatterSeries;
        dots->setName(QStringLiteral("Round / tender"));
        dots->setMarkerSize(7);
        dots->setColor(QColor(ui::colors::AMBER()));
        dots->setBorderColor(QColor(ui::colors::AMBER()));
        for (int i = 0; i < priced.size(); ++i) {
            const QDate from = priced[i]->month;
            const QDate to = (i + 1 < priced.size()) ? priced[i + 1]->month : hi;
            if (to < lo || from > hi)
                continue;
            const QDate a = std::max(from, lo), b = std::min(to, hi);
            step->append(ms(a), priced[i]->pps);
            step->append(ms(b), priced[i]->pps);
            track(priced[i]->pps);
            if (from >= lo)
                dots->append(ms(from), priced[i]->pps);
        }
        if (step->count() > 0) {
            chart->addSeries(step);
            chart->addSeries(dots);
        } else {
            delete step;
            delete dots;
        }
    }

    // Fund marks.
    const MarkPick marks = pick_marks(c);
    if (!marks.shown.isEmpty()) {
        auto* sc = new QScatterSeries;
        sc->setName(QStringLiteral("Mutual-fund mark (N-PORT)"));
        sc->setMarkerShape(QScatterSeries::MarkerShapeRectangle);
        sc->setMarkerSize(7);
        sc->setColor(QColor(ui::colors::POSITIVE()));
        sc->setBorderColor(QColor(ui::colors::POSITIVE()));
        for (const auto& [d, v] : marks.shown)
            if (d >= lo && d <= hi) {
                sc->append(ms(d), v);
                track(v);
            }
        if (sc->count() > 0)
            chart->addSeries(sc);
        else
            delete sc;
    }

    if (note) {
        QStringList parts;
        if (!c.market.points.isEmpty())
            parts << QStringLiteral("Model price = %1's estimate from platform indications and trades, not a trade print.")
                         .arg(c.market.source);
        if (marks.dropped > 0)
            parts << QStringLiteral("%1 fund-mark date%2 left off: marked on another share basis (split or class).")
                         .arg(marks.dropped)
                         .arg(marks.dropped == 1 ? "" : "s");
        *note = parts.join(' ');
    }

    if (chart->series().isEmpty() || ymax < ymin) {
        delete chart;
        return nullptr;
    }
    auto* x = date_axis(lo, hi);
    chart->addAxis(x, Qt::AlignBottom);
    auto* y = new QValueAxis;
    const double pad = std::max((ymax - ymin) * 0.08, ymax * 0.02);
    y->setRange(std::max(0.0, ymin - pad), ymax + pad);
    y->setLabelFormat(QStringLiteral("$%.2f"));
    y->setTickCount(5);
    chart->addAxis(y, Qt::AlignRight);
    for (auto* s : chart->series()) {
        s->attachAxis(x);
        s->attachAxis(y);
    }
    QChartView* view = finish(chart);
    chart->legend()->setVisible(chart->series().size() > 1);
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->legend()->setLabelColor(QColor(ui::colors::TEXT_SECONDARY()));
    return view;
}

QChartView* index_history(const QVector<PricePoint>& pts, int days) {
    if (pts.size() < 2)
        return nullptr;
    const QDate hi = pts.last().day;
    const QDate lo = days > 0 ? hi.addDays(-days) : pts.first().day;
    auto* line = new QLineSeries;
    double ymin = 1e300, ymax = -1e300, first = pre_ipo::pm::kNaN;
    for (const auto& p : pts)
        if (p.day >= lo) {
            line->append(ms(p.day), p.value);
            ymin = std::min(ymin, p.value);
            ymax = std::max(ymax, p.value);
            if (!std::isfinite(first))
                first = p.value;
        }
    if (line->count() < 2) {
        delete line;
        return nullptr;
    }
    const bool up = pts.last().value >= first;
    line->setPen(QPen(QColor(up ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()), 1.6));
    auto* chart = new QChart;
    chart->addSeries(line);
    auto* x = date_axis(lo, hi);
    chart->addAxis(x, Qt::AlignBottom);
    auto* y = new QValueAxis;
    const double pad = std::max((ymax - ymin) * 0.08, 1.0);
    y->setRange(ymin - pad, ymax + pad);
    y->setLabelFormat(QStringLiteral("%.0f"));
    y->setTickCount(5);
    chart->addAxis(y, Qt::AlignRight);
    line->attachAxis(x);
    line->attachAxis(y);
    return finish(chart);
}

} // namespace pm_charts

} // namespace finterm::screens::widgets

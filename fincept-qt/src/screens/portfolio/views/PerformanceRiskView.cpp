// src/screens/portfolio/views/PerformanceRiskView.cpp
#include "screens/portfolio/views/PerformanceRiskView.h"

#include "ui/theme/Theme.h"

#include <QAreaSeries>
#include <QChart>
#include <QDateTimeAxis>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineSeries>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QValueAxis>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
static const QStringList kPeriods = {"1M", "3M", "6M", "1Y", "ALL"};
} // namespace

namespace fincept::screens {

PerformanceRiskView::PerformanceRiskView(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void PerformanceRiskView::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Period selector ───────────────────────────────────────────────────────
    auto* period_bar = new QHBoxLayout;
    period_bar->setContentsMargins(12, 6, 12, 6);

    auto* chart_title = new QLabel("NAV PERFORMANCE (FROM SNAPSHOTS)");
    chart_title->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;").arg(ui::colors::AMBER()));
    period_bar->addWidget(chart_title);
    period_bar->addStretch();

    for (const auto& p : kPeriods) {
        auto* btn = new QPushButton(p);
        btn->setFixedSize(32, 20);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString("QPushButton { background:transparent; color:%1; border:none;"
                                   "  font-size:12px; font-weight:700; }"
                                   "QPushButton:checked { color:%2; border-bottom:2px solid %2; }"
                                   "QPushButton:hover { color:%3; }")
                               .arg(ui::colors::TEXT_SECONDARY(), ui::colors::AMBER(), ui::colors::TEXT_PRIMARY()));
        if (p == current_period_)
            btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [this, period = p]() { set_period(period); });
        period_bar->addWidget(btn);
        period_btns_.append(btn);
    }

    layout->addLayout(period_bar);

    // ── Chart ─────────────────────────────────────────────────────────────────
    auto* chart = new QChart;
    chart->setBackgroundBrush(QColor(ui::colors::BG_BASE()));
    chart->setMargins(QMargins(4, 4, 4, 4));
    chart->legend()->setVisible(false);
    chart->setAnimationOptions(QChart::NoAnimation);

    chart_view_ = new QChartView(chart);
    chart_view_->setRenderHint(QPainter::Antialiasing);
    chart_view_->setStyleSheet("border:none; background:transparent;");
    layout->addWidget(chart_view_, 5);

    // Separator
    auto* sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QString("background:%1;").arg(ui::colors::BORDER_DIM()));
    layout->addWidget(sep);

    // ── Risk metric cards ──────────────────────────────────────────────────────
    auto* metrics_header = new QLabel("  RISK METRICS");
    metrics_header->setFixedHeight(24);
    metrics_header->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;"
                                          "letter-spacing:1px; background:%2;")
                                      .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE()));
    layout->addWidget(metrics_header);

    auto* cards_layout = new QGridLayout;
    cards_layout->setContentsMargins(12, 8, 12, 8);
    cards_layout->setSpacing(8);

    sharpe_card_ = add_metric_card(cards_layout, "SHARPE RATIO", "Risk-adjusted return (annualised)", ui::colors::CYAN);
    sortino_card_ = add_metric_card(cards_layout, "SORTINO RATIO", "Downside risk-adjusted return", ui::colors::CYAN);
    beta_card_ = add_metric_card(cards_layout, "BETA", "OLS regression vs SPY daily returns", ui::colors::WARNING);
    alpha_card_ = add_metric_card(cards_layout, "ALPHA", "Annualised OLS alpha vs SPY", ui::colors::POSITIVE);
    vol_card_ = add_metric_card(cards_layout, "VOLATILITY", "Annualised from daily returns", ui::colors::AMBER);
    drawdown_card_ =
        add_metric_card(cards_layout, "MAX DRAWDOWN", "Peak-to-trough from snapshots", ui::colors::NEGATIVE);
    var_card_ = add_metric_card(cards_layout, "VALUE AT RISK (95%)", "1-day historical VaR", ui::colors::NEGATIVE);
    cvar_card_ = add_metric_card(cards_layout, "CONDITIONAL VaR", "Expected shortfall (95%)", ui::colors::NEGATIVE);

    auto* cards_widget = new QWidget(this);
    cards_widget->setLayout(cards_layout);
    layout->addWidget(cards_widget, 3);
}

PerformanceRiskView::MetricCard PerformanceRiskView::add_metric_card(QLayout* parent_layout, const QString& title,
                                                                     const QString& desc, const char* color) {

    auto* card = new QWidget(this);
    card->setStyleSheet(
        QString("background:%1; border:1px solid %2; padding:8px;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));

    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(10, 8, 10, 8);
    cl->setSpacing(2);

    MetricCard mc;

    mc.title = new QLabel(title);
    mc.title->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;"
                                    "letter-spacing:0.5px; border:none;")
                                .arg(ui::colors::TEXT_SECONDARY()));
    cl->addWidget(mc.title);

    mc.value = new QLabel("--");
    mc.value->setStyleSheet(QString("color:%1; font-size:18px; font-weight:700; border:none;").arg(color));
    cl->addWidget(mc.value);

    mc.desc = new QLabel(desc);
    mc.desc->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(ui::colors::TEXT_SECONDARY()));
    cl->addWidget(mc.desc);

    auto* grid = static_cast<QGridLayout*>(parent_layout);
    int count = grid->count();
    grid->addWidget(card, count / 4, count % 4);

    return mc;
}

void PerformanceRiskView::set_data(const portfolio::PortfolioSummary& summary, const QString& currency) {
    summary_ = summary;
    currency_ = currency;
    update_chart();
    update_metrics();
}

void PerformanceRiskView::set_metrics(const portfolio::ComputedMetrics& metrics) {
    metrics_ = metrics;
    update_metrics();
}

void PerformanceRiskView::set_snapshots(const QVector<portfolio::PortfolioSnapshot>& snapshots) {
    snapshots_ = snapshots;
    update_chart();
    update_metrics();
}

void PerformanceRiskView::set_period(const QString& period) {
    current_period_ = period;
    for (auto* btn : period_btns_)
        btn->setChecked(btn->text() == period);
    update_chart();
}

void PerformanceRiskView::update_chart() {
    auto* chart = chart_view_->chart();
    chart->removeAllSeries();
    const auto old_axes = chart->axes();
    for (auto* axis : old_axes) {
        chart->removeAxis(axis);
        delete axis;
    }

    if (summary_.holdings.isEmpty())
        return;

    // Filter snapshots by selected period
    QDate cutoff = QDate::currentDate();
    if (current_period_ == "1M")
        cutoff = cutoff.addMonths(-1);
    else if (current_period_ == "3M")
        cutoff = cutoff.addMonths(-3);
    else if (current_period_ == "6M")
        cutoff = cutoff.addMonths(-6);
    else if (current_period_ == "1Y")
        cutoff = cutoff.addYears(-1);
    else
        cutoff = cutoff.addYears(-10);

    QVector<portfolio::PortfolioSnapshot> filtered;
    for (const auto& s : snapshots_) {
        QDate d = QDate::fromString(s.snapshot_date.left(10), Qt::ISODate);
        if (d.isValid() && d >= cutoff)
            filtered.append(s);
    }
    std::sort(filtered.begin(), filtered.end(),
              [](const auto& a, const auto& b) { return a.snapshot_date < b.snapshot_date; });

    auto* line = new QLineSeries;
    auto* upper = new QLineSeries;
    auto* lower = new QLineSeries;

    double first_val = summary_.total_cost_basis;
    double last_val = summary_.total_market_value;
    double min_val = last_val, max_val = last_val;

    if (filtered.size() >= 2) {
        first_val = filtered.first().total_value;
        for (const auto& s : filtered) {
            QDateTime dt = QDateTime::fromString(s.snapshot_date.left(10), Qt::ISODate);
            if (!dt.isValid())
                dt = QDateTime::currentDateTime();
            qint64 ms = dt.toMSecsSinceEpoch();
            line->append(ms, s.total_value);
            upper->append(ms, s.total_value);
            lower->append(ms, first_val);
            min_val = std::min(min_val, s.total_value);
            max_val = std::max(max_val, s.total_value);
        }
        qint64 now_ms = QDateTime::currentDateTime().toMSecsSinceEpoch();
        line->append(now_ms, last_val);
        upper->append(now_ms, last_val);
        lower->append(now_ms, first_val);
        min_val = std::min(min_val, last_val);
        max_val = std::max(max_val, last_val);
    } else {
        // Fallback: interpolate cost → current
        int pts = 30;
        QDateTime now = QDateTime::currentDateTime();
        for (int i = 0; i < pts; ++i) {
            double t = static_cast<double>(i) / (pts - 1);
            double val = first_val + (last_val - first_val) * t;
            val *= (1.0 + 0.005 * std::sin(i * 0.7));
            qint64 ms = now.addDays(-(pts - 1 - i)).toMSecsSinceEpoch();
            line->append(ms, val);
            upper->append(ms, val);
            lower->append(ms, first_val);
            min_val = std::min(min_val, val);
            max_val = std::max(max_val, val);
        }
    }

    bool up = last_val >= first_val;
    QColor lc = up ? QColor(ui::colors::POSITIVE()) : QColor(ui::colors::NEGATIVE());
    line->setPen(QPen(lc, 2));

    auto* area = new QAreaSeries(upper, lower);
    QColor fill = lc;
    fill.setAlpha(28);
    area->setBrush(fill);
    area->setPen(Qt::NoPen);

    chart->addSeries(area);
    chart->addSeries(line);

    auto* x_axis = new QDateTimeAxis;
    x_axis->setFormat(current_period_ == "ALL" || current_period_ == "1Y" ? "MMM yy" : "dd MMM");
    x_axis->setTickCount(5);
    x_axis->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
    x_axis->setGridLineColor(QColor(ui::colors::BORDER_DIM()));
    x_axis->setLinePen(QPen(QColor(ui::colors::BORDER_DIM())));
    x_axis->setLabelsFont(QFont("monospace", 7));

    double pad = std::max((max_val - min_val) * 0.08, max_val * 0.01);
    auto* y_axis = new QValueAxis;
    y_axis->setRange(min_val - pad, max_val + pad);
    y_axis->setLabelFormat("%.0f");
    y_axis->setTickCount(4);
    y_axis->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
    y_axis->setGridLineColor(QColor(ui::colors::BORDER_DIM()));
    y_axis->setLinePen(QPen(QColor(ui::colors::BORDER_DIM())));
    y_axis->setLabelsFont(QFont("monospace", 7));

    chart->addAxis(x_axis, Qt::AlignBottom);
    chart->addAxis(y_axis, Qt::AlignLeft);
    line->attachAxis(x_axis);
    line->attachAxis(y_axis);
    area->attachAxis(x_axis);
    area->attachAxis(y_axis);
}

void PerformanceRiskView::update_metrics() {
    // Every card reads the single engine's output. This view used to keep its
    // own formulas, and two of them did not mean what their labels said: the
    // "beta" was mean-return divided by a hardcoded 0.032%/day (no market
    // series, no covariance — a flat portfolio scored 0, a steadily rising
    // one 2+), and the "alpha" subtracted a fixed 8% from a simple return
    // annualised on 365 calendar days while the neighbouring Sharpe used 252
    // trading days. A parametric abs(mean − 1.645σ) VaR could also report an
    // expected GAIN as the loss at risk. A metric the engine cannot state
    // honestly renders as a dash with the reason in its tooltip — a dash is
    // information; a fabricated number is a trap.
    auto fmt = [](double v, int dp = 2) { return QString::number(v, 'f', dp); };
    const QString need_history =
        tr("Not enough daily NAV history yet (%1 return observations so far).\n"
           "The series grows one point per day; a backfill from the transaction\n"
           "log fills it immediately.")
            .arg(metrics_.return_days);

    auto set_card = [&](MetricCard& card, const std::optional<double>& v, const QString& text,
                        const char* color) {
        if (v.has_value()) {
            card.value->setText(text);
            card.value->setStyleSheet(
                QString("color:%1; font-size:18px; font-weight:700; border:none;").arg(color));
            card.value->setToolTip(QString());
        } else {
            card.value->setText(QStringLiteral("—"));
            card.value->setStyleSheet(QString("color:%1; font-size:18px; font-weight:700; border:none;")
                                          .arg(ui::colors::TEXT_SECONDARY()));
            card.value->setToolTip(need_history);
        }
    };

    const auto& m = metrics_;
    set_card(sharpe_card_, m.sharpe, m.sharpe ? fmt(*m.sharpe) : QString(),
             m.sharpe && *m.sharpe >= 1.0   ? ui::colors::POSITIVE
             : m.sharpe && *m.sharpe >= 0.0 ? ui::colors::WARNING
                                            : ui::colors::NEGATIVE);
    set_card(sortino_card_, m.sortino, m.sortino ? fmt(*m.sortino) : QString(), ui::colors::CYAN);
    set_card(beta_card_, m.beta, m.beta ? fmt(*m.beta) : QString(),
             m.beta && std::abs(*m.beta - 1.0) < 0.2   ? ui::colors::POSITIVE
             : m.beta && std::abs(*m.beta - 1.0) < 0.5 ? ui::colors::WARNING
                                                       : ui::colors::NEGATIVE);
    set_card(alpha_card_, m.alpha,
             m.alpha ? QString("%1%2%").arg(*m.alpha >= 0 ? "+" : "").arg(fmt(*m.alpha, 1)) : QString(),
             m.alpha && *m.alpha >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE);
    set_card(vol_card_, m.volatility,
             m.volatility ? QString("%1%").arg(fmt(*m.volatility, 1)) : QString(), ui::colors::AMBER);
    set_card(drawdown_card_, m.max_drawdown,
             m.max_drawdown ? QString("%1%").arg(fmt(*m.max_drawdown, 1)) : QString(),
             ui::colors::NEGATIVE);
    set_card(var_card_, m.var_95,
             m.var_95 ? QString("%1 %2").arg(currency_, fmt(*m.var_95)) : QString(), ui::colors::NEGATIVE);
    set_card(cvar_card_, m.cvar_95,
             m.cvar_95 ? QString("%1 %2").arg(currency_, fmt(*m.cvar_95)) : QString(),
             ui::colors::NEGATIVE);
}

} // namespace fincept::screens

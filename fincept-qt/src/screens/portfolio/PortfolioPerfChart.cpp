// src/screens/portfolio/PortfolioPerfChart.cpp
#include "screens/portfolio/PortfolioPerfChart.h"

#include "core/logging/Logger.h"
#include "services/markets/MarketDataService.h"
#include "ui/theme/Theme.h"

#include <QPointer>

#include <QAreaSeries>
#include <QCategoryAxis>
#include <QChart>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QHash>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineSeries>
#include <QMouseEvent>
#include <QScreen>
#include <QTimeZone>
#include <QVBoxLayout>
#include <QValueAxis>

#include <cmath>
#include <limits>

namespace {
static const QStringList kPeriods = {"1D", "1W", "1M", "3M", "YTD", "1Y", "ALL"};

// Filter ts_ms / vals in-place to NYSE regular-session bars only
// (Mon–Fri, 09:30–16:00 America/New_York). yfinance already omits
// non-trading bars for most symbols; this is a safety guard that also
// strips weekend timestamps that appear in raw 5d/30m responses.
void filter_to_market_hours(QVector<qint64>& ts_ms, QVector<double>& vals) {
    static const QTimeZone kET("America/New_York");
    int out = 0;
    for (int i = 0; i < ts_ms.size(); ++i) {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ts_ms[i], kET);
        if (dt.date().dayOfWeek() > 5) continue;
        const QTime t = dt.time();
        // 09:30 ≤ t ≤ 16:00 — both boundaries inclusive (16:00 IS the closing bar)
        if (t < QTime(9, 30) || t > QTime(16, 0)) continue;
        ts_ms[out] = ts_ms[i];
        vals[out]  = vals[i];
        ++out;
    }
    ts_ms.resize(out);
    vals.resize(out);
}

// Build QCategoryAxis label positions for a sequential-index daily chart.
// Returns (sequential_index, label_string) pairs. Targets ~5 visible labels.
// Consecutive identical dates (live-NAV point duplicating the last snapshot)
// are silently de-duplicated.
QVector<QPair<double, QString>> daily_seq_labels(const QVector<QDate>& dates,
                                                  const QString& period) {
    QVector<QPair<double, QString>> result;
    const int n = dates.size();
    if (n == 0) return result;

    QDate last_added;
    auto add = [&](int i, const QString& fmt) {
        if (dates[i] == last_added) return; // de-duplicate consecutive identical dates
        last_added = dates[i];
        result.append({static_cast<double>(i), dates[i].toString(fmt)});
    };

    if (period == "1W") {
        for (int i = 0; i < n; ++i) add(i, "ddd");
    } else if (period == "1M") {
        for (int i = 0; i < n; ++i)
            if (i == 0 || dates[i].weekNumber() != dates[i - 1].weekNumber())
                add(i, "MMM d");
    } else if (period == "3M" || period == "YTD" || period == "1Y") {
        for (int i = 0; i < n; ++i)
            if (i == 0 || dates[i].month() != dates[i - 1].month())
                add(i, "MMM");
    } else { // 5Y, ALL
        for (int i = 0; i < n; ++i)
            if (i == 0 || dates[i].year() != dates[i - 1].year())
                add(i, "yyyy");
    }
    return result;
}
} // namespace

namespace fincept::screens {

// ── CrosshairChartView ────────────────────────────────────────────────────────

CrosshairChartView::CrosshairChartView(QChart* chart, QWidget* parent) : QChartView(chart, parent) {
    setMouseTracking(true);

    // Vertical crosshair line (hidden by default)
    v_line_ = new QGraphicsLineItem(chart);
    QPen crosshair_pen(QColor(ui::colors::TEXT_SECONDARY()), 1, Qt::DashLine);
    v_line_->setPen(crosshair_pen);
    v_line_->setVisible(false);
    v_line_->setZValue(10);

    // Floating tooltip label (child of this widget, not the scene)
    tooltip_ = new QLabel(this);
    tooltip_->setWindowFlags(Qt::ToolTip);
    tooltip_->setStyleSheet(QString("QLabel { background:%1; color:%2; border:1px solid %3;"
                                    " font-size:12px; font-weight:600; padding:3px 6px; border-radius:3px; }")
                                .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED()));
    tooltip_->hide();
}

void CrosshairChartView::set_series_data(const QVector<QPointF>& pts,
                                          const QString& currency,
                                          const QVector<qint64>& snap_ts_ms) {
    pts_ = pts;
    currency_ = currency;
    snap_ts_ms_ = snap_ts_ms;
}

void CrosshairChartView::mouseMoveEvent(QMouseEvent* event) {
    QChartView::mouseMoveEvent(event);
    update_crosshair(event->pos());
}

void CrosshairChartView::leaveEvent(QEvent* event) {
    QChartView::leaveEvent(event);
    hide_crosshair();
}

void CrosshairChartView::update_crosshair(const QPoint& widget_pos) {
    if (pts_.isEmpty() || chart()->axes(Qt::Horizontal).isEmpty()) {
        hide_crosshair();
        return;
    }

    // Map widget pixel → chart value (sequential index or ms, depending on series type)
    const QPointF chart_val = chart()->mapToValue(QPointF(widget_pos));

    // Snap to nearest point by x
    int best_idx = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (int i = 0; i < pts_.size(); ++i) {
        const double dist = std::abs(pts_[i].x() - chart_val.x());
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }

    const QPointF& snap_pt = pts_[best_idx];

    // Map snapped point back to scene for the vertical crosshair line.
    // Only scene.x() is used (vertical line), so the y mismatch between
    // raw and display-transformed series is harmless.
    const QPointF scene_pos = chart()->mapToPosition(snap_pt);

    const QRectF plot = chart()->plotArea();
    v_line_->setLine(scene_pos.x(), plot.top(), scene_pos.x(), plot.bottom());
    v_line_->setVisible(true);

    // Resolve the actual epoch-ms for this point. snap_ts_ms_ is populated
    // when pts_ uses sequential bar indices as x (multi-day or daily-snap
    // charts). For 1D intraday pts_.x() is the epoch-ms directly.
    const bool has_real_ts = !snap_ts_ms_.isEmpty();
    const qint64 display_ms = has_real_ts
        ? (best_idx < snap_ts_ms_.size() ? snap_ts_ms_[best_idx] : 0)
        : static_cast<qint64>(snap_pt.x());
    const qint64 series_span_ms = has_real_ts && snap_ts_ms_.size() >= 2
        ? snap_ts_ms_.last() - snap_ts_ms_.first()
        : (pts_.size() >= 2
               ? static_cast<qint64>(pts_.last().x() - pts_.first().x())
               : 0);

    // Timestamps from iso_date_to_ms_utc are midnight UTC; intraday bars are not.
    // Use this to pick a format that shows time only when meaningful.
    const bool is_midnight_utc = (display_ms % (24LL * 60 * 60 * 1000)) == 0;
    constexpr qint64 k36hMs = 36LL * 60 * 60 * 1000;
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(display_ms);
    QString time_str;
    if (series_span_ms > 0 && series_span_ms <= k36hMs)
        time_str = dt.toString("HH:mm");           // 1D single session
    else if (!is_midnight_utc)
        time_str = dt.toString("ddd HH:mm");       // multi-day intraday (1W focus)
    else
        time_str = dt.toString("dd MMM yyyy");     // daily snapshot charts
    const QString val_str = QString("%1 %2").arg(currency_, QString::number(snap_pt.y(), 'f', 2));
    tooltip_->setText(time_str + "\n" + val_str);
    tooltip_->adjustSize();

    // Position tooltip: above-right of cursor, clamped to screen bounds
    QPoint global_pos = mapToGlobal(widget_pos) + QPoint(12, -tooltip_->height() - 4);
    const QRect scr = QGuiApplication::primaryScreen()->geometry();
    if (global_pos.x() + tooltip_->width() > scr.right())
        global_pos.rx() -= tooltip_->width() + 24;
    if (global_pos.y() < scr.top())
        global_pos.ry() = mapToGlobal(widget_pos).y() + 12;

    tooltip_->move(global_pos);
    tooltip_->show();
    tooltip_->raise();
}

void CrosshairChartView::hide_crosshair() {
    v_line_->setVisible(false);
    tooltip_->hide();
}

// ── PortfolioPerfChart ────────────────────────────────────────────────────────

PortfolioPerfChart::PortfolioPerfChart(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void PortfolioPerfChart::build_ui() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header: PERFORMANCE + period buttons
    auto* header = new QHBoxLayout;
    header->setContentsMargins(10, 6, 10, 4);

    // Scope-prefixed title: "HOLDINGS PERFORMANCE" at portfolio level, or
    // "{SYMBOL} PERFORMANCE" when a single ticker is focused. The scope word
    // renders in amber (same as the position-level data tokens below) so
    // the eye can distinguish scope from label without a separator glyph —
    // the prior "·" middle-dot rendered as a missing-glyph box on systems
    // without a font that covers U+00B7. Using rich text (color spans)
    // means the label needs Qt::RichText interpretation.
    title_label_ = new QLabel;
    title_label_->setTextFormat(Qt::RichText);
    title_label_->setStyleSheet(
        QString("font-size:12px; font-weight:700; letter-spacing:1.5px; background:transparent;"));
    title_label_->setText(
        QString("<span style='color:%1'>HOLDINGS</span> <span style='color:%2'>PERFORMANCE</span>")
            .arg(ui::colors::WARNING(), ui::colors::TEXT_SECONDARY()));
    header->addWidget(title_label_);

    // The former "← PORTFOLIO" back pill lived here; it was removed because
    // users couldn't tell what they were going back to. The left pane's
    // clickable "PORTFOLIO" title (PortfolioHeatmap) is now the affordance:
    // clicking it deselects the symbol and emits portfolio_view_requested,
    // which PortfolioScreen routes into clear_focus_symbol() below.
    header->addStretch();

    for (const auto& p : kPeriods) {
        auto* btn = new QPushButton(p);
        btn->setFixedSize(44, 22);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString("QPushButton { background:transparent; color:%1; border:1px solid transparent;"
                                   "  font-size:12px; font-weight:700; border-radius:2px; }"
                                   "QPushButton:checked { color:%4; background:%2;"
                                   "  border:1px solid %2; }"
                                   "QPushButton:hover:!checked { color:%3; border-color:%5; }")
                               .arg(ui::colors::TEXT_SECONDARY(), ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(),
                                    ui::colors::BG_BASE(), ui::colors::BORDER_MED()));

        if (p == current_period_)
            btn->setChecked(true);

        connect(btn, &QPushButton::clicked, this, [this, period = p]() { set_period(period); });

        header->addWidget(btn);
        period_btns_.append(btn);
    }

    // Benchmark toggle (label updates when set_benchmark_history fires with a
    // non-SPY symbol — e.g. ^GSPTSE for CAD portfolios).
    benchmark_btn_ = new QPushButton(benchmark_symbol_);
    benchmark_btn_->setFixedSize(60, 22);
    benchmark_btn_->setCheckable(true);
    benchmark_btn_->setCursor(Qt::PointingHandCursor);
    benchmark_btn_->setToolTip("Overlay benchmark index (auto-selected by portfolio currency)");
    benchmark_btn_->setStyleSheet(
        QString("QPushButton { background:transparent; color:%1; border:1px solid %1;"
                "  font-size:12px; font-weight:700; border-radius:2px; }"
                "QPushButton:checked { color:%4; background:%2; border:1px solid %2; }"
                "QPushButton:hover:!checked { color:%3; border-color:%3; }")
            .arg(ui::colors::CYAN(), ui::colors::CYAN(), ui::colors::TEXT_PRIMARY(), ui::colors::BG_BASE()));
    connect(benchmark_btn_, &QPushButton::clicked, this, [this]() {
        show_benchmark_ = benchmark_btn_->isChecked();
        update_chart();
    });
    header->addSpacing(6);
    header->addWidget(benchmark_btn_);

    // Indexed-mode toggle: switches the y-axis from currency value to
    // percent-indexed (base 100). Required for fair benchmark comparison
    // when the portfolio and benchmark are in different currencies — without
    // this, comparing CAD NAV against USD SPY closes is meaningless.
    indexed_btn_ = new QPushButton("%");
    indexed_btn_->setFixedSize(28, 22);
    indexed_btn_->setCheckable(true);
    indexed_btn_->setCursor(Qt::PointingHandCursor);
    indexed_btn_->setToolTip("Indexed view: rebase portfolio and benchmark to 100 at the start of\n"
                             "the selected period. Use when comparing different currencies.");
    indexed_btn_->setStyleSheet(
        QString("QPushButton { background:transparent; color:%1; border:1px solid %1;"
                "  font-size:12px; font-weight:700; border-radius:2px; }"
                "QPushButton:checked { color:%4; background:%2; border:1px solid %2; }"
                "QPushButton:hover:!checked { color:%3; border-color:%3; }")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BG_BASE()));
    connect(indexed_btn_, &QPushButton::clicked, this, [this]() {
        indexed_mode_ = indexed_btn_->isChecked();
        update_chart();
    });
    header->addSpacing(4);
    header->addWidget(indexed_btn_);

    layout->addLayout(header);

    // Info bar: period change, total return, NAV
    auto* info_bar = new QHBoxLayout;
    info_bar->setContentsMargins(10, 2, 10, 6);
    info_bar->setSpacing(14);

    period_change_label_ = new QLabel;
    period_change_label_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:600;").arg(ui::colors::TEXT_PRIMARY()));
    info_bar->addWidget(period_change_label_);

    auto* sep1 = new QLabel("|");
    sep1->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::BORDER_MED()));
    info_bar->addWidget(sep1);

    total_return_label_ = new QLabel;
    total_return_label_->setStyleSheet(
        QString("color:%1; font-size:14px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
    info_bar->addWidget(total_return_label_);

    auto* sep2 = new QLabel("|");
    sep2->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::BORDER_MED()));
    info_bar->addWidget(sep2);

    nav_label_ = new QLabel;
    nav_label_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;").arg(ui::colors::WARNING()));
    info_bar->addWidget(nav_label_);

    auto* sep3 = new QLabel("|");
    sep3->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::BORDER_MED()));
    info_bar->addWidget(sep3);

    cost_basis_label_ = new QLabel;
    cost_basis_label_->setStyleSheet(
        QString("color:%1; font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    cost_basis_label_->setToolTip("Total cost basis — the dashed horizontal line on the chart.");
    info_bar->addWidget(cost_basis_label_);

    // Spacer + live-quote group on the SAME row, only shown in symbol-focus
    // mode. The four tokens above (period change / TOTAL / MV / COST) cover
    // position-level performance; the group below covers today's live trade
    // (LAST / BID x ASK / DAY range / VOL). A fixed gap separates the two
    // groups so the user can tell they're orthogonal — "overall" vs "today".
    info_bar->addSpacing(40);

    const QString live_prim = QString("color:%1; font-size:12px; font-weight:700;"
                                      "font-family:Consolas,monospace; background:transparent;")
                                  .arg(ui::colors::TEXT_PRIMARY());
    const QString live_cyan = QString("color:%1; font-size:12px; font-weight:700;"
                                      "font-family:Consolas,monospace; background:transparent;")
                                  .arg(ui::colors::CYAN());
    const QString live_dim  = QString("color:%1; font-size:12px;"
                                      "font-family:Consolas,monospace; background:transparent;")
                                  .arg(ui::colors::TEXT_SECONDARY());

    live_last_label_ = new QLabel;
    live_last_label_->setStyleSheet(live_prim);
    live_last_label_->setToolTip(QStringLiteral(
        "Last trade — most recent transaction price from yfinance."));
    info_bar->addWidget(live_last_label_);

    live_bidask_label_ = new QLabel;
    live_bidask_label_->setStyleSheet(live_cyan);
    live_bidask_label_->setToolTip(QStringLiteral(
        "Bid x Ask — best-effort live order-book snapshot. May be delayed; "
        "zero / dash outside regular trading hours or for illiquid tickers. "
        "Not a real order book."));
    info_bar->addWidget(live_bidask_label_);

    live_range_label_ = new QLabel;
    live_range_label_->setStyleSheet(live_dim);
    live_range_label_->setToolTip(QStringLiteral("Today's intraday low - high."));
    info_bar->addWidget(live_range_label_);

    live_vol_label_ = new QLabel;
    live_vol_label_->setStyleSheet(live_dim);
    live_vol_label_->setToolTip(QStringLiteral("Today's traded volume."));
    info_bar->addWidget(live_vol_label_);

    info_bar->addStretch();
    layout->addLayout(info_bar);

    // Default: hide the live group at startup (portfolio view).
    set_live_group_visible(false);

    // Chart view
    auto* chart = new QChart;
    chart->setBackgroundBrush(QColor(ui::colors::BG_BASE()));
    chart->setMargins(QMargins(0, 0, 0, 0));
    chart->legend()->setVisible(false);
    chart->setAnimationOptions(QChart::NoAnimation);

    chart_view_ = new CrosshairChartView(chart, this);
    chart_view_->setRenderHint(QPainter::Antialiasing);
    chart_view_->setStyleSheet("border:none; background:transparent;");
    layout->addWidget(chart_view_, 1);
}

void PortfolioPerfChart::set_summary(const portfolio::PortfolioSummary& summary) {
    summary_ = summary;
    // Refresh the title so the "CACHED" badge reflects the freshness of
    // the newly-arrived summary — the cached-then-live emit pattern in
    // PortfolioService::load_summary lands cached=true first, then a
    // second emit with cached=false replaces it.
    if (title_label_ && focus_symbol_.isEmpty()) {
        const QString stale_tag = summary.from_cache
            ? QString(" <span style='color:%1;font-weight:600'>CACHED</span>")
                  .arg(ui::colors::TEXT_TERTIARY())
            : QString();
        title_label_->setText(
            QString("<span style='color:%1'>HOLDINGS</span>"
                    " <span style='color:%2'>PERFORMANCE</span>%3")
                .arg(ui::colors::WARNING(), ui::colors::TEXT_SECONDARY(), stale_tag));
    }
    update_chart();
}

void PortfolioPerfChart::set_snapshots(const QVector<portfolio::PortfolioSnapshot>& snapshots) {
    snapshots_ = snapshots;
    // Sort once so iso_date_to_ms_utc downstream produces a monotonic series.
    std::sort(snapshots_.begin(), snapshots_.end(),
              [](const auto& a, const auto& b) { return a.snapshot_date < b.snapshot_date; });
    update_period_buttons_enabled();
    update_chart();
}

void PortfolioPerfChart::set_currency(const QString& currency) {
    currency_ = currency;
}

void PortfolioPerfChart::set_spy_history(const QStringList& dates, const QVector<double>& closes) {
    set_benchmark_history(QStringLiteral("SPY"), dates, closes);
}

void PortfolioPerfChart::set_benchmark_history(const QString& symbol, const QStringList& dates,
                                               const QVector<double>& closes) {
    benchmark_symbol_ = symbol.isEmpty() ? QStringLiteral("SPY") : symbol;
    spy_dates_ = dates;   // field name kept for back-compat; holds chosen benchmark
    spy_closes_ = closes;
    if (benchmark_btn_)
        benchmark_btn_->setText(benchmark_symbol_);
    if (show_benchmark_)
        update_chart();
}

void PortfolioPerfChart::set_period(const QString& period) {
    const QString prev = current_period_;
    current_period_ = period;
    for (auto* btn : period_btns_)
        btn->setChecked(btn->text() == period);

    // 1D, and 1W in focus mode, render from intraday bars (epoch-ms axis).
    // PortfolioService::fetch_*_intraday is invoked by the screen owner; the
    // chart paints a placeholder until set_*_intraday() lands.
    if (is_intraday_period()) {
        intraday_requested_ = true;
        intraday_resolved_  = false;  // waiting for callback
        intraday_for_symbol_ = focus_symbol_;  // empty if portfolio view
        // 1D gets a 2-point prev-close → current seed so the chart isn't
        // blank during the fan-out. 1W intraday spans 5 trading days — the
        // 6.5h seed would mislead, so skip the seed and let the placeholder
        // render until real bars arrive.
        if (current_period_ == QStringLiteral("1D"))
            seed_intraday_from_summary();
        else
            { intraday_ts_ms_.clear(); intraday_values_.clear(); }
        // Indexed (base-100) mode only makes sense over multi-day windows;
        // 1D is a raw-value series. Keep it available for 1W intraday.
        if (indexed_btn_) {
            const bool indexed_ok = current_period_ != QStringLiteral("1D");
            indexed_btn_->setEnabled(indexed_ok);
            indexed_btn_->setToolTip(indexed_ok
                ? QStringLiteral("Indexed view: rebase to 100 at the start of the period.")
                : QStringLiteral(
                    "Indexed view is unavailable in 1D — intraday is rendered "
                    "as raw price/NAV."));
        }
        emit intraday_requested(focus_symbol_, period_for_yfinance(), intraday_interval());
        update_chart();
        return;
    }
    // Switching away from an intraday-rendered period — clear the cache so
    // a return to it re-fetches fresh data, and restore the indexed-mode
    // toggle.
    intraday_requested_ = false;
    intraday_ts_ms_.clear();
    intraday_values_.clear();
    if (indexed_btn_) {
        indexed_btn_->setEnabled(true);
        indexed_btn_->setToolTip(
            QStringLiteral("Indexed view: rebase portfolio and benchmark to 100 at the start of\n"
                           "the period for a like-for-like return comparison."));
    }

    // Focus mode: ask the owner to refetch this symbol's history for the new
    // period and exit early — backfill applies to NAV snapshots only.
    if (!focus_symbol_.isEmpty()) {
        if (prev != period)
            emit focus_symbol_period_requested(focus_symbol_, period_for_yfinance());
        update_chart();
        return;
    }

    // If the user is asking for a longer window than we have snapshots for,
    // request a backfill. The owner re-feeds set_snapshots when it lands.
    if (prev != period && !snapshots_.isEmpty()) {
        const QDate earliest = QDate::fromString(snapshots_.first().snapshot_date.left(10), Qt::ISODate);
        const QDate today = QDate::currentDate();
        const int needed_days = (period == "5Y" || period == "ALL") ? 1825
                              : (period == "1Y" || period == "YTD") ? 365
                              : (period == "3M") ? 95
                              : (period == "1M") ? 32
                              : 10;
        if (earliest.isValid() && earliest.daysTo(today) < needed_days - 5) {
            const QString backfill_period =
                (period == "5Y" || period == "ALL") ? "5y" : "1y";
            emit backfill_period_requested(backfill_period);
        }
    }
    update_chart();
}

QString PortfolioPerfChart::period_for_yfinance() const {
    // yfinance accepts: 1d 5d 1mo 3mo 6mo 1y 2y 5y 10y ytd max
    const QString& p = current_period_;
    if (p == "1D") return QStringLiteral("1d");
    if (p == "1W") return QStringLiteral("5d");
    if (p == "1M") return QStringLiteral("1mo");
    if (p == "3M") return QStringLiteral("3mo");
    if (p == "YTD") return QStringLiteral("ytd");
    if (p == "1Y") return QStringLiteral("1y");
    if (p == "5Y") return QStringLiteral("5y");
    return QStringLiteral("max"); // ALL
}

bool PortfolioPerfChart::is_intraday_period() const {
    if (current_period_ == QStringLiteral("1D"))
        return true;
    // 1W focus-mode uses intraday bars so the user sees a real curve over
    // the trading week rather than a 5-vertex polygon between daily closes.
    // Aggregate 1W stays on the snapshots path (no intraday aggregation
    // exists for multi-day windows).
    if (current_period_ == QStringLiteral("1W") && !focus_symbol_.isEmpty())
        return true;
    return false;
}

QString PortfolioPerfChart::intraday_interval() const {
    return current_period_ == QStringLiteral("1W")
        ? QStringLiteral("30m") : QStringLiteral("1m");
}

void PortfolioPerfChart::set_focus_symbol(const QString& symbol) {
    if (symbol.isEmpty()) {
        clear_focus_symbol();
        return;
    }
    if (focus_symbol_ == symbol) {
        // Already focused on this symbol — refresh data anyway. Route to the
        // intraday fetch when the current period is rendered intraday (1D
        // always, 1W in focus mode) so we don't hit the daily-close path.
        if (is_intraday_period()) {
            intraday_requested_ = true;
            intraday_resolved_  = false;
            if (current_period_ == QStringLiteral("1D"))
                seed_intraday_from_summary();
            else
                { intraday_ts_ms_.clear(); intraday_values_.clear(); }
            emit intraday_requested(focus_symbol_, period_for_yfinance(), intraday_interval());
        } else {
            emit focus_symbol_period_requested(focus_symbol_, period_for_yfinance());
        }
        fetch_focus_orderbook();
        return;
    }
    focus_symbol_ = symbol.toUpper();
    focus_dates_.clear();
    focus_closes_.clear();
    focus_data_loaded_ = false; // reset: waiting for set_focus_history()
    // Entering focus mode: 1W moves to the intraday path and no longer
    // requires 5+ days of snapshots, so re-evaluate which period buttons
    // are feasible.
    update_period_buttons_enabled();
    // Invalidate prior symbol's order-book snapshot so a fast switch can't
    // briefly show the previous spread.
    ob_symbol_.clear();
    ob_bid_ = ob_ask_ = ob_bid_size_ = ob_ask_size_ = 0;
    if (title_label_)
        title_label_->setText(
            QString("<span style='color:%1'>%2</span> <span style='color:%3'>PERFORMANCE</span>")
                .arg(ui::colors::WARNING(), focus_symbol_.toHtmlEscaped(),
                     ui::colors::TEXT_SECONDARY()));
    // Route the fetch by current period: intraday-rendered periods (1D, and
    // 1W in focus mode) need bar-level data, not daily closes. Clear the
    // prior symbol's intraday bars so the chart shows a "Loading…"
    // placeholder until the new symbol's bars land.
    if (is_intraday_period()) {
        intraday_requested_  = true;
        intraday_resolved_   = false;
        intraday_for_symbol_ = focus_symbol_;
        intraday_ts_ms_.clear();
        intraday_values_.clear();
        if (current_period_ == QStringLiteral("1D"))
            seed_intraday_from_summary();
        emit intraday_requested(focus_symbol_, period_for_yfinance(), intraday_interval());
    } else {
        emit focus_symbol_period_requested(focus_symbol_, period_for_yfinance());
    }
    fetch_focus_orderbook();
    update_chart(); // renders seed/loading placeholder until real data lands
}

void PortfolioPerfChart::fetch_focus_orderbook() {
    if (focus_symbol_.isEmpty())
        return;
    // In-flight guard: if a request is already in flight for the current
    // focus symbol, do nothing — its callback will install the result and,
    // if focus has since moved, kick off a fresh fetch then. This collapses
    // rapid focus switches (A→B→C→D) into at most 2 yfinance round trips
    // (A fires immediately, D fires after A returns) instead of 4.
    if (ob_fetch_for_ == focus_symbol_)
        return;
    ob_fetch_for_ = focus_symbol_;

    const QString want = focus_symbol_;
    QPointer<PortfolioPerfChart> self = this;
    services::MarketDataService::instance().fetch_orderbook(
        want,
        [self, want](bool ok, services::MarketDataService::OrderbookData ob) {
            if (!self)
                return;
            // Clear the in-flight marker only if it still matches *this*
            // request — defensive against an unlikely re-entry.
            if (self->ob_fetch_for_ == want)
                self->ob_fetch_for_.clear();
            // Race guard: if the user has switched tickers since the request
            // went out, drop the result rather than overwriting the current
            // symbol's spread with stale data. Then chase the new focus so
            // the user still gets a fresh spread for whatever they landed on.
            if (self->focus_symbol_ != want) {
                if (!self->focus_symbol_.isEmpty())
                    self->fetch_focus_orderbook();
                return;
            }
            if (!ok)
                return;
            self->ob_symbol_   = want;
            self->ob_bid_      = ob.bid;
            self->ob_ask_      = ob.ask;
            self->ob_bid_size_ = ob.bid_size;
            self->ob_ask_size_ = ob.ask_size;
            self->update_chart();
        });
}

void PortfolioPerfChart::clear_focus_symbol() {
    if (focus_symbol_.isEmpty())
        return;
    focus_symbol_.clear();
    focus_dates_.clear();
    focus_closes_.clear();
    focus_data_loaded_ = false;
    ob_symbol_.clear();
    ob_bid_ = ob_ask_ = ob_bid_size_ = ob_ask_size_ = 0;
    if (title_label_)
        title_label_->setText(
            QString("<span style='color:%1'>HOLDINGS</span> <span style='color:%2'>PERFORMANCE</span>")
                .arg(ui::colors::WARNING(), ui::colors::TEXT_SECONDARY()));
    set_live_group_visible(false);
    // Leaving focus mode: 1W aggregate's feasibility depends on snapshot
    // span again — re-evaluate the period buttons.
    update_period_buttons_enabled();
    // Drop any focus-mode 1D intraday bars so a return to portfolio 1D
    // doesn't briefly render the prior symbol's curve before the new
    // aggregate fan-out lands. If we're currently in 1D, also re-request
    // the portfolio aggregate so the chart starts loading immediately.
    intraday_ts_ms_.clear();
    intraday_values_.clear();
    intraday_for_symbol_.clear();
    if (current_period_ == QStringLiteral("1D")) {
        intraday_requested_ = true;
        seed_intraday_from_summary();  // aggregate seed now that focus is gone
        emit intraday_requested(QString(), QStringLiteral("1d"), QStringLiteral("1m"));
    } else if (current_period_ == QStringLiteral("1W")) {
        // Aggregate 1W uses the snapshots path, not intraday — leave the
        // chart to re-render from snapshots_ via update_chart() below.
    }
    update_chart();
}

void PortfolioPerfChart::set_focus_history(const QString& symbol, const QStringList& dates,
                                           const QVector<double>& closes) {
    if (focus_symbol_.isEmpty() || symbol.toUpper() != focus_symbol_)
        return; // stale or off-target fetch — ignore
    focus_dates_ = dates;
    focus_closes_ = closes;
    focus_data_loaded_ = true; // fetch complete — even if data is empty
    update_chart();
}

void PortfolioPerfChart::set_symbol_intraday(const QString& symbol,
                                             const QVector<qint64>& timestamps_ms,
                                             const QVector<double>& closes) {
    // Race-safe: only accept if we're still showing an intraday-rendered
    // period (1D, or 1W in focus mode) for this symbol.
    if (!is_intraday_period()) return;
    if (focus_symbol_.isEmpty() || symbol.toUpper() != focus_symbol_) return;
    intraday_for_symbol_ = focus_symbol_;
    intraday_ts_ms_      = timestamps_ms;
    intraday_values_     = closes;
    intraday_resolved_   = true;  // got a definitive answer (may be empty)
    update_chart();
}

void PortfolioPerfChart::set_portfolio_intraday(const QVector<qint64>& timestamps_ms,
                                                const QVector<double>& navs) {
    if (current_period_ != QStringLiteral("1D")) return;
    if (!focus_symbol_.isEmpty()) return; // stale aggregate while symbol focused
    intraday_for_symbol_ = QString();
    intraday_ts_ms_      = timestamps_ms;
    intraday_values_     = navs;
    intraday_resolved_   = true;
    update_chart();
}

qint64 PortfolioPerfChart::iso_date_to_ms_utc(const QString& iso_date) {
    const QDate d = QDate::fromString(iso_date.left(10), Qt::ISODate);
    if (!d.isValid())
        return QDateTime::currentMSecsSinceEpoch();
    return QDateTime(d, QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
}

void PortfolioPerfChart::update_period_buttons_enabled() {
    // 1D rides on an on-demand yfinance 1m intraday fetch, so it does NOT
    // require daily NAV snapshots. The old early-return-on-empty-snapshots
    // left it disabled for freshly-imported portfolios — exactly the case
    // where the user most wants to glance at today's curve.
    const QDate today = QDate::currentDate();
    const QDate earliest = snapshots_.isEmpty()
        ? QDate()
        : QDate::fromString(snapshots_.first().snapshot_date.left(10), Qt::ISODate);
    const int span_days = earliest.isValid() ? earliest.daysTo(today) : 0;
    for (auto* btn : period_btns_) {
        const QString p = btn->text();
        bool feasible = true;
        // 1W aggregate needs 5+ days of snapshots; 1W focus rides on the
        // on-demand yfinance intraday pull, so it's always feasible there.
        if (p == "1W" && focus_symbol_.isEmpty())
            feasible = span_days >= 5;
        // 1D and the longer periods always allowed: backfill / intraday fetch
        // kicks in when clicked.
        btn->setEnabled(feasible);
        btn->setToolTip(feasible ? QString()
                                 : QStringLiteral("Needs more snapshot history."));
    }
}

QColor PortfolioPerfChart::chart_color() const {
    return summary_.total_unrealized_pnl >= 0 ? QColor(ui::colors::POSITIVE()) : QColor(ui::colors::NEGATIVE());
}

void PortfolioPerfChart::set_live_group_visible(bool visible) {
    // The four right-group labels live inside info_bar alongside the
    // overall-performance tokens; toggle each individually since they share
    // a layout. Hidden labels collapse to zero width by default (Qt's
    // QSizePolicy::retainSizeWhenHidden is off).
    if (live_last_label_)   live_last_label_->setVisible(visible);
    if (live_bidask_label_) live_bidask_label_->setVisible(visible);
    if (live_range_label_)  live_range_label_->setVisible(visible);
    if (live_vol_label_)    live_vol_label_->setVisible(visible);
}

void PortfolioPerfChart::seed_intraday_from_summary() {
    intraday_ts_ms_.clear();
    intraday_values_.clear();

    double prev_val = 0;
    double cur_val  = 0;
    if (focus_symbol_.isEmpty()) {
        // Aggregate path: previous NAV = current MV minus today's $ change.
        if (summary_.total_market_value <= 0) return;
        cur_val  = summary_.total_market_value;
        prev_val = cur_val - summary_.total_day_change;
    } else {
        const portfolio::HoldingWithQuote* held = nullptr;
        for (const auto& h : summary_.holdings) {
            if (h.symbol == focus_symbol_) { held = &h; break; }
        }
        if (!held || held->current_price <= 0) return;
        cur_val  = held->current_price;
        prev_val = cur_val - held->day_change;
    }
    if (prev_val <= 0) return;

    // Anchor the seed to today's NYSE session (09:30 ET → now, capped at
    // 16:00 ET). The previous "now − 6.5h rolling window" approach was
    // exchange-agnostic in principle but in practice pushed the chart's
    // x-axis start into pre-market wall-clock time (e.g. 04:15 PT at
    // mid-session) since US bars themselves are NYSE-anchored. If we're
    // called before today's open (rare — pre-market refresh), fall back to
    // yesterday's session so prev_val→cur_val still spans a real interval.
    const QTimeZone et("America/New_York");
    const QDateTime now_et = QDateTime::currentDateTime().toTimeZone(et);
    QDateTime open_et(now_et.date(), QTime(9, 30), et);
    QDateTime close_et(now_et.date(), QTime(16, 0), et);
    if (now_et < open_et) {
        open_et  = open_et.addDays(-1);
        close_et = close_et.addDays(-1);
    }
    const qint64 now_ms          = QDateTime::currentMSecsSinceEpoch();
    const qint64 session_open_ms = open_et.toMSecsSinceEpoch();
    const qint64 session_end_ms  = std::min(close_et.toMSecsSinceEpoch(), now_ms);
    intraday_ts_ms_  = {session_open_ms, session_end_ms};
    intraday_values_ = {prev_val, cur_val};
}

bool PortfolioPerfChart::render_intraday(bool is_aggregate) {
    auto* chart = chart_view_->chart();
    chart->removeAllSeries();
    const auto old_axes = chart->axes();
    for (auto* axis : old_axes) {
        chart->removeAxis(axis);
        delete axis;
    }

    if (intraday_ts_ms_.size() != intraday_values_.size()) {
        LOG_WARN("PortfolioPerf",
                 QString("Intraday series size mismatch: %1 ts vs %2 vals — rendering placeholder")
                     .arg(intraday_ts_ms_.size()).arg(intraday_values_.size()));
    }
    if (intraday_ts_ms_.isEmpty() || intraday_values_.isEmpty()
        || intraday_ts_ms_.size() != intraday_values_.size()) {
        const QString label = is_aggregate ? QStringLiteral("portfolio NAV")
                                           : focus_symbol_;
        // Distinguish "still loading" (callback hasn't fired yet) from
        // "callback fired but returned empty" (market closed, holiday,
        // illiquid symbol). The latter would otherwise show "Loading…"
        // indefinitely, which is what the review flagged.
        if (intraday_resolved_) {
            period_change_label_->setText(
                QString("%1 unavailable for %2 (market closed or no bars)")
                    .arg(current_period_, label));
        } else {
            period_change_label_->setText(
                QString("Loading %1 %2…").arg(current_period_, label));
        }
        // Aggregate path owns its own info-bar; per-symbol path's
        // TOTAL/MV/COST + live row is populated by update_chart_focus()'s
        // tail from summary_.holdings, so leave those labels alone here.
        if (is_aggregate) {
            total_return_label_->clear();
            nav_label_->clear();
            if (cost_basis_label_) cost_basis_label_->clear();
        }
        chart_view_->set_series_data({}, currency_);
        return false;
    }

    // Multi-day intraday windows (1W focus, 30m bars): filter to market hours
    // and assign sequential bar indices as x so weekend/overnight gaps vanish.
    // Single-day (1D) keeps real ms timestamps — one session has no gaps.
    constexpr qint64 kOneDayMs = 24LL * 60 * 60 * 1000;
    const bool multi_day =
        (intraday_ts_ms_.last() - intraday_ts_ms_.first()) > kOneDayMs;

    QVector<QPointF> pts;
    QVector<qint64>  pts_ts_ms; // actual timestamps for crosshair (multi-day only)
    QAbstractAxis*   x = nullptr;

    if (multi_day) {
        QVector<qint64> mkt_ts   = intraday_ts_ms_;
        QVector<double>  mkt_vals = intraday_values_;
        filter_to_market_hours(mkt_ts, mkt_vals);
        // Guard: filter may empty the vectors for holiday stubs or all-weekend
        // data. Without this, pts.first() below would be undefined behaviour.
        // chart was already cleared at the top of this function; x is nullptr
        // but never used past this point, so no axis leak.
        if (mkt_ts.isEmpty()) {
            period_change_label_->setText(
                QString("%1 unavailable (no market-hour bars)").arg(current_period_));
            chart_view_->set_series_data({}, currency_);
            return false;
        }
        pts.reserve(mkt_ts.size());
        pts_ts_ms = mkt_ts;

        static const QTimeZone kET("America/New_York");
        QDate prev_date;
        QVector<QPair<double, QString>> session_starts;
        for (int i = 0; i < mkt_ts.size(); ++i) {
            const QDateTime dt = QDateTime::fromMSecsSinceEpoch(mkt_ts[i], kET);
            const QDate d = dt.date();
            if (d != prev_date) {
                session_starts.append({static_cast<double>(i), dt.toString("ddd")});
                prev_date = d;
            }
            pts.append(QPointF(static_cast<double>(i), mkt_vals[i]));
        }

        auto* cat_x = new QCategoryAxis;
        cat_x->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
        cat_x->setRange(-0.5, static_cast<double>(pts.size()) - 0.5);
        for (const auto& [idx, lbl] : session_starts)
            cat_x->append(lbl, idx);
        cat_x->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
        cat_x->setGridLineVisible(false);
        cat_x->setMinorGridLineVisible(false);
        cat_x->setLinePen(QPen(QColor(ui::colors::BORDER_MED()), 1));
        cat_x->setLabelsFont(QFont(ui::fonts::DATA_FAMILY(), 8));
        x = cat_x;
    } else {
        pts.reserve(intraday_ts_ms_.size());
        for (int i = 0; i < intraday_ts_ms_.size(); ++i)
            pts.append(QPointF(static_cast<double>(intraday_ts_ms_[i]), intraday_values_[i]));

        auto* dt_x = new QDateTimeAxis;
        dt_x->setFormat(QStringLiteral("HH:mm"));
        dt_x->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
        dt_x->setGridLineColor(QColor(ui::colors::BORDER_DIM()));
        dt_x->setLinePenColor(QColor(ui::colors::BORDER_MED()));
        dt_x->setRange(QDateTime::fromMSecsSinceEpoch(intraday_ts_ms_.first()),
                       QDateTime::fromMSecsSinceEpoch(intraday_ts_ms_.last()));
        x = dt_x;
    }

    const double first = pts.first().y();
    const double last  = pts.last().y();
    const double pnl_pct = first > 0 ? (last - first) / first * 100.0 : 0.0;

    auto* line  = new QLineSeries;
    auto* upper = new QLineSeries;
    auto* lower = new QLineSeries;
    double y_min = std::numeric_limits<double>::max();
    double y_max = std::numeric_limits<double>::lowest();
    for (const auto& p : pts) {
        line->append(p);
        upper->append(p);
        lower->append(p.x(), first);
        y_min = std::min(y_min, p.y());
        y_max = std::max(y_max, p.y());
    }
    const QColor lc = pnl_pct >= 0 ? QColor(ui::colors::POSITIVE())
                                   : QColor(ui::colors::NEGATIVE());
    line->setPen(QPen(lc, 2));
    auto* area = new QAreaSeries(upper, lower);
    QColor fill = lc; fill.setAlpha(35);
    area->setBrush(fill);
    area->setPen(QPen(Qt::NoPen));

    chart->addSeries(area);
    chart->addSeries(line);

    chart->addAxis(x, Qt::AlignBottom);
    line->attachAxis(x);
    area->attachAxis(x);

    auto* y = new QValueAxis;
    y->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
    y->setGridLineColor(QColor(ui::colors::BORDER_DIM()));
    y->setLinePenColor(QColor(ui::colors::BORDER_MED()));
    const double range = y_max - y_min;
    const double pad = range > 0 ? range * 0.05 : std::max(1.0, std::abs(y_max) * 0.02);
    y->setRange(y_min - pad, y_max + pad);
    y->setLabelFormat(QStringLiteral("%.2f"));
    chart->addAxis(y, Qt::AlignLeft);
    line->attachAxis(y);
    area->attachAxis(y);

    chart_view_->set_series_data(pts, QStringLiteral("$"), pts_ts_ms);

    // Info-bar: period change %, current value, and (aggregate only) cost.
    const char* pcol = pnl_pct >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
    period_change_label_->setText(QString("%1  %2%3%")
                                      .arg(current_period_)
                                      .arg(pnl_pct >= 0 ? "+" : "")
                                      .arg(QString::number(pnl_pct, 'f', 2)));
    period_change_label_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:600;").arg(pcol));

    if (is_aggregate) {
        total_return_label_->setText(QString("NAV %1 %2")
                                         .arg(currency_).arg(QString::number(last, 'f', 2)));
        total_return_label_->setStyleSheet(
            QString("color:%1; font-size:14px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
        nav_label_->clear();
        if (cost_basis_label_) cost_basis_label_->clear();
    }
    // Non-aggregate: TOTAL/MV/COST handled by update_chart_focus()'s tail
    // so the labels track summary_.holdings instead of being overwritten
    // with a duplicate "LAST" token (the live row already shows LAST).
    return true;
}

bool PortfolioPerfChart::render_daily_focus(double* last_out) {
    auto* chart = chart_view_->chart();
    chart->removeAllSeries();
    const auto old_axes = chart->axes();
    for (auto* axis : old_axes) {
        chart->removeAxis(axis);
        delete axis;
    }

    if (focus_dates_.isEmpty() || focus_closes_.isEmpty() ||
        focus_dates_.size() != focus_closes_.size()) {
        // Loading placeholder; caller's tail still populates held-based labels.
        period_change_label_->setText(QStringLiteral("Loading %1…").arg(focus_symbol_));
        chart_view_->set_series_data({}, currency_);
        return false;
    }

    // Sort by ms timestamp, then assign sequential bar indices as x so
    // weekends don't produce gaps on the x-axis.
    QVector<QPointF> ms_pts; // (ms, raw_close) — used only for ordering
    ms_pts.reserve(focus_closes_.size());
    for (int i = 0; i < focus_closes_.size(); ++i)
        ms_pts.append(QPointF(static_cast<double>(iso_date_to_ms_utc(focus_dates_[i])),
                              focus_closes_[i]));
    std::sort(ms_pts.begin(), ms_pts.end(),
              [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });

    const double first    = ms_pts.first().y();
    const double last_val = ms_pts.last().y();
    if (last_out) *last_out = last_val;
    const double pnl     = last_val - first;
    const double pnl_pct = first > 0 ? (pnl / first) * 100.0 : 0.0;

    auto to_display = [&](double v) -> double {
        if (!indexed_mode_)
            return v;
        return first > 0 ? (v / first) * 100.0 : 0.0;
    };

    // Sequential-index pts (raw y for crosshair) + actual timestamps + dates
    QVector<QPointF> pts;
    QVector<qint64>  seq_ts_ms;
    QVector<QDate>   seq_dates;
    pts.reserve(ms_pts.size());
    seq_ts_ms.reserve(ms_pts.size());
    seq_dates.reserve(ms_pts.size());
    for (int i = 0; i < ms_pts.size(); ++i) {
        const qint64 ms = static_cast<qint64>(ms_pts[i].x());
        pts.append(QPointF(static_cast<double>(i), ms_pts[i].y()));
        seq_ts_ms.append(ms);
        seq_dates.append(QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).date());
    }

    auto* line  = new QLineSeries;
    auto* upper = new QLineSeries;
    auto* lower = new QLineSeries;
    double y_min = std::numeric_limits<double>::max();
    double y_max = std::numeric_limits<double>::lowest();
    const double base_disp = to_display(first);
    for (const auto& p : pts) {
        const double y = to_display(p.y());
        line->append(p.x(), y);
        upper->append(p.x(), y);
        lower->append(p.x(), base_disp);
        y_min = std::min(y_min, y);
        y_max = std::max(y_max, y);
    }
    QColor lc = pnl >= 0 ? QColor(ui::colors::POSITIVE()) : QColor(ui::colors::NEGATIVE());
    line->setPen(QPen(lc, 2));
    auto* area = new QAreaSeries(upper, lower);
    QColor fill = lc; fill.setAlpha(35);
    area->setBrush(fill);
    area->setPen(QPen(Qt::NoPen));

    chart->addSeries(area);
    chart->addSeries(line);

    auto* x = new QCategoryAxis;
    x->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
    x->setRange(-0.5, static_cast<double>(pts.size()) - 0.5);
    for (const auto& [idx, lbl] : daily_seq_labels(seq_dates, current_period_))
        x->append(lbl, idx);
    x->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
    x->setGridLineVisible(false);
    x->setMinorGridLineVisible(false);
    x->setLinePen(QPen(QColor(ui::colors::BORDER_MED()), 1));
    x->setLabelsFont(QFont(ui::fonts::DATA_FAMILY(), 8));
    chart->addAxis(x, Qt::AlignBottom);
    line->attachAxis(x);
    area->attachAxis(x);

    auto* y = new QValueAxis;
    y->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
    y->setGridLineColor(QColor(ui::colors::BORDER_DIM()));
    y->setLinePenColor(QColor(ui::colors::BORDER_MED()));
    // Guard against zero-range axis (flat synthetic series) — Qt Charts
    // otherwise stretches each point into a full-height vertical spike.
    const double range = y_max - y_min;
    const double pad = range > 0 ? range * 0.05 : std::max(1.0, std::abs(y_max) * 0.02);
    y->setRange(y_min - pad, y_max + pad);
    y->setLabelFormat(indexed_mode_ ? QStringLiteral("%.1f") : QStringLiteral("%.2f"));
    chart->addAxis(y, Qt::AlignLeft);
    line->attachAxis(y);
    area->attachAxis(y);

    chart_view_->set_series_data(pts, indexed_mode_ ? QStringLiteral("%") : QStringLiteral("$"),
                                 seq_ts_ms);

    const char* period_color = pnl_pct >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
    period_change_label_->setText(QString("%1  %2%3%")
                                      .arg(current_period_)
                                      .arg(pnl_pct >= 0 ? "+" : "")
                                      .arg(QString::number(pnl_pct, 'f', 2)));
    period_change_label_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:600;").arg(period_color));
    return true;
}

void PortfolioPerfChart::update_chart_focus() {
    // Chart-render branches:
    //   1D, and 1W (focus) → render_intraday() (epoch-ms axis).
    //   1M/3M/…/ALL        → daily-close ISO-date series.
    // Both branches set period_change_label_ themselves. The shared tail
    // below populates TOTAL/MV/COST and the live row (LAST/BID/ASK/DAY/VOL)
    // from summary_.holdings unconditionally, so a ticker switch reflects
    // in those labels immediately — before the per-period fetch returns.
    bool have_data = false;
    double last_price = 0;

    if (is_intraday_period()) {
        have_data = render_intraday(/*is_aggregate=*/false);
        if (have_data && !intraday_values_.isEmpty())
            last_price = intraday_values_.last();
    } else {
        have_data = render_daily_focus(&last_price);
    }

    // ── Shared tail: position-level labels + live row ──────────────────────
    // Pulls from summary_.holdings so a ticker switch refreshes immediately,
    // independent of the chart's fetch state.
    const portfolio::HoldingWithQuote* held = nullptr;
    for (const auto& h : summary_.holdings) {
        if (h.symbol == focus_symbol_) { held = &h; break; }
    }

    if (held && held->cost_basis > 0) {
        const double total_pct = held->unrealized_pnl_percent;
        const char* tot_color = total_pct >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
        total_return_label_->setText(
            QString("TOTAL  %1%2%").arg(total_pct >= 0 ? "+" : "")
                                   .arg(QString::number(total_pct, 'f', 2)));
        total_return_label_->setStyleSheet(
            QString("color:%1; font-size:14px; font-weight:700;").arg(tot_color));

        nav_label_->setText(
            QString("MV %1 %2").arg(currency_).arg(QString::number(held->market_value, 'f', 2)));
        if (cost_basis_label_)
            cost_basis_label_->setText(
                QString("COST %1 %2").arg(currency_).arg(QString::number(held->cost_basis, 'f', 2)));
    } else if (have_data && last_price > 0) {
        // Not a held position but chart drew — stand-in price.
        total_return_label_->setText(QString("PRICE %1 %2")
                                         .arg(currency_).arg(QString::number(last_price, 'f', 2)));
        total_return_label_->setStyleSheet(
            QString("color:%1; font-size:14px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
        nav_label_->clear();
        if (cost_basis_label_) cost_basis_label_->clear();
    } else {
        total_return_label_->clear();
        nav_label_->clear();
        if (cost_basis_label_) cost_basis_label_->clear();
    }

    // Live trade snapshot row — only when we have a held position.
    // Zeros render as "--" so the user knows the feed didn't provide that field.
    if (held) {
        auto fmt_or_dash = [](double v, int dec) -> QString {
            return v > 0 ? QString::number(v, 'f', dec) : QStringLiteral("--");
        };
        auto fmt_vol = [](double v) -> QString {
            if (v <= 0) return QStringLiteral("--");
            if (v >= 1e9) return QString::number(v / 1e9, 'f', 2) + "B";
            if (v >= 1e6) return QString::number(v / 1e6, 'f', 2) + "M";
            if (v >= 1e3) return QString::number(v / 1e3, 'f', 1) + "K";
            return QString::number(v, 'f', 0);
        };
        live_last_label_->setText(QString("LAST %1").arg(fmt_or_dash(held->current_price, 2)));
        // Prefer the on-demand orderbook snapshot (fetch_focus_orderbook → fast_info)
        // over the held quote, which leaves bid/ask at 0 because batch_quotes
        // intentionally skips fast_info for performance.
        const bool ob_match = (ob_symbol_ == focus_symbol_);
        const double bid = ob_match && ob_bid_ > 0 ? ob_bid_ : held->bid;
        const double ask = ob_match && ob_ask_ > 0 ? ob_ask_ : held->ask;
        const QString bid_s = fmt_or_dash(bid, 2);
        const QString ask_s = fmt_or_dash(ask, 2);
        live_bidask_label_->setText(QString("BID %1 x ASK %2").arg(bid_s, ask_s));
        live_range_label_->setText(
            QString("DAY %1-%2").arg(fmt_or_dash(held->day_low, 2),
                                     fmt_or_dash(held->day_high, 2)));
        live_vol_label_->setText(QString("VOL %1").arg(fmt_vol(held->day_volume)));
        set_live_group_visible(true);
    } else {
        set_live_group_visible(false);
    }
}

void PortfolioPerfChart::update_chart() {
    if (!focus_symbol_.isEmpty()) {
        update_chart_focus();
        return;
    }
    // Portfolio-level 1D — render the aggregate intraday NAV curve assembled
    // by PortfolioService::fetch_portfolio_intraday. Same daily snapshot
    // path is used for everything else.
    if (current_period_ == QStringLiteral("1D")) {
        render_intraday(/*is_aggregate=*/true);
        return;
    }

    auto* chart = chart_view_->chart();
    chart->removeAllSeries();

    const auto old_axes = chart->axes();
    for (auto* axis : old_axes) {
        chart->removeAxis(axis);
        delete axis;
    }

    if (summary_.holdings.isEmpty()) {
        period_change_label_->setText("No data");
        total_return_label_->clear();
        nav_label_->clear();
        if (cost_basis_label_)
            cost_basis_label_->clear();
        chart_view_->set_series_data({}, currency_);
        return;
    }

    const double live_nav = summary_.total_market_value;
    const double cost_basis = summary_.total_cost_basis;

    // ── Determine cutoff date from selected period ────────────────────────────
    QDate cutoff = QDate::currentDate();
    if (current_period_ == "1D")
        cutoff = cutoff.addDays(-1);
    else if (current_period_ == "1W")
        cutoff = cutoff.addDays(-7);
    else if (current_period_ == "1M")
        cutoff = cutoff.addMonths(-1);
    else if (current_period_ == "3M")
        cutoff = cutoff.addMonths(-3);
    else if (current_period_ == "YTD")
        cutoff = QDate(QDate::currentDate().year(), 1, 1);
    else if (current_period_ == "1Y")
        cutoff = cutoff.addYears(-1);
    else if (current_period_ == "5Y")
        cutoff = cutoff.addYears(-5);
    else
        cutoff = cutoff.addYears(-10); // ALL

    // ── Filter snapshots (already sorted ascending by set_snapshots) ─────────
    QVector<portfolio::PortfolioSnapshot> filtered;
    filtered.reserve(snapshots_.size());
    for (const auto& s : snapshots_) {
        const QDate d = QDate::fromString(s.snapshot_date.left(10), Qt::ISODate);
        if (d.isValid() && d >= cutoff)
            filtered.append(s);
    }

    // ── Build sequential-index NAV series (eliminates weekend/holiday gaps) ───
    // nav_pts:   (sequential_index, NAV) — x used by series + crosshair snap
    // nav_ts_ms: actual UTC ms per point — used by crosshair tooltip
    // nav_dates: calendar dates per point — used for axis label building
    // date_to_seq: snapshot date → sequential index (for benchmark overlay)
    QVector<QPointF>  nav_pts;
    QVector<qint64>   nav_ts_ms;
    QVector<QDate>    nav_dates;
    QHash<QDate, int> date_to_seq;
    nav_pts.reserve(filtered.size() + 1);
    nav_ts_ms.reserve(filtered.size() + 1);
    nav_dates.reserve(filtered.size() + 1);

    double period_baseline = 0;
    bool baseline_unavailable = false;

    if (filtered.size() >= 2) {
        period_baseline = filtered.first().total_value;
        for (int i = 0; i < filtered.size(); ++i) {
            const QDate d = QDate::fromString(filtered[i].snapshot_date.left(10), Qt::ISODate);
            const qint64 ms = iso_date_to_ms_utc(filtered[i].snapshot_date);
            nav_pts.append(QPointF(static_cast<double>(i), filtered[i].total_value));
            nav_ts_ms.append(ms);
            nav_dates.append(d);
            date_to_seq[d] = i;
        }
        // Pin a final point at "now" so the line meets the live NAV.
        const qint64 now_ms = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
        nav_pts.append(QPointF(static_cast<double>(filtered.size()), live_nav));
        nav_ts_ms.append(now_ms);
        nav_dates.append(QDate::currentDate());

        const QDate first_date = QDate::fromString(
            filtered.first().snapshot_date.left(10), Qt::ISODate);
        const int window_days = cutoff.daysTo(QDate::currentDate());
        if (first_date.isValid() && window_days > 0) {
            const int have_days = first_date.daysTo(QDate::currentDate());
            if (have_days * 2 < window_days)
                baseline_unavailable = true;
        }
    } else {
        period_baseline = cost_basis > 0 ? cost_basis : live_nav;
        const qint64 yest = QDateTime::currentDateTimeUtc().addDays(-1).toMSecsSinceEpoch();
        const qint64 now_ms = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
        nav_pts   = {QPointF(0.0, period_baseline), QPointF(1.0, live_nav)};
        nav_ts_ms = {yest, now_ms};
        nav_dates = {QDate::currentDate().addDays(-1), QDate::currentDate()};
        baseline_unavailable = true;
    }

    // ── Period P&L is *always* relative to the period baseline ───────────────
    const double period_pnl = live_nav - period_baseline;
    const double period_pnl_pct = period_baseline > 0 ? (period_pnl / period_baseline) * 100.0 : 0.0;

    // ── Convert series to display space (currency value vs. base-100 indexed) ─
    auto to_display = [&](double v) -> double {
        if (!indexed_mode_)
            return v;
        return period_baseline > 0 ? (v / period_baseline) * 100.0 : 0.0;
    };

    auto* nav_line = new QLineSeries;
    auto* area_upper = new QLineSeries;
    auto* area_lower = new QLineSeries;

    double y_min = std::numeric_limits<double>::max();
    double y_max = std::numeric_limits<double>::lowest();
    const double area_baseline_disp = to_display(period_baseline);

    for (const auto& p : nav_pts) {
        const double y = to_display(p.y());
        nav_line->append(p.x(), y);
        area_upper->append(p.x(), y);
        area_lower->append(p.x(), area_baseline_disp);
        y_min = std::min(y_min, y);
        y_max = std::max(y_max, y);
    }

    // Y-axis must always include the cost-basis reference so the user can see
    // the gap between NAV and cost. Skip in indexed mode where 100 is the line.
    const double cost_disp = to_display(cost_basis);
    if (cost_basis > 0) {
        y_min = std::min(y_min, cost_disp);
        y_max = std::max(y_max, cost_disp);
    }
    y_min = std::min(y_min, area_baseline_disp);
    y_max = std::max(y_max, area_baseline_disp);

    QColor lc = period_pnl >= 0 ? QColor(ui::colors::POSITIVE()) : QColor(ui::colors::NEGATIVE());
    nav_line->setPen(QPen(lc, 2));

    auto* area = new QAreaSeries(area_upper, area_lower);
    QColor fill = lc;
    fill.setAlpha(35);
    area->setBrush(fill);
    area->setPen(Qt::NoPen);

    chart->addSeries(area);
    chart->addSeries(nav_line);

    // ── Cost basis reference line ────────────────────────────────────────────
    // Skipped in indexed mode (where the period baseline IS the reference at 100).
    QLineSeries* cost_line = nullptr;
    if (cost_basis > 0 && !indexed_mode_) {
        cost_line = new QLineSeries;
        cost_line->setName("Cost basis");
        cost_line->append(nav_pts.first().x(), cost_disp);
        cost_line->append(nav_pts.last().x(), cost_disp);
        QPen cp(QColor(ui::colors::TEXT_SECONDARY()), 1, Qt::DashLine);
        cost_line->setPen(cp);
        chart->addSeries(cost_line);
    }

    // ── Axes ──────────────────────────────────────────────────────────────────
    auto* x_axis = new QCategoryAxis;
    x_axis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
    x_axis->setRange(-0.5, static_cast<double>(nav_pts.size()) - 0.5);
    for (const auto& [idx, lbl] : daily_seq_labels(nav_dates, current_period_))
        x_axis->append(lbl, idx);
    x_axis->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
    x_axis->setGridLineVisible(false);
    x_axis->setMinorGridLineVisible(false);
    x_axis->setLinePen(QPen(QColor(ui::colors::BORDER_DIM()), 1));
    x_axis->setLabelsFont(QFont(ui::fonts::DATA_FAMILY(), 8));

    auto* y_axis = new QValueAxis;
    const double padding = std::max((y_max - y_min) * 0.08, std::abs(y_max) * 0.01);
    y_axis->setRange(y_min - padding, y_max + padding);
    y_axis->setLabelFormat(indexed_mode_ ? "%.1f" : "%.0f");
    y_axis->setTickCount(5);
    y_axis->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
    y_axis->setGridLinePen(QPen(QColor(ui::colors::BORDER_DIM()), 1, Qt::DotLine));
    y_axis->setMinorGridLineVisible(false);
    y_axis->setLinePen(QPen(QColor(ui::colors::BORDER_DIM()), 1));
    y_axis->setLabelsFont(QFont(ui::fonts::DATA_FAMILY(), 8));

    chart->addAxis(x_axis, Qt::AlignBottom);
    chart->addAxis(y_axis, Qt::AlignLeft);
    nav_line->attachAxis(x_axis);
    nav_line->attachAxis(y_axis);
    area->attachAxis(x_axis);
    area->attachAxis(y_axis);
    if (cost_line) {
        cost_line->attachAxis(x_axis);
        cost_line->attachAxis(y_axis);
    }

    // nav_line->points() has sequential x + display-transformed y (matches chart)
    chart_view_->set_series_data(nav_line->points(),
                                 indexed_mode_ ? QStringLiteral("idx") : currency_,
                                 nav_ts_ms);

    // ── Benchmark overlay ─────────────────────────────────────────────────────
    if (show_benchmark_ && nav_line->count() >= 2) {
        if (spy_dates_.isEmpty() || spy_closes_.isEmpty()) {
            nav_label_->setText(nav_label_->text() +
                                QString("  |  %1: loading…").arg(benchmark_symbol_));
        } else {
            const QDate start_date =
                QDateTime::fromMSecsSinceEpoch(nav_ts_ms.first(), QTimeZone::UTC).date();
            const QDate end_date =
                QDateTime::fromMSecsSinceEpoch(nav_ts_ms.last(), QTimeZone::UTC).date();

            double bench_base = 0.0;
            int base_idx = -1;
            for (int i = 0; i < spy_dates_.size(); ++i) {
                const QDate d = QDate::fromString(spy_dates_[i], Qt::ISODate);
                if (d >= start_date) {
                    bench_base = spy_closes_[i];
                    base_idx = i;
                    break;
                }
            }

            if (base_idx >= 0 && bench_base > 1e-6) {
                auto* bench_line = new QLineSeries;
                double b_min = std::numeric_limits<double>::max();
                double b_max = std::numeric_limits<double>::lowest();
                for (int i = base_idx; i < spy_dates_.size(); ++i) {
                    const QDate d = QDate::fromString(spy_dates_[i], Qt::ISODate);
                    if (d > end_date) break;
                    auto it = date_to_seq.find(d);
                    if (it == date_to_seq.end()) continue; // no matching snapshot
                    const double bench_disp = indexed_mode_
                        ? (spy_closes_[i] / bench_base) * 100.0
                        : period_baseline * (spy_closes_[i] / bench_base);
                    bench_line->append(static_cast<double>(it.value()), bench_disp);
                    b_min = std::min(b_min, bench_disp);
                    b_max = std::max(b_max, bench_disp);
                }

                if (bench_line->count() >= 2) {
                    QPen bp(QColor(ui::colors::CYAN()), 1, Qt::DashLine);
                    bench_line->setPen(bp);
                    bench_line->setName(benchmark_symbol_);
                    chart->addSeries(bench_line);
                    bench_line->attachAxis(x_axis);
                    bench_line->attachAxis(y_axis);
                    const double new_min = std::min(y_min, b_min);
                    const double new_max = std::max(y_max, b_max);
                    const double new_pad = std::max((new_max - new_min) * 0.08, std::abs(new_max) * 0.01);
                    y_axis->setRange(new_min - new_pad, new_max + new_pad);
                } else {
                    delete bench_line;
                }
            }
        }
    }

    // ── Info labels ───────────────────────────────────────────────────────────
    // When the snapshot history doesn't cover the requested period, the
    // computed period_pnl_pct is really a (partial-window or total) return
    // and would mislead as "1W +X%". Show "—" instead until backfill lands.
    if (baseline_unavailable) {
        period_change_label_->setText(QString("%1  —").arg(current_period_));
        period_change_label_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:600;")
                .arg(ui::colors::TEXT_SECONDARY()));
        period_change_label_->setToolTip(
            QStringLiteral("Not enough snapshot history yet — backfill in progress."));
    } else {
        const char* pnl_color = period_pnl_pct >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
        period_change_label_->setText(QString("%1  %2%3%")
                                          .arg(current_period_)
                                          .arg(period_pnl_pct >= 0 ? "+" : "")
                                          .arg(QString::number(period_pnl_pct, 'f', 2)));
        period_change_label_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:600;").arg(pnl_color));
        period_change_label_->setToolTip(QString());
    }

    const double total_pnl_pct = summary_.total_unrealized_pnl_percent;
    const char* total_color = total_pnl_pct >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
    total_return_label_->setText(
        QString("TOTAL  %1%2%").arg(total_pnl_pct >= 0 ? "+" : "").arg(QString::number(total_pnl_pct, 'f', 2)));
    total_return_label_->setStyleSheet(
        QString("color:%1; font-size:14px; font-weight:700;").arg(total_color));

    nav_label_->setText(QString("NAV %1 %2").arg(currency_).arg(QString::number(live_nav, 'f', 2)));
    if (cost_basis_label_) {
        if (cost_basis > 0)
            cost_basis_label_->setText(
                QString("COST %1 %2").arg(currency_).arg(QString::number(cost_basis, 'f', 2)));
        else
            cost_basis_label_->clear();
    }
}

void PortfolioPerfChart::refresh_theme() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    const QString bsz = QString::number(ui::fonts::font_px(-3));
    // Re-style period buttons
    for (auto* btn : period_btns_) {
        btn->setStyleSheet(QString("QPushButton { background:transparent; color:%1; border:1px solid transparent;"
                                   "  font-size:" +
                                   bsz +
                                   "px; font-weight:700; border-radius:2px; }"
                                   "QPushButton:checked { color:%4; background:%2;"
                                   "  border:1px solid %2; }"
                                   "QPushButton:hover:!checked { color:%3; border-color:%5; }")
                               .arg(ui::colors::TEXT_SECONDARY(), ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(),
                                    ui::colors::BG_BASE(), ui::colors::BORDER_MED()));
    }
    if (benchmark_btn_) {
        benchmark_btn_->setStyleSheet(
            QString("QPushButton { background:transparent; color:%1; border:1px solid %1;"
                    "  font-size:" +
                    bsz +
                    "px; font-weight:700; border-radius:2px; }"
                    "QPushButton:checked { color:%4; background:%2; border:1px solid %2; }"
                    "QPushButton:hover:!checked { color:%3; border-color:%3; }")
                .arg(ui::colors::CYAN(), ui::colors::CYAN(), ui::colors::TEXT_PRIMARY(), ui::colors::BG_BASE()));
    }

    // Chart background
    if (chart_view_ && chart_view_->chart())
        chart_view_->chart()->setBackgroundBrush(QColor(ui::colors::BG_BASE()));

    // Rebuild chart with current theme colors (axes, line colors, etc.)
    update_chart();
}

} // namespace fincept::screens

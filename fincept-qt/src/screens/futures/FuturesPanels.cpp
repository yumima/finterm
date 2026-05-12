#include "screens/futures/FuturesPanels.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "screens/futures/FuturesContracts.h"
#include "screens/futures/FuturesQuoteCache.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QChart>
#include <QChartView>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineSeries>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QStackedLayout>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens::futures {

using namespace fincept::ui;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

static QString lbl_ss(const QString& color, bool bold = false, int px = -1) {
    const int sz = (px > 0) ? px : fonts::font_px(-2);
    return QString("color:%1;background:transparent;font-size:%2px;font-family:'%3';%4")
        .arg(color).arg(sz).arg(fonts::DATA_FAMILY()).arg(bold ? "font-weight:bold;" : "");
}

static QString fmt_num(double v, int decimals = 2) {
    if (!std::isfinite(v)) return "—";
    return QString::number(v, 'f', decimals);
}

static QString fmt_signed(double v, int decimals = 2) {
    if (!std::isfinite(v)) return "—";
    return (v > 0 ? "+" : "") + QString::number(v, 'f', decimals);
}

static QString fmt_volume(double v) {
    if (!std::isfinite(v) || v <= 0) return "—";
    if (v >= 1e9) return QString::number(v / 1e9, 'f', 2) + "B";
    if (v >= 1e6) return QString::number(v / 1e6, 'f', 2) + "M";
    if (v >= 1e3) return QString::number(v / 1e3, 'f', 1) + "K";
    return QString::number(v, 'f', 0);
}

static QString color_for_change(double v) {
    if (v > 0) return colors::POSITIVE();
    if (v < 0) return colors::NEGATIVE();
    return colors::TEXT_SECONDARY();
}

static QString table_ss() {
    return QString(
        "QTableWidget { background:%1; color:%2; gridline-color:%3; border:none;"
        " font-family:'%4'; font-size:%5px; alternate-background-color:%6; }"
        "QHeaderView::section { background:%7; color:%2; padding:4px 6px;"
        " border:none; border-bottom:1px solid %3; font-weight:600; }")
        .arg(colors::BG_BASE())
        .arg(colors::TEXT_PRIMARY())
        .arg(colors::BORDER_DIM())
        .arg(fonts::DATA_FAMILY())
        .arg(fonts::font_px(-2))
        .arg(colors::BG_SURFACE())
        .arg(colors::BG_RAISED());
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesPanelBase
// ─────────────────────────────────────────────────────────────────────────────

FuturesPanelBase::FuturesPanelBase(const QString& title, QWidget* parent) : QWidget(parent) {
    setObjectName("futuresPanel");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 8);
    root->setSpacing(4);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 2);
    header->setSpacing(8);

    title_label_ = new QLabel(title);
    title_label_->setStyleSheet(lbl_ss(colors::AMBER(), true, fonts::font_px(0)));
    header->addWidget(title_label_);

    header->addStretch();

    status_label_ = new QLabel;
    status_label_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
    header->addWidget(status_label_);

    root->addLayout(header);

    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
            [this](const ThemeTokens&) { apply_theme(); });
    apply_theme();
}

void FuturesPanelBase::apply_theme() {
    setStyleSheet(QString("#futuresPanel { background:%1; border:1px solid %2; }")
                      .arg(colors::BG_BASE())
                      .arg(colors::BORDER_DIM()));
    if (title_label_)
        title_label_->setStyleSheet(lbl_ss(colors::AMBER(), true, fonts::font_px(0)));
    if (status_label_)
        status_label_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
}

void FuturesPanelBase::set_status(const QString& s, const QString& color) {
    if (!status_label_) return;
    status_label_->setText(s);
    if (!color.isEmpty())
        status_label_->setStyleSheet(lbl_ss(color, false, fonts::font_px(-3)));
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesWatchlistPanel — cache-driven
// ─────────────────────────────────────────────────────────────────────────────

FuturesWatchlistPanel::FuturesWatchlistPanel(QWidget* parent) : FuturesPanelBase("WATCHLIST", parent) {
    table_ = new QTableWidget(0, 7, this);
    table_->setHorizontalHeaderLabels({"Symbol", "Name", "Last", "Chg", "%Chg", "Volume", "OI"});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->setStyleSheet(table_ss());
    layout()->addWidget(table_);

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0) return;
        auto* it = table_->item(row, 0);
        if (it) emit symbol_clicked(it->text());
    });

    // Subscribe to cache updates — re-render whenever a new batch lands.
    connect(&FuturesQuoteCache::instance(), &FuturesQuoteCache::quotes_updated,
            this, &FuturesWatchlistPanel::render_from_cache);
}

void FuturesWatchlistPanel::set_asset_class(const QString& cls) {
    active_class_ = cls;
    render_from_cache();                 // instant — synchronous read
    FuturesQuoteCache::instance().refresh();  // top-up if stale
}

void FuturesWatchlistPanel::refresh() {
    render_from_cache();
}

void FuturesWatchlistPanel::render_from_cache() {
    if (active_class_.isEmpty() || active_class_ == "CHINA") {
        table_->setRowCount(0);
        set_status("—");
        return;
    }
    auto& cache = FuturesQuoteCache::instance();
    const auto rows = cache.quotes_for_class(active_class_);
    if (rows.isEmpty()) {
        table_->setRowCount(0);
        set_status(cache.has_data() ? "no data" : "loading…");
        return;
    }
    set_status(QString("%1 · %2").arg(cache.last_source(),
                                       cache.last_refresh().toString("HH:mm:ss")));

    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& q = rows[i];
        auto add = [&](int col, const QString& text, const QString& color = QString()) {
            auto* it = new QTableWidgetItem(text);
            if (!color.isEmpty()) it->setForeground(QColor(color));
            if (col >= 2) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(i, col, it);
        };
        add(0, q.symbol);
        add(1, q.name);
        add(2, fmt_num(q.last, 4));
        add(3, fmt_signed(q.change, 4), color_for_change(q.change));
        add(4, fmt_signed(q.change_pct, 2) + "%", color_for_change(q.change));
        add(5, fmt_volume(q.volume));
        add(6, fmt_volume(q.open_interest));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesTermStructurePanel — symbol-driven, generation-tokened
// ─────────────────────────────────────────────────────────────────────────────

FuturesTermStructurePanel::FuturesTermStructurePanel(QWidget* parent)
    : FuturesPanelBase("TERM STRUCTURE", parent) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 2, 0, 2);
    row->setSpacing(8);

    auto* lbl = new QLabel("Symbol");
    lbl->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
    row->addWidget(lbl);

    symbol_combo_ = new QComboBox;
    symbol_combo_->setMinimumWidth(150);
    row->addWidget(symbol_combo_);

    contango_label_ = new QLabel;
    contango_label_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), true, fonts::font_px(-2)));
    row->addStretch();
    row->addWidget(contango_label_);
    layout()->addItem(row);

    // Stack of {chart, placeholder} so we can swap when only FRONT is available.
    auto* stack_host = new QWidget(this);
    auto* stack = new QStackedLayout(stack_host);
    stack->setContentsMargins(0, 0, 0, 0);

    auto* chart = new QChart;
    chart->legend()->hide();
    chart->setBackgroundBrush(QColor(colors::BG_BASE()));
    chart->setMargins({0, 0, 0, 0});
    chart_view_ = new QChartView(chart, stack_host);
    chart_view_->setRenderHint(QPainter::Antialiasing);
    chart_view_->setMinimumHeight(140);
    stack->addWidget(chart_view_);

    placeholder_ = new QLabel(stack_host);
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    placeholder_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-1)));
    stack->addWidget(placeholder_);

    layout()->addWidget(stack_host);

    connect(symbol_combo_, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (s.isEmpty()) return;
        active_symbol_ = s;
        refresh();
    });
}

void FuturesTermStructurePanel::set_asset_class(const QString& cls) {
    active_class_ = cls;
    QStringList syms = symbols_for_class(cls);
    symbol_combo_->blockSignals(true);
    symbol_combo_->clear();
    symbol_combo_->addItems(syms);
    symbol_combo_->blockSignals(false);
    if (!syms.isEmpty()) {
        active_symbol_ = syms.first();
        symbol_combo_->setCurrentText(active_symbol_);
        refresh();
    } else {
        active_symbol_.clear();
        chart_view_->chart()->removeAllSeries();
        contango_label_->setText("");
        show_placeholder("no contracts");
    }
}

void FuturesTermStructurePanel::set_symbol(const QString& sym) {
    if (sym.isEmpty()) return;
    active_symbol_ = sym;
    if (symbol_combo_) {
        const int idx = symbol_combo_->findText(sym);
        if (idx >= 0) {
            symbol_combo_->blockSignals(true);
            symbol_combo_->setCurrentIndex(idx);
            symbol_combo_->blockSignals(false);
        }
    }
    refresh();
}

void FuturesTermStructurePanel::refresh() {
    const QString sym = active_symbol_;
    if (sym.isEmpty()) return;
    // Skip timer-driven refreshes while a fetch is already in flight OR within
    // the failure backoff window. The backoff prevents "Loading → error →
    // Loading" cycling when the data source is unavailable.
    if (fetch_in_flight_) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (last_failed_ms_ > 0 && (now - last_failed_ms_) < kFailureBackoffMs) return;
    fetch_in_flight_ = true;
    set_status("loading…");
    show_placeholder(QString("Loading term structure for %1…").arg(sym));
    const quint64 my_gen = bump_gen();
    FuturesDataService::instance().fetch_term_structure(
        sym, 8, [this, my_gen, sym](bool ok, QVector<TermStructurePoint> pts, QString source) {
            fetch_in_flight_ = false;
            if (!is_current(my_gen)) return;          // stale — drop
            if (!ok) {
                last_failed_ms_ = QDateTime::currentMSecsSinceEpoch();
                set_status("error", colors::NEGATIVE());
                show_placeholder(QString("Term structure unavailable for %1.\n\n"
                                         "Neither CME public data nor a Databento key is\n"
                                         "available. To enable live forward curves:\n"
                                         "  • Set DATABENTO_API_KEY in your environment\n"
                                         "    (free tier available at databento.com)\n"
                                         "  • Or ensure network access to cmegroup.com").arg(sym));
                return;
            }
            set_status(QString("%1 · %2").arg(source, sym));
            render(pts);
        });
}

void FuturesTermStructurePanel::show_placeholder(const QString& message) {
    if (auto* stack = qobject_cast<QStackedLayout*>(placeholder_->parentWidget()->layout()))
        stack->setCurrentIndex(1);
    placeholder_->setText(message);
    contango_label_->setText("");
}

void FuturesTermStructurePanel::render(const QVector<TermStructurePoint>& pts) {
    // The yfinance fallback only delivers a single "FRONT" point. Detect this
    // and render a clear placeholder rather than a misleading single dot.
    if (pts.size() <= 1) {
        show_placeholder("Front-month only.\nLive forward curve requires a Databento subscription\n"
                         "(set DATABENTO_API_KEY and restart).");
        return;
    }

    if (auto* stack = qobject_cast<QStackedLayout*>(placeholder_->parentWidget()->layout()))
        stack->setCurrentIndex(0);

    auto* chart = chart_view_->chart();
    chart->removeAllSeries();
    for (auto* axis : chart->axes()) chart->removeAxis(axis);

    auto* series = new QLineSeries;
    series->setColor(QColor(colors::AMBER()));
    double min_v = std::numeric_limits<double>::infinity();
    double max_v = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < pts.size(); ++i) {
        if (!std::isfinite(pts[i].last) || pts[i].last <= 0) continue;
        series->append(i, pts[i].last);
        min_v = std::min(min_v, pts[i].last);
        max_v = std::max(max_v, pts[i].last);
    }
    chart->addSeries(series);

    auto* x_axis = new QValueAxis;
    x_axis->setRange(0, std::max<int>(1, static_cast<int>(pts.size()) - 1));
    x_axis->setTickCount(std::min<int>(static_cast<int>(pts.size()), 8));
    x_axis->setLabelFormat("%d");
    x_axis->setGridLineColor(QColor(colors::BORDER_DIM()));
    x_axis->setLabelsBrush(QColor(colors::TEXT_SECONDARY()));

    auto* y_axis = new QValueAxis;
    if (std::isfinite(min_v) && std::isfinite(max_v) && max_v > min_v) {
        const double pad = (max_v - min_v) * 0.05;
        y_axis->setRange(min_v - pad, max_v + pad);
    }
    y_axis->setLabelFormat("%.2f");
    y_axis->setGridLineColor(QColor(colors::BORDER_DIM()));
    y_axis->setLabelsBrush(QColor(colors::TEXT_SECONDARY()));

    chart->addAxis(x_axis, Qt::AlignBottom);
    chart->addAxis(y_axis, Qt::AlignLeft);
    series->attachAxis(x_axis);
    series->attachAxis(y_axis);

    if (std::isfinite(pts.first().last) && std::isfinite(pts.last().last) && pts.first().last > 0) {
        const bool contango = pts.last().last > pts.first().last;
        const double front  = pts.first().last;
        const double back   = pts.last().last;
        const double total_pct = (back - front) / front * 100.0;

        // Roll cost: front → second-month is what a long position pays (or
        // earns) each time it rolls. Magnitude matters more than the label —
        // contango of 2% is meaningful, 0.05% isn't.
        QString roll_segment;
        if (pts.size() >= 2 && std::isfinite(pts[1].last) && pts[1].last > 0) {
            const double roll_pct = (pts[1].last - front) / front * 100.0;
            roll_segment = QString("  ·  Roll %1%2%")
                               .arg(roll_pct >= 0 ? "+" : "")
                               .arg(QString::number(roll_pct, 'f', 2));
        }

        contango_label_->setText(QString("%1  %2%3%%4")
                                     .arg(contango ? "CONTANGO" : "BACKWARDATION")
                                     .arg(total_pct >= 0 ? "+" : "")
                                     .arg(QString::number(total_pct, 'f', 2))
                                     .arg(roll_segment));
        contango_label_->setStyleSheet(lbl_ss(contango ? colors::POSITIVE() : colors::NEGATIVE(),
                                              true, fonts::font_px(-2)));
    } else {
        contango_label_->setText("");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesSpreadPanel — cache-driven
// ─────────────────────────────────────────────────────────────────────────────

FuturesSpreadPanel::FuturesSpreadPanel(QWidget* parent) : FuturesPanelBase("SPREAD MONITOR", parent) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 2, 0, 2);
    row->setSpacing(8);

    leg1_combo_ = new QComboBox;
    leg2_combo_ = new QComboBox;
    auto* dash = new QLabel("−");
    dash->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), true));
    row->addWidget(leg1_combo_);
    row->addWidget(dash);
    row->addWidget(leg2_combo_);
    row->addStretch();
    layout()->addItem(row);

    auto* result = new QHBoxLayout;
    spread_lbl_ = new QLabel("—");
    spread_lbl_->setStyleSheet(lbl_ss(colors::TEXT_PRIMARY(), true, fonts::font_px(6)));
    pct_lbl_ = new QLabel;
    pct_lbl_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(0)));
    result->addStretch();
    result->addWidget(spread_lbl_);
    result->addSpacing(12);
    result->addWidget(pct_lbl_);
    result->addStretch();
    layout()->addItem(result);
    if (auto* vb = qobject_cast<QVBoxLayout*>(layout())) vb->addStretch();

    connect(leg1_combo_, &QComboBox::currentTextChanged, this, [this](const QString&) { render_from_cache(); });
    connect(leg2_combo_, &QComboBox::currentTextChanged, this, [this](const QString&) { render_from_cache(); });

    connect(&FuturesQuoteCache::instance(), &FuturesQuoteCache::quotes_updated,
            this, &FuturesSpreadPanel::render_from_cache);
}

void FuturesSpreadPanel::set_asset_class(const QString& cls) {
    active_class_ = cls;
    QStringList syms = symbols_for_class(cls);
    leg1_combo_->blockSignals(true);
    leg2_combo_->blockSignals(true);
    leg1_combo_->clear();
    leg2_combo_->clear();
    leg1_combo_->addItems(syms);
    leg2_combo_->addItems(syms);
    if (syms.size() >= 2) {
        leg1_combo_->setCurrentIndex(0);
        leg2_combo_->setCurrentIndex(1);
    } else if (syms.size() == 1) {
        leg1_combo_->setCurrentIndex(0);
    }
    leg1_combo_->blockSignals(false);
    leg2_combo_->blockSignals(false);
    render_from_cache();
}

void FuturesSpreadPanel::refresh() {
    render_from_cache();
}

void FuturesSpreadPanel::render_from_cache() {
    if (!leg1_combo_ || !leg2_combo_) return;
    const QString a = leg1_combo_->currentText();
    const QString b = leg2_combo_->currentText();
    if (a.isEmpty() || b.isEmpty() || a == b) {
        spread_lbl_->setText("—");
        pct_lbl_->setText("");
        return;
    }
    auto& cache = FuturesQuoteCache::instance();
    const auto rows = cache.quotes_for_class(active_class_);
    const FuturesQuote* qa = nullptr;
    const FuturesQuote* qb = nullptr;
    for (const auto& q : rows) {
        if (q.symbol == a) qa = &q;
        if (q.symbol == b) qb = &q;
    }
    if (!qa || !qb) {
        spread_lbl_->setText("—");
        pct_lbl_->setText(cache.has_data() ? "missing leg" : "loading…");
        return;
    }
    const double spread = qa->last - qb->last;
    const double pct = qa->change_pct - qb->change_pct;
    spread_lbl_->setText(fmt_signed(spread, 4));
    spread_lbl_->setStyleSheet(lbl_ss(color_for_change(spread), true, fonts::font_px(6)));
    pct_lbl_->setText(QString("%1 vs %2  ·  Δ%% %3").arg(qa->symbol, qb->symbol, fmt_signed(pct, 2)));
    set_status(cache.last_source());
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesSettlementsPanel — symbol-driven, generation-tokened
// ─────────────────────────────────────────────────────────────────────────────

FuturesSettlementsPanel::FuturesSettlementsPanel(QWidget* parent)
    : FuturesPanelBase("SETTLEMENTS · OI", parent) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 2, 0, 2);
    auto* lbl = new QLabel("Product");
    lbl->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
    row->addWidget(lbl);
    symbol_combo_ = new QComboBox;
    symbol_combo_->setMinimumWidth(150);
    row->addWidget(symbol_combo_);
    row->addStretch();
    layout()->addItem(row);

    auto* stack_host = new QWidget(this);
    auto* stack = new QStackedLayout(stack_host);
    stack->setContentsMargins(0, 0, 0, 0);

    table_ = new QTableWidget(0, 5, stack_host);
    table_->setHorizontalHeaderLabels({"Month", "Settle", "Chg", "Volume", "OI"});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setStyleSheet(table_ss());
    table_->setAlternatingRowColors(true);
    stack->addWidget(table_);

    placeholder_ = new QLabel(stack_host);
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    placeholder_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-1)));
    stack->addWidget(placeholder_);

    layout()->addWidget(stack_host);

    connect(symbol_combo_, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (s.isEmpty()) return;
        active_symbol_ = s;
        refresh();
    });
}

void FuturesSettlementsPanel::set_asset_class(const QString& cls) {
    active_class_ = cls;
    QStringList syms = symbols_for_class(cls);
    symbol_combo_->blockSignals(true);
    symbol_combo_->clear();
    symbol_combo_->addItems(syms);
    symbol_combo_->blockSignals(false);
    if (!syms.isEmpty()) {
        active_symbol_ = syms.first();
        symbol_combo_->setCurrentText(active_symbol_);
        refresh();
    } else {
        active_symbol_.clear();
        table_->setRowCount(0);
        show_placeholder("no contracts");
    }
}

void FuturesSettlementsPanel::set_symbol(const QString& sym) {
    if (sym.isEmpty()) return;
    active_symbol_ = sym;
    if (symbol_combo_) {
        const int idx = symbol_combo_->findText(sym);
        if (idx >= 0) {
            symbol_combo_->blockSignals(true);
            symbol_combo_->setCurrentIndex(idx);
            symbol_combo_->blockSignals(false);
        }
    }
    refresh();
}

void FuturesSettlementsPanel::refresh() {
    const QString sym = active_symbol_;
    if (sym.isEmpty()) return;
    if (fetch_in_flight_) return;
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (last_failed_ms_ > 0 && (now_ms - last_failed_ms_) < kFailureBackoffMs) return;
    fetch_in_flight_ = true;
    set_status("loading…");
    show_placeholder(QString("Loading settlements for %1…").arg(sym));
    const quint64 my_gen = bump_gen();
    FuturesDataService::instance().fetch_term_structure(
        sym, 12, [this, my_gen, sym](bool ok, QVector<TermStructurePoint> pts, QString source) {
            fetch_in_flight_ = false;
            if (!is_current(my_gen)) return;
            if (!ok) {
                last_failed_ms_ = QDateTime::currentMSecsSinceEpoch();
                set_status("error", colors::NEGATIVE());
                show_placeholder(QString("Per-month settlements unavailable for %1.\n\n"
                                         "Neither CME public data nor a Databento key is\n"
                                         "available. To enable live settlement data:\n"
                                         "  • Set DATABENTO_API_KEY in your environment\n"
                                         "    (free tier available at databento.com)\n"
                                         "  • Or ensure network access to cmegroup.com").arg(sym));
                return;
            }
            set_status(QString("%1 · %2").arg(source, sym));
            populate(pts);
        });
}

void FuturesSettlementsPanel::show_placeholder(const QString& message) {
    if (auto* stack = qobject_cast<QStackedLayout*>(placeholder_->parentWidget()->layout()))
        stack->setCurrentIndex(1);
    placeholder_->setText(message);
}

void FuturesSettlementsPanel::populate(const QVector<TermStructurePoint>& pts) {
    if (pts.size() <= 1) {
        show_placeholder("Per-month settlements unavailable.\n"
                         "Live data requires Databento (set DATABENTO_API_KEY).");
        return;
    }
    if (auto* stack = qobject_cast<QStackedLayout*>(placeholder_->parentWidget()->layout()))
        stack->setCurrentIndex(0);

    table_->setRowCount(pts.size());
    for (int i = 0; i < pts.size(); ++i) {
        const auto& p = pts[i];
        auto add = [&](int col, const QString& text, const QString& color = QString()) {
            auto* it = new QTableWidgetItem(text);
            if (!color.isEmpty()) it->setForeground(QColor(color));
            if (col >= 1) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(i, col, it);
        };
        add(0, p.month.isEmpty() ? QString("M%1").arg(i + 1) : p.month);
        add(1, fmt_num(p.last, 4));
        add(2, fmt_signed(p.change, 4), color_for_change(p.change));
        add(3, fmt_volume(p.volume));
        add(4, fmt_volume(p.open_interest));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesHeatmapPanel — cache-driven
// ─────────────────────────────────────────────────────────────────────────────

FuturesHeatmapPanel::FuturesHeatmapPanel(QWidget* parent) : FuturesPanelBase("HEATMAP · ALL ASSET CLASSES", parent) {
    canvas_ = new QWidget(this);
    canvas_->setObjectName("heatmapCanvas");
    grid_ = new QGridLayout(canvas_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(2);
    layout()->addWidget(canvas_);

    connect(&FuturesQuoteCache::instance(), &FuturesQuoteCache::quotes_updated,
            this, &FuturesHeatmapPanel::render_from_cache);
}

void FuturesHeatmapPanel::clear_grid() {
    while (auto* item = grid_->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
}

void FuturesHeatmapPanel::refresh() {
    render_from_cache();
}

void FuturesHeatmapPanel::render_from_cache() {
    auto& cache = FuturesQuoteCache::instance();
    const auto groups = cache.grouped_by_class();
    if (!cache.has_data()) {
        set_status("loading…");
        return;
    }
    set_status(QString("%1 · %2").arg(cache.last_source(),
                                       cache.last_refresh().toString("HH:mm:ss")));

    clear_grid();
    int row = 0;
    const QStringList order = {"INDEX", "RATES", "ENERGY", "METALS", "AGS", "FX", "CRYPTO"};
    for (const auto& cls : order) {
        if (!groups.contains(cls)) continue;
        auto* row_label = new QLabel(cls);
        row_label->setFixedWidth(70);
        row_label->setStyleSheet(lbl_ss(colors::AMBER(), true, fonts::font_px(-2)));
        grid_->addWidget(row_label, row, 0);

        const auto& cells = groups.value(cls);
        for (int i = 0; i < cells.size(); ++i) {
            const auto& q = cells[i];
            auto* tile = new QLabel(QString("%1\n%2%").arg(q.symbol, fmt_signed(q.change_pct, 1)));
            tile->setAlignment(Qt::AlignCenter);
            tile->setMinimumSize(56, 36);
            tile->setToolTip(QString("%1\nLast %2  ·  %3").arg(q.name, fmt_num(q.last, 4),
                                                                fmt_signed(q.change_pct, 2) + "%"));
            const double pct = std::clamp(q.change_pct / 3.0, -1.0, 1.0);
            const int alpha = static_cast<int>(80 + std::abs(pct) * 175);
            QColor bg = pct >= 0 ? QColor(colors::POSITIVE_BG()) : QColor(colors::NEGATIVE_BG());
            bg.setAlpha(alpha);
            tile->setStyleSheet(QString("QLabel { background:rgba(%1,%2,%3,%4); color:%5;"
                                        " border:1px solid %6; font-family:'%7';"
                                        " font-size:%8px; font-weight:600; padding:2px; }")
                                    .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
                                    .arg(colors::TEXT_PRIMARY())
                                    .arg(colors::BORDER_DIM())
                                    .arg(fonts::DATA_FAMILY())
                                    .arg(fonts::font_px(-3)));
            grid_->addWidget(tile, row, i + 1);
        }
        ++row;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesChartPanel — symbol-driven, generation-tokened
// ─────────────────────────────────────────────────────────────────────────────

FuturesChartPanel::FuturesChartPanel(QWidget* parent) : FuturesPanelBase("CONTINUOUS CHART", parent) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 2, 0, 2);
    row->setSpacing(8);
    auto* lbl1 = new QLabel("Symbol");
    lbl1->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
    symbol_combo_ = new QComboBox;
    symbol_combo_->setMinimumWidth(150);
    auto* lbl2 = new QLabel("Period");
    lbl2->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
    period_combo_ = new QComboBox;
    period_combo_->addItems({"1mo", "3mo", "6mo", "1y", "2y"});
    period_combo_->setCurrentText("6mo");
    row->addWidget(lbl1);
    row->addWidget(symbol_combo_);
    row->addSpacing(8);
    row->addWidget(lbl2);
    row->addWidget(period_combo_);
    row->addStretch();
    layout()->addItem(row);

    auto* chart = new QChart;
    chart->legend()->hide();
    chart->setBackgroundBrush(QColor(colors::BG_BASE()));
    chart->setMargins({0, 0, 0, 0});
    chart_view_ = new QChartView(chart, this);
    chart_view_->setRenderHint(QPainter::Antialiasing);
    chart_view_->setMinimumHeight(180);
    layout()->addWidget(chart_view_);

    connect(symbol_combo_, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (s.isEmpty()) return;
        active_symbol_ = s;
        refresh();
    });
    connect(period_combo_, &QComboBox::currentTextChanged, this, [this](const QString&) { refresh(); });
}

void FuturesChartPanel::set_asset_class(const QString& cls) {
    active_class_ = cls;
    QStringList syms = symbols_for_class(cls);
    symbol_combo_->blockSignals(true);
    symbol_combo_->clear();
    symbol_combo_->addItems(syms);
    symbol_combo_->blockSignals(false);
    if (!syms.isEmpty()) {
        active_symbol_ = syms.first();
        symbol_combo_->setCurrentText(active_symbol_);
        refresh();
    } else {
        active_symbol_.clear();
        chart_view_->chart()->removeAllSeries();
    }
}

void FuturesChartPanel::set_symbol(const QString& sym) {
    if (sym.isEmpty()) return;
    active_symbol_ = sym;
    if (symbol_combo_) {
        const int idx = symbol_combo_->findText(sym);
        if (idx >= 0) {
            symbol_combo_->blockSignals(true);
            symbol_combo_->setCurrentIndex(idx);
            symbol_combo_->blockSignals(false);
        }
    }
    refresh();
}

void FuturesChartPanel::refresh() {
    const QString sym = active_symbol_;
    const QString period = period_combo_ ? period_combo_->currentText() : "6mo";
    if (sym.isEmpty()) return;
    set_status("loading…");
    const quint64 my_gen = bump_gen();
    FuturesDataService::instance().fetch_history(
        sym, period, [this, my_gen, sym](bool ok, QVector<HistoryPoint> pts, QString source) {
            if (!is_current(my_gen)) return;
            if (!ok) { set_status("error", colors::NEGATIVE()); return; }
            set_status(QString("%1 · %2").arg(source, sym));
            render(pts);
        });
}

void FuturesChartPanel::render(const QVector<HistoryPoint>& pts) {
    auto* chart = chart_view_->chart();
    chart->removeAllSeries();
    for (auto* axis : chart->axes()) chart->removeAxis(axis);

    if (pts.isEmpty()) return;
    auto* series = new QLineSeries;
    series->setColor(QColor(colors::AMBER()));
    double min_v = std::numeric_limits<double>::infinity();
    double max_v = -std::numeric_limits<double>::infinity();
    for (const auto& p : pts) {
        if (!std::isfinite(p.close) || p.close <= 0) continue;
        series->append(static_cast<qreal>(p.timestamp) * 1000.0, p.close);
        min_v = std::min(min_v, p.close);
        max_v = std::max(max_v, p.close);
    }
    chart->addSeries(series);

    auto* x_axis = new QDateTimeAxis;
    x_axis->setFormat("MMM dd");
    x_axis->setTickCount(6);
    x_axis->setLabelsBrush(QColor(colors::TEXT_SECONDARY()));
    x_axis->setGridLineColor(QColor(colors::BORDER_DIM()));

    auto* y_axis = new QValueAxis;
    if (std::isfinite(min_v) && std::isfinite(max_v) && max_v > min_v) {
        const double pad = (max_v - min_v) * 0.05;
        y_axis->setRange(min_v - pad, max_v + pad);
    }
    y_axis->setLabelFormat("%.2f");
    y_axis->setLabelsBrush(QColor(colors::TEXT_SECONDARY()));
    y_axis->setGridLineColor(QColor(colors::BORDER_DIM()));

    chart->addAxis(x_axis, Qt::AlignBottom);
    chart->addAxis(y_axis, Qt::AlignLeft);
    series->attachAxis(x_axis);
    series->attachAxis(y_axis);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesChinaPanel — akshare main contracts
// ─────────────────────────────────────────────────────────────────────────────

FuturesChinaPanel::FuturesChinaPanel(QWidget* parent) : FuturesPanelBase("CHINA · MAIN CONTRACTS", parent) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 2, 0, 2);
    auto* lbl = new QLabel("Exchange");
    lbl->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
    row->addWidget(lbl);
    exchange_combo_ = new QComboBox;
    exchange_combo_->addItems({"all", "SHFE", "DCE", "CZCE", "INE"});
    row->addWidget(exchange_combo_);
    row->addStretch();
    layout()->addItem(row);

    table_ = new QTableWidget(0, 0, this);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setStyleSheet(table_ss());
    layout()->addWidget(table_);

    connect(exchange_combo_, &QComboBox::currentTextChanged, this, [this](const QString&) { refresh(); });
}

void FuturesChinaPanel::refresh() {
    set_status("loading…");
    const QString ex = exchange_combo_ ? exchange_combo_->currentText() : "all";
    const quint64 my_gen = bump_gen();
    FuturesDataService::instance().fetch_china_main(
        ex, [this, my_gen, ex](bool ok, QJsonArray rows, QString source) {
            if (!is_current(my_gen)) return;
            if (!ok) { set_status("akshare error", colors::NEGATIVE()); return; }
            set_status(QString("%1 · %2").arg(source, ex));
            populate(rows);
        });
}

void FuturesChinaPanel::populate(const QJsonArray& rows) {
    if (rows.isEmpty()) {
        table_->setRowCount(0);
        table_->setColumnCount(0);
        return;
    }
    const QJsonObject first = rows.first().toObject();
    QStringList headers = first.keys();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto obj = rows[i].toObject();
        for (int c = 0; c < headers.size(); ++c) {
            const auto val = obj.value(headers[c]);
            QString text;
            if (val.isString()) text = val.toString();
            else if (val.isDouble()) text = QString::number(val.toDouble(), 'f', 4);
            else if (val.isBool()) text = val.toBool() ? "true" : "false";
            else text = val.toVariant().toString();
            table_->setItem(i, c, new QTableWidgetItem(text));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesExpiryPanel — client-side expiry calendar, cache-free
// ─────────────────────────────────────────────────────────────────────────────

FuturesExpiryPanel::FuturesExpiryPanel(QWidget* parent)
    : FuturesPanelBase("EXPIRY CALENDAR", parent) {
    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"Symbol", "Name", "Next Expiry", "DTE", "Init. Margin"});
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->verticalHeader()->setVisible(false);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(0, 60);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(2, 110);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(3, 60);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(4, 100);
    table_->setStyleSheet(
        QString("QTableWidget{background:%1;color:%2;border:none;"
                "  font-family:'%3';font-size:%4px;gridline-color:transparent;}"
                "QTableWidget::item{padding:4px 6px;border-bottom:1px solid %5;}")
            .arg(colors::BG_BASE(), colors::TEXT_PRIMARY(),
                 fonts::DATA_FAMILY())
            .arg(fonts::font_px(-2))
            .arg(colors::BORDER_DIM()));
    table_->horizontalHeader()->setStyleSheet(
        QString("QHeaderView::section{background:%1;color:%2;border:none;"
                "  border-bottom:1px solid %3;padding:4px 8px;font-weight:700;}")
            .arg(colors::BG_RAISED(), colors::TEXT_SECONDARY(), colors::AMBER()));
    layout()->addWidget(table_);
}

void FuturesExpiryPanel::set_asset_class(const QString& cls) {
    active_class_ = cls;
    rebuild();
}

void FuturesExpiryPanel::refresh() {
    rebuild();
}

void FuturesExpiryPanel::rebuild() {
    table_->setSortingEnabled(false);
    table_->setRowCount(0);
    if (active_class_.isEmpty()) return;

    const QDate today = QDate::currentDate();
    const auto& all = all_contracts();

    QVector<ContractDef> in_class;
    for (const auto& c : all)
        if (c.asset_class == active_class_) in_class.append(c);

    table_->setRowCount(in_class.size());
    int row = 0;
    for (const auto& c : in_class) {
        const QDate exp = next_expiry(c, today);
        const long long dte = exp.isValid() ? static_cast<long long>(today.daysTo(exp)) : -1;

        auto* sym = new QTableWidgetItem(c.symbol);
        sym->setForeground(QColor(colors::CYAN()));
        table_->setItem(row, 0, sym);

        table_->setItem(row, 1, new QTableWidgetItem(c.name));

        auto* exp_item = new QTableWidgetItem;
        if (exp.isValid()) {
            exp_item->setData(Qt::EditRole, exp);
            exp_item->setData(Qt::DisplayRole, exp.toString("yyyy-MM-dd"));
        } else {
            exp_item->setData(Qt::DisplayRole, QStringLiteral("—"));
        }
        exp_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 2, exp_item);

        auto* dte_item = new QTableWidgetItem;
        if (dte >= 0) {
            dte_item->setData(Qt::EditRole, dte);
            dte_item->setData(Qt::DisplayRole, QString::number(dte) + "d");
            if (dte < 7)       dte_item->setForeground(QColor(colors::NEGATIVE()));
            else if (dte < 30) dte_item->setForeground(QColor(colors::AMBER()));
            else               dte_item->setForeground(QColor(colors::TEXT_PRIMARY()));
        } else {
            dte_item->setData(Qt::DisplayRole, QStringLiteral("—"));
        }
        dte_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 3, dte_item);

        auto* mg = new QTableWidgetItem;
        mg->setData(Qt::EditRole, c.initial_margin_usd);
        mg->setData(Qt::DisplayRole,
                    c.initial_margin_usd > 0
                        ? QString("$%L1").arg(static_cast<long long>(c.initial_margin_usd))
                        : QStringLiteral("—"));
        mg->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 4, mg);

        ++row;
    }
    table_->setSortingEnabled(true);
    table_->sortByColumn(3, Qt::AscendingOrder);  // soonest expiries first
    set_status("computed locally");
}

// ─────────────────────────────────────────────────────────────────────────────
//  FuturesCotPanel — CFTC Commitments of Traders positioning, symbol-driven
// ─────────────────────────────────────────────────────────────────────────────

namespace {
// Map our futures-catalog root symbols to CFTC market identifiers that
// scripts/cftc_data.py understands. CFTC publishes COT for the full CME
// product list every Friday; only a subset overlaps with our 36-contract
// catalog. Roots not in this map render a "no COT mapping" placeholder
// rather than firing a wasted CFTC fetch.
static const QHash<QString, QString> kCotIdentifier = {
    // Index
    {"ES", "sp500"}, {"NQ", "nasdaq"},
    // Rates
    {"ZB", "treasury_bonds"}, {"ZN", "treasury_notes_10y"},
    {"ZF", "treasury_notes_5y"}, {"ZT", "treasury_notes_2y"},
    {"ZQ", "fed_funds"},
    // Energy
    {"CL", "crude_oil"}, {"BZ", "brent"}, {"NG", "natural_gas"},
    {"RB", "gasoline"}, {"HO", "heating_oil"},
    // Metals
    {"GC", "gold"}, {"SI", "silver"}, {"HG", "copper"},
    {"PL", "platinum"}, {"PA", "palladium"},
    // Ags
    {"ZC", "corn"}, {"ZW", "wheat"}, {"ZS", "soybeans"},
    {"KC", "coffee"}, {"SB", "sugar"}, {"CC", "cocoa"}, {"CT", "cotton"},
    // FX
    {"6E", "euro"}, {"6J", "jpy"}, {"6B", "british_pound"},
    {"6A", "australian_dollar"}, {"6C", "canadian_dollar"},
    // Crypto
    {"BTC", "bitcoin"}, {"MET", "ether"},
};
} // namespace

QString FuturesCotPanel::cftc_identifier_for(const QString& symbol) {
    return kCotIdentifier.value(symbol);
}

FuturesCotPanel::FuturesCotPanel(QWidget* parent)
    : FuturesPanelBase("COT POSITIONING", parent) {
    // ── Top row: symbol picker + sentiment badge + report date ───────────────
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 2, 0, 2);
    row->setSpacing(8);

    auto* lbl = new QLabel("Symbol");
    lbl->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
    row->addWidget(lbl);

    symbol_combo_ = new QComboBox;
    symbol_combo_->setMinimumWidth(120);
    row->addWidget(symbol_combo_);

    sentiment_lbl_ = new QLabel;
    sentiment_lbl_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), true, fonts::font_px(-2)));
    row->addWidget(sentiment_lbl_);

    row->addStretch();

    date_lbl_ = new QLabel;
    date_lbl_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
    row->addWidget(date_lbl_);
    layout()->addItem(row);

    // ── Stack: table | placeholder ───────────────────────────────────────────
    auto* stack_host = new QWidget(this);
    auto* stack = new QStackedLayout(stack_host);
    stack->setContentsMargins(0, 0, 0, 0);

    table_ = new QTableWidget(stack_host);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"Category", "Long", "Short", "Net", "ΔWoW"});
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->verticalHeader()->setVisible(false);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setMinimumHeight(110);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 5; ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(1, 90);
    table_->horizontalHeader()->resizeSection(2, 90);
    table_->horizontalHeader()->resizeSection(3, 100);
    table_->horizontalHeader()->resizeSection(4, 100);
    table_->setStyleSheet(
        QString("QTableWidget{background:%1;color:%2;border:none;"
                "  font-family:'%3';font-size:%4px;gridline-color:transparent;}"
                "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %5;}")
            .arg(colors::BG_BASE(), colors::TEXT_PRIMARY(),
                 fonts::DATA_FAMILY())
            .arg(fonts::font_px(-2))
            .arg(colors::BORDER_DIM()));
    table_->horizontalHeader()->setStyleSheet(
        QString("QHeaderView::section{background:%1;color:%2;border:none;"
                "  border-bottom:1px solid %3;padding:4px 8px;font-weight:700;}")
            .arg(colors::BG_RAISED(), colors::TEXT_SECONDARY(), colors::AMBER()));
    stack->addWidget(table_);

    placeholder_ = new QLabel(stack_host);
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    placeholder_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-1)));
    stack->addWidget(placeholder_);

    layout()->addWidget(stack_host);

    connect(symbol_combo_, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (s.isEmpty()) return;
        active_symbol_ = s;
        refresh();
    });
}

void FuturesCotPanel::set_asset_class(const QString& cls) {
    active_class_ = cls;
    QStringList syms = symbols_for_class(cls);
    // Only show symbols that have a CFTC mapping; the rest would just render
    // a "no COT mapping" placeholder.
    QStringList covered;
    for (const auto& s : syms)
        if (kCotIdentifier.contains(s)) covered.append(s);

    symbol_combo_->blockSignals(true);
    symbol_combo_->clear();
    symbol_combo_->addItems(covered);
    symbol_combo_->blockSignals(false);

    if (!covered.isEmpty()) {
        active_symbol_ = covered.first();
        symbol_combo_->setCurrentText(active_symbol_);
        fetch_in_flight_ = false;
        last_failed_ms_ = 0;
        refresh();
    } else {
        active_symbol_.clear();
        show_placeholder(QString("COT positioning isn't published by CFTC for %1 contracts.").arg(cls));
    }
}

void FuturesCotPanel::set_symbol(const QString& sym) {
    if (sym.isEmpty()) return;
    active_symbol_ = sym;
    if (symbol_combo_) {
        const int idx = symbol_combo_->findText(sym);
        if (idx >= 0) {
            symbol_combo_->blockSignals(true);
            symbol_combo_->setCurrentIndex(idx);
            symbol_combo_->blockSignals(false);
        }
    }
    fetch_in_flight_ = false;
    last_failed_ms_ = 0;
    refresh();
}

void FuturesCotPanel::show_placeholder(const QString& message) {
    if (auto* stack = qobject_cast<QStackedLayout*>(placeholder_->parentWidget()->layout()))
        stack->setCurrentIndex(1);
    placeholder_->setText(message);
    sentiment_lbl_->setText("");
    date_lbl_->setText("");
}

void FuturesCotPanel::refresh() {
    const QString sym = active_symbol_;
    if (sym.isEmpty()) return;
    const QString ident = cftc_identifier_for(sym);
    if (ident.isEmpty()) {
        show_placeholder(QString("No CFTC mapping for %1.").arg(sym));
        return;
    }
    if (fetch_in_flight_) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (last_failed_ms_ > 0 && (now - last_failed_ms_) < kFailureBackoffMs) return;
    fetch_in_flight_ = true;
    set_status("loading…");
    show_placeholder(QString("Loading CFTC COT for %1…").arg(sym));

    const quint64 my_gen = bump_gen();
    QPointer<FuturesCotPanel> self(this);
    // cftc_data.py takes positional args:
    //   cot_data <identifier> <report_type> <futures_only> <start> <end> <limit>
    // Limit 4 = last 4 weekly reports — enough for WoW diff plus context.
    python::PythonRunner::instance().run(
        QStringLiteral("cftc_data.py"),
        {QStringLiteral("cot_data"), ident,
         QStringLiteral("legacy"), QStringLiteral("false"),
         QStringLiteral(""), QStringLiteral(""), QStringLiteral("4")},
        [self, my_gen, sym](python::PythonResult result) {
            if (!self) return;
            self->fetch_in_flight_ = false;
            if (!self->is_current(my_gen)) return;
            if (!result.success || result.output.trimmed().isEmpty()) {
                self->last_failed_ms_ = QDateTime::currentMSecsSinceEpoch();
                self->set_status("error", colors::NEGATIVE());
                self->show_placeholder(QString("CFTC COT unavailable for %1.\n\n"
                                               "Source: publicreporting.cftc.gov (Socrata).\n"
                                               "Check network access; no API key required.").arg(sym));
                return;
            }
            const QString js = python::extract_json(result.output);
            const auto doc = QJsonDocument::fromJson(js.toUtf8());
            if (!doc.isObject() || !doc.object().value("success").toBool()) {
                self->last_failed_ms_ = QDateTime::currentMSecsSinceEpoch();
                self->set_status("error", colors::NEGATIVE());
                self->show_placeholder(QString("CFTC returned no data for %1.").arg(sym));
                return;
            }
            const auto arr = doc.object().value("data").toArray();
            if (arr.isEmpty()) {
                self->show_placeholder(QString("No recent COT reports for %1.").arg(sym));
                return;
            }
            self->set_status(QString("CFTC · %1").arg(sym));
            self->render(arr);
        },
        /*stream*/ {}, /*timeout*/ 30'000);
}

void FuturesCotPanel::render(const QJsonArray& rows) {
    if (auto* stack = qobject_cast<QStackedLayout*>(placeholder_->parentWidget()->layout()))
        stack->setCurrentIndex(0);

    // Most recent first per cftc_data.py.
    const auto latest = rows.first().toObject();
    const auto prior  = rows.size() > 1 ? rows.at(1).toObject() : QJsonObject{};

    auto i = [](const QJsonObject& o, const char* key) -> long long {
        const auto v = o.value(QLatin1String(key));
        if (v.isDouble()) return static_cast<long long>(v.toDouble());
        return v.toString().toLongLong();
    };

    const long long oi          = i(latest, "open_interest_all");
    const long long comm_long   = i(latest, "comm_long_all");
    const long long comm_short  = i(latest, "comm_short_all");
    const long long nc_long     = i(latest, "noncomm_long_all");
    const long long nc_short    = i(latest, "noncomm_short_all");
    const long long nr_long     = i(latest, "nonreportable_long_all");
    const long long nr_short    = i(latest, "nonreportable_short_all");
    const long long p_comm_long  = i(prior, "comm_long_all");
    const long long p_comm_short = i(prior, "comm_short_all");
    const long long p_nc_long    = i(prior, "noncomm_long_all");
    const long long p_nc_short   = i(prior, "noncomm_short_all");
    const long long p_nr_long    = i(prior, "nonreportable_long_all");
    const long long p_nr_short   = i(prior, "nonreportable_short_all");

    auto delta = [](long long now_long, long long now_short, long long was_long, long long was_short) {
        return (now_long - now_short) - (was_long - was_short);
    };

    struct Row { QString name; long long lng, sht, net, wow; };
    const QVector<Row> rs = {
        {"Commercials (hedgers)",        comm_long, comm_short, comm_long - comm_short,
            delta(comm_long, comm_short, p_comm_long, p_comm_short)},
        {"Large Spec (managed money)",   nc_long,   nc_short,   nc_long - nc_short,
            delta(nc_long, nc_short, p_nc_long, p_nc_short)},
        {"Small Spec (non-reportable)",  nr_long,   nr_short,   nr_long - nr_short,
            delta(nr_long, nr_short, p_nr_long, p_nr_short)},
    };

    table_->setRowCount(rs.size());
    auto fmt_signed = [](long long v) {
        const QString prefix = v > 0 ? "+" : "";
        return prefix + QString("%L1").arg(v);
    };
    for (int row = 0; row < rs.size(); ++row) {
        const auto& r = rs[row];
        auto* name = new QTableWidgetItem(r.name);
        name->setForeground(QColor(colors::TEXT_PRIMARY()));
        table_->setItem(row, 0, name);

        auto* lng = new QTableWidgetItem(QString("%L1").arg(r.lng));
        lng->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 1, lng);

        auto* sht = new QTableWidgetItem(QString("%L1").arg(r.sht));
        sht->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 2, sht);

        auto* net = new QTableWidgetItem(fmt_signed(r.net));
        net->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        net->setForeground(QColor(r.net >= 0 ? colors::POSITIVE() : colors::NEGATIVE()));
        table_->setItem(row, 3, net);

        auto* wow = new QTableWidgetItem(fmt_signed(r.wow));
        wow->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        wow->setForeground(QColor(r.wow >= 0 ? colors::POSITIVE() : colors::NEGATIVE()));
        table_->setItem(row, 4, wow);
    }

    // Sentiment: large-spec net long as % of OI is the classic "managed
    // money is X% long" gauge. >+15% is extreme long, <-15% extreme short.
    const long long large_net = nc_long - nc_short;
    const double pct = oi > 0 ? static_cast<double>(large_net) / oi * 100.0 : 0.0;
    QString label;
    const char* color = colors::TEXT_SECONDARY();
    if (oi <= 0) {
        label = "—";
    } else if (pct >= 25) { label = "EXTREME LONG"; color = colors::POSITIVE(); }
    else if (pct >= 10)   { label = "NET LONG";     color = colors::POSITIVE(); }
    else if (pct <= -25)  { label = "EXTREME SHORT"; color = colors::NEGATIVE(); }
    else if (pct <= -10)  { label = "NET SHORT";    color = colors::NEGATIVE(); }
    else                  { label = "NEUTRAL";      color = colors::TEXT_SECONDARY(); }
    sentiment_lbl_->setText(QString("Mgr Money: %1  (%2%3% of OI)")
                                .arg(label)
                                .arg(pct >= 0 ? "+" : "")
                                .arg(QString::number(pct, 'f', 1)));
    sentiment_lbl_->setStyleSheet(lbl_ss(color, true, fonts::font_px(-2)));

    const QString rep_date = latest.value(QStringLiteral("report_date_as_yyyy_mm_dd")).toString();
    date_lbl_->setText(rep_date.isEmpty() ? "" : "Report: " + rep_date.left(10));
}

} // namespace fincept::screens::futures

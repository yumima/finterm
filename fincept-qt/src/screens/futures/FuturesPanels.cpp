#include "screens/futures/FuturesPanels.h"

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
#include <QJsonObject>
#include <QLineSeries>
#include <QPainter>
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
    status_label_->setStyleSheet(lbl_ss(colors::TEXT_DIM(), false, fonts::font_px(-3)));
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
        status_label_->setStyleSheet(lbl_ss(colors::TEXT_DIM(), false, fonts::font_px(-3)));
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
    contango_label_->setStyleSheet(lbl_ss(colors::TEXT_DIM(), true, fonts::font_px(-2)));
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
    placeholder_->setStyleSheet(lbl_ss(colors::TEXT_DIM(), false, fonts::font_px(-1)));
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
    set_status("loading…");
    const quint64 my_gen = bump_gen();
    FuturesDataService::instance().fetch_term_structure(
        sym, 8, [this, my_gen, sym](bool ok, QVector<TermStructurePoint> pts, QString source) {
            if (!is_current(my_gen)) return;          // stale — drop
            if (!ok) { set_status("error", colors::NEGATIVE()); return; }
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
    x_axis->setLabelsBrush(QColor(colors::TEXT_DIM()));

    auto* y_axis = new QValueAxis;
    if (std::isfinite(min_v) && std::isfinite(max_v) && max_v > min_v) {
        const double pad = (max_v - min_v) * 0.05;
        y_axis->setRange(min_v - pad, max_v + pad);
    }
    y_axis->setLabelFormat("%.2f");
    y_axis->setGridLineColor(QColor(colors::BORDER_DIM()));
    y_axis->setLabelsBrush(QColor(colors::TEXT_DIM()));

    chart->addAxis(x_axis, Qt::AlignBottom);
    chart->addAxis(y_axis, Qt::AlignLeft);
    series->attachAxis(x_axis);
    series->attachAxis(y_axis);

    if (std::isfinite(pts.first().last) && std::isfinite(pts.last().last)) {
        const bool contango = pts.last().last > pts.first().last;
        contango_label_->setText(contango ? "CONTANGO" : "BACKWARDATION");
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
    dash->setStyleSheet(lbl_ss(colors::TEXT_DIM(), true));
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
    placeholder_->setStyleSheet(lbl_ss(colors::TEXT_DIM(), false, fonts::font_px(-1)));
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
    set_status("loading…");
    const quint64 my_gen = bump_gen();
    FuturesDataService::instance().fetch_term_structure(
        sym, 12, [this, my_gen, sym](bool ok, QVector<TermStructurePoint> pts, QString source) {
            if (!is_current(my_gen)) return;
            if (!ok) { set_status("error", colors::NEGATIVE()); return; }
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
    x_axis->setLabelsBrush(QColor(colors::TEXT_DIM()));
    x_axis->setGridLineColor(QColor(colors::BORDER_DIM()));

    auto* y_axis = new QValueAxis;
    if (std::isfinite(min_v) && std::isfinite(max_v) && max_v > min_v) {
        const double pad = (max_v - min_v) * 0.05;
        y_axis->setRange(min_v - pad, max_v + pad);
    }
    y_axis->setLabelFormat("%.2f");
    y_axis->setLabelsBrush(QColor(colors::TEXT_DIM()));
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

} // namespace fincept::screens::futures

#include "screens/pre_ipo/IpoWatchView.h"

#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "services/markets/MarketDataService.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/charts/ChartFactory.h"
#include "ui/theme/Theme.h"

#include <QtCharts/QChartView>
#include <QTabWidget>

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens::widgets {

namespace {
constexpr int kMonthsBack    = 3;   // pricedDate window for KPIs / RECENTLY PRICED
constexpr int kMonthsForward = 12;  // forward window: pulls This Week / This Month / 6 / 12 mo

// Time-window threshold helpers (days).
constexpr qint64 kDaysWeek    = 7;
constexpr qint64 kDays30      = 30;
constexpr qint64 kDays6Months = 30 * 6;
constexpr qint64 kDays12Mo    = 30 * 12;

// Small layout constants — chosen for density on 1920+ widths but graceful on 1440.
constexpr int kTitleHeight   = 38;
constexpr int kKpiHeight     = 44;
constexpr int kFilterHeight  = 36;
constexpr int kKpiCellMinW   = 130;

/// Table item whose `operator<` compares a numeric sort key stored in UserRole+1
/// instead of the display string. Used for the DATE column so QTableWidget's
/// click-to-sort puts "Apr 1" before "May 14" instead of doing a lexical sort
/// that wedges "Apr 7" between "Apr 30" and "Feb 10".
class DateSortItem : public QTableWidgetItem {
  public:
    DateSortItem(const QString& display, qint64 sort_key)
        : QTableWidgetItem(display) {
        setData(Qt::UserRole + 1, sort_key);
    }
    bool operator<(const QTableWidgetItem& other) const override {
        return data(Qt::UserRole + 1).toLongLong() <
               other.data(Qt::UserRole + 1).toLongLong();
    }
};

/// Same trick for numeric columns (POP %, DEAL $, OFFER, LAST) so sorting by
/// click-on-header gives a real numeric ordering instead of "$1.02B" < "$100M"
/// lexical compare.
class NumSortItem : public QTableWidgetItem {
  public:
    NumSortItem(const QString& display, double sort_key)
        : QTableWidgetItem(display) {
        setData(Qt::UserRole + 1, sort_key);
    }
    bool operator<(const QTableWidgetItem& other) const override {
        return data(Qt::UserRole + 1).toDouble() <
               other.data(Qt::UserRole + 1).toDouble();
    }
};

/// Helper: pick the sort key for a date — invalid dates sort to the end on
/// ascending sort (so "TBD" rows fall after every real date instead of
/// inheriting julian day 0 and floating to the top).
qint64 date_sort_key(const QDate& d) {
    return d.isValid() ? d.toJulianDay() : 0x7fffffffffffLL;
}
} // namespace

const char* IpoWatchView::lens_label(Lens l) {
    switch (l) {
        case LensCalendar:    return "CALENDAR";
        case LensPerformance: return "PERFORMANCE";
        case LensWatchlist:   return "WATCHLIST";
        default:              return "";
    }
}

// ── Construction ─────────────────────────────────────────────────────────────

IpoWatchView::IpoWatchView(QWidget* parent) : QWidget(parent) {
    load_starred();
    build_ui();
    apply_styles();
    refresh();

    // Wake-on-resume — when the user returns to the app after a long idle
    // (overnight, long lunch), our per-Entry perf_fetched/profile_fetched
    // flags plus info_cache_/history_cache_ would otherwise serve up
    // yesterday's prices indefinitely. main.cpp's global hook clears the
    // CacheManager "market:" prefix but doesn't reach into this screen's
    // private state; do the equivalent here on the same signal.
    connect(qApp, &QApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                static qint64 s_last_inactive_ms = 0;
                constexpr qint64 kWakeThresholdMs = 5LL * 60LL * 1000LL;
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (state == Qt::ApplicationInactive || state == Qt::ApplicationSuspended) {
                    s_last_inactive_ms = now;
                    return;
                }
                if (state != Qt::ApplicationActive) return;
                if (s_last_inactive_ms == 0) return;
                if (now - s_last_inactive_ms < kWakeThresholdMs) return;
                s_last_inactive_ms = 0;
                // Drop per-symbol caches so the next enrichment pass re-hits
                // the daemon. entries_.clear() inside refresh() resets the
                // perf_fetched/profile_fetched flags.
                info_cache_.clear();
                history_cache_.clear();
                history_inflight_.clear();
                ipo_extras_.clear();
                ipo_extras_inflight_.clear();
                filings_cache_.clear();
                filings_inflight_.clear();
                wiki_cache_.clear();
                wiki_inflight_.clear();
                wiki_misses_.clear();
                refresh();
            });
}

void IpoWatchView::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    build_title_bar(root);
    build_kpi_strip(root);
    build_filter_row(root);
    build_workspace(root);
}

void IpoWatchView::build_title_bar(QVBoxLayout* root) {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(kTitleHeight);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(0);

    title_lbl_ = new QLabel("IPO WATCH");
    h->addWidget(title_lbl_);

    h->addSpacing(20);

    // Lens tabs — top-right "tab" feel like Bloomberg's function selector.
    for (int i = 0; i < LensCount; ++i) {
        auto* b = new QPushButton(lens_label(static_cast<Lens>(i)));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(28);
        b->setProperty("lensIdx", i);
        connect(b, &QPushButton::clicked, this, [this, i]() {
            active_lens_ = static_cast<Lens>(i);
            for (int j = 0; j < LensCount; ++j)
                if (lens_btns_[j]) lens_btns_[j]->setChecked(j == i);
            // Belt-and-braces: in case the initial pre-fetch failed (network
            // hiccup), re-attempt on PERFORMANCE open. enrich is idempotent.
            if (active_lens_ == LensPerformance) enrich_priced_with_quotes();
            render();
        });
        lens_btns_[i] = b;
        h->addWidget(b);
    }
    lens_btns_[LensCalendar]->setChecked(true);

    h->addStretch();

    status_lbl_ = new QLabel("Loading…");
    h->addWidget(status_lbl_);

    refresh_btn_ = new QPushButton(QStringLiteral("↻"));
    refresh_btn_->setFixedSize(28, 24);
    refresh_btn_->setCursor(Qt::PointingHandCursor);
    refresh_btn_->setToolTip("Re-fetch the Nasdaq IPO calendar");
    connect(refresh_btn_, &QPushButton::clicked, this, &IpoWatchView::refresh);
    h->addWidget(refresh_btn_);

    root->addWidget(bar);
}

void IpoWatchView::build_kpi_strip(QVBoxLayout* root) {
    kpi_strip_ = new QWidget(this);
    kpi_strip_->setFixedHeight(kKpiHeight);
    auto* h = new QHBoxLayout(kpi_strip_);
    h->setContentsMargins(12, 4, 12, 4);
    h->setSpacing(0);

    // Each cell is a QWidget that doubles as a click target. Cells with a
    // valid "kpiTw" property switch time_window_ to that TimeWindow on click,
    // syncing with the window_chip_ — so clicking "THIS WEEK" instantly
    // narrows the table to that range.
    auto add_cell = [&](QLabel*& target, const QString& seed, int tw_or_minus1) {
        auto* w = new QWidget;
        w->setMinimumWidth(kKpiCellMinW);
        if (tw_or_minus1 >= 0) {
            w->setCursor(Qt::PointingHandCursor);
            w->setProperty("kpiTw", tw_or_minus1);
            w->installEventFilter(this);
        }
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(8, 2, 8, 2);
        v->setSpacing(0);
        target = new QLabel(seed);
        target->setProperty("kpi", true);
        v->addWidget(target);
        h->addWidget(w);
    };
    add_cell(kpi_week_,  "THIS WEEK —",   TW_ThisWeek);
    add_cell(kpi_month_, "THIS MONTH —",  TW_30Days);
    add_cell(kpi_pop_,   "30D POP —",     TW_Past30Days);
    add_cell(kpi_above_, "ABOVE —",       TW_Past30Days);
    add_cell(kpi_in_,    "IN-RANGE —",    TW_Past30Days);
    add_cell(kpi_below_, "BELOW —",       TW_Past30Days);
    h->addStretch();

    root->addWidget(kpi_strip_);
}

bool IpoWatchView::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::MouseButtonRelease) {
        const QVariant v = obj->property("kpiTw");
        if (v.isValid()) {
            const int tw_int = v.toInt();
            time_window_ = static_cast<TimeWindow>(tw_int);
            if (window_chip_) window_chip_->setCurrentIndex(tw_int);
            render();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void IpoWatchView::build_filter_row(QVBoxLayout* root) {
    filter_row_ = new QWidget(this);
    filter_row_->setFixedHeight(kFilterHeight);
    auto* h = new QHBoxLayout(filter_row_);
    h->setContentsMargins(12, 4, 12, 4);
    h->setSpacing(8);

    auto make_chip = [&](const QString& placeholder, QStringList opts) -> QComboBox* {
        auto* c = new QComboBox;
        c->setFixedHeight(24);
        c->setMinimumWidth(110);
        c->setProperty("chip", true);
        c->setToolTip(placeholder);
        c->addItems(std::move(opts));
        h->addWidget(c);
        return c;
    };
    status_chip_ = make_chip("Status",   {"ALL", "UPCOMING", "PRICED"});
    // Match status_filter_ initial value (see header) — pipeline-research
    // first; user can flip to ALL / PRICED.
    status_chip_->setCurrentText("UPCOMING");
    window_chip_ = make_chip("Window",   {"ALL UPCOMING", "THIS WEEK", "NEXT 30 DAYS",
                                          "NEXT 3 MONTHS", "NEXT 6 MONTHS", "NEXT 12 MONTHS",
                                          "PAST 30 DAYS (PRICED)"});
    exch_chip_   = make_chip("Exchange", {"ALL", "NASDAQ", "NYSE", "AMEX"});
    size_chip_   = make_chip("Deal Size",{"ALL", "< $100M", "$100-500M", "$500M+"});

    h->addSpacing(8);
    auto* lbl = new QLabel("⌕");
    lbl->setStyleSheet(QString("color:%1;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    h->addWidget(lbl);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Company, ticker, bookrunner…");
    search_->setFixedHeight(24);
    h->addWidget(search_, 1);

    connect(status_chip_, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        status_filter_ = s.toLower();
        render();
    });
    connect(window_chip_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        time_window_ = static_cast<TimeWindow>(i);
        render();
    });
    connect(exch_chip_, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        exch_filter_ = s == "ALL" ? "all" : s;
        render();
    });
    connect(size_chip_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        static const char* keys[] = {"all", "<100", "100-500", "500+"};
        size_filter_ = keys[std::min(i, 3)];
        render();
    });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& q) {
        search_query_ = q.trimmed().toLower();
        render();
    });

    root->addWidget(filter_row_);
}

void IpoWatchView::build_workspace(QVBoxLayout* root) {
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setHandleWidth(1);
    splitter_->setChildrenCollapsible(true);  // user can drag any pane to 0

    // ── Table (left column — IPO list) ──
    table_ = new QTableWidget;
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setSortingEnabled(true);
    table_->setShowGrid(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col != 0) return;
        auto* it = table_->item(row, 0);
        if (!it) return;
        const int idx = it->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= entries_.size()) return;
        toggle_star(entries_.at(idx));
    });
    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int /*col*/, int prev_row, int /*prev_col*/) {
                if (row < 0 || row == prev_row) return;
                auto* it = table_->item(row, 0);
                if (!it) return;
                const int idx = it->data(Qt::UserRole).toInt();
                if (idx >= 0 && idx < entries_.size())
                    render_detail(&entries_.at(idx));
            });
    splitter_->addWidget(table_);

    // ── Detail rail (middle + right columns) ──
    build_detail_rail(splitter_);

    // Stretch 2 : 4 : 2 → list ≈ 25%, middle ≈ 50%, right ≈ 25%. The user
    // wanted the IPO list narrower (~1/3 max) so the company research area
    // gets the bulk of the screen.
    splitter_->setStretchFactor(0, 2);
    splitter_->setStretchFactor(1, 4);
    splitter_->setStretchFactor(2, 2);
    splitter_->setSizes({420, 840, 420});

    root->addWidget(splitter_, 1);
}

void IpoWatchView::build_detail_rail(QSplitter* splitter) {
    // Helper: wrap a body QLabel in a QScrollArea so long content scrolls.
    auto make_scroll_label = [](QLabel*& out, const QString& seed = "") -> QScrollArea* {
        out = new QLabel(seed);
        out->setTextFormat(Qt::RichText);
        out->setWordWrap(true);
        out->setAlignment(Qt::AlignTop);
        out->setOpenExternalLinks(true);
        out->setContentsMargins(12, 10, 12, 10);
        auto* sc = new QScrollArea;
        sc->setWidget(out);
        sc->setWidgetResizable(true);
        sc->setFrameShape(QFrame::NoFrame);
        return sc;
    };

    // ── MIDDLE PANE: vertical splitter — facts tabs on top, charts on bottom
    auto* middle_split = new QSplitter(Qt::Vertical);
    middle_split->setHandleWidth(1);
    middle_split->setChildrenCollapsible(false);
    middle_pane_ = middle_split;

    // Company header above the facts tabs (always visible — anchors the view
    // to the currently selected company).
    auto* middle_top_container = new QWidget;
    auto* mtv = new QVBoxLayout(middle_top_container);
    mtv->setContentsMargins(0, 0, 0, 0);
    mtv->setSpacing(0);
    header_lbl_ = new QLabel("<i>Select a deal</i>");
    header_lbl_->setTextFormat(Qt::RichText);
    header_lbl_->setWordWrap(true);
    header_lbl_->setContentsMargins(12, 10, 12, 6);
    mtv->addWidget(header_lbl_);

    tabs_facts_ = new QTabWidget;
    tabs_facts_->setDocumentMode(true);
    tabs_facts_->setTabPosition(QTabWidget::North);
    tabs_facts_->addTab(make_scroll_label(page_deal_),    "DEAL");
    tabs_facts_->addTab(make_scroll_label(page_business_),"BUSINESS");
    tabs_facts_->addTab(make_scroll_label(page_leader_),  "LEADERSHIP");
    tabs_facts_->addTab(make_scroll_label(page_fund_),    "FUNDAMENTALS");
    tabs_facts_->addTab(make_scroll_label(page_news_),    "NEWS");
    tabs_facts_->addTab(make_scroll_label(page_holders_), "HOLDERS");
    tabs_facts_->addTab(make_scroll_label(page_filings_), "FILINGS");
    tabs_facts_->addTab(make_scroll_label(page_pipeline_),"PIPELINE");
    mtv->addWidget(tabs_facts_, 1);
    middle_split->addWidget(middle_top_container);

    // Charts tabs — Price chart is a real QChartView, the others are HTML.
    tabs_charts_ = new QTabWidget;
    tabs_charts_->setDocumentMode(true);
    tabs_charts_->setTabPosition(QTabWidget::North);

    page_price_chart_host_ = new QWidget;
    auto* pch = new QVBoxLayout(page_price_chart_host_);
    pch->setContentsMargins(8, 8, 8, 8);
    pch->setSpacing(0);
    // price_chart_view_ replaced lazily in rebuild_price_chart().
    auto* placeholder = new QLabel("<i style='color:#7a7a7a;'>Select a priced deal to see its price chart.</i>");
    placeholder->setTextFormat(Qt::RichText);
    placeholder->setAlignment(Qt::AlignCenter);
    pch->addWidget(placeholder, 1);
    page_revenue_chart_host_ = new QWidget;
    auto* rch = new QVBoxLayout(page_revenue_chart_host_);
    rch->setContentsMargins(8, 8, 8, 8);
    rch->setSpacing(0);
    auto* rev_placeholder = new QLabel("<i style='color:#7a7a7a;'>Quarterly revenue appears for priced deals once yfinance data lands.</i>");
    rev_placeholder->setTextFormat(Qt::RichText);
    rev_placeholder->setAlignment(Qt::AlignCenter);
    rch->addWidget(rev_placeholder, 1);

    tabs_charts_->addTab(page_price_chart_host_,          "PRICE");
    tabs_charts_->addTab(page_revenue_chart_host_,        "REVENUE");
    tabs_charts_->addTab(make_scroll_label(page_range_),  "RANGE");
    tabs_charts_->addTab(make_scroll_label(page_lockup_), "LOCK-UP");
    tabs_charts_->addTab(make_scroll_label(page_timeline_),"TIMELINE");
    middle_split->addWidget(tabs_charts_);

    middle_split->setStretchFactor(0, 3);
    middle_split->setStretchFactor(1, 2);
    splitter->addWidget(middle_split);

    // ── RIGHT PANE: vertical splitter — sector heat + comps on top, links bottom
    auto* right_split = new QSplitter(Qt::Vertical);
    right_split->setHandleWidth(1);
    right_split->setChildrenCollapsible(false);
    right_pane_ = right_split;

    right_split->addWidget(make_scroll_label(right_top_,
        "<i style='color:#7a7a7a;'>Sector heat and peer comparables.</i>"));
    right_split->addWidget(make_scroll_label(right_bottom_,
        "<i style='color:#7a7a7a;'>Research links.</i>"));
    right_split->setStretchFactor(0, 3);
    right_split->setStretchFactor(1, 2);
    splitter->addWidget(right_split);
}

void IpoWatchView::apply_styles() {
    using ui::colors::AMBER;
    using ui::colors::BG_BASE;
    using ui::colors::BG_RAISED;
    using ui::colors::BG_SURFACE;
    using ui::colors::BORDER_DIM;
    using ui::colors::TEXT_PRIMARY;
    using ui::colors::TEXT_SECONDARY;

    setStyleSheet(QString("background:%1;").arg(BG_BASE()));

    if (title_lbl_)
        title_lbl_->setStyleSheet(
            QString("color:%1;font-size:14px;font-weight:800;letter-spacing:2px;background:transparent;")
                .arg(AMBER()));

    const QString lens_qss =
        QString("QPushButton{background:transparent;color:%1;border:none;padding:0 14px;"
                "font-size:12px;font-weight:700;letter-spacing:1px;"
                "border-bottom:2px solid transparent;}"
                "QPushButton:checked{color:%2;border-bottom-color:%2;}"
                "QPushButton:hover:!checked{color:%3;}")
            .arg(TEXT_SECONDARY(), AMBER(), TEXT_PRIMARY());
    for (int i = 0; i < LensCount; ++i)
        if (lens_btns_[i]) lens_btns_[i]->setStyleSheet(lens_qss);

    if (refresh_btn_)
        refresh_btn_->setStyleSheet(
            QString("QPushButton{background:%1;color:%2;border:1px solid %2;border-radius:2px;font-weight:bold;}"
                    "QPushButton:hover{background:%2;color:%3;}")
                .arg(BG_SURFACE(), AMBER(), BG_BASE()));
    if (status_lbl_)
        status_lbl_->setStyleSheet(
            QString("color:%1;font-size:11px;background:transparent;padding-right:10px;")
                .arg(TEXT_SECONDARY()));

    if (kpi_strip_)
        kpi_strip_->setStyleSheet(
            QString("background:%1;border-bottom:1px solid %2;border-top:1px solid %2;")
                .arg(BG_SURFACE(), BORDER_DIM()));

    const QString kpi_qss =
        QString("QLabel{color:%1;font-size:11px;background:transparent;}").arg(TEXT_PRIMARY());
    for (QLabel* l : {kpi_week_, kpi_month_, kpi_pop_, kpi_above_, kpi_in_, kpi_below_})
        if (l) l->setStyleSheet(kpi_qss);

    if (filter_row_)
        filter_row_->setStyleSheet(QString("background:%1;").arg(BG_RAISED()));
    const QString chip_qss =
        QString("QComboBox{background:%1;color:%2;border:1px solid %3;border-radius:2px;padding:0 6px;}"
                "QComboBox::drop-down{border:none;width:14px;}"
                "QComboBox QAbstractItemView{background:%1;color:%2;selection-background-color:%4;}")
            .arg(BG_BASE(), TEXT_PRIMARY(), BORDER_DIM(), AMBER());
    for (QComboBox* c : {status_chip_, window_chip_, exch_chip_, size_chip_})
        if (c) c->setStyleSheet(chip_qss);
    if (search_)
        search_->setStyleSheet(
            QString("QLineEdit{background:%1;color:%2;border:1px solid %3;border-radius:2px;padding:2px 6px;}"
                    "QLineEdit:focus{border-color:%4;}")
                .arg(BG_BASE(), TEXT_PRIMARY(), BORDER_DIM(), AMBER()));

    if (table_)
        table_->setStyleSheet(
            QString("QTableWidget{background:%1;color:%2;border:0;gridline-color:%3;"
                    "alternate-background-color:%4;}"
                    "QHeaderView::section{background:%5;color:%6;border:0;padding:4px 8px;"
                    "font-weight:bold;font-size:10px;}"
                    "QTableWidget::item:selected{background:%7;color:%8;}")
                .arg(BG_BASE(), TEXT_PRIMARY(), BORDER_DIM(), BG_RAISED(), BG_RAISED(),
                     TEXT_SECONDARY(), AMBER(), BG_BASE()));
    // Detail rail — middle + right panes share the surface background. The
    // table on the left uses BG_BASE so the splitter handles read as faint
    // 1px dividers; the panes step up to BG_SURFACE so the dark/lighter
    // pairing reads as "list vs research" at a glance.
    const QString pane_qss =
        QString("background:%1;border-left:1px solid %2;").arg(BG_SURFACE(), BORDER_DIM());
    if (middle_pane_) middle_pane_->setStyleSheet(pane_qss);
    if (right_pane_)  right_pane_->setStyleSheet(pane_qss);
    if (header_lbl_)
        header_lbl_->setStyleSheet(
            QString("color:%1;background:%2;border-bottom:1px solid %3;padding-bottom:6px;")
                .arg(TEXT_PRIMARY(), BG_SURFACE(), BORDER_DIM()));

    const QString tabs_qss =
        QString("QTabWidget::pane{background:%1;border:none;border-top:1px solid %2;}"
                "QTabBar::tab{background:transparent;color:%3;padding:4px 12px;"
                "  border:none;border-bottom:2px solid transparent;"
                "  font-size:11px;font-weight:600;letter-spacing:1px;}"
                "QTabBar::tab:selected{color:%4;border-bottom-color:%4;}"
                "QTabBar::tab:hover:!selected{color:%5;}"
                "QTabBar::tab:disabled{color:#444;}")
            .arg(BG_SURFACE(), BORDER_DIM(), TEXT_SECONDARY(), AMBER(), TEXT_PRIMARY());
    if (tabs_facts_)  tabs_facts_->setStyleSheet(tabs_qss);
    if (tabs_charts_) tabs_charts_->setStyleSheet(tabs_qss);

    const QString body_qss =
        QString("QLabel{color:%1;background:%2;}").arg(TEXT_PRIMARY(), BG_SURFACE());
    for (QLabel* lbl : {page_deal_, page_business_, page_leader_, page_fund_, page_pipeline_,
                        page_news_, page_holders_, page_filings_,
                        page_range_, page_lockup_, page_timeline_,
                        right_top_, right_bottom_}) {
        if (lbl) lbl->setStyleSheet(body_qss);
    }
}

// ── Networking ───────────────────────────────────────────────────────────────

void IpoWatchView::refresh() {
    if (pending_fetches_ > 0) return;
    entries_.clear();
    if (status_lbl_) status_lbl_->setText("Loading…");

    const QDate today = QDate::currentDate();
    pending_fetches_ = kMonthsBack + kMonthsForward + 1;
    for (int m = -kMonthsBack; m <= kMonthsForward; ++m)
        fetch_month(today.addMonths(m).toString("yyyy-MM"));
}

void IpoWatchView::fetch_month(const QString& yyyymm) {
    const QString url = QString("https://api.nasdaq.com/api/ipo/calendar?date=%1").arg(yyyymm);
    QPointer<IpoWatchView> self = this;
    HttpClient::instance().get(url, [self, yyyymm](Result<QJsonDocument> res) {
        if (!self) return;
        if (res.is_ok() && res.value().isObject()) {
            const auto root = res.value().object();
            const auto data = root["data"].toObject();
            auto parse_date = [](const QString& s) -> QDate {
                QDate d = QDate::fromString(s, "M/d/yyyy");
                if (!d.isValid()) d = QDate::fromString(s, "MM/dd/yyyy");
                if (!d.isValid()) d = QDate::fromString(s.left(10), "yyyy/MM/dd");
                return d;
            };
            auto parse_bookrunners = [](const QString& raw) -> QStringList {
                // Nasdaq returns "JP Morgan, Goldman Sachs, Morgan Stanley" — split + trim.
                QStringList out;
                for (const auto& s : raw.split(',', Qt::SkipEmptyParts))
                    out.append(s.trimmed());
                return out;
            };

            const auto upcoming_rows =
                data["upcoming"].toObject()["upcomingTable"].toObject()["rows"].toArray();
            for (const auto& v : upcoming_rows) {
                const auto e = v.toObject();
                Entry x;
                x.company    = e["companyName"].toString();
                x.ticker     = e["proposedTickerSymbol"].toString();
                x.exchange   = e["proposedExchange"].toString();
                x.date_raw   = e["expectedPriceDate"].toString();
                x.date       = parse_date(x.date_raw);
                x.price_range = e["proposedSharePrice"].toString();
                x.shares_raw = e["sharesOffered"].toString();
                x.shares     = parse_shares(x.shares_raw);
                x.deal_size  = format_deal_size(x.price_range, x.shares_raw);
                bool ok_mid = false;
                const double mid = parse_price_mid(x.price_range, &ok_mid);
                x.deal_size_dollars = (ok_mid && x.shares > 0) ? mid * x.shares : 0.0;
                x.all_bookrunners = parse_bookrunners(e["leadFirmName"].toString());
                x.bookrunner      = x.all_bookrunners.value(0);
                x.status     = "upcoming";
                if (!x.company.isEmpty()) self->entries_.append(std::move(x));
            }
            // ── FILED ───────────────────────────────────────────────────────
            // S-1 filings — the leading pipeline. Nasdaq's `upcoming` bucket
            // only covers the next ~2 weeks (companies need to file the
            // definitive S-1 and announce a price window first), so the real
            // forward pipeline for 1–6 months out lives here. Fields are
            // sparse: company name, proposed ticker, filed date and offer
            // amount — no exchange, no price range, no share count.
            const auto filed_rows = data["filed"].toObject()["rows"].toArray();
            for (const auto& v : filed_rows) {
                const auto e = v.toObject();
                Entry x;
                x.company    = e["companyName"].toString();
                x.ticker     = e["proposedTickerSymbol"].toString();
                x.date_raw   = e["filedDate"].toString();
                x.date       = parse_date(x.date_raw);
                x.filed_date = x.date;
                // dollarValueOfSharesOffered comes pre-formatted like
                // "$230,000,000" — strip commas/$ and store dollars.
                const QString dollar_raw = e["dollarValueOfSharesOffered"].toString();
                QString d_clean = dollar_raw; d_clean.remove(',').remove('$').remove(' ');
                bool ok_d = false;
                const double dv = d_clean.toDouble(&ok_d);
                if (ok_d) {
                    x.deal_size_dollars = dv;
                    x.deal_size         = format_money(dv);
                }
                x.status     = "filed";
                if (!x.company.isEmpty()) self->entries_.append(std::move(x));
            }

            const auto priced_rows = data["priced"].toObject()["rows"].toArray();
            for (const auto& v : priced_rows) {
                const auto e = v.toObject();
                Entry x;
                x.company    = e["companyName"].toString();
                x.ticker     = e["proposedTickerSymbol"].toString();
                x.exchange   = e["proposedExchange"].toString();
                x.date_raw   = e["pricedDate"].toString();
                x.date       = parse_date(x.date_raw);
                x.price_range = e["proposedSharePrice"].toString();
                x.final_price_raw = x.price_range; // priced API returns the final price here
                bool ok_fp = false;
                x.final_price = parse_price_mid(x.price_range, &ok_fp);
                x.shares_raw = e["sharesOffered"].toString();
                x.shares     = parse_shares(x.shares_raw);
                x.deal_size  = format_deal_size(x.price_range, x.shares_raw);
                x.deal_size_dollars = (ok_fp && x.shares > 0) ? x.final_price * x.shares : 0.0;
                x.all_bookrunners = parse_bookrunners(e["leadFirmName"].toString());
                x.bookrunner      = x.all_bookrunners.value(0);
                x.status     = "priced";
                if (!x.company.isEmpty()) self->entries_.append(std::move(x));
            }
        } else {
            LOG_WARN("IpoWatch", "Nasdaq fetch failed for " + yyyymm);
        }
        self->on_fetch_done();
    });
}

void IpoWatchView::on_fetch_done() {
    if (--pending_fetches_ > 0) return;

    // Dedup — adjacent months sometimes return the same priced rows.
    QHash<QString, int> seen;
    QVector<Entry> uniq;
    uniq.reserve(entries_.size());
    for (const auto& e : entries_) {
        const QString key = e.company + "|" + e.date_raw + "|" + e.status;
        if (seen.contains(key)) continue;
        seen.insert(key, 1);
        uniq.append(e);
    }
    entries_ = std::move(uniq);

    if (status_lbl_) {
        // Surface the data window so the user can see at a glance whether
        // Nasdaq's feed has caught up to "today's" priced + upcoming dates
        // (rare slow-update windows otherwise look like a UI bug).
        QDate latest_priced, earliest_upcoming;
        const QDate today = QDate::currentDate();
        for (const auto& e : entries_) {
            if (!e.date.isValid()) continue;
            if (e.status == "priced") {
                if (!latest_priced.isValid() || e.date > latest_priced) latest_priced = e.date;
            } else if (e.status == "upcoming" && today.daysTo(e.date) >= 0) {
                if (!earliest_upcoming.isValid() || e.date < earliest_upcoming) earliest_upcoming = e.date;
            }
        }
        QString range;
        if (latest_priced.isValid())
            range += QString("priced→%1").arg(latest_priced.toString("MMM d"));
        if (earliest_upcoming.isValid()) {
            if (!range.isEmpty()) range += " · ";
            range += QString("next %1").arg(earliest_upcoming.toString("MMM d"));
        }
        status_lbl_->setText(
            QString("%1 deals · %2%3")
                .arg(entries_.size())
                .arg(range.isEmpty() ? "" : range + " · ")
                .arg(QDateTime::currentDateTime().toString("HH:mm")));
    }

    render();

    // Pre-fetch live quotes for priced tickers so the PERFORMANCE lens and
    // KPI strip have pop % ready on first interaction. Lazy enrichment used
    // to gate this behind a lens-tab click, which left "—" in the POP cell
    // for several seconds on first open.
    enrich_priced_with_quotes();
    // Pre-fetch sector / industry / company profile for every ticker so the
    // SECTOR column populates and the detail-rail comps can prefer
    // same-sector peers over the cruder same-exchange fallback.
    enrich_with_profiles();
}

void IpoWatchView::enrich_with_profiles() {
    QStringList syms;
    for (const auto& e : entries_)
        if (!e.ticker.isEmpty() && !e.profile_fetched)
            syms.append(e.ticker);
    if (syms.isEmpty()) return;

    QPointer<IpoWatchView> self = this;
    services::MarketDataService::instance().fetch_company_profiles(
        syms, [self](bool ok, QVector<services::MarketDataService::CompanyProfile> profiles) {
            if (!self || !ok) return;
            QHash<QString, services::MarketDataService::CompanyProfile> by_sym;
            for (const auto& p : profiles) by_sym.insert(p.symbol, p);
            for (auto& e : self->entries_) {
                if (e.ticker.isEmpty() || e.profile_fetched) continue;
                auto it = by_sym.constFind(e.ticker);
                if (it == by_sym.constEnd()) continue;
                e.sector   = it.value().sector;
                e.industry = it.value().industry;
                e.website  = it.value().website;
                e.profile_fetched = true;
            }
            self->render();
        });
}

void IpoWatchView::fetch_history_for_detail(const Entry& e) {
    if (e.status != "priced" || e.ticker.isEmpty()) return;
    if (history_cache_.contains(e.ticker) || history_inflight_.contains(e.ticker))
        return;
    history_inflight_.insert(e.ticker);
    QPointer<IpoWatchView> self = this;
    const QString sym = e.ticker;
    // Pick a window long enough to span most IPOs we care about. yfinance's
    // 6mo window covers the typical research horizon for a recently priced
    // deal; for older priced rows the curve will just clip to listing date.
    services::MarketDataService::instance().fetch_history(
        sym, "6mo", "1d",
        [self, sym](bool ok, QVector<services::HistoryPoint> points) {
            if (!self) return;
            QVector<double> closes;
            if (ok) {
                closes.reserve(points.size());
                for (const auto& p : points) closes.append(p.close);
            }
            self->history_inflight_.remove(sym);
            self->history_cache_.insert(sym, closes);
            // Only re-render if this ticker is still on screen.
            if (self->detail_symbol_ == sym) {
                for (const auto& en : self->entries_) {
                    if (en.ticker == sym) {
                        self->render_detail(&en);
                        return;
                    }
                }
            }
        });
}

// ── Lazy fetchers for the new detail-rail tabs (NEWS / FINANCIALS / HOLDERS /
//    FILINGS / Wikipedia fallback) ────────────────────────────────────────────
// Each follows the same pattern as fetch_history_for_detail: gate on a
// presence + inflight set so repeat clicks don't re-issue; re-render once
// the data arrives if the same row is still selected.

void IpoWatchView::fetch_ipo_extras_for_detail(const Entry& e) {
    if (e.ticker.isEmpty()) return;
    if (ipo_extras_.contains(e.ticker) || ipo_extras_inflight_.contains(e.ticker)) return;
    ipo_extras_inflight_.insert(e.ticker);
    QPointer<IpoWatchView> self = this;
    const QString sym = e.ticker;
    services::MarketDataService::instance().fetch_ipo_extras(
        sym, [self, sym](bool ok, services::MarketDataService::IpoExtras data) {
            if (!self) return;
            self->ipo_extras_inflight_.remove(sym);
            if (!ok) return;
            self->ipo_extras_.insert(sym, data);
            if (self->detail_symbol_ == sym) {
                for (const auto& en : self->entries_) {
                    if (en.ticker == sym) { self->render_detail(&en); return; }
                }
            }
        });
}

void IpoWatchView::fetch_sec_filings_for_detail(const Entry& e) {
    if (e.ticker.isEmpty()) return;
    if (filings_cache_.contains(e.ticker) || filings_inflight_.contains(e.ticker)) return;
    filings_inflight_.insert(e.ticker);
    QPointer<IpoWatchView> self = this;
    const QString sym = e.ticker;
    services::MarketDataService::instance().fetch_sec_filings(
        sym, [self, sym](bool ok, QVector<services::MarketDataService::SecFiling> filings) {
            if (!self) return;
            self->filings_inflight_.remove(sym);
            // We insert even on failure (empty list) so we don't re-issue.
            // The "no filings yet" placeholder shows in that case.
            self->filings_cache_.insert(sym, ok ? filings : QVector<services::MarketDataService::SecFiling>{});
            if (self->detail_symbol_ == sym) {
                for (const auto& en : self->entries_) {
                    if (en.ticker == sym) { self->render_detail(&en); return; }
                }
            }
        });
}

void IpoWatchView::fetch_wikipedia_for_detail(const Entry& e) {
    // Use the company name as the Wikipedia title — there's no ticker→title
    // mapping that's reliable. False positives (e.g. matching a person, not
    // a company) are filtered by the user reading the result; this is a
    // fallback for when yfinance has no description anyway.
    if (e.company.isEmpty()) return;
    if (wiki_cache_.contains(e.company) || wiki_inflight_.contains(e.company)) return;
    if (wiki_misses_.contains(e.company)) return;
    wiki_inflight_.insert(e.company);
    QPointer<IpoWatchView> self = this;
    const QString name = e.company;
    services::MarketDataService::instance().fetch_wikipedia_summary(
        name, [self, name](bool ok, services::MarketDataService::WikipediaSummary s) {
            if (!self) return;
            self->wiki_inflight_.remove(name);
            if (!ok) { self->wiki_misses_.insert(name); return; }
            self->wiki_cache_.insert(name, s);
            // Only re-render if the currently selected row still has this
            // company name — we keyed on name (not ticker) so this differs
            // from the other fetchers.
            for (const auto& en : self->entries_) {
                if (en.company == name && self->detail_symbol_ == en.ticker) {
                    self->render_detail(&en); return;
                }
            }
        });
}

// Lazily fetch yfinance quotes for priced tickers so the PERFORMANCE lens can
// show pop %. Single batch_quotes call — cheap. Only re-fetches symbols whose
// perf_fetched is false, so subsequent lens switches are no-ops.
void IpoWatchView::enrich_priced_with_quotes() {
    QStringList syms;
    for (const auto& e : entries_)
        if (e.status == "priced" && !e.ticker.isEmpty() && !e.perf_fetched)
            syms.append(e.ticker);
    if (syms.isEmpty()) return;

    QPointer<IpoWatchView> self = this;
    services::MarketDataService::instance().fetch_quotes(
        syms, [self](bool ok, QVector<services::QuoteData> quotes) {
            if (!self || !ok) return;
            QHash<QString, double> price_map;
            for (const auto& q : quotes) price_map.insert(q.symbol, q.price);
            for (auto& e : self->entries_) {
                if (e.status != "priced" || e.ticker.isEmpty()) continue;
                auto it = price_map.constFind(e.ticker);
                if (it == price_map.constEnd()) continue;
                e.last_price = it.value();
                e.pop_pct = (e.final_price > 0)
                    ? (e.last_price - e.final_price) / e.final_price * 100.0 : 0;
                e.perf_fetched = true;
            }
            if (self->active_lens_ == LensPerformance) self->render();
        });
}

// ── Rendering ────────────────────────────────────────────────────────────────

bool IpoWatchView::entry_passes_filters(const Entry& e, bool apply_status) const {
    if (apply_status && status_filter_ != "all") {
        // "upcoming" is treated as "forward pipeline" — both the small set
        // of scheduled deals (status=="upcoming") and the larger leading
        // pipeline of S-1 filings (status=="filed") pass this filter. The
        // STATUS column distinguishes them visually.
        if (status_filter_ == "upcoming") {
            if (e.status != "upcoming" && e.status != "filed") return false;
        } else if (e.status != status_filter_) {
            return false;
        }
    }
    if (exch_filter_ != "all" && !e.exchange.contains(exch_filter_, Qt::CaseInsensitive)) return false;
    if (size_filter_ != "all") {
        const double m = e.deal_size_dollars / 1e6;
        if      (size_filter_ == "<100"    && !(m < 100))            return false;
        else if (size_filter_ == "100-500" && !(m >= 100 && m < 500)) return false;
        else if (size_filter_ == "500+"    && !(m >= 500))           return false;
    }
    // Time window semantics differ by status:
    //   upcoming → days-to-expected-price (future)
    //   filed    → days-since-filing      (past, "how fresh is the filing")
    //   priced   → days-since-pricing     (only PAST 30 DAYS narrows)
    const QDate today = QDate::currentDate();
    auto window_max_days = [&]() -> qint64 {
        switch (time_window_) {
            case TW_ThisWeek:    return kDaysWeek;
            case TW_30Days:      return kDays30;
            case TW_3Months:     return 30 * 3;
            case TW_6Months:     return kDays6Months;
            case TW_12Months:    return kDays12Mo;
            case TW_Past30Days:  return kDays30;
            case TW_AllUpcoming: default: return kDays12Mo; // generous default
        }
    };

    if (e.status == "upcoming" && e.date.isValid()) {
        const qint64 d = today.daysTo(e.date);
        // Drop stale upcoming entries — Nasdaq leaves postponed deals dated
        // indefinitely. See the dashboard widget for the narrower ±1mo fetch
        // window that hides these naturally.
        if (d < 0) return false;
        if (time_window_ == TW_Past30Days) return false;
        if (d > window_max_days()) return false;
    } else if (e.status == "filed" && e.date.isValid()) {
        // FILED entries are pipeline candidates — sort and filter by how
        // recent the filing is. Older filings frequently went stale (the
        // company quietly pulled the deal) so we apply a hard 12-month
        // cap regardless of the chip selection.
        const qint64 d = e.date.daysTo(today);
        if (d < 0) return false; // future-dated filing (shouldn't happen)
        if (time_window_ == TW_Past30Days) return false;
        if (d > std::min<qint64>(window_max_days(), kDays12Mo)) return false;
    } else if (e.status == "priced") {
        // Apply only the "past 30d" window; other windows keep priced visible.
        if (time_window_ == TW_Past30Days && e.date.isValid()) {
            const qint64 d = e.date.daysTo(today);
            if (!(d >= 0 && d <= kDays30)) return false;
        }
    }
    if (!search_query_.isEmpty()) {
        if (!e.company.toLower().contains(search_query_) &&
            !e.ticker.toLower().contains(search_query_) &&
            !e.exchange.toLower().contains(search_query_) &&
            !e.bookrunner.toLower().contains(search_query_))
            return false;
    }
    return true;
}

QVector<int> IpoWatchView::filtered_indices() const {
    QVector<int> out;
    out.reserve(entries_.size());
    for (int i = 0; i < entries_.size(); ++i)
        if (entry_passes_filters(entries_.at(i)))
            out.append(i);
    // Default order: closest UPCOMING first → most-recent FILED → most-recent
    // PRICED last. Without this, ascending date sort by display string is
    // lexical ("Apr 7" > "Apr 30" > "Feb 10"), and even with a numeric date
    // sort, past filed/priced entries would float above future upcoming ones.
    // Users can still click a column header to override.
    const QDate today = QDate::currentDate();
    auto bucket = [](const QString& s) -> int {
        if (s == "upcoming") return 0;
        if (s == "filed")    return 1;
        return 2; // priced
    };
    auto sort_key = [&](const Entry& e) -> qint64 {
        const int b = bucket(e.status);
        qint64 inner = 0;
        if (!e.date.isValid()) inner = 1LL << 30;
        else if (e.status == "upcoming") inner = today.daysTo(e.date);              // smaller = sooner
        else                             inner = e.date.daysTo(today);              // smaller = more recent
        return (qint64(b) << 40) | (inner & 0xFFFFFFFFLL);
    };
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        return sort_key(entries_.at(a)) < sort_key(entries_.at(b));
    });
    return out;
}

void IpoWatchView::render() {
    render_kpis();
    switch (active_lens_) {
        case LensCalendar:    render_calendar(); break;
        case LensPerformance: render_performance(); break;
        case LensWatchlist:   render_watchlist(); break;
        default: break;
    }
}

void IpoWatchView::render_kpis() {
    using ui::colors::AMBER;
    using ui::colors::POSITIVE;
    using ui::colors::NEGATIVE;
    using ui::colors::TEXT_SECONDARY;

    const QDate today = QDate::currentDate();
    int week_n = 0, month_n = 0;
    double week_$ = 0, month_$ = 0;
    int priced_30 = 0; double pop_sum = 0;
    int n_priced_with_pop = 0;
    // Above/in/below range — for priced entries we'd need filed range AND
    // final price. Nasdaq's priced rows only carry the final price; we don't
    // have the original range. Approximate "above-range" buckets using
    // performance-since-IPO instead: pop>0 ≈ market validated, pop<0 ≈ broke.
    int abv = 0, inr = 0, blw = 0;

    for (const auto& e : entries_) {
        if (e.status == "upcoming" && e.date.isValid()) {
            const qint64 d = today.daysTo(e.date);
            if (d >= 0 && d <= kDaysWeek)  { ++week_n;  week_$  += e.deal_size_dollars; }
            if (d >= 0 && d <= kDays30)    { ++month_n; month_$ += e.deal_size_dollars; }
        } else if (e.status == "priced" && e.date.isValid()) {
            const qint64 d = e.date.daysTo(today);
            if (d >= 0 && d <= kDays30) {
                ++priced_30;
                if (e.perf_fetched && e.final_price > 0) {
                    pop_sum += e.pop_pct; ++n_priced_with_pop;
                    if      (e.pop_pct >  2)  ++abv;
                    else if (e.pop_pct < -2) ++blw;
                    else                      ++inr;
                }
            }
        }
    }

    auto fmt = [](int n, double dollars) {
        return QString("%1 · %2").arg(n).arg(format_money(dollars));
    };
    auto pct = [](int part, int whole) -> QString {
        if (whole == 0) return "—";
        return QString::number(part * 100.0 / whole, 'f', 0) + "%";
    };

    if (kpi_week_)
        kpi_week_->setText(QString("<b>THIS WEEK</b><br>%1").arg(fmt(week_n, week_$)));
    if (kpi_month_)
        kpi_month_->setText(QString("<b>THIS MONTH</b><br>%1").arg(fmt(month_n, month_$)));
    if (kpi_pop_) {
        const QString s = (n_priced_with_pop == 0)
            ? "—"
            : QString("%1%2%").arg(pop_sum / n_priced_with_pop >= 0 ? "+" : "")
                              .arg(pop_sum / n_priced_with_pop, 0, 'f', 1);
        const QString color = (n_priced_with_pop == 0)
            ? QString(TEXT_SECONDARY)
            : (pop_sum >= 0 ? QString(POSITIVE) : QString(NEGATIVE));
        kpi_pop_->setText(QString("<b>30D AVG POP</b><br><span style='color:%1;'>%2</span>").arg(color, s));
    }
    if (kpi_above_)
        kpi_above_->setText(QString("<b>ABOVE (pop&gt;2%%)</b><br><span style='color:%1;'>%2 (%3)</span>")
                                .arg(POSITIVE).arg(abv).arg(pct(abv, n_priced_with_pop)));
    if (kpi_in_)
        kpi_in_->setText(QString("<b>NEAR-RANGE</b><br>%1 (%2)").arg(inr).arg(pct(inr, n_priced_with_pop)));
    if (kpi_below_)
        kpi_below_->setText(QString("<b>BELOW (pop&lt;-2%%)</b><br><span style='color:%1;'>%2 (%3)</span>")
                                .arg(NEGATIVE).arg(blw).arg(pct(blw, n_priced_with_pop)));
}

void IpoWatchView::render_calendar() {
    using ui::colors::AMBER;
    using ui::colors::INFO;
    using ui::colors::POSITIVE;
    using ui::colors::NEGATIVE;

    const QStringList headers{"★", "COMPANY", "TKR", "EXCH", "DATE", "STATUS",
                              "PRICE / RANGE", "SHARES", "DEAL SIZE", "SECTOR"};
    // sortingEnabled is left OFF: filtered_indices() already returns rows in
    // the natural order (upcoming-soonest → filed-recent → priced-recent).
    // Users who want a different order click the column header, which Qt
    // then enables for that interaction. We re-enable below.
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);

    const QVector<int> indices = filtered_indices();
    table_->setRowCount(indices.size());
    for (int i = 0; i < indices.size(); ++i) {
        const int gi = indices.at(i);
        const Entry& e = entries_.at(gi);
        // Column 0 — star cell. Carry the entry index in UserRole so the
        // cellClicked handler can resolve back to the Entry without searching.
        auto* star = new QTableWidgetItem(is_starred(e) ? QStringLiteral("★") : QStringLiteral("☆"));
        star->setData(Qt::UserRole, gi);
        star->setTextAlignment(Qt::AlignCenter);
        star->setForeground(QBrush(QColor(is_starred(e) ? AMBER() : "#666")));
        star->setToolTip(is_starred(e) ? "Remove from watchlist" : "Add to watchlist");
        table_->setItem(i, 0, star);
        table_->setItem(i, 1, new QTableWidgetItem(e.company));
        table_->item(i, 1)->setToolTip(e.company);
        table_->setItem(i, 2, new QTableWidgetItem(e.ticker.isEmpty() ? "—" : e.ticker));
        table_->setItem(i, 3, new QTableWidgetItem(e.exchange.isEmpty() ? "—" : e.exchange));
        table_->setItem(i, 4, new DateSortItem(
            e.date.isValid() ? e.date.toString("MMM d, yyyy") : (e.date_raw.isEmpty() ? "—" : e.date_raw),
            date_sort_key(e.date)));
        auto* st = new QTableWidgetItem(e.status.toUpper());
        // Color by status: priced=green, filed=info-cyan, upcoming=amber.
        QString status_color = AMBER();
        if      (e.status == "priced") status_color = POSITIVE();
        else if (e.status == "filed")  status_color = INFO();
        st->setForeground(QBrush(QColor(status_color)));
        table_->setItem(i, 5, st);
        table_->setItem(i, 6, new QTableWidgetItem(e.price_range.isEmpty() ? "—" : e.price_range));
        table_->setItem(i, 7, new QTableWidgetItem(e.shares_raw.isEmpty() ? "—" : e.shares_raw));
        table_->setItem(i, 8, new NumSortItem(
            e.deal_size.isEmpty() ? "—" : e.deal_size, e.deal_size_dollars));
        // SECTOR is populated lazily after the batch_info enrichment lands;
        // empty until then, "—" once we know the daemon couldn't resolve it.
        const QString sector_disp = e.sector.isEmpty()
            ? (e.profile_fetched ? QStringLiteral("—") : QStringLiteral("…"))
            : e.sector;
        table_->setItem(i, 9, new QTableWidgetItem(sector_disp));
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->setColumnWidth(0, 28);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < table_->columnCount(); ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
}

void IpoWatchView::render_performance() {
    using ui::colors::AMBER;
    using ui::colors::POSITIVE;
    using ui::colors::NEGATIVE;
    using ui::colors::TEXT_SECONDARY;

    const QStringList headers{"★", "COMPANY", "TKR", "PRICED", "OFFER", "LAST", "POP %", "DEAL $", "SECTOR"};
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);

    // Only priced entries that pass filters. The status filter is skipped —
    // the lens itself selects "priced", so applying the default UPCOMING
    // status filter on top would hide every row.
    QVector<int> rows;
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_.at(i);
        if (e.status != "priced") continue;
        if (!entry_passes_filters(e, /*apply_status=*/false)) continue;
        rows.append(i);
    }
    table_->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const int gi = rows.at(r);
        const Entry& e = entries_.at(gi);
        auto* star = new QTableWidgetItem(is_starred(e) ? QStringLiteral("★") : QStringLiteral("☆"));
        star->setData(Qt::UserRole, gi);
        star->setTextAlignment(Qt::AlignCenter);
        star->setForeground(QBrush(QColor(is_starred(e) ? AMBER() : "#666")));
        table_->setItem(r, 0, star);
        table_->setItem(r, 1, new QTableWidgetItem(e.company));
        table_->item(r, 1)->setToolTip(e.company);
        table_->setItem(r, 2, new QTableWidgetItem(e.ticker.isEmpty() ? "—" : e.ticker));
        table_->setItem(r, 3, new DateSortItem(
            e.date.isValid() ? e.date.toString("MMM d") : (e.date_raw.isEmpty() ? "—" : e.date_raw),
            date_sort_key(e.date)));
        table_->setItem(r, 4, new NumSortItem(
            e.final_price > 0 ? QString("$%1").arg(e.final_price, 0, 'f', 2) : "—",
            e.final_price));
        table_->setItem(r, 5, new NumSortItem(
            e.perf_fetched && e.last_price > 0
                ? QString("$%1").arg(e.last_price, 0, 'f', 2) : "—",
            e.last_price));
        QString pop_str;
        QColor  pop_col(TEXT_SECONDARY());
        if (e.perf_fetched && e.final_price > 0) {
            pop_str = QString("%1%2%").arg(e.pop_pct >= 0 ? "+" : "").arg(e.pop_pct, 0, 'f', 1);
            pop_col = QColor(e.pop_pct >= 0 ? POSITIVE() : NEGATIVE());
        } else {
            pop_str = "…";
        }
        auto* pop = new NumSortItem(pop_str, e.perf_fetched ? e.pop_pct : -1e9);
        pop->setForeground(QBrush(pop_col));
        table_->setItem(r, 6, pop);
        table_->setItem(r, 7, new NumSortItem(
            e.deal_size.isEmpty() ? "—" : e.deal_size, e.deal_size_dollars));
        const QString sector_disp = e.sector.isEmpty()
            ? (e.profile_fetched ? QStringLiteral("—") : QStringLiteral("…"))
            : e.sector;
        table_->setItem(r, 8, new QTableWidgetItem(sector_disp));
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->setColumnWidth(0, 28);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < table_->columnCount(); ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    // Sort by pop % desc — performance lens is specifically about return
    // ranking, so an explicit default sort here is justified.
    table_->sortItems(6, Qt::DescendingOrder);
}

void IpoWatchView::render_watchlist() {
    using ui::colors::AMBER;
    using ui::colors::INFO;
    using ui::colors::POSITIVE;
    using ui::colors::NEGATIVE;
    using ui::colors::TEXT_SECONDARY;

    // Starred deals only. We intentionally ignore the status/window/size chips
    // here — the watchlist is a curated set the user wants to track regardless
    // of what filter the other lenses use. Search and exchange still apply.
    const QStringList headers{"★", "COMPANY", "TKR", "EXCH", "DATE", "STATUS",
                              "OFFER / RANGE", "LAST", "POP %", "DEAL $", "SECTOR"};
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);

    QVector<int> rows;
    for (int i = 0; i < entries_.size(); ++i) {
        if (!is_starred(entries_.at(i))) continue;
        const Entry& e = entries_.at(i);
        if (exch_filter_ != "all" && !e.exchange.contains(exch_filter_, Qt::CaseInsensitive)) continue;
        if (!search_query_.isEmpty() &&
            !e.company.toLower().contains(search_query_) &&
            !e.ticker.toLower().contains(search_query_)) continue;
        rows.append(i);
    }
    table_->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const int gi = rows.at(r);
        const Entry& e = entries_.at(gi);
        auto* star = new QTableWidgetItem(QStringLiteral("★"));
        star->setData(Qt::UserRole, gi);
        star->setTextAlignment(Qt::AlignCenter);
        star->setForeground(QBrush(QColor(AMBER())));
        star->setToolTip("Remove from watchlist");
        table_->setItem(r, 0, star);
        table_->setItem(r, 1, new QTableWidgetItem(e.company));
        table_->item(r, 1)->setToolTip(e.company);
        table_->setItem(r, 2, new QTableWidgetItem(e.ticker.isEmpty() ? "—" : e.ticker));
        table_->setItem(r, 3, new QTableWidgetItem(e.exchange.isEmpty() ? "—" : e.exchange));
        table_->setItem(r, 4, new DateSortItem(
            e.date.isValid() ? e.date.toString("MMM d, yyyy") : (e.date_raw.isEmpty() ? "—" : e.date_raw),
            date_sort_key(e.date)));
        auto* st = new QTableWidgetItem(e.status.toUpper());
        QString status_color = AMBER();
        if      (e.status == "priced") status_color = POSITIVE();
        else if (e.status == "filed")  status_color = INFO();
        st->setForeground(QBrush(QColor(status_color)));
        table_->setItem(r, 5, st);
        table_->setItem(r, 6, new QTableWidgetItem(e.price_range.isEmpty() ? "—" : e.price_range));
        table_->setItem(r, 7, new NumSortItem(
            e.perf_fetched && e.last_price > 0
                ? QString("$%1").arg(e.last_price, 0, 'f', 2) : "—",
            e.last_price));
        QString pop_str = "—";
        QColor  pop_col(TEXT_SECONDARY());
        if (e.status == "priced" && e.perf_fetched && e.final_price > 0) {
            pop_str = QString("%1%2%").arg(e.pop_pct >= 0 ? "+" : "").arg(e.pop_pct, 0, 'f', 1);
            pop_col = QColor(e.pop_pct >= 0 ? POSITIVE() : NEGATIVE());
        }
        auto* pop = new NumSortItem(pop_str, (e.status == "priced" && e.perf_fetched) ? e.pop_pct : -1e9);
        pop->setForeground(QBrush(pop_col));
        table_->setItem(r, 8, pop);
        table_->setItem(r, 9, new NumSortItem(
            e.deal_size.isEmpty() ? "—" : e.deal_size, e.deal_size_dollars));
        const QString sector_disp = e.sector.isEmpty()
            ? (e.profile_fetched ? QStringLiteral("—") : QStringLiteral("…"))
            : e.sector;
        table_->setItem(r, 10, new QTableWidgetItem(sector_disp));
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->setColumnWidth(0, 28);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < table_->columnCount(); ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    // No explicit sortItems — rows arrive in entries_ order (insertion order
    // from the Nasdaq fetch). Users who want a specific order click the
    // header; DateSortItem makes that a real date compare.

    if (rows.isEmpty() && header_lbl_) {
        header_lbl_->setText(
            "<i style='color:#7a7a7a;'>Watchlist is empty — click the ☆ next to a deal on CALENDAR or "
            "PERFORMANCE to add it.</i>");
        for (QLabel* p : {page_deal_, page_business_, page_leader_, page_fund_, page_pipeline_,
                          page_range_, page_lockup_, page_timeline_, right_top_, right_bottom_})
            if (p) p->setText("");
        detail_symbol_.clear();
    }
}

// ── Watchlist helpers ────────────────────────────────────────────────────────

QString IpoWatchView::starred_key(const Entry& e) {
    // Prefer ticker — stable once announced. Pre-pricing deals frequently have
    // no ticker yet; fall back to company so they can still be starred.
    return e.ticker.isEmpty() ? e.company : e.ticker;
}

void IpoWatchView::load_starred() {
    auto r = fincept::SettingsRepository::instance().get("ipo_watch.starred");
    if (r.is_err()) return;
    const QString raw = r.value();
    if (raw.isEmpty()) return;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return;
    for (const auto& v : doc.array()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty()) starred_.insert(s);
    }
}

void IpoWatchView::save_starred() {
    QJsonArray arr;
    for (const auto& k : starred_) arr.append(k);
    const QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    auto r = fincept::SettingsRepository::instance().set(
        "ipo_watch.starred", json, "ipo_watch");
    if (r.is_err())
        LOG_WARN("IpoWatch", "failed to persist ipo_watch.starred");
}

void IpoWatchView::toggle_star(const Entry& e) {
    const QString k = starred_key(e);
    if (k.isEmpty()) return;
    if (starred_.contains(k)) starred_.remove(k);
    else                       starred_.insert(k);
    save_starred();
    render();
}

// (BOOKRUNNERS lens removed — Nasdaq's public IPO calendar API doesn't
// expose lead-firm names, so the aggregate was just one big "Unknown" row.
// Re-introduce when an SEC EDGAR-driven source lands.)

// ── Detail rail ──────────────────────────────────────────────────────────────

// Build a 20-segment horizontal bar showing where `pos` (0..1) sits in a
// range. Used for filed-range positioning ("priced at top of range") and the
// lock-up countdown. Returns an inline HTML <table> of coloured cells.
static QString position_bar(double pos, const QString& fill_color, int segments = 20) {
    pos = std::clamp(pos, 0.0, 1.0);
    const int n_fill = static_cast<int>(std::round(pos * segments));
    QString s = "<table style='border-collapse:collapse;margin-top:2px;'><tr>";
    for (int i = 0; i < segments; ++i) {
        const QString c = i < n_fill ? fill_color : QStringLiteral("#1a1a1a");
        s += QString("<td style='width:8px;height:6px;background:%1;border-right:1px solid #000;'></td>").arg(c);
    }
    s += "</tr></table>";
    return s;
}

// (unicode_sparkline removed — the PRICE chart is now a real QChartView from
// ChartFactory::line_chart, rendered inline in the middle-bottom tab.)

// Shared CSS injected once into every detail-rail QLabel — same as before
// but compacted into a static so each page renders consistently.
static const char* kDetailCss =
    "<style>"
    "body{font-family:Consolas,Menlo,monospace;font-size:12px;color:#e5e5e5;}"
    "h3{margin:0 0 4px 0;font-size:15px;color:#fff;}"
    ".chip{display:inline-block;padding:2px 7px;background:#222;color:#d97706;"
    "  border:1px solid #444;border-radius:2px;margin-right:4px;font-size:11px;}"
    ".chip.priced{color:#10b981;border-color:#10b981;}"
    ".chip.filed{color:#3b82f6;border-color:#3b82f6;}"
    ".chip.upcoming{color:#d97706;border-color:#d97706;}"
    "table.kv{border-collapse:collapse;width:100%;}"
    "table.kv td{padding:3px 8px 3px 0;vertical-align:top;font-size:12px;}"
    "table.grid{border-collapse:collapse;width:100%;margin-top:4px;}"
    "table.grid td{padding:3px 6px;border-bottom:1px solid #2a2a2a;font-size:12px;}"
    "table.grid td.k{color:#b8b8b8;font-size:11px;width:46%;letter-spacing:.3px;}"
    ".k{color:#b8b8b8;font-size:11px;letter-spacing:.3px;}"
    ".sec{color:#d97706;font-weight:bold;font-size:11px;letter-spacing:1.5px;"
    "  margin:8px 0 4px 0;display:block;border-bottom:1px solid #333;padding-bottom:2px;}"
    "a{color:#d97706;text-decoration:none;}"
    "a:hover{text-decoration:underline;}"
    ".pos{color:#10b981;}"
    ".neg{color:#ef4444;}"
    ".big{font-size:17px;font-weight:bold;}"
    ".muted{color:#7a7a7a;}"
    "</style>";

void IpoWatchView::render_detail(const Entry* e) {
    if (!header_lbl_) return;
    if (!e) {
        header_lbl_->setText("<i style='color:#7a7a7a;'>Select a deal to begin research.</i>");
        for (QLabel* p : {page_deal_, page_business_, page_leader_, page_fund_, page_pipeline_,
                          page_news_, page_holders_, page_filings_,
                          page_range_, page_lockup_, page_timeline_, right_top_, right_bottom_})
            if (p) p->setText("");
        detail_symbol_.clear();
        return;
    }
    detail_symbol_ = e->ticker;

    // Lazy fetch the price-since-IPO history (priced only, no-op otherwise).
    fetch_history_for_detail(*e);
    // Fire the new lazy fetches — each is gated by its own cache + inflight
    // set, so this is cheap on repeat row clicks.
    fetch_ipo_extras_for_detail(*e);
    fetch_sec_filings_for_detail(*e);
    fetch_wikipedia_for_detail(*e);
    // Lazy fetch fundamentals.
    if (!e->ticker.isEmpty() && !info_cache_.contains(e->ticker)) {
        QPointer<IpoWatchView> self = this;
        const QString sym = e->ticker;
        services::MarketDataService::instance().fetch_info(
            sym, [self, sym](bool ok, services::InfoData info) {
                if (!self || !ok) return;
                self->info_cache_.insert(sym, info);
                if (self->detail_symbol_ == sym) {
                    for (const auto& en : self->entries_) {
                        if (en.ticker == sym) { self->render_detail(&en); return; }
                    }
                }
            });
    }

    const services::InfoData info = info_cache_.value(e->ticker, {});
    const bool have_info = info_cache_.contains(e->ticker);

    // ── HEADER (company name + chips, anchored above the facts tabs) ──
    QString hdr = QString(kDetailCss);
    hdr += "<h3>" + e->company.toHtmlEscaped() + "</h3>";
    QStringList chips;
    if (!e->ticker.isEmpty())   chips << "<span class='chip'><b>" + e->ticker.toHtmlEscaped() + "</b></span>";
    if (!e->exchange.isEmpty()) chips << "<span class='chip'>" + e->exchange.toHtmlEscaped() + "</span>";
    if (!e->sector.isEmpty())   chips << "<span class='chip'>" + e->sector.toHtmlEscaped() + "</span>";
    if (!e->industry.isEmpty()) chips << "<span class='chip'>" + e->industry.toHtmlEscaped() + "</span>";
    const QString status_cls   = e->status == "priced" ? "priced" : e->status == "filed" ? "filed" : "upcoming";
    const QString status_label = e->status == "priced" ? "PRICED" : e->status == "filed" ? "FILED (S-1)" : "UPCOMING";
    chips << QString("<span class='chip %1'><b>%2</b></span>").arg(status_cls, status_label);
    hdr += chips.join(" ");
    header_lbl_->setText(hdr);

    // ── FACTS TAB PAGES (middle-top) ────────────────────────────────────────
    page_deal_     ->setText(QString(kDetailCss) + build_deal_html(*e));
    page_business_ ->setText(QString(kDetailCss) + build_business_html(*e, info, have_info));
    page_leader_   ->setText(QString(kDetailCss) + build_leadership_html(info, have_info));
    page_fund_     ->setText(QString(kDetailCss) + build_fundamentals_html(info, have_info, *e));
    page_news_     ->setText(QString(kDetailCss) + build_news_html(*e));
    page_holders_  ->setText(QString(kDetailCss) + build_holders_html(*e));
    page_filings_  ->setText(QString(kDetailCss) + build_filings_html(*e));
    page_pipeline_ ->setText(QString(kDetailCss) + build_pipeline_html(*e));

    // Tab visibility. Indexes match addTab order in build_detail_rail:
    //   0 DEAL · 1 BUSINESS · 2 LEADERSHIP · 3 FUNDAMENTALS · 4 NEWS · 5 HOLDERS · 6 FILINGS · 7 PIPELINE
    // Data-dependent tabs stay visible while a ticker is resolvable so the
    // builders' "Loading…" placeholders are seen; only the no-ticker case
    // collapses them. PIPELINE only applies to upcoming/filed entries.
    const bool has_ticker = !e->ticker.isEmpty();
    tabs_facts_->setTabVisible(1, has_ticker || (have_info &&
        (!info.description.isEmpty() || info.employees > 0 || !info.website.isEmpty())));
    tabs_facts_->setTabVisible(2, has_ticker || (have_info && !info.officers.isEmpty()));
    tabs_facts_->setTabVisible(3, has_ticker);
    tabs_facts_->setTabVisible(4, has_ticker);           // NEWS
    tabs_facts_->setTabVisible(5, has_ticker);           // HOLDERS (priced-only really, but builder gates)
    tabs_facts_->setTabVisible(6, has_ticker);           // FILINGS
    tabs_facts_->setTabVisible(7, e->status != "priced"); // PIPELINE

    // ── CHARTS TAB PAGES (middle-bottom) ────────────────────────────────────
    rebuild_price_chart(*e);
    rebuild_revenue_chart(*e);
    page_range_   ->setText(QString(kDetailCss) + build_range_html(*e));
    page_lockup_  ->setText(QString(kDetailCss) + build_lockup_html(*e));
    page_timeline_->setText(QString(kDetailCss) + build_timeline_html(*e));

    // Chart tab indexes:
    //   0 PRICE · 1 REVENUE · 2 RANGE · 3 LOCK-UP · 4 TIMELINE
    tabs_charts_->setTabVisible(0, e->status == "priced");                          // PRICE
    tabs_charts_->setTabVisible(1, has_ticker);                                     // REVENUE (lazy)
    tabs_charts_->setTabVisible(2, has_ticker);                                     // RANGE
    tabs_charts_->setTabVisible(3, e->status == "priced");                          // LOCK-UP
    tabs_charts_->setTabVisible(4, e->status != "priced");                          // TIMELINE
    // If the currently-visible tab got hidden, jump to the first visible one.
    if (!tabs_charts_->isTabVisible(tabs_charts_->currentIndex())) {
        for (int i = 0; i < tabs_charts_->count(); ++i)
            if (tabs_charts_->isTabVisible(i)) { tabs_charts_->setCurrentIndex(i); break; }
    }
    if (!tabs_facts_->isTabVisible(tabs_facts_->currentIndex())) {
        for (int i = 0; i < tabs_facts_->count(); ++i)
            if (tabs_facts_->isTabVisible(i)) { tabs_facts_->setCurrentIndex(i); break; }
    }

    // ── RIGHT PANE: sector heat + comps (top), research links (bottom) ─────
    right_top_   ->setText(QString(kDetailCss) + build_sector_comps_html(*e));
    right_bottom_->setText(QString(kDetailCss) + build_links_html(*e, info, have_info));
}

// ── Small format helpers shared by the per-tab builders ─────────────────────
namespace {
const char* pct_cls(double v) { return v >= 0 ? "pos" : "neg"; }
QString fmt_pct(double v, int prec = 1) {
    if (std::abs(v) < 1e-9) return QStringLiteral("—");
    const double pct = v * 100.0;
    return QString("%1%2%").arg(pct >= 0 ? "+" : "").arg(pct, 0, 'f', prec);
}
QString fmt_num(double v, int prec = 2) {
    if (std::abs(v) < 1e-9) return QStringLiteral("—");
    return QString::number(v, 'f', prec);
}
QString kvg_row(const QString& k, const QString& v) {
    return "<tr><td class='k'>" + k + "</td><td>" + (v.isEmpty() ? "—" : v) + "</td></tr>";
}
} // namespace

// ── DEAL tab ────────────────────────────────────────────────────────────────
QString IpoWatchView::build_deal_html(const Entry& e) const {
    QString h = "<table class='grid'>";
    const QString date_label = e.status == "priced" ? "Priced on"
                              : e.status == "filed" ? "S-1 filed"
                                                    : "Expected price date";
    h += kvg_row(date_label, e.date.isValid() ? e.date.toString("MMMM d, yyyy") : e.date_raw.toHtmlEscaped());
    if (!e.price_range.isEmpty())
        h += kvg_row(e.status == "priced" ? "Final price" : "Filed range", e.price_range.toHtmlEscaped());
    if (e.status == "priced" && e.perf_fetched && e.final_price > 0) {
        h += kvg_row("Last price", QString("<span class='big'>$%1</span>").arg(e.last_price, 0, 'f', 2));
        h += kvg_row("Pop since IPO",
                     QString("<span class='%1 big'>%2%3%</span>")
                         .arg(pct_cls(e.pop_pct), e.pop_pct >= 0 ? "+" : "").arg(e.pop_pct, 0, 'f', 1));
    }
    if (!e.shares_raw.isEmpty()) h += kvg_row("Shares offered", e.shares_raw.toHtmlEscaped());
    if (!e.deal_size.isEmpty())  h += kvg_row("Deal size",      e.deal_size.toHtmlEscaped());
    if (!e.exchange.isEmpty())   h += kvg_row("Exchange",       e.exchange.toHtmlEscaped());
    if (!e.bookrunner.isEmpty()) h += kvg_row("Lead bookrunner", e.bookrunner.toHtmlEscaped());
    h += "</table>";
    return h;
}

// ── BUSINESS tab ────────────────────────────────────────────────────────────
QString IpoWatchView::build_business_html(const Entry& e, const services::InfoData& info, bool have_info) const {
    QString h;
    // Surface a description from whichever source has one: prefer the richer
    // yfinance `longBusinessSummary`; fall back to the Wikipedia extract for
    // pre-IPO / freshly-listed companies that yfinance doesn't know yet.
    QString desc = have_info ? info.description : QString();
    QString desc_source;
    if (desc.isEmpty()) {
        const auto wit = wiki_cache_.constFind(e.company);
        if (wit != wiki_cache_.constEnd() && !wit.value().extract.isEmpty()) {
            desc = wit.value().extract;
            desc_source = wit.value().url;
        }
    }
    if (!desc.isEmpty()) {
        constexpr int kMaxChars = 1200;
        if (desc.size() > kMaxChars) {
            int cut = desc.lastIndexOf('.', kMaxChars);
            if (cut < 400) cut = kMaxChars;
            desc = desc.left(cut + 1) + " …";
        }
        h += "<div style='line-height:1.5;'>" + desc.toHtmlEscaped() + "</div>";
        if (!desc_source.isEmpty()) {
            h += "<div class='muted' style='margin-top:4px;'>source: "
                 "<a href='" + desc_source.toHtmlEscaped() + "'>Wikipedia</a></div>";
        }
    } else if (!have_info) {
        h += e.ticker.isEmpty()
            ? "<div class='muted'>No ticker assigned yet — business profile will populate after pricing.</div>"
            : "<div class='muted'>Loading business profile…</div>";
    } else {
        h += "<div class='muted'>No business summary available from yfinance or Wikipedia.</div>";
    }
    h += "<table class='grid' style='margin-top:8px;'>";
    if (have_info) {
        if (info.employees > 0)        h += kvg_row("Employees", QString::number(info.employees));
        if (!info.country.isEmpty())   h += kvg_row("Country",   info.country.toHtmlEscaped());
        if (!info.currency.isEmpty())  h += kvg_row("Currency",  info.currency.toHtmlEscaped());
        if (!info.website.isEmpty()) {
            const QString href = info.website.toHtmlEscaped();
            h += "<tr><td class='k'>Website</td><td><a href='" + href + "'>" + href + "</a></td></tr>";
        }
    }
    h += "</table>";
    return h;
}

// ── LEADERSHIP tab ──────────────────────────────────────────────────────────
QString IpoWatchView::build_leadership_html(const services::InfoData& info, bool have_info) const {
    if (!have_info) return "<div class='muted'>Loading leadership data…</div>";
    if (info.officers.isEmpty())
        return "<div class='muted'>No officer data available for this ticker.</div>";
    QString h = "<table class='grid'>";
    for (const auto& o : info.officers) {
        h += "<tr><td class='k' style='width:40%;'>" + o.title.toHtmlEscaped() + "</td><td>" +
             o.name.toHtmlEscaped() + "</td></tr>";
    }
    h += "</table>";
    return h;
}

// ── FUNDAMENTALS tab ────────────────────────────────────────────────────────
QString IpoWatchView::build_fundamentals_html(const services::InfoData& info, bool have_info, const Entry& e) const {
    if (!have_info) {
        if (e.ticker.isEmpty())
            return "<div class='muted'>No ticker — fundamentals unavailable until pricing.</div>";
        return "<div class='muted'>Loading fundamentals…</div>";
    }
    QString h = "<table class='grid'>";
    if (info.market_cap > 0)       h += kvg_row("Market cap",     format_money(info.market_cap));
    if (info.pe_ratio   > 0)       h += kvg_row("P/E (trailing)", QString::number(info.pe_ratio, 'f', 1) + "x");
    if (info.forward_pe > 0)       h += kvg_row("P/E (forward)",  QString::number(info.forward_pe, 'f', 1) + "x");
    if (info.price_to_book > 0)    h += kvg_row("Price / Book",   QString::number(info.price_to_book, 'f', 2) + "x");
    if (info.dividend_yield > 0)   h += kvg_row("Dividend yield", fmt_pct(info.dividend_yield, 2));
    if (info.beta > 0)             h += kvg_row("Beta",           fmt_num(info.beta));
    if (info.avg_volume > 0)       h += kvg_row("Avg volume",     format_money(info.avg_volume));
    if (info.profit_margin != 0)
        h += kvg_row("Profit margin",
                     QString("<span class='%1'>%2</span>").arg(pct_cls(info.profit_margin), fmt_pct(info.profit_margin)));
    if (info.roe != 0)
        h += kvg_row("Return on equity",
                     QString("<span class='%1'>%2</span>").arg(pct_cls(info.roe), fmt_pct(info.roe)));
    if (info.debt_to_equity > 0)   h += kvg_row("Debt / Equity",  fmt_num(info.debt_to_equity));
    if (info.current_ratio > 0)    h += kvg_row("Current ratio",  fmt_num(info.current_ratio));
    h += "</table>";
    return h;
}

// ── PIPELINE tab (upcoming/filed only) ──────────────────────────────────────
QString IpoWatchView::build_pipeline_html(const Entry& e) const {
    QString h;
    if (e.status == "upcoming" && e.date.isValid()) {
        const QDate today = QDate::currentDate();
        const qint64 days_to = today.daysTo(e.date);
        h += QString("<div class='big' style='color:#d97706;'>%1 day%2</div>")
                 .arg(days_to).arg(days_to == 1 ? "" : "s");
        h += QString("<div class='muted'>until expected pricing on %1</div>")
                 .arg(e.date.toString("MMM d, yyyy"));
        const double frac = std::clamp(1.0 - double(days_to) / 14.0, 0.0, 1.0);
        h += "<div style='margin-top:8px;'>" + position_bar(frac, "#d97706") + "</div>";
        h += "<div class='muted' style='margin-top:6px;'>Nasdaq's UPCOMING bucket carries the next ~2 weeks of scheduled deals. "
             "Earlier-stage pipeline candidates show up under FILED (S-1).</div>";
    } else if (e.status == "filed" && e.date.isValid()) {
        const QDate today = QDate::currentDate();
        const qint64 days_since = e.date.daysTo(today);
        h += QString("<div class='big' style='color:#3b82f6;'>S-1 filed %1 day%2 ago</div>")
                 .arg(days_since).arg(days_since == 1 ? "" : "s");
        const QString stage =
            days_since < 30  ? "Typical pricing window starts ~30d after filing."
            : days_since < 90 ? "In typical pricing window (30-90d post-filing)."
            : days_since < 180 ? "Beyond typical window — may need an updated filing."
                              : "Stale filing — deal may have been quietly withdrawn.";
        h += "<div class='muted' style='margin-top:4px;'>" + stage + "</div>";
        const double frac = std::clamp(double(days_since) / 180.0, 0.0, 1.0);
        const QString col = days_since < 30 ? "#10b981" : days_since < 90 ? "#d97706" : "#ef4444";
        h += "<div style='margin-top:8px;'>" + position_bar(frac, col) + "</div>";
    } else {
        h += "<div class='muted'>Pipeline data unavailable for this entry.</div>";
    }
    return h;
}

// ── RANGE tab (FUNDAMENTALS 52-week range, visualized) ──────────────────────
QString IpoWatchView::build_range_html(const Entry& e) const {
    const services::InfoData info = info_cache_.value(e.ticker, {});
    const bool have_info = info_cache_.contains(e.ticker);
    if (!have_info || info.week52_high <= info.week52_low || info.week52_low <= 0)
        return "<div class='muted'>52-week range data not yet available.</div>";
    QString h = QString("<div class='big'>$%1 <span class='muted'>–</span> $%2</div>")
                    .arg(info.week52_low, 0, 'f', 2).arg(info.week52_high, 0, 'f', 2);
    if (e.last_price > 0) {
        const double pos = (e.last_price - info.week52_low) /
                           (info.week52_high - info.week52_low);
        h += QString("<div class='muted' style='margin-top:6px;'>Current $%1 sits at %2% of the band</div>")
                 .arg(e.last_price, 0, 'f', 2).arg(int(pos * 100));
        h += "<div style='margin-top:6px;'>" + position_bar(pos, "#d97706") + "</div>";
    } else {
        h += "<div class='muted' style='margin-top:6px;'>Current price unavailable.</div>";
    }
    return h;
}

// ── LOCK-UP tab (priced only) ───────────────────────────────────────────────
QString IpoWatchView::build_lockup_html(const Entry& e) const {
    if (e.status != "priced" || !e.date.isValid())
        return "<div class='muted'>Lock-up applies only to priced deals.</div>";
    const QDate lockup_end = e.date.addDays(180);
    const QDate today = QDate::currentDate();
    const qint64 days_left = today.daysTo(lockup_end);
    const qint64 days_elapsed = e.date.daysTo(today);
    const double progress = std::clamp(double(days_elapsed) / 180.0, 0.0, 1.0);
    QString h;
    h += "<div class='muted'>Insider/early-investor lock-up window (180d est.)</div>";
    if (days_left > 0)
        h += QString("<div class='big' style='margin-top:6px;'>%1 days left</div>"
                     "<div class='muted'>expires ~%2</div>")
                 .arg(days_left).arg(lockup_end.toString("MMM d, yyyy"));
    else
        h += QString("<div class='muted' style='margin-top:6px;'>Expired %1</div>")
                 .arg(lockup_end.toString("MMM d, yyyy"));
    h += "<div style='margin-top:8px;'>" + position_bar(progress, days_left > 0 ? "#10b981" : "#666") + "</div>";
    return h;
}

// ── TIMELINE tab (upcoming/filed only — alternate visualization of pipeline) ─
QString IpoWatchView::build_timeline_html(const Entry& e) const {
    if (e.status == "priced") return "<div class='muted'>—</div>";
    QString h;
    h += "<div class='muted'>S-1 filed &rarr; pricing window &rarr; listing</div>";
    if (e.status == "upcoming" && e.date.isValid()) {
        const qint64 days_to = QDate::currentDate().daysTo(e.date);
        h += QString("<div class='big' style='margin-top:6px;color:#d97706;'>T-%1 days</div>")
                 .arg(days_to);
        // Visualize progress through the 90-day pricing window.
        const double frac = std::clamp(1.0 - double(days_to) / 90.0, 0.0, 1.0);
        h += "<div style='margin-top:8px;'>" + position_bar(frac, "#d97706") + "</div>";
        h += QString("<div class='muted' style='margin-top:6px;'>Expected price date: %1</div>")
                 .arg(e.date.toString("MMM d, yyyy"));
    } else if (e.status == "filed" && e.date.isValid()) {
        const qint64 days_since = e.date.daysTo(QDate::currentDate());
        h += QString("<div class='big' style='margin-top:6px;color:#3b82f6;'>Day %1</div>")
                 .arg(days_since);
        const double frac = std::clamp(double(days_since) / 180.0, 0.0, 1.0);
        h += "<div style='margin-top:8px;'>" + position_bar(frac, "#3b82f6") + "</div>";
        h += QString("<div class='muted' style='margin-top:6px;'>S-1 filing date: %1</div>")
                 .arg(e.date.toString("MMM d, yyyy"));
    }
    return h;
}

// ── NEWS tab — recent headlines from yfinance via ipo_extras ────────────────
QString IpoWatchView::build_news_html(const Entry& e) const {
    if (e.ticker.isEmpty())
        return "<div class='muted'>News appears once the ticker is assigned.</div>";
    if (!ipo_extras_.contains(e.ticker))
        return "<div class='muted'>Loading news…</div>";
    const auto& ex = ipo_extras_.value(e.ticker);
    if (ex.news.isEmpty())
        return "<div class='muted'>No recent news for this ticker.</div>";

    QString h = "<table class='kv'>";
    for (const auto& n : ex.news) {
        // ts is either an integer epoch seconds (older yfinance) or ISO 8601
        // (newer). Try both; show "—" if neither parses.
        QString when;
        bool ok = false;
        const qint64 epoch = n.ts.toLongLong(&ok);
        if (ok && epoch > 1e9) {
            when = QDateTime::fromSecsSinceEpoch(epoch).toString("MMM d");
        } else {
            const QDateTime iso = QDateTime::fromString(n.ts.left(10), "yyyy-MM-dd");
            when = iso.isValid() ? iso.toString("MMM d") : QString();
        }
        h += "<tr><td colspan='2' style='padding-bottom:0;'>"
             "<a href='" + n.link.toHtmlEscaped() + "'>" + n.title.toHtmlEscaped() + "</a>"
             "</td></tr>";
        h += "<tr><td colspan='2' class='muted' style='padding-top:0;padding-bottom:8px;'>"
             + n.publisher.toHtmlEscaped();
        if (!when.isEmpty()) h += " · " + when;
        h += "</td></tr>";
    }
    h += "</table>";
    return h;
}

// ── HOLDERS tab — institutional + major holders from ipo_extras ─────────────
QString IpoWatchView::build_holders_html(const Entry& e) const {
    if (e.ticker.isEmpty())
        return "<div class='muted'>Holder data appears after the deal lists.</div>";
    if (!ipo_extras_.contains(e.ticker))
        return "<div class='muted'>Loading holders…</div>";
    const auto& ex = ipo_extras_.value(e.ticker);
    if (ex.institutional_holders.isEmpty() && ex.major_holders.isEmpty())
        return "<div class='muted'>No holder data available for this ticker.</div>";

    QString h;
    if (!ex.major_holders.isEmpty()) {
        h += "<span class='sec'>OWNERSHIP BREAKDOWN</span>";
        h += "<table class='grid'>";
        auto fmt_pct_or_count = [](const QString& label, double v) {
            // yfinance's major_holders uses ratios for percent rows and a raw
            // count for `institutionsCount`. Disambiguate by label.
            if (label.contains("Count", Qt::CaseInsensitive))
                return QString::number(int(v));
            return QString("%1%").arg(v * 100.0, 0, 'f', 2);
        };
        for (const auto& m : ex.major_holders) {
            // The labels yfinance returns are camelCase identifiers; tidy
            // them to human-readable form for the rail.
            QString pretty = m.label;
            pretty.replace("insidersPercentHeld", "Insiders held");
            pretty.replace("institutionsPercentHeld", "Institutions held");
            pretty.replace("institutionsFloatPercentHeld", "Institutions (of float)");
            pretty.replace("institutionsCount", "Institutional holders");
            h += "<tr><td class='k'>" + pretty.toHtmlEscaped() + "</td><td>" +
                 fmt_pct_or_count(m.label, m.value) + "</td></tr>";
        }
        h += "</table>";
    }
    if (!ex.institutional_holders.isEmpty()) {
        h += "<span class='sec'>TOP INSTITUTIONAL HOLDERS</span>";
        h += "<table class='grid'>";
        h += "<tr><td class='k'>Holder</td><td class='k' style='width:auto;text-align:right;'>% Held · Value</td></tr>";
        for (const auto& ih : ex.institutional_holders) {
            const QString pct = QString("%1%").arg(ih.pct * 100.0, 0, 'f', 2);
            h += "<tr><td>" + ih.holder.toHtmlEscaped() + "</td><td style='text-align:right;'>"
                 + pct + " · " + format_money(ih.value) + "</td></tr>";
        }
        h += "</table>";
    }
    return h;
}

// ── FILINGS tab — recent SEC submissions ─────────────────────────────────────
QString IpoWatchView::build_filings_html(const Entry& e) const {
    if (e.ticker.isEmpty())
        return "<div class='muted'>SEC filings appear once the ticker is assigned.</div>";
    if (!filings_cache_.contains(e.ticker))
        return "<div class='muted'>Loading SEC filings…</div>";
    const auto& filings = filings_cache_.value(e.ticker);
    if (filings.isEmpty())
        return "<div class='muted'>No SEC filings indexed for this ticker yet.<br><br>"
               "If this is a fresh IPO, the company's CIK may not appear in SEC's "
               "company_tickers.json until ~24h after listing.</div>";

    QString h = "<table class='grid'>";
    h += "<tr><td class='k' style='width:18%;'>Form</td>"
         "<td class='k' style='width:24%;'>Date</td>"
         "<td class='k'>Document</td></tr>";
    for (const auto& f : filings) {
        h += "<tr><td><b>" + f.form.toHtmlEscaped() + "</b></td>"
             "<td>" + f.filing_date.toHtmlEscaped() + "</td>"
             "<td><a href='" + f.url.toHtmlEscaped() + "'>" +
             f.primary_doc.toHtmlEscaped() + "</a></td></tr>";
    }
    h += "</table>";
    return h;
}

// ── PRICE chart — real QChartView built from history_cache_ ─────────────────
void IpoWatchView::rebuild_price_chart(const Entry& e) {
    if (!page_price_chart_host_) return;
    // The host layout is the QVBoxLayout we installed in build_detail_rail.
    // Cast to access the stretch-aware addWidget overload.
    auto* host_layout = qobject_cast<QVBoxLayout*>(page_price_chart_host_->layout());
    if (!host_layout) return;
    // Wipe old chart contents.
    while (host_layout->count() > 0) {
        QLayoutItem* item = host_layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    price_chart_view_ = nullptr;

    auto add_placeholder = [&](const QString& html) {
        auto* lbl = new QLabel(html);
        lbl->setTextFormat(Qt::RichText);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("color:#7a7a7a;");
        host_layout->addWidget(lbl, 1);
    };

    if (e.status != "priced" || e.ticker.isEmpty()) {
        add_placeholder("<i>Price chart appears once the deal lists.</i>");
        return;
    }
    if (history_inflight_.contains(e.ticker)) {
        add_placeholder("<i>Loading price history…</i>");
        return;
    }
    auto it = history_cache_.constFind(e.ticker);
    if (it == history_cache_.constEnd() || it.value().size() < 2) {
        add_placeholder("<i>No price history available yet.</i>");
        return;
    }
    const auto& closes = it.value();
    QVector<ui::ChartFactory::DataPoint> pts;
    pts.reserve(closes.size());
    for (int i = 0; i < closes.size(); ++i)
        pts.append({double(i), closes.at(i)});

    const double first = closes.first();
    const double last  = closes.last();
    const QString color = last >= first ? QString(ui::colors::POSITIVE())
                                        : QString(ui::colors::NEGATIVE());
    auto* view = ui::ChartFactory::line_chart(QString(), pts, color);
    price_chart_view_ = view;
    host_layout->addWidget(view, 1);
}

// ── REVENUE chart — quarterly bars from ipo_extras ───────────────────────────
void IpoWatchView::rebuild_revenue_chart(const Entry& e) {
    if (!page_revenue_chart_host_) return;
    auto* host_layout = qobject_cast<QVBoxLayout*>(page_revenue_chart_host_->layout());
    if (!host_layout) return;
    while (host_layout->count() > 0) {
        QLayoutItem* item = host_layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    revenue_chart_view_ = nullptr;

    auto add_placeholder = [&](const QString& html) {
        auto* lbl = new QLabel(html);
        lbl->setTextFormat(Qt::RichText);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("color:#7a7a7a;");
        host_layout->addWidget(lbl, 1);
    };

    if (e.ticker.isEmpty()) {
        add_placeholder("<i>Revenue history appears after the deal lists.</i>");
        return;
    }
    if (!ipo_extras_.contains(e.ticker)) {
        add_placeholder("<i>Loading quarterly revenue…</i>");
        return;
    }
    const auto& ex = ipo_extras_.value(e.ticker);
    if (ex.quarterly_revenue.isEmpty()) {
        add_placeholder("<i>No quarterly revenue history available.</i>");
        return;
    }
    QStringList categories;
    QVector<double> values;
    categories.reserve(ex.quarterly_revenue.size());
    values.reserve(ex.quarterly_revenue.size());
    for (const auto& p : ex.quarterly_revenue) {
        // Convert "2026-03-31" → "Q1 26" style for readability.
        const QDate d = QDate::fromString(p.date, "yyyy-MM-dd");
        QString label = p.date;
        if (d.isValid()) {
            const int q = (d.month() - 1) / 3 + 1;
            label = QString("Q%1 %2").arg(q).arg(d.year() % 100, 2, 10, QChar('0'));
        }
        categories.append(label);
        // Display in $M so the y-axis is readable.
        values.append(p.value / 1e6);
    }
    auto* view = ui::ChartFactory::bar_chart(QStringLiteral("Quarterly revenue ($M)"),
                                             categories, values, ui::colors::AMBER());
    revenue_chart_view_ = view;
    host_layout->addWidget(view, 1);
}

// ── RIGHT-TOP: sector heat + comps ──────────────────────────────────────────
QString IpoWatchView::build_sector_comps_html(const Entry& e) const {
    auto comp_match = [&](const Entry& other) -> bool {
        if (&other == &e || other.status != "priced") return false;
        if (!e.sector.isEmpty() && !other.sector.isEmpty()) return other.sector == e.sector;
        if (!e.exchange.isEmpty() && !other.exchange.isEmpty()) return other.exchange == e.exchange;
        return false;
    };
    int comp_count = 0; double comp_pop_sum = 0; int comp_pop_n = 0;
    QVector<const Entry*> comp_top;
    for (const auto& other : entries_) {
        if (!comp_match(other)) continue;
        if (e.date.isValid() && other.date.isValid() &&
            std::abs(e.date.daysTo(other.date)) > 180) continue;
        ++comp_count;
        if (other.perf_fetched) { comp_pop_sum += other.pop_pct; ++comp_pop_n; }
        if (comp_top.size() < 8) comp_top.append(&other);
    }

    const QString basis = !e.sector.isEmpty() ? "same sector" : "same exchange";

    QString h;
    h += QString("<span class='sec'>SECTOR HEAT · %1</span>").arg(basis);
    if (comp_pop_n == 0) {
        h += "<span class='muted'>No priced comparables in window.</span>";
    } else {
        const double avg = comp_pop_sum / comp_pop_n;
        const QString verdict =
            avg >  15 ? "<span class='pos'>🔥 HOT</span>"
            : avg >   5 ? "<span class='pos'>WARM</span>"
            : avg >  -5 ? "<span class='muted'>NEUTRAL</span>"
            : avg > -15 ? "<span class='neg'>COOL</span>"
                        : "<span class='neg'>❄ COLD</span>";
        h += QString("<div>%1 · <span class='big %2'>%3%4%</span> "
                     "<span class='muted'>avg pop · %5 peer%6</span></div>")
                 .arg(verdict, pct_cls(avg))
                 .arg(avg >= 0 ? "+" : "").arg(avg, 0, 'f', 1)
                 .arg(comp_pop_n).arg(comp_pop_n == 1 ? "" : "s");
        const double frac = std::clamp((avg + 50.0) / 100.0, 0.0, 1.0);
        const QString col = avg > 5 ? "#10b981" : avg < -5 ? "#ef4444" : "#7a7a7a";
        h += "<div style='margin-top:6px;'>" + position_bar(frac, col) + "</div>";
    }

    h += QString("<span class='sec'>COMPS · %1 · ±180d priced</span>").arg(basis);
    if (comp_count == 0) {
        h += "<span class='muted'>No comparable priced deals in the current dataset.</span>";
    } else {
        h += QString("<b>%1 deals</b>").arg(comp_count);
        h += "<table class='grid' style='margin-top:4px;'>";
        for (const auto* c : comp_top) {
            QString pop;
            if (c->perf_fetched && c->final_price > 0) {
                pop = QString("<span class='%1'>%2%3%</span>")
                          .arg(pct_cls(c->pop_pct), c->pop_pct >= 0 ? "+" : "")
                          .arg(c->pop_pct, 0, 'f', 1);
            } else {
                pop = "<span class='muted'>…</span>";
            }
            h += "<tr><td>" + c->company.toHtmlEscaped() + " <span class='muted'>(" +
                 c->ticker.toHtmlEscaped() + ")</span></td><td>" + pop + "</td></tr>";
        }
        h += "</table>";
    }
    return h;
}

// ── RIGHT-BOTTOM: research links ────────────────────────────────────────────
QString IpoWatchView::build_links_html(const Entry& e, const services::InfoData& info, bool have_info) const {
    const QString company_q = QUrl::toPercentEncoding(e.company);
    const QString edgar_s1   = "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=" + company_q + "&type=S-1&dateb=&owner=include&count=20";
    const QString edgar_all  = "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=" + company_q + "&type=&dateb=&owner=include&count=40";
    const QString edgar_144  = "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=" + company_q + "&type=144&dateb=&owner=include&count=20";
    const QString yahoo      = e.ticker.isEmpty() ? QString() : ("https://finance.yahoo.com/quote/" + e.ticker);
    const QString gnews      = "https://news.google.com/search?q=" + company_q + "%20IPO";
    const QString gsearch    = "https://www.google.com/search?q=" + QUrl::toPercentEncoding(e.company + " founders CEO");
    const QString wiki       = "https://en.wikipedia.org/wiki/Special:Search?search=" + company_q;
    const QString crunchbase = "https://www.crunchbase.com/textsearch?q=" + company_q;
    const QString linkedin   = "https://www.linkedin.com/search/results/companies/?keywords=" + company_q;

    QString h = "<table class='kv'>";
    h += "<tr><td><a href='" + edgar_s1   + "'>SEC: S-1 / 424B</a></td>"
         "<td><a href='" + edgar_all  + "'>SEC: all filings</a></td></tr>";
    h += "<tr><td><a href='" + edgar_144 + "'>SEC: Form 144</a></td>"
         "<td>" + (yahoo.isEmpty() ? QStringLiteral("—") : "<a href='" + yahoo + "'>Yahoo quote</a>") + "</td></tr>";
    h += "<tr><td><a href='" + gnews    + "'>Google News</a></td>"
         "<td><a href='" + wiki     + "'>Wikipedia</a></td></tr>";
    h += "<tr><td><a href='" + linkedin + "'>LinkedIn</a></td>"
         "<td><a href='" + crunchbase + "'>Crunchbase</a></td></tr>";
    h += "<tr><td colspan='2'><a href='" + gsearch + "'>Google: founders &amp; CEO</a></td></tr>";
    if (have_info && !info.country.isEmpty())
        h += "<tr><td colspan='2' class='muted'>HQ: " + info.country.toHtmlEscaped() + "</td></tr>";
    h += "</table>";
    return h;
}
// ── Parsing helpers ──────────────────────────────────────────────────────────

QString IpoWatchView::format_money(double dollars) {
    if (dollars <= 0)   return QStringLiteral("—");
    if (dollars >= 1e9) return QString("$%1B").arg(dollars / 1e9, 0, 'f', 2);
    if (dollars >= 1e6) return QString("$%1M").arg(dollars / 1e6, 0, 'f', 0);
    return QString("$%1K").arg(dollars / 1e3, 0, 'f', 0);
}

QString IpoWatchView::format_deal_size(const QString& price_range, const QString& shares) {
    if (price_range.isEmpty() || shares.isEmpty()) return {};
    bool ok = false;
    const double mid = parse_price_mid(price_range, &ok);
    if (!ok) return {};
    const double n = parse_shares(shares);
    if (n <= 0) return {};
    return format_money(mid * n);
}

// Parse "$15.00-$17.00" / "$17.00" / "$15.00 - $17.00" → midpoint.
double IpoWatchView::parse_price_mid(const QString& price_range, bool* ok_out) {
    if (ok_out) *ok_out = false;
    if (price_range.isEmpty()) return 0;
    QString pr = price_range;
    pr.remove('$');
    const QStringList parts = pr.split('-', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return 0;
    bool ok_lo = false, ok_hi = false;
    const double lo = parts.at(0).trimmed().toDouble(&ok_lo);
    const double hi = parts.at(parts.size() > 1 ? 1 : 0).trimmed().toDouble(&ok_hi);
    if (!ok_lo) return 0;
    if (ok_out) *ok_out = true;
    return (ok_hi && parts.size() > 1) ? (lo + hi) / 2.0 : lo;
}

double IpoWatchView::parse_shares(const QString& shares) {
    QString s = shares; s.remove(',');
    bool ok = false;
    const double n = s.trimmed().toDouble(&ok);
    return ok ? n : 0;
}

} // namespace fincept::screens::widgets

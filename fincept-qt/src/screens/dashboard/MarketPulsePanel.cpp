#include "screens/dashboard/MarketPulsePanel.h"

#include "screens/markets/MarketChartDialog.h"

#include "services/markets/MarketDataService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"
#    include <QSet>

#include <QDateTime>
#include <QFrame>
#include <QPalette>
#include <QPointer>
#include <QShowEvent>
#include <QTimeZone>

#include <algorithm>

namespace fincept::screens {

// ── Symbols used by this panel ───────────────────────────────────────────────

// Fixed watch basket of ~50 large US stocks (+ ^VIX): used for the mood
// gauge and the basket breadth row. This is NOT exchange-wide breadth — the
// UI labels it as a basket with its stock count, never as NYSE/NASDAQ/S&P.
static const QStringList kBreadthSymbols = {
    "^VIX",
    // Large caps
    "AAPL",
    "MSFT",
    "GOOGL",
    "AMZN",
    "NVDA",
    "META",
    "TSLA",
    "BRK-B",
    "JPM",
    "UNH",
    "V",
    "XOM",
    "LLY",
    "JNJ",
    "WMT",
    "MA",
    "PG",
    "HD",
    "CVX",
    "MRK",
    // Tech
    "NFLX",
    "AMD",
    "INTC",
    "QCOM",
    "ADBE",
    "CSCO",
    "ORCL",
    "CRM",
    "AVGO",
    "TXN",
    // Financials, energy, consumer, industrials
    "GS",
    "BAC",
    "WFC",
    "C",
    "MS",
    "BLK",
    "AXP",
    "CAT",
    "BA",
    "GE",
    "DIS",
    "NKE",
    "KO",
    "PEP",
    "MCD",
    "PFE",
    "ABT",
    "TMO",
    "UPS",
    "FDX",
};

// Number of stocks in the breadth basket (excludes ^VIX).
static int breadth_basket_size() {
    int n = 0;
    for (const auto& s : kBreadthSymbols)
        if (!s.startsWith('^'))
            ++n;
    return n;
}

// Gainers/losers come from the real market-wide yfinance screener
// (day_gainers / day_losers) via MarketDataService::fetch_top_movers — the
// same source TopMoversWidget uses. Count 10 shares that widget's cache key.
static constexpr int kTopMoversFetchCount = 10;

// Global snapshot symbols
static const QStringList kSnapshotSymbols = services::MarketDataService::global_snapshot_symbols();

// ── Constructor ──────────────────────────────────────────────────────────────

MarketPulsePanel::MarketPulsePanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(180);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    // Constrain preferred width so the QSplitter doesn't give this panel
    // the majority of space before the user sees the dashboard.
    // The user can still drag the splitter handle wider if needed.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    vl->addWidget(build_header());

    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);
    // Styling applied by refresh_theme() called at end of constructor

    auto* content = new QWidget(this);
    auto* cl = new QVBoxLayout(content);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    cl->addWidget(build_fear_greed_section());
    cl->addWidget(build_breadth_section());
    cl->addWidget(build_gainers_section());
    cl->addWidget(build_losers_section());
    cl->addWidget(build_global_snapshot_section());
    cl->addWidget(build_market_hours_section());
    cl->addStretch();

    scroll_area_->setWidget(content);
    vl->addWidget(scroll_area_, 1);

    // ── Timers ──

    hours_timer_ = new QTimer(this);
    hours_timer_->setInterval(60000); // 1 min — market open/close status
    connect(hours_timer_, &QTimer::timeout, this, &MarketPulsePanel::refresh_market_hours);

    // ── Coalesce timers ──
    // A refresh cycle delivers ~50 per-symbol quote callbacks; without
    // coalescing each one re-runs the whole section render. These single-shot
    // timers collapse a burst into one in-place update per section.
    breadth_coalesce_ = new QTimer(this);
    breadth_coalesce_->setSingleShot(true);
    breadth_coalesce_->setInterval(16);
    connect(breadth_coalesce_, &QTimer::timeout, this, &MarketPulsePanel::rebuild_breadth_from_cache);
    movers_coalesce_ = new QTimer(this);
    movers_coalesce_->setSingleShot(true);
    movers_coalesce_->setInterval(16);
    connect(movers_coalesce_, &QTimer::timeout, this, &MarketPulsePanel::rebuild_movers_from_cache);
    // Screener-driven movers are not on the DataHub quote stream; poll them
    // while visible. fetch_top_movers() caches for 60s, so this is cheap.
    movers_timer_ = new QTimer(this);
    movers_timer_->setInterval(60000);
    connect(movers_timer_, &QTimer::timeout, this, &MarketPulsePanel::fetch_movers);
    snapshot_coalesce_ = new QTimer(this);
    snapshot_coalesce_->setSingleShot(true);
    snapshot_coalesce_->setInterval(16);
    connect(snapshot_coalesce_, &QTimer::timeout, this, &MarketPulsePanel::rebuild_snapshot_from_cache);

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });
    refresh_theme();
}

// ── Theme refresh ────────────────────────────────────────────────────────────

void MarketPulsePanel::refresh_theme() {
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(ui::colors::PANEL()));
    pal.setColor(QPalette::Base, QColor(ui::colors::PANEL()));
    setPalette(pal);

    // ── Root panel ──
    setStyleSheet(QString("background:%1;").arg(ui::colors::PANEL()));

    // ── Header bar ──
    if (header_bar_)
        header_bar_->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                                       .arg(ui::colors::BG_RAISED(), ui::colors::AMBER_DIM()));
    if (header_icon_)
        header_icon_->setStyleSheet(
            QString("color: %1; font-size: 12px; background: transparent;").arg(ui::colors::AMBER()));
    if (header_title_)
        header_title_->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold; letter-spacing: 1px; background: transparent;")
                .arg(ui::colors::AMBER()));
    if (header_live_dot_)
        header_live_dot_->setStyleSheet(QString("background: %1; border-radius: 3px;").arg(ui::colors::POSITIVE()));

    // ── Scroll area ──
    if (scroll_area_)
        scroll_area_->setStyleSheet(
            QString("QScrollArea{border:none;background:transparent;}"
                    "QScrollBar:vertical{width:6px;background:transparent;}"
                    "QScrollBar::handle:vertical{background:%1;border-radius:3px;min-height:20px;}"
                    "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}")
                .arg(ui::colors::BORDER_MED()));

    // ── Section headers ──
    auto style_section = [](SectionHeader& sh, const QString& icon_color) {
        if (sh.container)
            sh.container->setStyleSheet(
                QString("background: %1; border-bottom: 1px solid %2; border-top: 1px solid %2;")
                    .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
        if (sh.icon)
            sh.icon->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent;").arg(icon_color));
        if (sh.title)
            sh.title->setStyleSheet(
                QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 0.5px; background: transparent;")
                    .arg(ui::colors::TEXT_SECONDARY()));
    };

    style_section(sh_breadth_, ui::colors::CYAN());
    style_section(sh_gainers_, ui::colors::POSITIVE());
    style_section(sh_losers_, ui::colors::NEGATIVE());
    style_section(sh_snapshot_, ui::colors::INFO());
    style_section(sh_hours_, ui::colors::WARNING());

    // ── Fear & Greed ──
    if (fg_header_label_)
        fg_header_label_->setStyleSheet(
            QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 0.5px; background: transparent;")
                .arg(ui::colors::TEXT_SECONDARY()));
    if (fg_gauge_icon_)
        fg_gauge_icon_->setStyleSheet(
            QString("color: %1; font-size: 12px; background: transparent;").arg(ui::colors::AMBER()));
    if (fg_gradient_bar_)
        fg_gradient_bar_->setStyleSheet(
            QString("QFrame { border-radius: 3px; "
                    "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
                    "stop:0 %1, stop:0.25 %2, stop:0.5 %3, stop:0.75 %4, stop:1 %5); }")
                .arg(ui::colors::NEGATIVE(), ui::colors::AMBER(), ui::colors::WARNING(),
                     ui::colors::POSITIVE_DIM(), ui::colors::POSITIVE()));
    if (fg_score_val_)
        fg_score_val_->setStyleSheet(QString("color: %1; font-size: 18px; font-weight: bold; background: transparent;")
                                         .arg(ui::colors::TEXT_SECONDARY()));
    if (fg_score_max_)
        fg_score_max_->setStyleSheet(
            QString("color: %1; font-size: 9px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
    if (fg_sentiment_)
        fg_sentiment_->setStyleSheet(
            QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 0.5px; background: transparent;")
                .arg(ui::colors::TEXT_SECONDARY()));

    // ── Market Breadth rows ──
    auto style_breadth = [](BreadthRow& row) {
        if (row.name)
            row.name->setStyleSheet(QString("color: %1; font-size: 9px; font-weight: bold; background: transparent;")
                                        .arg(ui::colors::TEXT_SECONDARY()));
        if (row.adv)
            row.adv->setStyleSheet(
                QString("color: %1; font-size: 8px; background: transparent;").arg(ui::colors::POSITIVE()));
        if (row.slash)
            row.slash->setStyleSheet(
                QString("color: %1; font-size: 8px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
        if (row.dec)
            row.dec->setStyleSheet(
                QString("color: %1; font-size: 8px; background: transparent;").arg(ui::colors::NEGATIVE()));
        if (row.green)
            row.green->setStyleSheet(QString("background: %1; border-radius: 0;").arg(ui::colors::POSITIVE()));
        if (row.red)
            row.red->setStyleSheet(QString("background: %1; border-radius: 0;").arg(ui::colors::NEGATIVE()));
    };

    style_breadth(basket_row_);

    // ── Global Snapshot rows ──
    // Each stat row has a fixed val_color that maps to a specific token.
    // Re-resolve them here so theme changes take effect.
    struct StatDef {
        StatRow& row;
        QString val_color;
    };
    StatDef stat_defs[] = {
        {vix_row_, ui::colors::WARNING()},  {us10y_row_, ui::colors::CYAN()}, {dxy_row_, ui::colors::CYAN()},
        {gold_row_, ui::colors::WARNING()}, {oil_row_, ui::colors::CYAN()},   {btc_row_, ui::colors::AMBER()},
    };

    for (auto& sd : stat_defs) {
        if (sd.row.container)
            sd.row.container->setStyleSheet(QString("border-bottom: 1px solid %1;").arg(ui::colors::BORDER_DIM()));
        if (sd.row.name_lbl)
            sd.row.name_lbl->setStyleSheet(
                QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 0.3px; background: transparent;")
                    .arg(ui::colors::TEXT_SECONDARY()));
        if (sd.row.val)
            sd.row.val->setStyleSheet(
                QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;").arg(sd.val_color));
        if (sd.row.chg)
            sd.row.chg->setStyleSheet(QString("color: %1; font-size: 8px; font-weight: bold; background: transparent;")
                                          .arg(ui::colors::TEXT_SECONDARY()));
    }

    // ── Market Hours rows ──
    for (auto& hr : hours_rows_) {
        if (hr.container)
            hr.container->setStyleSheet(QString("border-bottom: 1px solid %1;").arg(ui::colors::BORDER_DIM()));
        if (hr.name_lbl)
            hr.name_lbl->setStyleSheet(QString("color: %1; font-size: 9px; font-weight: bold; background: transparent;")
                                           .arg(ui::colors::TEXT_SECONDARY()));
        // dot and status colors are data-driven (open/closed/pre) —
        // refresh_market_hours() handles those, call it to re-resolve tokens.
    }

    // Re-resolve data-driven status dot/label colors.
    refresh_market_hours();

    // Mover rows are persistent now: restyle the static (symbol/volume/border)
    // bits here; the sign-driven arrow/change colors are re-resolved by
    // rebuild_movers_from_cache() below.
    for (auto& row : gainer_rows_)
        style_mover_row(row);
    for (auto& row : loser_rows_)
        style_mover_row(row);

    // Re-run the in-place updates so persistent rows pick up new theme colors.
    if (isVisible()) {
        rebuild_breadth_from_cache();
        rebuild_movers_from_cache();
        rebuild_snapshot_from_cache();
    }
}

void MarketPulsePanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    refresh_theme();
    if (!hub_active_)
        hub_subscribe_all();
    refresh_market_hours();
    hours_timer_->start();
    fetch_movers();
    movers_timer_->start();
}

void MarketPulsePanel::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    hub_unsubscribe_all();
    hours_timer_->stop();
    movers_timer_->stop();
}

// ── Header ───────────────────────────────────────────────────────────────────

QWidget* MarketPulsePanel::build_header() {
    header_bar_ = new QWidget(this);
    header_bar_->setFixedHeight(30);
    // Styling applied by refresh_theme()

    auto* hl = new QHBoxLayout(header_bar_);
    hl->setContentsMargins(12, 0, 12, 0);

    header_icon_ = new QLabel(QChar(0x25C8));
    hl->addWidget(header_icon_);

    header_title_ = new QLabel("MARKET PULSE");
    hl->addWidget(header_title_);
    hl->addStretch();

    header_live_dot_ = new QLabel;
    header_live_dot_->setFixedSize(6, 6);
    hl->addWidget(header_live_dot_);

    return header_bar_;
}

QWidget* MarketPulsePanel::build_section_header(const QString& title, const QString& icon_char, const QString& color) {
    Q_UNUSED(color)
    auto* w = new QWidget(this);
    w->setFixedHeight(26);
    // Styling applied by refresh_theme() via style_section lambda

    auto* hl = new QHBoxLayout(w);
    hl->setContentsMargins(12, 0, 12, 0);

    auto* icn = new QLabel(icon_char);
    hl->addWidget(icn);

    auto* lbl = new QLabel(title);
    hl->addWidget(lbl);
    hl->addStretch();

    // Store pointers into the corresponding SectionHeader member
    // so refresh_theme() can re-apply styles later.
    SectionHeader* sh = nullptr;
    if (title == "BASKET BREADTH")
        sh = &sh_breadth_;
    else if (title == "TOP GAINERS")
        sh = &sh_gainers_;
    else if (title == "TOP LOSERS")
        sh = &sh_losers_;
    else if (title == "GLOBAL SNAPSHOT")
        sh = &sh_snapshot_;
    else if (title == "MARKET HOURS")
        sh = &sh_hours_;

    if (sh) {
        sh->container = w;
        sh->icon = icn;
        sh->title = lbl;
    }

    return w;
}

// ── Fear & Greed ─────────────────────────────────────────────────────────────

QWidget* MarketPulsePanel::build_fear_greed_section() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 8, 12, 8);
    vl->setSpacing(6);

    // Header row
    auto* header_row = new QWidget(this);
    auto* hrl = new QHBoxLayout(header_row);
    hrl->setContentsMargins(0, 0, 0, 0);

    // NOT CNN's Fear & Greed Index. This is finterm's own breadth-and-VIX
    // gauge (advancing vs declining share, adjusted by VIX bands). It may
    // well be a useful reading — but it is not the published index whose
    // name it used to carry, and a user comparing the two would find they
    // disagree with no explanation.
    fg_header_label_ = new QLabel(QString("MOOD (%1-STOCK BASKET + VIX)").arg(breadth_basket_size()));
    fg_header_label_->setToolTip(
        QString("finterm's own gauge: advancing vs declining share of a fixed %1-stock watch basket, "
                "adjusted by VIX bands. Not CNN's Fear & Greed Index and not market-wide breadth.")
            .arg(breadth_basket_size()));
    hrl->addWidget(fg_header_label_);
    hrl->addStretch();

    fg_gauge_icon_ = new QLabel(QChar(0x25CE));
    hrl->addWidget(fg_gauge_icon_);

    vl->addWidget(header_row);

    // Gradient bar (red→green, themed)
    fg_gradient_bar_ = new QFrame;
    fg_gradient_bar_->setFixedHeight(6);
    vl->addWidget(fg_gradient_bar_);

    // Score row
    auto* score_row = new QWidget(this);
    auto* srl = new QHBoxLayout(score_row);
    srl->setContentsMargins(0, 0, 0, 0);

    fg_score_val_ = new QLabel("--");
    srl->addWidget(fg_score_val_);

    fg_score_max_ = new QLabel("/100");
    srl->addWidget(fg_score_max_);

    srl->addStretch();

    fg_sentiment_ = new QLabel("LOADING...");
    srl->addWidget(fg_sentiment_);
    // All styling applied by refresh_theme()

    vl->addWidget(score_row);
    return w;
}

// ── Market Breadth ────────────────────────────────────────────────────────────

QWidget* MarketPulsePanel::build_breadth_section() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    vl->addWidget(build_section_header("BASKET BREADTH", QChar(0x2593), ui::colors::CYAN()));

    auto* bars = new QWidget(this);
    auto* bl = new QVBoxLayout(bars);
    bl->setContentsMargins(0, 6, 0, 6);
    bl->setSpacing(0);

    auto make_row = [&](const QString& name, BreadthRow& row) {
        auto* rw = new QWidget(this);
        auto* rl = new QVBoxLayout(rw);
        rl->setContentsMargins(12, 4, 12, 4);
        rl->setSpacing(3);

        auto* top = new QWidget(this);
        auto* tl = new QHBoxLayout(top);
        tl->setContentsMargins(0, 0, 0, 0);

        row.name = new QLabel(name);
        tl->addWidget(row.name);
        tl->addStretch();

        row.adv = new QLabel("--");
        tl->addWidget(row.adv);

        row.slash = new QLabel("/");
        tl->addWidget(row.slash);

        row.dec = new QLabel("--");
        tl->addWidget(row.dec);
        // All label styling applied by refresh_theme() via style_breadth lambda

        rl->addWidget(top);

        auto* bar_container = new QWidget(this);
        bar_container->setFixedHeight(4);
        auto* bar_layout = new QHBoxLayout(bar_container);
        bar_layout->setContentsMargins(0, 0, 0, 0);
        bar_layout->setSpacing(0);

        auto* green = new QFrame;
        bar_layout->addWidget(green, 1);
        row.green = green;

        auto* red = new QFrame;
        bar_layout->addWidget(red, 1);
        row.red = red;
        // Bar styling applied by refresh_theme() via style_breadth lambda

        rl->addWidget(bar_container);
        bl->addWidget(rw);
    };

    // One honest row: advancers/decliners within the fixed watch basket.
    // There is no real exchange-wide advance/decline source wired in, so no
    // NYSE/NASDAQ/S&P labels.
    make_row(QString("WATCH BASKET (%1 STOCKS)").arg(breadth_basket_size()), basket_row_);
    if (basket_row_.name)
        basket_row_.name->setToolTip("Advancing (>+0.3%) / declining (<-0.3%) count within a fixed basket of large "
                                     "US stocks. Not exchange-wide breadth.");

    vl->addWidget(bars);
    return w;
}

// ── Top Movers ────────────────────────────────────────────────────────────────

// Build N persistent mover rows once. Each row is later updated in place by
// update_mover_row() instead of being torn down + recreated every tick.
void MarketPulsePanel::make_mover_rows(QVBoxLayout* layout, QVector<MoverRow>& rows, int n) {
    for (int i = 0; i < n; ++i) {
        MoverRow row;
        row.container = new QWidget(this);

        auto* hl = new QHBoxLayout(row.container);
        hl->setContentsMargins(12, 5, 12, 5);
        hl->setSpacing(4);

        // Neutral placeholder until real screener data arrives — never a
        // green "+0.00%" that reads like a real quote.
        row.sym = new QLabel(ui::formatting::placeholder());
        hl->addWidget(row.sym);
        hl->addStretch();

        row.arrow = new QLabel(QString());
        hl->addWidget(row.arrow);

        row.chg = new QLabel(ui::formatting::placeholder());
        hl->addWidget(row.chg);

        row.vol = new QLabel("");
        hl->addWidget(row.vol);

        style_mover_row(row);
        // The row's symbol changes every tick; update_mover_row retargets it.
        MarketChartDialog::make_clickable(row.container);
        layout->addWidget(row.container);
        rows.append(row);
    }
}

// Re-apply token-based styling (colors depend on the current sign, so this is
// also called from update_mover_row()). Placeholder rows stay neutral.
void MarketPulsePanel::style_mover_row(MoverRow& row) {
    if (!row.container)
        return;
    row.container->setStyleSheet(QString("border-bottom: 1px solid %1;").arg(ui::colors::BORDER_DIM()));
    row.sym->setStyleSheet(QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;")
                               .arg(ui::colors::TEXT_PRIMARY()));
    row.vol->setStyleSheet(
        QString("color: %1; font-size: 8px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
    // Neutral colour for the placeholder state; update_mover_row() re-colours
    // by sign once real data is shown.
    row.chg->setStyleSheet(QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;")
                               .arg(ui::colors::TEXT_SECONDARY()));
}

// Update a row's text + sign-driven colors in place.
void MarketPulsePanel::update_mover_row(MoverRow& row, const services::QuoteData& q) {
    const bool positive = q.change_pct >= 0;
    const QString col = positive ? ui::colors::POSITIVE() : ui::colors::NEGATIVE();
    row.sym->setText(q.symbol);
    MarketChartDialog::set_target(row.container, q.symbol, q.name);
    row.arrow->setText(positive ? QChar(0x25B2) : QChar(0x25BC));
    row.arrow->setStyleSheet(
        QString("color: %1; font-size: 8px; background: transparent;").arg(col));
    row.chg->setText(ui::formatting::format_percent(q.change_pct, 2, true));
    row.chg->setStyleSheet(
        QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;").arg(col));
    const QString volume = ui::formatting::format_compact_volume(q.volume);
    row.vol->setText(volume.isEmpty() ? QString() : QString("VOL: %1").arg(volume));
    row.container->show();
}

QWidget* MarketPulsePanel::build_gainers_section() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    vl->addWidget(build_section_header("TOP GAINERS", QChar(0x2191), ui::colors::POSITIVE()));

    auto* rows_w = new QWidget(this);
    gainers_layout_ = new QVBoxLayout(rows_w);
    gainers_layout_->setContentsMargins(0, 0, 0, 0);
    gainers_layout_->setSpacing(0);

    // Persistent placeholder rows, updated in place once data arrives.
    make_mover_rows(gainers_layout_, gainer_rows_, 3);

    vl->addWidget(rows_w);
    return w;
}

QWidget* MarketPulsePanel::build_losers_section() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    vl->addWidget(build_section_header("TOP LOSERS", QChar(0x2193), ui::colors::NEGATIVE()));

    auto* rows_w = new QWidget(this);
    losers_layout_ = new QVBoxLayout(rows_w);
    losers_layout_->setContentsMargins(0, 0, 0, 0);
    losers_layout_->setSpacing(0);

    make_mover_rows(losers_layout_, loser_rows_, 3);

    vl->addWidget(rows_w);
    return w;
}

// ── Global Snapshot ───────────────────────────────────────────────────────────

QWidget* MarketPulsePanel::build_stat_row(const QString& label, const QString& value, const QString& change,
                                          const QString& color) {
    auto* w = new QWidget(this);
    w->setStyleSheet(QString("border-bottom: 1px solid %1;").arg(ui::colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(w);
    hl->setContentsMargins(12, 4, 12, 4);

    auto* lbl = new QLabel(label);
    lbl->setStyleSheet(
        QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 0.3px; background: transparent;")
            .arg(ui::colors::TEXT_SECONDARY()));
    hl->addWidget(lbl);
    hl->addStretch();

    auto* val = new QLabel(value);
    val->setStyleSheet(QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;").arg(color));
    hl->addWidget(val);

    if (!change.isEmpty()) {
        bool positive = change.startsWith('+');
        auto* chg = new QLabel(change);
        chg->setStyleSheet(QString("color: %1; font-size: 8px; font-weight: bold; background: transparent;")
                               .arg(positive                 ? ui::colors::POSITIVE()
                                    : change.startsWith('-') ? ui::colors::NEGATIVE()
                                                             : ui::colors::TEXT_SECONDARY()));
        hl->addWidget(chg);
    }

    return w;
}

QWidget* MarketPulsePanel::build_global_snapshot_section() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    vl->addWidget(build_section_header("GLOBAL SNAPSHOT", QChar(0x25CB), ui::colors::INFO()));

    // Build stat rows — colors are applied by refresh_theme()
    struct RowDef {
        const char* label;
        StatRow& row;
    };
    RowDef defs[] = {
        {"VIX", vix_row_},     {"US 10Y", us10y_row_}, {"DXY", dxy_row_},
        {"GOLD", gold_row_},   {"OIL WTI", oil_row_},  {"BTC", btc_row_},
    };

    for (auto& d : defs) {
        auto* rw = new QWidget(this);
        auto* hl = new QHBoxLayout(rw);
        hl->setContentsMargins(12, 4, 12, 4);

        auto* lbl = new QLabel(d.label);
        hl->addWidget(lbl);
        hl->addStretch();

        d.row.container = rw;
        d.row.name_lbl = lbl;

        d.row.val = new QLabel("--");
        hl->addWidget(d.row.val);

        d.row.chg = new QLabel("");
        hl->addWidget(d.row.chg);
        // All styling applied by refresh_theme()

        vl->addWidget(rw);
    }

    return w;
}

// ── Market Hours ─────────────────────────────────────────────────────────────

// Regular sessions in each exchange's own IANA time zone, so DST and
// half-hour opens are exact. Times are local HHMM; a second session models
// the Asian lunch break. Exchange holiday calendars are NOT consulted.
namespace {
struct ExchangeSession {
    const char* region;
    const char* tz;
    int pre_open; // -1 = no pre-market shown
    int open1, close1;
    int open2, close2; // -1 = single session
};
constexpr ExchangeSession kExchangeSessions[] = {
    {"US", "America/New_York", 400, 930, 1600, -1, -1},  // NYSE/NASDAQ; pre-market from 04:00
    {"UK", "Europe/London", 750, 800, 1630, -1, -1},     // LSE; opening auction 07:50
    {"DE", "Europe/Berlin", -1, 900, 1730, -1, -1},      // XETRA
    {"JP", "Asia/Tokyo", -1, 900, 1130, 1230, 1530},     // TSE (close 15:30 since Nov 2024)
    {"CN", "Asia/Shanghai", 915, 930, 1130, 1300, 1500}, // SSE; call auction from 09:15
};

const ExchangeSession* session_for(const QString& region) {
    for (const auto& s : kExchangeSessions)
        if (region == QLatin1String(s.region))
            return &s;
    return nullptr;
}

QString hhmm(int t) {
    return QStringLiteral("%1:%2").arg(t / 100, 2, 10, QLatin1Char('0')).arg(t % 100, 2, 10, QLatin1Char('0'));
}

QString session_tooltip(const ExchangeSession& s) {
    QString hours = hhmm(s.open1) + "–" + hhmm(s.close1);
    if (s.open2 >= 0)
        hours += ", " + hhmm(s.open2) + "–" + hhmm(s.close2);
    QString tip = QStringLiteral("Regular session %1 (%2), Mon–Fri").arg(hours, QString::fromLatin1(s.tz));
    if (s.pre_open >= 0)
        tip += QStringLiteral("\nPRE from %1").arg(hhmm(s.pre_open));
    tip += QStringLiteral("\nExchange holidays are not checked.");
    return tip;
}
} // namespace

QWidget* MarketPulsePanel::build_market_hours_section() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    vl->addWidget(build_section_header("MARKET HOURS", QChar(0x26A1), ui::colors::WARNING()));

    auto* content = new QWidget(this);
    auto* cl = new QVBoxLayout(content);
    cl->setContentsMargins(12, 6, 12, 6);
    cl->setSpacing(0);

    struct ExDef {
        const char* name;
        const char* region;
    };
    ExDef exchanges[] = {
        {"NYSE/NASDAQ", "US"}, {"LSE", "UK"}, {"XETRA (FRANKFURT)", "DE"}, {"TSE (TOKYO)", "JP"}, {"SSE (SHANGHAI)", "CN"},
    };

    for (auto& ex : exchanges) {
        auto* row = new QWidget(this);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 3, 0, 3);

        auto* name = new QLabel(ex.name);
        rl->addWidget(name);
        rl->addStretch();

        HoursRow hr;
        hr.container = row;
        hr.name_lbl = name;
        hr.region = ex.region;

        hr.dot = new QLabel;
        hr.dot->setFixedSize(5, 5);
        rl->addWidget(hr.dot);

        hr.status = new QLabel;
        rl->addWidget(hr.status);
        if (const auto* sess = session_for(hr.region))
            row->setToolTip(session_tooltip(*sess));
        // All styling applied by refresh_theme() + refresh_market_hours()

        hours_rows_.append(hr);
        cl->addWidget(row);
    }

    vl->addWidget(content);
    return w;
}

// ── Market status helper ─────────────────────────────────────────────────────

QString MarketPulsePanel::market_status(const QString& region) {
    const ExchangeSession* s = session_for(region);
    if (!s)
        return "CLOSED";
    const QTimeZone tz(QByteArray(s->tz));
    if (!tz.isValid())
        return "N/A"; // no tz database entry: don't guess
    const QDateTime local = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    if (local.date().dayOfWeek() >= 6) // Sat/Sun in the exchange's own time zone
        return "CLOSED";

    const int t = local.time().hour() * 100 + local.time().minute();
    if ((t >= s->open1 && t < s->close1) || (s->open2 >= 0 && t >= s->open2 && t < s->close2))
        return "OPEN";
    if (s->open2 >= 0 && t >= s->close1 && t < s->open2)
        return "BREAK"; // lunch break between sessions
    if (s->pre_open >= 0 && t >= s->pre_open && t < s->open1)
        return "PRE";
    return "CLOSED";
}

// ── Refresh ───────────────────────────────────────────────────────────────────

void MarketPulsePanel::refresh_market_hours() {
    for (auto& hr : hours_rows_) {
        QString status = market_status(hr.region);
        QString color = (status == "OPEN")                      ? ui::colors::POSITIVE()
                        : (status == "PRE" || status == "BREAK") ? ui::colors::WARNING()
                                                                 : ui::colors::NEGATIVE();
        hr.dot->setStyleSheet(QString("background: %1; border-radius: 2px;").arg(color));
        hr.status->setText(status);
        hr.status->setStyleSheet(
            QString("color: %1; font-size: 8px; font-weight: bold; background: transparent;").arg(color));
    }
}


void MarketPulsePanel::rebuild_breadth_from_cache() {
    if (breadth_cache_.isEmpty())
        return;

    // Advancers/decliners within the fixed watch basket (not exchange-wide).
    double vix = -1;
    int bullish = 0, bearish = 0, neutral_count = 0;

    for (const auto& sym : kBreadthSymbols) {
        if (!breadth_cache_.contains(sym))
            continue;
        const auto& q = breadth_cache_.value(sym);
        if (q.symbol == "^VIX") {
            vix = q.price;
            continue;
        }
        if (!std::isfinite(q.change_pct))
            continue;  // unknown change — not an advancer, decliner or "neutral"
        if (q.change_pct > 0.3) {
            ++bullish;
        } else if (q.change_pct < -0.3) {
            ++bearish;
        } else {
            ++neutral_count;
        }
    }

    auto update_row = [](MarketPulsePanel::BreadthRow& row, int adv, int dec) {
        if (!row.adv)
            return;
        row.adv->setText(QString::number(adv));
        row.dec->setText(QString::number(dec));
        int total = adv + dec;
        if (total == 0)
            total = 1;
        int adv_pct = static_cast<int>((double(adv) / total) * 100);
        auto* layout = qobject_cast<QHBoxLayout*>(row.green->parentWidget()->layout());
        if (layout) {
            layout->setStretch(0, adv_pct);
            layout->setStretch(1, 100 - adv_pct);
        }
    };
    update_row(basket_row_, bullish, bearish);

    // ── Fear & Greed score ──
    int total_stocks = bullish + bearish + neutral_count;
    if (total_stocks == 0)
        total_stocks = 1;
    int score = 50 + static_cast<int>(((bullish - bearish) / static_cast<double>(total_stocks)) * 50);
    if (vix > 0) {
        if (vix > 30)
            score -= 20;
        else if (vix > 25)
            score -= 10;
        else if (vix < 15)
            score += 10;
    }
    score = qBound(0, score, 100);

    QString sentiment_text, sentiment_color;
    if (score <= 20) {
        sentiment_text = "EXTREME FEAR";
        sentiment_color = ui::colors::NEGATIVE();
    } else if (score <= 40) {
        sentiment_text = "FEAR";
        sentiment_color = ui::colors::WARNING();
    } else if (score <= 60) {
        sentiment_text = "NEUTRAL";
        sentiment_color = ui::colors::WARNING();
    } else if (score <= 80) {
        sentiment_text = "GREED";
        sentiment_color = ui::colors::POSITIVE();
    } else {
        sentiment_text = "EXTREME GREED";
        sentiment_color = ui::colors::POSITIVE();
    }

    if (fg_score_val_) {
        fg_score_val_->setText(QString::number(score));
        fg_score_val_->setStyleSheet(
            QString("color: %1; font-size: 18px; font-weight: bold; background: transparent;").arg(sentiment_color));
    }
    if (fg_sentiment_) {
        fg_sentiment_->setText(sentiment_text);
        fg_sentiment_->setStyleSheet(
            QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 0.5px; background: transparent;")
                .arg(sentiment_color));
    }
}

void MarketPulsePanel::fetch_movers() {
    QPointer<MarketPulsePanel> self = this;
    services::MarketDataService::instance().fetch_top_movers(
        kTopMoversFetchCount, [self](bool ok, services::MarketDataService::TopMovers tm) {
            if (!self)
                return;
            if (!ok) {
                // Keep movers already on screen; otherwise say so honestly.
                if (!self->movers_loaded_) {
                    for (auto* rows : {&self->gainer_rows_, &self->loser_rows_}) {
                        for (int i = 0; i < rows->size(); ++i) {
                            auto& row = (*rows)[i];
                            if (!row.sym)
                                continue;
                            row.sym->setText(i == 0 ? QStringLiteral("UNAVAILABLE") : ui::formatting::placeholder());
                            MarketChartDialog::set_target(row.container, QString(), QString());
                        }
                    }
                }
                return;
            }
            self->movers_data_ = std::move(tm);
            self->movers_loaded_ = true;
            self->movers_coalesce_->start();
        });
}

void MarketPulsePanel::rebuild_movers_from_cache() {
    if (!movers_loaded_ || gainer_rows_.isEmpty() || loser_rows_.isEmpty())
        return;

    // Real market-wide screener results (yfinance day_gainers / day_losers),
    // already ranked by the source. Show the top N with the correct sign;
    // unused slots are hidden.
    auto fill = [this](QVector<MoverRow>& rows, const QVector<services::QuoteData>& src, bool want_positive) {
        int added = 0;
        for (const auto& q : src) {
            if (added >= rows.size())
                break;
            if (!std::isfinite(q.change_pct) || (want_positive ? q.change_pct <= 0 : q.change_pct >= 0))
                continue;
            update_mover_row(rows[added], q);
            ++added;
        }
        for (int i = added; i < rows.size(); ++i)
            if (rows[i].container)
                rows[i].container->hide();
    };
    fill(gainer_rows_, movers_data_.gainers, true);
    fill(loser_rows_, movers_data_.losers, false);
}

void MarketPulsePanel::rebuild_snapshot_from_cache() {
    auto fmt_price = [](const services::QuoteData& q) -> QString {
        if (q.price >= 1000)
            return QString("$%1K").arg(q.price / 1000.0, 0, 'f', 1);
        return QString("%1").arg(q.price, 0, 'f', 2);
    };
    auto fmt_chg = [](const services::QuoteData& q) -> QString {
        return ui::formatting::format_percent(q.change_pct, 2, true);
    };
    auto update_stat = [&](const QString& sym, StatRow& row) {
        if (!snapshot_cache_.contains(sym) || !row.val)
            return;
        const auto& q = snapshot_cache_.value(sym);
        // Idempotent: re-installing the same filter is a no-op in Qt.
        MarketChartDialog::make_clickable(row.container);
        MarketChartDialog::set_target(row.container, sym, row.name_lbl ? row.name_lbl->text() : QString());
        row.val->setText(fmt_price(q));
        row.chg->setText(fmt_chg(q));
        QString chg_color = ui::change_color(q.change_pct);
        row.chg->setStyleSheet(
            QString("color: %1; font-size: 8px; font-weight: bold; background: transparent;").arg(chg_color));
    };
    update_stat("^VIX", vix_row_);
    update_stat("^TNX", us10y_row_);
    update_stat("DX-Y.NYB", dxy_row_);
    update_stat("GC=F", gold_row_);
    update_stat("CL=F", oil_row_);
    update_stat("BTC-USD", btc_row_);
}

void MarketPulsePanel::hub_subscribe_all() {
    auto& hub = datahub::DataHub::instance();

    // Union of the breadth + snapshot symbol sets — but dispatch per-set so each cache
    // only holds its own universe (keeps rebuild_* loops cheap).
    QSet<QString> all_syms;
    for (const auto& s : kBreadthSymbols)
        all_syms.insert(s);
    for (const auto& s : kSnapshotSymbols)
        all_syms.insert(s);

    for (const QString& sym : all_syms) {
        const QString topic = QStringLiteral("market:quote:") + sym;
        const bool in_breadth = kBreadthSymbols.contains(sym);
        const bool in_snapshot = kSnapshotSymbols.contains(sym);

        hub.subscribe(this, topic, [this, sym, in_breadth, in_snapshot](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            const auto q = v.value<services::QuoteData>();
            if (in_breadth) {
                breadth_cache_.insert(sym, q);
                breadth_coalesce_->start();
            }
            if (in_snapshot) {
                snapshot_cache_.insert(sym, q);
                snapshot_coalesce_->start();
            }
        });
    }
    hub_active_ = true;
}

void MarketPulsePanel::hub_unsubscribe_all() {
    if (!hub_active_)
        return;
    datahub::DataHub::instance().unsubscribe(this);
    hub_active_ = false;
}


void MarketPulsePanel::refresh_data() {
    // Hub owns cadence. Force a kick so consumers see data immediately
    // (e.g., on theme-triggered refresh while visible).
    auto& hub = datahub::DataHub::instance();
    QStringList topics;
    QSet<QString> seen;
    auto push = [&](const QStringList& syms) {
        for (const auto& s : syms) {
            if (seen.contains(s))
                continue;
            seen.insert(s);
            topics.append(QStringLiteral("market:quote:") + s);
        }
    };
    push(kBreadthSymbols);
    push(kSnapshotSymbols);
    hub.request(topics, /*force=*/true);  // user-triggered refresh
    fetch_movers();
}

} // namespace fincept::screens


#include "screens/alpha_arena/AlphaArenaScreen.h"

#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "core/session/ScreenStateManager.h"
#include "services/alpha_arena/AlphaArenaService.h"
#include "storage/repositories/LlmConfigRepository.h"
#include "storage/repositories/LlmProfileRepository.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLocale>
#include <QPointer>
#include <QScrollArea>
#include <QShowEvent>
#include <QUrl>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

// ── Stylesheet ──────────────────────────────────────────────────────────────

namespace {
using namespace finterm::ui;

static const QString kStyle =
    // Placeholder numbers must be CONTIGUOUS from %1: Qt assigns the Nth
    // argument to the Nth-LOWEST placeholder present, not to %N. This block
    // passed a BORDER_BRIGHT for a %9 the string never used, so every colour
    // from %10 up was shifted down one and the final one was discarded
    // ("QString::arg: Argument missing" at startup).
    QStringLiteral("#aaScreen { background: %1; }"

                   "#aaHeader { background: %2; border-bottom: 2px solid %3; }"
                   "#aaHeaderTitle { color: %4; font-size: 12px; font-weight: 700; background: transparent; }"
                   "#aaHeaderSub { color: %5; font-size: 9px; letter-spacing: 0.5px; background: transparent; }"

                   "#aaStatusBadge { font-size: 8px; font-weight: 700; padding: 2px 6px; }"
                   "#aaCycleLabel { color: %12; font-size: 11px; font-weight: 700; background: transparent; }"
                   "#aaPriceLabel { color: %6; font-size: 11px; font-weight: 700; "
                   "  background: rgba(22,163,74,0.15); padding: 2px 8px; }"

                   "#aaCreatePanel { background: %7; border: 1px solid %8; }"
                   "#aaCreateHeader { background: %2; border-bottom: 1px solid %8; }"
                   "#aaCreateTitle { color: %4; font-size: 11px; font-weight: 700; background: transparent; }"

                   "#aaLabel { color: %5; font-size: 9px; font-weight: 700; "
                   "  letter-spacing: 0.5px; background: transparent; }"
                   "QLineEdit { background: %1; color: %4; border: 1px solid %8; "
                   "  padding: 4px 8px; font-size: 11px; }"
                   "QComboBox { background: %1; color: %4; border: 1px solid %8; "
                   "  padding: 4px 8px; font-size: 11px; }"
                   "QComboBox::drop-down { border: none; width: 16px; }"
                   "QComboBox QAbstractItemView { background: %2; color: %4; border: 1px solid %8; "
                   "  selection-background-color: %3; }"
                   "QDoubleSpinBox, QSpinBox { background: %1; color: %4; border: 1px solid %8; "
                   "  padding: 4px 8px; font-size: 11px; }"
                   "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button, "
                   "QSpinBox::up-button, QSpinBox::down-button { width: 0; }"

                   "#aaCreateBtn { background: %3; color: %1; border: none; padding: 6px 16px; "
                   "  font-size: 10px; font-weight: 700; }"
                   "#aaCreateBtn:hover { background: #FF8800; }"
                   "#aaCreateBtn:disabled { background: %9; color: %10; }"

                   "#aaControlsBar { background: %2; border-bottom: 1px solid %8; }"
                   "#aaRunBtn { background: %3; color: %1; border: none; padding: 5px 14px; "
                   "  font-size: 9px; font-weight: 700; }"
                   "#aaRunBtn:hover { background: #FF8800; }"
                   "#aaRunBtn:disabled { background: %9; color: %10; }"
                   "#aaAutoBtn { background: %7; color: %5; border: 1px solid %8; "
                   "  padding: 5px 14px; font-size: 9px; font-weight: 700; }"
                   "#aaAutoBtn[running=\"true\"] { color: %13; border-color: %13; }"
                   "#aaResetBtn { background: %7; color: %5; border: 1px solid %8; "
                   "  padding: 5px 14px; font-size: 9px; font-weight: 700; }"
                   "#aaIntervalLabel { color: %5; font-size: 9px; background: transparent; }"

                   "#aaLeaderboard { background: %7; border: 1px solid %8; }"
                   "#aaLeaderboardHeader { background: %2; border-bottom: 1px solid %8; }"
                   "#aaLeaderboardTitle { color: %4; font-size: 11px; font-weight: 700; background: transparent; }"

                   "QTableWidget { background: %1; color: %4; border: none; gridline-color: %8; "
                   "  font-size: 11px; }"
                   "QTableWidget::item { padding: 2px 6px; border-bottom: 1px solid %8; }"
                   "QHeaderView::section { background: %2; color: %5; border: none; "
                   "  border-bottom: 1px solid %8; border-right: 1px solid %8; "
                   "  padding: 4px 6px; font-size: 10px; font-weight: 700; }"

                   "#aaRightPanel { background: %7; border-left: 1px solid %8; }"
                   "#aaRightTabBtn { background: transparent; color: %5; border: none; "
                   "  font-size: 9px; font-weight: 700; padding: 6px 10px; "
                   "  border-bottom: 2px solid transparent; }"
                   "#aaRightTabBtn:hover { color: %4; }"
                   "#aaRightTabBtn[active=\"true\"] { color: %4; border-bottom-color: %3; }"

                   // No colour on ::item: it would win over the items' own.
                   "#aaDecisionList { background: %1; color: %5; border: none; outline: none; font-size: 11px; }"
                   "#aaDecisionList::item { padding: 6px 8px; border-bottom: 1px solid %8; }"
                   "#aaDecisionList::item:selected { background: rgba(217,119,6,0.1); }"

                   "#aaModelList { background: %1; border: none; outline: none; font-size: 11px; }"
                   "#aaModelList::item { color: %5; padding: 4px 8px; border-bottom: 1px solid %8; }"
                   "#aaModelList::item:selected { color: %3; background: rgba(217,119,6,0.1); }"
                   "#aaModelList::item:hover { color: %4; background: %11; }"

                   "#aaPastPanel { background: %7; border: 1px solid %8; }"
                   "#aaPastList { background: %1; border: none; font-size: 11px; }"
                   "#aaPastList::item { color: %5; padding: 6px 8px; border-bottom: 1px solid %8; }"
                   "#aaPastList::item:hover { color: %4; background: %11; }"

                   "#aaStatusBar { background: %2; border-top: 1px solid %8; }"
                   "#aaStatusText { color: %5; font-size: 9px; background: transparent; }"
                   "#aaStatusHighlight { color: %12; font-size: 9px; background: transparent; }"

                   "QSplitter::handle { background: %8; }"
                   "QScrollBar:vertical { background: %1; width: 6px; }"
                   "QScrollBar::handle:vertical { background: %8; min-height: 20px; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(colors::BG_BASE())        // %1
        .arg(colors::BG_RAISED())      // %2
        .arg(colors::AMBER())          // %3
        .arg(colors::TEXT_PRIMARY())   // %4
        .arg(colors::TEXT_SECONDARY()) // %5
        .arg(colors::POSITIVE())       // %6
        .arg(colors::BG_SURFACE())     // %7
        .arg(colors::BORDER_DIM())     // %8
        .arg(colors::AMBER_DIM())      // %9
        .arg(colors::TEXT_SECONDARY()) // %10
        .arg(colors::BG_HOVER())       // %11
        .arg(colors::CYAN())           // %12
        .arg(colors::NEGATIVE())       // %13
    ;

// ── Trading symbols ─────────────────────────────────────────────────────────

static const QStringList CRYPTO_SYMBOLS = {
    "BTC/USD",  "ETH/USD",   "SOL/USD",  "XRP/USD", "DOGE/USD", "ADA/USD", "AVAX/USD", "DOT/USD",
    "LINK/USD", "POL/USD", "ATOM/USD", "UNI/USD", "LTC/USD",  "BCH/USD", "APT/USD",  "ARB/USD",
    "OP/USD",   "FIL/USD",   "NEAR/USD", "INJ/USD", "SUI/USD",  "SEI/USD", "TIA/USD",  "RENDER/USD",
    "FET/USD",  "AAVE/USD",  "SKY/USD",  "CRV/USD", "PEPE/USD", "WIF/USD",
};

static const QStringList EXCHANGES = {"kraken", "coinbase"}; // scripts/alpha_arena/market.py

static const QStringList COMP_MODES = {"baseline", "monk", "situational", "max_leverage"};

static constexpr const char* kGridProvider = "rules"; // traders.py

enum RightTab { DecisionsTab, MetricsTab, SentimentTab, GridTab, ExchangesTab };

// A price with the digits it needs: PEPE trades at $0.0000034.
static QString price_text(double p) {
    if (!(p > 0))
        return QStringLiteral("—");
    const int digits = p >= 100 ? 2 : p >= 1 ? 4 : qMin(12, 3 - int(std::floor(std::log10(p))));
    return QStringLiteral("$") + QLocale::c().toString(p, 'f', digits);
}

static QString percent_text(const QJsonValue& v, bool signed_ = true) {
    if (!v.isDouble())
        return QStringLiteral("—");
    return QString("%1%2%").arg(signed_ && v.toDouble() >= 0 ? "+" : "").arg(v.toDouble(), 0, 'f', 2);
}

static QString money_text(const QJsonValue& v) {
    return v.isDouble() ? QString("%1$%2").arg(v.toDouble() < 0 ? "-" : "").arg(qAbs(v.toDouble()), 0, 'f', 2)
                        : QStringLiteral("—");
}

static QTableWidgetItem* cell(const QString& text, const QString& color = {}, Qt::Alignment align = Qt::AlignRight) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align | Qt::AlignVCenter);
    if (!color.isEmpty())
        item->setForeground(QColor(color));
    return item;
}

// ── Model colors for leaderboard ────────────────────────────────────────────

static const QStringList MODEL_COLORS = {
    "#FF8800", "#00E5FF", "#9D4EDD", "#00D66F", "#FF3B3B", "#FFD700", "#0088FF", "#E91E63", "#4CAF50", "#FF5722",
};
} // namespace

namespace finterm::screens {

using namespace finterm::ui;

// ── Constructor ─────────────────────────────────────────────────────────────

AlphaArenaScreen::AlphaArenaScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("aaScreen");
    setStyleSheet(kStyle);

    auto_timer_ = new QTimer(this);
    connect(auto_timer_, &QTimer::timeout, this, &AlphaArenaScreen::on_run_cycle);

    setup_ui();
    populate_model_list();
    LOG_INFO("AlphaArena", "Screen constructed");
}

void AlphaArenaScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    populate_model_list(); // refresh in case user changed LLM config in Settings
    if (!to_open_.isEmpty())
        open_competition(std::exchange(to_open_, {}));
    if (is_auto_running_)
        auto_timer_->start();
    load_tab();
}

void AlphaArenaScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    auto_timer_->stop();
}

// ── UI Setup ────────────────────────────────────────────────────────────────

void AlphaArenaScreen::setup_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(create_header());

    // Create panel (collapsible)
    create_panel_ = create_create_panel();
    root->addWidget(create_panel_);

    // Controls bar
    root->addWidget(create_controls_bar());

    // Main content
    root->addWidget(create_main_content(), 1);

    // Past competitions (collapsible)
    past_panel_ = create_past_competitions_panel();
    past_panel_->hide();
    root->addWidget(past_panel_);

    root->addWidget(create_status_bar());
}

QWidget* AlphaArenaScreen::create_header() {
    auto* bar = new QWidget(this);
    bar->setObjectName("aaHeader");
    bar->setFixedHeight(42);

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(16, 0, 16, 0);
    hl->setSpacing(10);

    auto* title = new QLabel("ALPHA ARENA");
    title->setObjectName("aaHeaderTitle");
    hl->addWidget(title);

    status_badge_ = new QLabel("READY");
    status_badge_->setObjectName("aaStatusBadge");
    status_badge_->setStyleSheet(QString("color: %1; background: rgba(217,119,6,0.15); "
                                         "font-size: 8px; font-weight: 700; padding: 2px 6px;")
                                     .arg(colors::AMBER()));
    hl->addWidget(status_badge_);

    cycle_label_ = new QLabel("CYCLE 0");
    cycle_label_->setObjectName("aaCycleLabel");
    hl->addWidget(cycle_label_);

    price_label_ = new QLabel;
    price_label_->setObjectName("aaPriceLabel");
    price_label_->hide();
    hl->addWidget(price_label_);

    hl->addStretch(1);

    auto* past_btn = new QPushButton("HISTORY");
    past_btn->setObjectName("aaResetBtn");
    past_btn->setCursor(Qt::PointingHandCursor);
    connect(past_btn, &QPushButton::clicked, this, &AlphaArenaScreen::on_past_competitions_toggle);
    hl->addWidget(past_btn);

    auto* refresh_btn = new QPushButton("REFRESH");
    refresh_btn->setObjectName("aaResetBtn");
    refresh_btn->setCursor(Qt::PointingHandCursor);
    connect(refresh_btn, &QPushButton::clicked, this, [this]() {
        if (!competition_id_.isEmpty())
            open_competition(competition_id_);
        load_tab();
    });
    hl->addWidget(refresh_btn);

    return bar;
}

QWidget* AlphaArenaScreen::create_create_panel() {
    auto* panel = new QWidget(this);
    panel->setObjectName("aaCreatePanel");

    auto* vl = new QVBoxLayout(panel);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // Header
    auto* hdr = new QWidget(this);
    hdr->setObjectName("aaCreateHeader");
    hdr->setFixedHeight(34);
    auto* hhl = new QHBoxLayout(hdr);
    hhl->setContentsMargins(12, 0, 12, 0);
    auto* ht = new QLabel("CREATE COMPETITION");
    ht->setObjectName("aaCreateTitle");
    hhl->addWidget(ht);
    hhl->addStretch(1);
    vl->addWidget(hdr);

    // Body
    auto* body = new QWidget(this);
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(12, 12, 12, 12);
    bl->setSpacing(10);

    // Row 1: Name + Type
    auto* r1 = new QHBoxLayout;
    r1->setSpacing(8);

    auto* name_col = new QVBoxLayout;
    auto* name_lbl = new QLabel("COMPETITION NAME");
    name_lbl->setObjectName("aaLabel");
    comp_name_ = new QLineEdit("Alpha Arena Competition");
    name_col->addWidget(name_lbl);
    name_col->addWidget(comp_name_);
    r1->addLayout(name_col, 2);

    auto* type_col = new QVBoxLayout;
    auto* type_lbl = new QLabel("TYPE");
    type_lbl->setObjectName("aaLabel");
    comp_type_ = new QComboBox;
    comp_type_->addItems({"Crypto Trading", "Prediction Markets"});
    connect(comp_type_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AlphaArenaScreen::on_competition_type_changed);
    type_col->addWidget(type_lbl);
    type_col->addWidget(comp_type_);
    r1->addLayout(type_col, 1);

    bl->addLayout(r1);

    const auto add_field = [](QHBoxLayout* row, const QString& label, QWidget* field, int stretch = 1) {
        auto* col = new QVBoxLayout;
        auto* lbl = new QLabel(label);
        lbl->setObjectName("aaLabel");
        col->addWidget(lbl);
        col->addWidget(field);
        row->addLayout(col, stretch);
    };

    // Row 2: what is traded
    crypto_fields_ = new QWidget(this);
    auto* rc = new QHBoxLayout(crypto_fields_);
    rc->setContentsMargins(0, 0, 0, 0);
    rc->setSpacing(8);
    comp_symbol_ = new QComboBox;
    comp_symbol_->addItems(CRYPTO_SYMBOLS);
    connect(comp_symbol_, &QComboBox::currentIndexChanged, this, &AlphaArenaScreen::load_tab);
    add_field(rc, "SYMBOL", comp_symbol_);
    comp_exchange_ = new QComboBox;
    comp_exchange_->addItems(EXCHANGES);
    comp_exchange_->setToolTip("Whose prices the competition trades at; the EXCHANGES tab compares them");
    add_field(rc, "PRICES FROM", comp_exchange_);
    grid_range_ = new QDoubleSpinBox;
    grid_range_->setRange(0.5, 50);
    grid_range_->setValue(5);
    grid_range_->setDecimals(1);
    grid_range_->setPrefix(QStringLiteral("±"));
    grid_range_->setSuffix("%");
    grid_range_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    add_field(rc, "GRID BOT RANGE", grid_range_);
    grid_levels_ = new QSpinBox;
    grid_levels_->setRange(2, 100);
    grid_levels_->setValue(10);
    grid_levels_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    add_field(rc, "GRID BOT LEVELS", grid_levels_);
    bl->addWidget(crypto_fields_);

    market_fields_ = new QWidget(this);
    auto* rm = new QVBoxLayout(market_fields_);
    rm->setContentsMargins(0, 0, 0, 0);
    auto* market_lbl = new QLabel("POLYMARKET MARKETS (select 1 to 10)");
    market_lbl->setObjectName("aaLabel");
    rm->addWidget(market_lbl);
    market_list_ = new QListWidget;
    market_list_->setObjectName("aaModelList");
    market_list_->setSelectionMode(QAbstractItemView::MultiSelection);
    market_list_->setFixedHeight(110);
    rm->addWidget(market_list_);
    market_fields_->hide();
    bl->addWidget(market_fields_);

    // Row 3: how
    auto* r3 = new QHBoxLayout;
    r3->setSpacing(8);
    comp_mode_ = new QComboBox;
    comp_mode_->addItems(COMP_MODES);
    add_field(r3, "MODE", comp_mode_);
    comp_capital_ = new QDoubleSpinBox;
    comp_capital_->setRange(100, 10000000);
    comp_capital_->setValue(10000);
    comp_capital_->setDecimals(0);
    comp_capital_->setPrefix("$");
    comp_capital_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    add_field(r3, "INITIAL CAPITAL ($)", comp_capital_);
    comp_interval_ = new QSpinBox;
    comp_interval_->setRange(10, 600);
    comp_interval_->setValue(interval_s_);
    comp_interval_->setSuffix("s");
    comp_interval_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    add_field(r3, "INTERVAL (sec)", comp_interval_);
    bl->addLayout(r3);

    // AI Models
    auto* models_lbl = new QLabel("COMPETITORS (select 2+)");
    models_lbl->setObjectName("aaLabel");
    bl->addWidget(models_lbl);

    model_list_ = new QListWidget;
    model_list_->setObjectName("aaModelList");
    model_list_->setSelectionMode(QAbstractItemView::MultiSelection);
    model_list_->setFixedHeight(80);
    bl->addWidget(model_list_);

    // Create button
    create_btn_ = new QPushButton("CREATE COMPETITION");
    create_btn_->setObjectName("aaCreateBtn");
    create_btn_->setCursor(Qt::PointingHandCursor);
    create_btn_->setFixedHeight(34);
    connect(create_btn_, &QPushButton::clicked, this, &AlphaArenaScreen::on_create_competition);
    bl->addWidget(create_btn_);

    vl->addWidget(body);
    return panel;
}

QWidget* AlphaArenaScreen::create_controls_bar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("aaControlsBar");
    bar->setFixedHeight(36);

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(12, 0, 12, 0);
    hl->setSpacing(8);

    run_btn_ = new QPushButton("RUN CYCLE");
    run_btn_->setObjectName("aaRunBtn");
    run_btn_->setCursor(Qt::PointingHandCursor);
    run_btn_->setEnabled(false);
    connect(run_btn_, &QPushButton::clicked, this, &AlphaArenaScreen::on_run_cycle);
    hl->addWidget(run_btn_);

    auto_btn_ = new QPushButton("AUTO RUN");
    auto_btn_->setObjectName("aaAutoBtn");
    auto_btn_->setCursor(Qt::PointingHandCursor);
    auto_btn_->setEnabled(false);
    auto_btn_->setProperty("running", false);
    connect(auto_btn_, &QPushButton::clicked, this, &AlphaArenaScreen::on_toggle_auto_run);
    hl->addWidget(auto_btn_);

    reset_btn_ = new QPushButton("RESET");
    reset_btn_->setObjectName("aaResetBtn");
    reset_btn_->setCursor(Qt::PointingHandCursor);
    connect(reset_btn_, &QPushButton::clicked, this, &AlphaArenaScreen::on_reset);
    hl->addWidget(reset_btn_);

    hl->addStretch(1);

    interval_label_ = new QLabel;
    interval_label_->setObjectName("aaIntervalLabel");
    hl->addWidget(interval_label_);

    return bar;
}

QWidget* AlphaArenaScreen::create_main_content() {
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);

    // Left: leaderboard
    splitter->addWidget(create_leaderboard_panel());

    // Right: tabs
    splitter->addWidget(create_right_panel());

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({700, 520});

    return splitter;
}

QWidget* AlphaArenaScreen::create_leaderboard_panel() {
    auto* panel = new QWidget(this);
    panel->setObjectName("aaLeaderboard");

    auto* vl = new QVBoxLayout(panel);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* hdr = new QWidget(this);
    hdr->setObjectName("aaLeaderboardHeader");
    hdr->setFixedHeight(34);
    auto* hhl = new QHBoxLayout(hdr);
    hhl->setContentsMargins(12, 0, 12, 0);
    auto* ht = new QLabel("LEADERBOARD");
    ht->setObjectName("aaLeaderboardTitle");
    hhl->addWidget(ht);
    hhl->addStretch(1);
    leaderboard_cycle_ = new QLabel("Cycle 0");
    leaderboard_cycle_->setObjectName("aaStatusText");
    hhl->addWidget(leaderboard_cycle_);
    vl->addWidget(hdr);

    leaderboard_table_ = new QTableWidget;
    leaderboard_table_->setColumnCount(7);
    leaderboard_table_->setHorizontalHeaderLabels(
        {"RANK", "MODEL", "PORTFOLIO", "PnL", "RETURN %", "TRADES", "WIN RATE"});
    leaderboard_table_->horizontalHeader()->setStretchLastSection(true);
    leaderboard_table_->verticalHeader()->setVisible(false);
    leaderboard_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    leaderboard_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    vl->addWidget(leaderboard_table_, 1);

    return panel;
}

QWidget* AlphaArenaScreen::create_right_panel() {
    auto* panel = new QWidget(this);
    panel->setObjectName("aaRightPanel");
    panel->setMinimumWidth(280);

    auto* vl = new QVBoxLayout(panel);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // Tab bar
    auto* tab_bar = new QWidget(this);
    tab_bar->setFixedHeight(32);
    auto* tbl = new QHBoxLayout(tab_bar);
    tbl->setContentsMargins(4, 0, 4, 0);
    tbl->setSpacing(0);

    const QStringList tabs = {"DECISIONS", "METRICS", "SENTIMENT", "GRID", "EXCHANGES"}; // RightTab
    for (int i = 0; i < tabs.size(); ++i) {
        auto* btn = new QPushButton(tabs[i]);
        btn->setObjectName("aaRightTabBtn");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("active", i == 0);
        connect(btn, &QPushButton::clicked, this, [this, i]() { on_right_tab_changed(i); });
        tbl->addWidget(btn);
        right_tab_btns_.append(btn);
    }
    vl->addWidget(tab_bar);

    right_stack_ = new QStackedWidget;
    const auto make_table = [](const QStringList& headers) {
        auto* t = new QTableWidget;
        t->setColumnCount(int(headers.size()));
        t->setHorizontalHeaderLabels(headers);
        t->verticalHeader()->setVisible(false);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionMode(QAbstractItemView::NoSelection);
        return t;
    };
    const auto make_note = []() {
        auto* l = new QLabel;
        l->setObjectName("aaStatusText");
        l->setWordWrap(true);
        l->setContentsMargins(10, 8, 10, 8);
        return l;
    };
    const auto add_page = [this](QLabel* note, QWidget* body) {
        auto* page = new QWidget(this);
        auto* pl = new QVBoxLayout(page);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(0);
        pl->addWidget(note);
        pl->addWidget(body, 1);
        right_stack_->addWidget(page);
    };

    decisions_list_ = new QListWidget;
    decisions_list_->setObjectName("aaDecisionList");
    decisions_list_->setWordWrap(true);
    decisions_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    right_stack_->addWidget(decisions_list_);

    metrics_table_ = make_table({});
    right_stack_->addWidget(metrics_table_);

    news_summary_ = make_note();
    news_list_ = new QListWidget;
    news_list_->setObjectName("aaDecisionList");
    news_list_->setWordWrap(true);
    news_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(news_list_, &QListWidget::itemActivated, this, [](QListWidgetItem* item) {
        const QUrl url(item->data(Qt::UserRole).toString());
        if (url.scheme() == QLatin1String("https"))
            QDesktopServices::openUrl(url);
    });
    add_page(news_summary_, news_list_);

    grid_summary_ = make_note();
    grid_table_ = make_table({"BUYS AT", "UNITS", "HELD"});
    add_page(grid_summary_, grid_table_);

    quotes_summary_ = make_note();
    quotes_table_ = make_table({"EXCHANGE", "LAST", "BID", "ASK", "SPREAD", "24H VOLUME"});
    add_page(quotes_summary_, quotes_table_);

    vl->addWidget(right_stack_, 1);
    show_grid({});
    return panel;
}

QWidget* AlphaArenaScreen::create_past_competitions_panel() {
    auto* panel = new QWidget(this);
    panel->setObjectName("aaPastPanel");
    panel->setFixedHeight(200);

    auto* vl = new QVBoxLayout(panel);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* hdr = new QWidget(this);
    hdr->setFixedHeight(30);
    auto* hhl = new QHBoxLayout(hdr);
    hhl->setContentsMargins(12, 0, 12, 0);
    auto* ht = new QLabel("PAST COMPETITIONS");
    ht->setObjectName("aaLeaderboardTitle");
    hhl->addWidget(ht);
    hhl->addStretch(1);
    vl->addWidget(hdr);

    past_list_ = new QListWidget;
    past_list_->setObjectName("aaPastList");
    connect(past_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty())
            return;
        on_past_competitions_toggle();
        set_auto_running(false);
        open_competition(id);
    });
    vl->addWidget(past_list_, 1);

    return panel;
}

QWidget* AlphaArenaScreen::create_status_bar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("aaStatusBar");
    bar->setFixedHeight(26);

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(16, 0, 16, 0);

    auto* left = new QLabel("ALPHA ARENA");
    left->setObjectName("aaStatusText");
    hl->addWidget(left);
    hl->addStretch(1);

    status_comp_ = new QLabel("NO COMPETITION");
    status_comp_->setObjectName("aaStatusText");
    hl->addWidget(status_comp_);

    hl->addSpacing(12);
    status_models_ = new QLabel;
    status_models_->setObjectName("aaStatusText");
    hl->addWidget(status_models_);

    hl->addSpacing(12);
    status_info_ = new QLabel;
    status_info_->setObjectName("aaStatusHighlight");
    hl->addWidget(status_info_);

    return bar;
}

// ── Engine ──────────────────────────────────────────────────────────────────

QJsonArray AlphaArenaScreen::connections() const {
    QJsonArray out;
    for (const auto& e : model_entries_) {
        if (e.provider == kGridProvider)
            continue;
        out.append(QJsonObject{{"key", e.key},
                               {"name", e.display_name},
                               {"provider", e.provider},
                               {"model_id", e.model_id},
                               {"api_key", e.api_key},
                               {"base_url", ai_chat::LlmService::provider_api_root(e.provider, e.base_url)},
                               {"max_tokens", e.max_tokens}});
    }
    return out;
}

void AlphaArenaScreen::run_action(const QString& action, const QJsonObject& params, Answer done,
                                  std::function<void()> failed) {
    QJsonObject payload{{"action", action}, {"params", params}};
    if (action == QLatin1String("create_competition") || action == QLatin1String("run_cycle"))
        payload["connections"] = connections();
    QPointer<AlphaArenaScreen> self = this;
    services::alpha_arena::AlphaArenaService::instance().run_action(
        payload, [self, done = std::move(done), failed = std::move(failed)](const services::alpha_arena::ActionResult& r) {
            if (!self)
                return;
            if (r.success) {
                done(r.data);
                return;
            }
            if (failed)
                failed();
            self->say(r.error, true);
        });
}

void AlphaArenaScreen::say(const QString& text, bool problem) {
    const QString line = text.section('\n', 0, 0);
    status_info_->setText(line.size() > 90 ? line.left(89) + QStringLiteral("…") : line);
    status_info_->setToolTip(text);
    status_info_->setStyleSheet(
        problem ? QString("color: %1; font-size: 9px; background: transparent;").arg(colors::NEGATIVE()) : QString());
}

// ── Slots ───────────────────────────────────────────────────────────────────

void AlphaArenaScreen::on_create_competition() {
    if (busy_)
        return;
    QJsonArray models;
    for (auto* item : model_list_->selectedItems()) {
        const int row = model_list_->row(item);
        if (row < 0 || row >= model_entries_.size())
            continue;
        const auto& e = model_entries_[row];
        models.append(QJsonObject{
            {"key", e.key}, {"name", e.display_name}, {"provider", e.provider}, {"model_id", e.model_id}});
    }
    if (models.size() < 2) {
        say("Select at least 2 competitors", true);
        return;
    }

    QJsonObject params{{"competition_name", comp_name_->text()}, {"mode", comp_mode_->currentText()},
                       {"initial_capital", comp_capital_->value()}, {"cycle_interval_seconds", comp_interval_->value()},
                       {"models", models}};
    if (comp_type_->currentIndex() == 0) {
        params["competition_type"] = "crypto";
        params["symbol"] = comp_symbol_->currentText();
        params["exchange_id"] = comp_exchange_->currentText();
        params["grid_range_pct"] = grid_range_->value();
        params["grid_levels"] = grid_levels_->value();
    } else {
        params["competition_type"] = "polymarket";
        QJsonArray ids;
        for (auto* item : market_list_->selectedItems())
            ids.append(item->data(Qt::UserRole).toString());
        params["market_ids"] = ids;
    }

    set_busy(true);
    say("Creating the competition…");
    run_action("create_competition", params,
               [this](const QJsonObject& state) {
                   set_busy(false);
                   LOG_INFO("AlphaArena", "Competition created: " + state["competition_id"].toString());
                   say("Created: run a cycle");
                   show_competition(state);
               },
               [this]() { set_busy(false); });
}

void AlphaArenaScreen::on_run_cycle() {
    if (busy_ || competition_id_.isEmpty())
        return;
    set_busy(true);
    set_badge("RUNNING", colors::POSITIVE());
    say("Asking the models…");
    const QString id = competition_id_;
    run_action("run_cycle", {{"competition_id", id}},
               [this, id](const QJsonObject& state) {
                   set_busy(false);
                   if (id != competition_id_)
                       return; // the reader has moved to another competition
                   show_competition(state);
                   QStringList notes;
                   for (const auto& n : state["notes"].toArray())
                       notes << n.toString();
                   say(notes.isEmpty() ? QString("Cycle %1 complete").arg(state["cycle_number"].toInt())
                                       : notes.join("\n"),
                       !notes.isEmpty());
               },
               [this, id]() {
                   set_busy(false);
                   if (id == competition_id_)
                       set_badge("CYCLE FAILED", colors::NEGATIVE());
               });
}

void AlphaArenaScreen::set_auto_running(bool on) {
    is_auto_running_ = on;
    auto_btn_->setProperty("running", on);
    auto_btn_->style()->unpolish(auto_btn_);
    auto_btn_->style()->polish(auto_btn_);
    auto_btn_->setText(on ? "STOP AUTO" : "AUTO RUN");
    if (on)
        auto_timer_->start(interval_s_ * 1000);
    else
        auto_timer_->stop();
}

void AlphaArenaScreen::on_toggle_auto_run() {
    set_auto_running(!is_auto_running_);
    LOG_INFO("AlphaArena", is_auto_running_ ? "Auto-run started" : "Auto-run stopped");
    if (is_auto_running_)
        on_run_cycle();
}

void AlphaArenaScreen::on_reset() {
    set_auto_running(false);
    competition_id_.clear();
    competition_symbol_.clear();
    competition_exchange_.clear();
    set_badge("READY", colors::AMBER());
    cycle_label_->setText("CYCLE 0");
    price_label_->hide();
    leaderboard_table_->setRowCount(0);
    leaderboard_cycle_->setText("Cycle 0");
    decisions_list_->clear();
    show_metrics({});
    show_grid({});
    interval_label_->clear();
    status_comp_->setText("NO COMPETITION");
    status_models_->clear();
    say({});
    set_busy(busy_);
    create_panel_->show();
    load_tab();
    ScreenStateManager::instance().notify_changed(this);
}

void AlphaArenaScreen::on_right_tab_changed(int tab) {
    right_stack_->setCurrentIndex(tab);
    for (int i = 0; i < right_tab_btns_.size(); ++i) {
        right_tab_btns_[i]->setProperty("active", i == tab);
        right_tab_btns_[i]->style()->unpolish(right_tab_btns_[i]);
        right_tab_btns_[i]->style()->polish(right_tab_btns_[i]);
    }
    load_tab();
    ScreenStateManager::instance().notify_changed(this);
}

void AlphaArenaScreen::on_competition_type_changed(int index) {
    crypto_fields_->setVisible(index == 0);
    market_fields_->setVisible(index == 1);
    if (index == 1 && market_list_->count() == 0)
        load_markets();
    load_tab();
}

void AlphaArenaScreen::on_past_competitions_toggle() {
    past_visible_ = !past_visible_;
    past_panel_->setVisible(past_visible_);
    if (past_visible_)
        load_past_competitions();
}

void AlphaArenaScreen::open_competition(const QString& id) {
    competition_id_ = id; // an answer for another one is not shown
    set_busy(busy_);
    run_action("get_state", {{"competition_id", id}},
               [this, id](const QJsonObject& state) {
                   if (id == competition_id_)
                       show_competition(state);
               },
               [this, id]() {
                   if (id == competition_id_)
                       on_reset();
               });
}

void AlphaArenaScreen::load_past_competitions() {
    run_action("list_competitions", {{"limit", 20}}, [this](const QJsonObject& answer) {
        past_list_->clear();
        for (const auto& v : answer["competitions"].toArray()) {
            const QJsonObject c = v.toObject();
            QStringList models;
            for (const auto& m : c["models"].toArray())
                models << m.toString();
            auto* item = new QListWidgetItem(QString("%1 — %2 cycles — %3 — %4")
                                                 .arg(c["name"].toString())
                                                 .arg(c["cycle_count"].toInt())
                                                 .arg(c["created_at"].toString().left(16).replace('T', ' '),
                                                      models.join(", ")));
            item->setData(Qt::UserRole, c["competition_id"].toString());
            past_list_->addItem(item);
        }
        if (past_list_->count() == 0)
            past_list_->addItem("No competitions yet");
    });
}

// ── What a tab shows besides a competition ──────────────────────────────────

QString AlphaArenaScreen::current_symbol() const {
    if (!competition_id_.isEmpty())
        return competition_symbol_;
    return comp_type_->currentIndex() == 0 ? comp_symbol_->currentText() : QString();
}

void AlphaArenaScreen::load_tab() {
    if (!isVisible())
        return;
    const int tab = right_stack_->currentIndex();
    if (tab == SentimentTab)
        load_news();
    else if (tab == ExchangesTab)
        load_quotes();
}

void AlphaArenaScreen::load_news() {
    const QString symbol = current_symbol();
    if (symbol.isEmpty()) {
        news_list_->clear();
        news_summary_->setText("Headlines are for a coin; this competition trades prediction markets.");
        return;
    }
    news_summary_->setText("Loading headlines about " + symbol + "…");
    run_action("get_news", {{"symbol", symbol}},
               [this, symbol](const QJsonObject& answer) {
                   if (symbol == current_symbol())
                       show_news(answer["news"].toObject());
               },
               [this, symbol]() {
                   if (symbol == current_symbol())
                       news_summary_->setText("Headlines could not be loaded (see the status bar).");
               });
}

void AlphaArenaScreen::load_quotes() {
    const QString symbol = current_symbol();
    quotes_table_->setRowCount(0);
    if (symbol.isEmpty()) {
        quotes_summary_->setText("Prediction markets trade on Polymarket alone.");
        return;
    }
    quotes_summary_->setText("Asking the exchanges for " + symbol + "…");
    run_action("get_quotes", {{"symbol", symbol}},
               [this, symbol](const QJsonObject& answer) {
                   if (symbol != current_symbol())
                       return;
                   const auto rows = answer["quotes"].toArray();
                   quotes_table_->setRowCount(int(rows.size()));
                   for (int i = 0; i < rows.size(); ++i) {
                       const QJsonObject q = rows[i].toObject();
                       const QString exchange = q["exchange"].toString();
                       quotes_table_->setItem(i, 0, cell(exchange, exchange == competition_exchange_ ? colors::AMBER() : QString(), Qt::AlignLeft));
                       if (q.contains("error")) {
                           quotes_table_->setItem(i, 1, cell(q["error"].toString(), colors::NEGATIVE(), Qt::AlignLeft));
                           quotes_table_->setSpan(i, 1, 1, 5);
                           continue;
                       }
                       const double bid = q["bid"].toDouble(), ask = q["ask"].toDouble();
                       quotes_table_->setSpan(i, 1, 1, 1);
                       quotes_table_->setItem(i, 1, cell(price_text(q["price"].toDouble()), colors::CYAN()));
                       quotes_table_->setItem(i, 2, cell(price_text(bid)));
                       quotes_table_->setItem(i, 3, cell(price_text(ask)));
                       quotes_table_->setItem(i, 4, cell(QString("%1 bp").arg((ask - bid) / ((ask + bid) / 2) * 1e4, 0, 'f', 2)));
                       quotes_table_->setItem(i, 5, cell(q["volume_24h"].isDouble()
                                                             ? QLocale::c().toString(q["volume_24h"].toDouble(), 'f', 0)
                                                             : QStringLiteral("—")));
                   }
                   quotes_table_->resizeColumnsToContents();
                   quotes_summary_->setText(
                       QString("%1 at %2. %3")
                           .arg(symbol, QTime::currentTime().toString("HH:mm:ss"),
                                competition_exchange_.isEmpty()
                                    ? QStringLiteral("PRICES FROM sets whose a new competition trades at.")
                                    : QString("This competition trades at %1's: buys at the ask, sells at the bid, "
                                              "0.1% a trade.").arg(competition_exchange_)));
               },
               [this, symbol]() {
                   if (symbol == current_symbol())
                       quotes_summary_->setText("The quotes could not be loaded (see the status bar).");
               });
}

void AlphaArenaScreen::load_markets() {
    say("Loading Polymarket's most traded markets…");
    run_action("list_markets", {}, [this](const QJsonObject& answer) {
        market_list_->clear();
        for (const auto& v : answer["markets"].toArray()) {
            const QJsonObject m = v.toObject();
            const auto outcomes = m["outcomes"].toArray();
            const auto prices = m["prices"].toArray();
            auto* item = new QListWidgetItem(QString("%1 — %2 %3%")
                                                 .arg(m["question"].toString(), outcomes.at(0).toString())
                                                 .arg(prices.at(0).toDouble() * 100, 0, 'f', 1));
            item->setData(Qt::UserRole, m["id"].toString());
            item->setToolTip(QString("%1\nEnds %2\nLiquidity $%3")
                                 .arg(m["question"].toString(), m["end_date"].toString(),
                                      QLocale::c().toString(m["liquidity"].toDouble(), 'f', 0)));
            market_list_->addItem(item);
        }
        say({});
    });
}

// ── Display ─────────────────────────────────────────────────────────────────

void AlphaArenaScreen::show_competition(const QJsonObject& state) {
    competition_id_ = state["competition_id"].toString();
    competition_symbol_ = state["symbol"].toString();
    competition_exchange_ = state["exchange"].toString();
    interval_s_ = qMax(10, state["interval"].toInt(150));
    create_panel_->hide();

    const int cycle = state["cycle_number"].toInt();
    const bool over = state["over"].toBool();
    cycle_label_->setText(QString("CYCLE %1").arg(cycle));
    leaderboard_cycle_->setText(QString("Cycle %1").arg(cycle));
    set_badge(over ? "DECIDED" : cycle == 0 ? "CREATED" : "ACTIVE", over ? colors::AMBER() : colors::POSITIVE());
    if (over && is_auto_running_)
        set_auto_running(false);
    price_label_->setVisible(state["price"].isDouble());
    price_label_->setText(competition_symbol_ + " " + price_text(state["price"].toDouble()));
    interval_label_->setText(QString("INTERVAL: %1s").arg(interval_s_));
    status_comp_->setText(QString("%1 · %2").arg(state["name"].toString(), state["mode"].toString()));
    status_models_->setText(QString("%1 competitors").arg(state["models_count"].toInt()));
    if (is_auto_running_ && auto_timer_->interval() != interval_s_ * 1000)
        auto_timer_->setInterval(interval_s_ * 1000);

    show_leaderboard(state["leaderboard"].toArray());
    show_decisions(state["decisions"].toArray());
    show_metrics(state["metrics"].toArray());
    show_grid(state["grid"].toObject());
    if (state["news"].isObject()) // what the models were shown
        show_news(state["news"].toObject());
    set_busy(busy_);
    ScreenStateManager::instance().notify_changed(this);
}

void AlphaArenaScreen::set_badge(const QString& text, const QString& color) {
    status_badge_->setText(text);
    status_badge_->setStyleSheet(QString("color: %1; background: rgba(217,119,6,0.15); "
                                         "font-size: 8px; font-weight: 700; padding: 2px 6px;")
                                     .arg(color));
}

void AlphaArenaScreen::show_leaderboard(const QJsonArray& rows) {
    auto* t = leaderboard_table_;
    t->setRowCount(int(rows.size()));
    for (int i = 0; i < rows.size(); ++i) {
        const QJsonObject e = rows[i].toObject();
        const int rank = e["rank"].toInt(i + 1);
        const QString pnl_color = e["total_pnl"].toDouble() >= 0 ? colors::POSITIVE() : colors::NEGATIVE();
        const QString medal = rank == 1 ? "#FFD700" : rank == 2 ? "#C0C0C0" : rank == 3 ? "#CD7F32" : QString();
        t->setItem(i, 0, cell(rank == 1 ? "1st" : rank == 2 ? "2nd" : rank == 3 ? "3rd" : QString::number(rank), medal,
                              Qt::AlignHCenter));
        t->setItem(i, 1, cell(e["model_name"].toString(),
                              i < MODEL_COLORS.size() ? MODEL_COLORS[i] : colors::TEXT_SECONDARY(), Qt::AlignLeft));
        t->setItem(i, 2, cell(money_text(e["portfolio_value"]), colors::CYAN()));
        t->setItem(i, 3, cell((e["total_pnl"].toDouble() >= 0 ? "+" : "") + money_text(e["total_pnl"]), pnl_color));
        t->setItem(i, 4, cell(percent_text(e["return_pct"]), pnl_color));
        t->setItem(i, 5, cell(QString::number(e["trades_count"].toInt())));
        t->setItem(i, 6, cell(e["win_rate"].isDouble() ? QString("%1%").arg(e["win_rate"].toDouble() * 100, 0, 'f', 1)
                                                       : QStringLiteral("—")));
    }
    t->resizeColumnsToContents();
}

void AlphaArenaScreen::show_decisions(const QJsonArray& rows) {
    decisions_list_->clear();
    for (const auto& v : rows) {
        const QJsonObject d = v.toObject();
        const QString action = d["action"].toString().toUpper(), status = d["status"].toString();
        const QString error = d["error"].toString();
        QString head = QString("#%1  %2 | ").arg(d["cycle_number"].toInt()).arg(d["model_name"].toString());
        QString color = colors::TEXT_SECONDARY();
        if (action == "NONE") { // it made none: not a HOLD
            head += "NO DECISION";
            color = colors::WARNING();
        } else {
            head += action;
            if (status == "executed") {
                head += QString(" %1 %2 at %3").arg(QString::number(d["quantity"].toDouble(), 'g', 6),
                                                    d["label"].toString(), price_text(d["price"].toDouble()));
                color = action == "BUY" ? colors::POSITIVE() : action == "SELL" ? colors::NEGATIVE() : QString("#9D4EDD");
                if (d["pnl"].isDouble())
                    head += " | realized " + money_text(d["pnl"]);
            } else if (status == "rejected") {
                head += " " + d["label"].toString() + " | not carried out";
                color = colors::WARNING();
            }
            if (d["confidence"].isDouble())
                head += QString(" | conf. %1%").arg(qRound(d["confidence"].toDouble() * 100));
        }
        QStringList text{head};
        if (!error.isEmpty())
            text << error;
        if (!d["reasoning"].toString().isEmpty())
            text << d["reasoning"].toString();
        auto* item = new QListWidgetItem(text.join('\n'));
        item->setForeground(QColor(color));
        decisions_list_->addItem(item);
    }
}

void AlphaArenaScreen::show_metrics(const QJsonArray& rows) {
    auto* t = metrics_table_;
    const auto count = [](const QJsonValue& v) { return QString::number(v.toInt()); };
    const auto unsigned_percent = [](const QJsonValue& v) { return percent_text(v, false); };
    const auto signed_percent = [](const QJsonValue& v) { return percent_text(v); };
    const auto rate = [](const QJsonValue& v) {
        return v.isDouble() ? QString("%1%").arg(v.toDouble() * 100, 0, 'f', 1) : QStringLiteral("—");
    };
    const auto ratio = [](const QJsonValue& v) {
        return v.isDouble() ? QString::number(v.toDouble(), 'f', 2) : QStringLiteral("—");
    };
    struct Row {
        const char* key;
        const char* label;
        const char* tip;
        std::function<QString(const QJsonValue&)> text;
    };
    const QList<Row> lines = {
        {"return_pct", "Return", "Portfolio value against the initial capital", signed_percent},
        {"max_drawdown_pct", "Max drawdown", "Largest fall from a peak, cycle to cycle", unsigned_percent},
        {"volatility_pct", "Volatility", "Standard deviation of the return per cycle; not annualized", unsigned_percent},
        {"best_cycle_pct", "Best cycle", "", signed_percent},
        {"worst_cycle_pct", "Worst cycle", "", signed_percent},
        {"trades", "Trades", "", count},
        {"closed", "Closing trades", "Trades that closed a position or part of one", count},
        {"win_rate", "Win rate", "Closing trades with a profit after fees", rate},
        {"profit_factor", "Profit factor", "Profits over losses of the closing trades", ratio},
        {"average_win", "Average win", "", money_text},
        {"average_loss", "Average loss", "", money_text},
        {"fees", "Fees paid", "", money_text},
        {"no_decision", "No decision", "Times the model gave none: no answer, or one that can't be read", count},
    };
    t->setRowCount(rows.isEmpty() ? 0 : int(lines.size()));
    t->setColumnCount(int(rows.size()) + 1);
    QStringList headers{"METRIC"};
    for (const auto& r : rows)
        headers << r.toObject()["model_name"].toString();
    t->setHorizontalHeaderLabels(headers);
    for (int i = 0; i < t->rowCount(); ++i) {
        auto* label = cell(lines[i].label, {}, Qt::AlignLeft);
        label->setToolTip(lines[i].tip);
        t->setItem(i, 0, label);
        for (int c = 0; c < rows.size(); ++c)
            t->setItem(i, c + 1, cell(lines[i].text(rows[c].toObject()[QLatin1String(lines[i].key)])));
    }
    t->resizeColumnsToContents();
}

void AlphaArenaScreen::show_news(const QJsonObject& news) {
    news_list_->clear();
    int positive = 0, negative = 0;
    const auto items = news["items"].toArray();
    for (const auto& v : items) {
        const QJsonObject n = v.toObject();
        const QString tone = n["tone"].toString();
        positive += tone == "positive";
        negative += tone == "negative";
        auto* item = new QListWidgetItem(
            QString("%1\n%2 · %3").arg(n["title"].toString(), n["publisher"].toString(), n["published"].toString()));
        item->setForeground(QColor(tone == "positive"   ? colors::POSITIVE()
                                   : tone == "negative" ? colors::NEGATIVE()
                                                        : colors::TEXT_SECONDARY()));
        item->setData(Qt::UserRole, n["url"].toString());
        item->setToolTip("Double-click to open the article");
        news_list_->addItem(item);
    }
    // EVENT-STAMP: when the engine fetched the headlines (its own clock)
    const QString at = QDateTime::fromSecsSinceEpoch(qint64(news["at"].toDouble())).toString("d MMM HH:mm");
    if (!news["error"].toString().isEmpty())
        news_summary_->setText("No headlines: " + news["error"].toString());
    else
        news_summary_->setText(QString("%1, %2: %3 headlines of the last two days, %4 positive, %5 negative — by "
                                       "counting words in the titles, not by reading the articles. The models are "
                                       "given the headlines.")
                                   .arg(news["symbol"].toString(), at)
                                   .arg(items.size())
                                   .arg(positive)
                                   .arg(negative));
}

void AlphaArenaScreen::show_grid(const QJsonObject& grid) {
    grid_table_->setRowCount(0);
    if (grid.isEmpty()) {
        grid_summary_->setText("The grid bot trades by rule: a unit bought for every line the price falls through, one "
                               "sold for every line it rises through. Select it among the competitors of a crypto "
                               "competition to see its lines here.");
        return;
    }
    const auto rows = grid["rows"].toArray();
    const bool priced = grid["price"].isDouble();
    grid_summary_->setText(QString("%1 to %2 in %3 steps, %4 a unit. Holds %5. %6")
                               .arg(price_text(grid["lower"].toDouble()), price_text(grid["upper"].toDouble()))
                               .arg(grid["levels"].toInt())
                               .arg(QString::number(grid["unit"].toDouble(), 'g', 6),
                                    QString::number(grid["held"].toDouble(), 'g', 6),
                                    priced ? "Price: " + price_text(grid["price"].toDouble()) + "."
                                           : QString("It trades from the first cycle on.")));
    grid_table_->setRowCount(int(rows.size()));
    for (int i = 0; i < rows.size(); ++i) {
        const QJsonObject r = rows[i].toObject();
        const bool held = priced && r["held"].toBool();
        const QString color = held ? colors::POSITIVE() : QString();
        grid_table_->setItem(i, 0, cell(price_text(r["price"].toDouble()), color));
        grid_table_->setItem(i, 1, cell(QString::number(r["units"].toInt()), color));
        grid_table_->setItem(i, 2, cell(held ? "yes" : "", color, Qt::AlignHCenter));
    }
    grid_table_->resizeColumnsToContents();
}

void AlphaArenaScreen::set_busy(bool busy) {
    busy_ = busy;
    const bool open = !competition_id_.isEmpty();
    create_btn_->setEnabled(!busy);
    run_btn_->setEnabled(!busy && open);
    run_btn_->setText(busy && open ? "RUNNING..." : "RUN CYCLE");
    auto_btn_->setEnabled(open);
}

// ── Model list population ────────────────────────────────────────────────────

void AlphaArenaScreen::populate_model_list() {
    // Preserve previously selected display names so we can restore selection
    QStringList previously_selected;
    for (auto* item : model_list_->selectedItems())
        previously_selected << item->text();

    model_list_->clear();
    model_entries_.clear();

    // A competitor is its name: two profiles of one name are told apart.
    const auto add = [this](ArenaModelEntry entry) {
        QString name = entry.display_name;
        const auto taken = [this](const QString& n) {
            return std::any_of(model_entries_.cbegin(), model_entries_.cend(),
                               [&](const ArenaModelEntry& e) { return e.display_name == n; });
        };
        for (int n = 2; taken(name); ++n)
            name = QString("%1 #%2").arg(entry.display_name).arg(n);
        entry.display_name = name;
        model_entries_.append(entry);
        model_list_->addItem(name);
    };

    // 1. Named profiles (highest fidelity — user configured them explicitly)
    auto profiles_res = LlmProfileRepository::instance().list_profiles();
    if (profiles_res.is_ok()) {
        for (const auto& p : profiles_res.value()) {
            if (p.provider.isEmpty() || p.model_id.isEmpty())
                continue;
            ArenaModelEntry entry;
            entry.display_name = p.name + " (" + p.provider + ")";
            entry.provider = p.provider;
            entry.model_id = p.model_id;
            entry.api_key = p.api_key;
            entry.base_url = p.base_url;
            entry.max_tokens = p.max_tokens;
            entry.key = p.id;
            add(entry);
        }
    }

    // 2. Configured providers (one entry per provider using its active model)
    auto providers_res = LlmConfigRepository::instance().list_providers();
    if (providers_res.is_ok()) {
        for (const auto& c : providers_res.value()) {
            if (c.provider.isEmpty() || c.model.isEmpty())
                continue;
            // Avoid duplicating a provider already covered by a profile
            bool already_added = false;
            for (const auto& e : std::as_const(model_entries_)) {
                if (e.provider == c.provider && e.model_id == c.model) {
                    already_added = true;
                    break;
                }
            }
            if (already_added)
                continue;

            ArenaModelEntry entry;
            entry.display_name = c.model + " (" + c.provider + ")";
            entry.provider = c.provider;
            entry.model_id = c.model;
            entry.api_key = c.api_key;
            entry.base_url = c.base_url;
            entry.key = "provider:" + c.provider;
            add(entry);
        }
    }

    // 3. Fallback: show a hint if nothing is configured
    if (model_entries_.isEmpty()) {
        auto* placeholder = new QListWidgetItem("No LLM providers configured — go to Settings → LLM Config");
        placeholder->setFlags(Qt::NoItemFlags); // not selectable
        placeholder->setForeground(QColor(colors::TEXT_SECONDARY()));
        model_list_->addItem(placeholder);
        LOG_WARN("AlphaArena", "No LLM providers or profiles configured");
        return;
    }

    ArenaModelEntry grid;
    grid.key = "grid";
    grid.display_name = "Grid bot (rules, no model)";
    grid.provider = kGridProvider;
    grid.model_id = "grid";
    add(grid);

    // Restore previous selections by display name
    for (int i = 0; i < model_list_->count(); ++i) {
        auto* item = model_list_->item(i);
        if (previously_selected.contains(item->text()))
            item->setSelected(true);
    }

    LOG_DEBUG("AlphaArena", QString("Model list populated: %1 entries").arg(model_entries_.size()));
}

// ── IStatefulScreen ───────────────────────────────────────────────────────────

QVariantMap AlphaArenaScreen::save_state() const {
    return {
        {"tab", right_stack_ ? right_stack_->currentIndex() : 0}, // RightTab
        {"competition", competition_id_.isEmpty() ? to_open_ : competition_id_},
    };
}

void AlphaArenaScreen::restore_state(const QVariantMap& state) {
    // "right_view" and "right_tab" are of the time there were other tabs.
    const int tab = state.value("tab", 0).toInt();
    if (tab > 0 && tab < right_stack_->count())
        on_right_tab_changed(tab);
    to_open_ = state.value("competition").toString();
}

} // namespace finterm::screens

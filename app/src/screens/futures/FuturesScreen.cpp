// src/screens/futures/FuturesScreen.cpp
#include "screens/futures/FuturesScreen.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "screens/equity_research/EquityOverviewTab.h"
#include "screens/futures/FuturesCharts.h"
#include "screens/futures/FuturesPanels.h"
#include "services/futures/FuturesService.h"
#include "services/markets/LastKnownQuote.h"
#include "storage/repositories/SettingsRepository.h"
#include "trading/PaperTrading.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"
#include "ui/widgets/LoadingOverlay.h"
#include "ui/widgets/StatusPill.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace finterm::screens {

using namespace services::futures;
namespace fmt = ui::formatting;

namespace {

// One batch quote per refresh; the back months print rarely and the feed is
// ~10 minutes behind, so faster would only repeat the same rows.
constexpr int kCurveRefreshMs = 20'000;

// The paper account. Margin is a modelled 5% of notional — a declared
// parameter of the simulation, not any exchange's requirement.
constexpr double kPaperBalance = 250'000.0;
constexpr double kPaperLeverage = 20.0;
const QString kPaperName = QStringLiteral("Futures Paper");
const QString kPaperExchange = QStringLiteral("futures");

struct PeriodDef {
    const char* label;
    const char* period;
    const char* interval;
    bool intraday;
};
constexpr PeriodDef kPeriods[] = {
    {"1D", "1d", "5m", true},   {"5D", "5d", "30m", true}, {"1M", "1mo", "1d", false}, {"3M", "3mo", "1d", false},
    {"6M", "6mo", "1d", false}, {"1Y", "1y", "1d", false}, {"5Y", "5y", "1wk", false},
};
const PeriodDef& period_def(const QString& label) {
    for (const auto& p : kPeriods)
        if (label == QLatin1String(p.label))
            return p;
    return kPeriods[4];
}

enum BoardCol { BRoot, BName, BLast, BChg, BPct, BVol, BCount };
enum MonthCol { MMonth, MCode, MLast, MChg, MPct, MSpread, MCarry, MVol, MOi, MExpiry, MDays, MCount };
enum PosCol { PContract, PPosition, PEntry, PLast, PPnl, PLevels, PClose, PCount };

// Stops and targets of the paper positions, kept across sessions.
const QString kBracketsKey = QStringLiteral("futures.paper_brackets");
// Where the panes sit and how big they are.
const QString kLayoutKey = QStringLiteral("futures.layout");
constexpr int kLayoutVersion = 2; // 2: sizes as pixel lists

QString num(double v, int dp) {
    return std::isfinite(v) ? QLocale(QLocale::English).toString(v, 'f', dp) : fmt::placeholder();
}

QString signed_num(double v, int dp) {
    if (!std::isfinite(v))
        return fmt::placeholder();
    return (v > 0 ? QStringLiteral("+") : v < 0 ? QStringLiteral("−") : QString()) +
           QLocale(QLocale::English).toString(std::abs(v), 'f', dp);
}

QString whole(double v) {
    return std::isfinite(v) ? QLocale(QLocale::English).toString(static_cast<qlonglong>(std::llround(v)))
                            : fmt::placeholder();
}

QString dollars(double v) {
    if (!std::isfinite(v))
        return fmt::placeholder();
    return (v < 0 ? QStringLiteral("−$") : QStringLiteral("$")) +
           QLocale(QLocale::English).toString(std::abs(v), 'f', std::abs(v) >= 1000 ? 0 : 2);
}

QColor tone(double v) {
    return QColor(!std::isfinite(v) || v == 0 ? ui::colors::TEXT_SECONDARY()
                  : v > 0                     ? ui::colors::POSITIVE()
                                              : ui::colors::NEGATIVE());
}

QTableWidgetItem* cell(QTableWidget* t, int row, int col) {
    QTableWidgetItem* it = t->item(row, col);
    if (!it) {
        it = new QTableWidgetItem;
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        t->setItem(row, col, it);
    }
    return it;
}

void put(QTableWidget* t, int row, int col, const QString& text, const QColor& color, bool right = true) {
    QTableWidgetItem* it = cell(t, row, col);
    if (it->text() != text)
        it->setText(text);
    it->setForeground(color);
    it->setTextAlignment((right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
}

QTableWidget* make_table(const QStringList& headers, QWidget* parent) {
    auto* t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(22);
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setFocusPolicy(Qt::NoFocus);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    return t;
}

QLabel* panel_title(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("futPanelTitle"));
    return l;
}


/// "CLZ26.NYM" -> "CL".
QString root_of(const QString& contract_symbol) {
    const QString code = contract_symbol.section(QLatin1Char('.'), 0, 0);
    return code.size() > 3 ? code.left(code.size() - 3) : QString();
}

} // namespace

FuturesScreen::FuturesScreen(QWidget* parent) : QWidget(parent) {
    build_ui();

    curve_timer_ = new QTimer(this);
    curve_timer_->setInterval(kCurveRefreshMs);
    connect(curve_timer_, &QTimer::timeout, this, &FuturesScreen::fetch_curve);

    // Live prints of the contracts the paper account holds.
    connect(&services::MarketDataService::instance(), &services::MarketDataService::stream_ticks, this,
            [this](const QVector<services::StreamTick>& ticks) {
                if (!shown_)
                    return;
                bool moved = false;
                for (const auto& t : ticks) {
                    if (!(t.price > 0))
                        continue;
                    if (t.symbol == curve_.front)
                        on_front_tick(t);
                    if (marks_.contains(t.symbol) && marks_.value(t.symbol) != t.price) {
                        marks_.insert(t.symbol, t.price);
                        mark_ts_.insert(t.symbol, t.time_ms / 1000);
                        mark_seen_ms_.insert(t.symbol, QDateTime::currentMSecsSinceEpoch());
                        moved = true;
                    }
                }
                if (moved) {
                    check_brackets();
                    positions_render_.request(); // one repaint per burst, from the cached book
                }
            });

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { apply_theme(); });
    apply_theme();

    group_ = groups().first();
    root_ = contracts_in(group_).first()->root;
    set_group(group_);
}

void FuturesScreen::on_front_tick(const services::StreamTick& tick) {
    // Between curve reads the front month moves with the tape. A print
    // carries its own change against the vendor's previous close, so price
    // and change stay a pair; a print without one moves neither.
    const int i = curve_.index_of(curve_.front);
    if (i < 0 || !std::isfinite(tick.change))
        return;
    const double prev = tick.price - tick.change;
    if (!(prev > 0))
        return;
    auto& c = curve_.contracts[i];
    if (c.price == tick.price)
        return;
    c.price = tick.price;
    c.prev_close = prev;
    c.change = tick.change;
    c.pct = tick.change / prev * 100.0;
    if (tick.time_ms > 0)
        c.ts = tick.time_ms / 1000;
    front_live_ = c; // kept across the next curve read, which is ~10 minutes older
    // marks_ is left to the caller's loop: it compares against the old mark to
    // decide whether the positions table needs repainting.
    strip_render_.request();
}

const ContractSpec* FuturesScreen::spec() const {
    return find_contract(root_);
}

QString FuturesScreen::price_text(double v) const {
    const auto* s = spec();
    return num(v, s ? price_decimals(s->tick) : 2);
}

// ── layout ─────────────────────────────────────────────────────────────────

void FuturesScreen::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header: asset classes.
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("futHeader"));
    header->setFixedHeight(32);
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 0, 10, 0);
    hl->setSpacing(2);
    auto* tag = new QLabel(QStringLiteral("FUTURES"), header);
    tag->setObjectName(QStringLiteral("futTag"));
    hl->addWidget(tag);
    hl->addSpacing(12);
    group_buttons_ = new QButtonGroup(this);
    group_buttons_->setExclusive(true);
    for (const QString& g : groups()) {
        auto* b = new QPushButton(g, header);
        b->setObjectName(QStringLiteral("futSeg"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("group", g);
        group_buttons_->addButton(b);
        hl->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, g]() { set_group(g); });
    }
    hl->addStretch(1);
    auto* reset = new QPushButton(QStringLiteral("RESET LAYOUT"), header);
    reset->setObjectName(QStringLiteral("futSeg"));
    reset->setCursor(Qt::PointingHandCursor);
    reset->setToolTip(QStringLiteral("Put the panels back in their original places and sizes"));
    connect(reset, &QPushButton::clicked, this, &FuturesScreen::reset_layout);
    hl->addWidget(reset);
    hl->addSpacing(8);
    status_ = new ui::StatusPill(header);
    hl->addWidget(status_);
    root->addWidget(header);

    // Selected-market strip.
    auto* strip = new QWidget(this);
    strip->setObjectName(QStringLiteral("futStrip"));
    strip->setFixedHeight(34);
    auto* sl = new QHBoxLayout(strip);
    sl->setContentsMargins(10, 0, 10, 0);
    sl->setSpacing(12);
    strip_name_ = new QLabel(strip);
    strip_name_->setObjectName(QStringLiteral("futStripName"));
    strip_price_ = new QLabel(strip);
    strip_price_->setObjectName(QStringLiteral("futStripPrice"));
    strip_change_ = new QLabel(strip);
    strip_change_->setObjectName(QStringLiteral("futStripChange"));
    strip_facts_ = new QLabel(strip);
    strip_facts_->setObjectName(QStringLiteral("futStripFacts"));
    strip_facts_->setTextFormat(Qt::RichText);
    strip_facts_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    sl->addWidget(strip_name_);
    sl->addWidget(strip_price_);
    sl->addWidget(strip_change_);
    sl->addWidget(strip_facts_, 1);
    root->addWidget(strip);

    top_split_ = new QSplitter(Qt::Horizontal, this);
    top_split_->addWidget(build_chart_panel());
    top_split_->addWidget(build_curve_panel());

    bottom_split_ = new QSplitter(Qt::Horizontal, this);
    bottom_split_->addWidget(build_months_panel());
    bottom_split_->addWidget(build_side_tabs());

    right_split_ = new QSplitter(Qt::Vertical, this);
    right_split_->addWidget(top_split_);
    right_split_->addWidget(bottom_split_);

    body_split_ = new QSplitter(Qt::Horizontal, this);
    body_split_->addWidget(build_board());
    body_split_->addWidget(right_split_);
    for (auto* sp : {top_split_, bottom_split_, right_split_, body_split_}) {
        sp->setHandleWidth(3); // wide enough to find and drag
        sp->setChildrenCollapsible(false);
        connect(sp, &QSplitter::splitterMoved, this, [this]() { layout_save_->start(); });
    }
    root->addWidget(body_split_, 1);

    for (auto* p : std::as_const(panels_))
        connect(p, &futures::PanelFrame::swap_requested, this, &FuturesScreen::swap_panels);

    // Where each pane's numbers come from.
    panels_.value(QStringLiteral("board"))
        ->set_source(QStringLiteral("Yahoo"),
                     QStringLiteral("Yahoo Finance quotes for each root's continuous front month (ROOT=F), "
                                    "about ten minutes behind the exchange."));
    panels_.value(QStringLiteral("chart"))
        ->set_source(QStringLiteral("Yahoo bars"),
                     QStringLiteral("Daily and intraday bars from Yahoo Finance. The continuous series is Yahoo's "
                                    "own front-month stitch; a single month is that contract alone."));
    panels_.value(QStringLiteral("curve"))
        ->set_source(QStringLiteral("Yahoo · a quote per month"),
                     QStringLiteral("One Yahoo Finance quote per listed delivery month. Past curves are each "
                                    "month's daily close on that date."));
    panels_.value(QStringLiteral("months"))
        ->set_source(QStringLiteral("Yahoo"),
                     QStringLiteral("Price, volume and open interest as Yahoo Finance reports them; last trading "
                                    "day as Yahoo lists it. Carry is computed here from those prices."));
    update_detail_source();

    layout_save_ = new QTimer(this);
    layout_save_->setSingleShot(true);
    layout_save_->setInterval(400);
    connect(layout_save_, &QTimer::timeout, this, &FuturesScreen::save_layout);
    apply_stretch();
    apply_default_sizes();
    restore_layout();
}

QStringList FuturesScreen::default_order() {
    return {QStringLiteral("board"), QStringLiteral("chart"), QStringLiteral("curve"), QStringLiteral("months"),
            QStringLiteral("detail")};
}

QStringList FuturesScreen::current_order() const {
    // The places, in a fixed sequence: the left column, the upper row's two
    // halves, the lower row's two halves.
    QStringList out;
    auto id_at = [](QSplitter* sp, int i) {
        auto* f = qobject_cast<futures::PanelFrame*>(sp->widget(i));
        return f ? f->id() : QString();
    };
    out << id_at(body_split_, 0) << id_at(top_split_, 0) << id_at(top_split_, 1) << id_at(bottom_split_, 0)
        << id_at(bottom_split_, 1);
    return out;
}

void FuturesScreen::apply_stretch() {
    // How extra room is shared when the window grows. A splitter keeps this
    // on the pane, not the place, so it is set again after every swap.
    const QList<QPair<QSplitter*, QList<int>>> k = {
        {body_split_, {0, 1}}, {right_split_, {5, 4}}, {top_split_, {1, 1}}, {bottom_split_, {2, 3}}};
    for (const auto& [sp, f] : k)
        for (int i = 0; i < f.size(); ++i)
            sp->setStretchFactor(i, f[i]);
}

void FuturesScreen::apply_default_sizes() {
    // The board needs its six columns; the forward curve as much room as the
    // price chart; and the detail pane — spread and positioning charts, the
    // ticket — more than the contract table, which scrolls sideways if it must.
    body_split_->setSizes({460, 1260});
    right_split_->setSizes({520, 420});
    top_split_->setSizes({650, 650});
    bottom_split_->setSizes({560, 740});
}

void FuturesScreen::swap_panels(const QString& a, const QString& b) {
    auto* fa = panels_.value(a);
    auto* fb = panels_.value(b);
    if (!fa || !fb || fa == fb)
        return;
    auto* sa = qobject_cast<QSplitter*>(fa->parentWidget());
    auto* sb = qobject_cast<QSplitter*>(fb->parentWidget());
    if (!sa || !sb)
        return;
    const int ia = sa->indexOf(fa), ib = sb->indexOf(fb);
    const QList<int> sizes_a = sa->sizes(), sizes_b = sb->sizes();
    // Through a stand-in: a widget cannot take a place while it holds another.
    auto* stand_in = new QWidget;
    sa->replaceWidget(ia, stand_in);
    sb->replaceWidget(ib, fa);
    sa->replaceWidget(ia, fb);
    delete stand_in;
    fa->show();
    fb->show();
    sa->setSizes(sizes_a); // the places keep their sizes; the panes trade them
    if (sb != sa)
        sb->setSizes(sizes_b);
    apply_stretch();
    save_layout();
}

void FuturesScreen::restore_layout() {
    const auto r = SettingsRepository::instance().get(kLayoutKey);
    QJsonObject o;
    if (!r.is_err())
        o = QJsonDocument::fromJson(r.value().toUtf8()).object();
    if (o.value(QLatin1String("v")).toInt() == kLayoutVersion) {
        QStringList saved;
        for (const auto& v : o.value(QLatin1String("order")).toArray())
            saved << v.toString();
        const QStringList order = futures::valid_order(saved, default_order());
        // Swap into place one slot at a time.
        for (int slot = 0; slot < order.size(); ++slot) {
            const QString here = current_order().value(slot);
            if (here != order[slot])
                swap_panels(order[slot], here);
        }
        const QJsonObject sizes = o.value(QLatin1String("sizes")).toObject();
        const QList<QPair<QString, QSplitter*>> named = {{QStringLiteral("body"), body_split_},
                                                        {QStringLiteral("rows"), right_split_},
                                                        {QStringLiteral("top"), top_split_},
                                                        {QStringLiteral("bottom"), bottom_split_}};
        // Sizes only — not the splitter's whole saved state, which would also
        // bring back an old build's handle width or collapsibility.
        for (const auto& [name, sp] : named) {
            QList<int> px;
            for (const auto& v : sizes.value(name).toArray())
                px << v.toInt();
            if (px.size() == sp->count() && std::all_of(px.begin(), px.end(), [](int v) { return v > 0; }))
                sp->setSizes(px);
        }
    }
    layout_ready_ = true; // from here on, changes are the user's and are saved
}

void FuturesScreen::save_layout() {
    if (!layout_ready_)
        return;
    QJsonArray order;
    for (const QString& id : current_order())
        order.append(id);
    auto px = [](QSplitter* sp) {
        QJsonArray a;
        for (int v : sp->sizes())
            a.append(v);
        return a;
    };
    QJsonObject sizes{{"body", px(body_split_)}, {"rows", px(right_split_)}, {"top", px(top_split_)},
                      {"bottom", px(bottom_split_)}};
    const QJsonObject o{{"v", kLayoutVersion}, {"order", order}, {"sizes", sizes}};
    SettingsRepository::instance().set(kLayoutKey, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)),
                                       QStringLiteral("futures"));
}

void FuturesScreen::reset_layout() {
    layout_ready_ = false; // the swaps below are not the user's arrangement
    const QStringList order = default_order();
    for (int slot = 0; slot < order.size(); ++slot) {
        const QString here = current_order().value(slot);
        if (here != order[slot])
            swap_panels(order[slot], here);
    }
    apply_default_sizes();
    layout_ready_ = true;
    save_layout();
}

void FuturesScreen::update_detail_source() {
    auto* frame = panels_.value(QStringLiteral("detail"));
    if (!frame || !tabs_)
        return;
    // Each tab's source travels with its page, whatever order the tabs are in.
    const QVariant v = tabs_->currentWidget() ? tabs_->currentWidget()->property("futSource") : QVariant();
    const QStringList src = v.toStringList();
    frame->set_source(src.value(0), src.value(1));
}

futures::PanelFrame* FuturesScreen::build_board() {
    board_ = make_table({QStringLiteral("ROOT"), QStringLiteral("MARKET"), QStringLiteral("LAST"),
                         QStringLiteral("CHG"), QStringLiteral("%"), QStringLiteral("VOLUME")},
                        this);
    board_->horizontalHeader()->setSectionResizeMode(BName, QHeaderView::Stretch);
    board_->horizontalHeaderItem(BRoot)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    board_->horizontalHeaderItem(BName)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    board_->setMinimumWidth(340);
    connect(board_, &QTableWidget::cellClicked, this, [this](int row, int) {
        const auto list = contracts_in(group_);
        if (row >= 0 && row < list.size())
            set_root(list[row]->root);
    });
    auto* row = new QHBoxLayout;
    row->addWidget(panel_title(QStringLiteral("BOARD · continuous front month"), this));
    row->addStretch(1);
    auto* frame = new futures::PanelFrame(QStringLiteral("board"), row, board_, this);
    panels_.insert(frame->id(), frame);
    return frame;
}

futures::PanelFrame* FuturesScreen::build_chart_panel() {
    auto* host = new QWidget(this);
    auto* v = new QVBoxLayout(host);
    v->setContentsMargins(0, 0, 0, 0);
    canvas_ = new ResearchCandleCanvas(host);
    canvas_->set_show_volume(true);
    v->addWidget(canvas_);
    chart_overlay_ = new ui::LoadingOverlay(canvas_);

    auto* row = new QHBoxLayout;
    chart_title_ = panel_title(QString(), this);
    row->addWidget(chart_title_);
    continuous_btn_ = new QPushButton(QStringLiteral("CONTINUOUS"), this);
    continuous_btn_->setObjectName(QStringLiteral("futSeg"));
    continuous_btn_->setCursor(Qt::PointingHandCursor);
    continuous_btn_->setToolTip(QStringLiteral("Back to the continuous front-month series"));
    connect(continuous_btn_, &QPushButton::clicked, this, [this]() { set_chart_symbol({}); });
    row->addWidget(continuous_btn_);
    row->addStretch(1);
    period_buttons_ = new QButtonGroup(this);
    period_buttons_->setExclusive(true);
    for (const auto& p : kPeriods) {
        const QString label = QLatin1String(p.label);
        auto* b = new QPushButton(label, this);
        b->setObjectName(QStringLiteral("futSeg"));
        b->setCheckable(true);
        b->setChecked(label == period_);
        b->setCursor(Qt::PointingHandCursor);
        period_buttons_->addButton(b);
        row->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, label]() {
            period_ = label;
            load_chart();
        });
    }
    auto* frame = new futures::PanelFrame(QStringLiteral("chart"), row, host, this);
    panels_.insert(frame->id(), frame);
    return frame;
}

futures::PanelFrame* FuturesScreen::build_curve_panel() {
    curve_chart_ = new futures::CurveChart(this);
    connect(curve_chart_, &futures::CurveChart::contract_clicked, this, &FuturesScreen::set_chart_symbol);
    auto* row = new QHBoxLayout;
    curve_title_ = panel_title(QStringLiteral("FORWARD CURVE"), this);
    row->addWidget(curve_title_);
    row->addStretch(1);
    auto* frame = new futures::PanelFrame(QStringLiteral("curve"), row, curve_chart_, this);
    panels_.insert(frame->id(), frame);
    return frame;
}

futures::PanelFrame* FuturesScreen::build_months_panel() {
    months_ = make_table({QStringLiteral("MONTH"), QStringLiteral("CONTRACT"), QStringLiteral("LAST"),
                          QStringLiteral("CHG"), QStringLiteral("%"), QStringLiteral("VS FRONT"),
                          QStringLiteral("CARRY/YR"), QStringLiteral("VOLUME"), QStringLiteral("OPEN INT"),
                          QStringLiteral("LAST DAY"), QStringLiteral("DAYS")},
                         this);
    months_->horizontalHeaderItem(MMonth)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    months_->horizontalHeaderItem(MCode)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    months_->horizontalHeader()->setStretchLastSection(true);
    months_->horizontalHeaderItem(MSpread)->setToolTip(QStringLiteral("This month's price minus the front month's"));
    months_->horizontalHeaderItem(MCarry)->setToolTip(
        QStringLiteral("The spread to the front month as a percent of the front price, scaled to a year by the days "
                       "between the two last trading days. For Fed Funds and SOFR: the rate the price implies."));
    months_->horizontalHeaderItem(MExpiry)->setToolTip(QStringLiteral("Last trading day, as the vendor reports it"));
    connect(months_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row >= 0 && row < curve_.contracts.size())
            set_chart_symbol(curve_.contracts[row].symbol);
    });
    auto* row = new QHBoxLayout;
    row->addWidget(panel_title(QStringLiteral("CONTRACT MONTHS · click one to chart and trade it"), this));
    row->addStretch(1);
    auto* frame = new futures::PanelFrame(QStringLiteral("months"), row, months_, this);
    panels_.insert(frame->id(), frame);
    return frame;
}

futures::PanelFrame* FuturesScreen::build_side_tabs() {
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);

    // Positioning
    auto* pos = new QWidget(tabs_);
    auto* pv = new QVBoxLayout(pos);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(0);
    cot_summary_ = new QLabel(pos);
    cot_summary_->setObjectName(QStringLiteral("futNote"));
    cot_summary_->setWordWrap(true);
    cot_summary_->setTextFormat(Qt::RichText);
    cot_chart_ = new futures::CotChart(pos);
    pv->addWidget(cot_summary_);
    pv->addWidget(cot_chart_, 1);
    // Calendar spread
    auto* sp = new QWidget(tabs_);
    auto* sv = new QVBoxLayout(sp);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(0);
    spread_summary_ = new QLabel(sp);
    spread_summary_->setObjectName(QStringLiteral("futNote"));
    spread_summary_->setWordWrap(true);
    spread_summary_->setTextFormat(Qt::RichText);
    spread_summary_->setToolTip(QStringLiteral(
        "The later month's price minus the nearer one's, on the sessions both traded. Above zero the curve between "
        "them is in contango, below it in backwardation. Click a month in the table to pair it with the front."));
    spread_chart_ = new futures::SpreadChart(sp);
    sv->addWidget(spread_summary_);
    sv->addWidget(spread_chart_, 1);
    tabs_->addTab(sp, QStringLiteral("SPREAD"));
    tabs_->addTab(pos, QStringLiteral("POSITIONING"));

    // Ticket
    auto* tk = new QWidget(tabs_);
    auto* tv = new QVBoxLayout(tk);
    tv->setContentsMargins(8, 6, 8, 6);
    tv->setSpacing(6);
    ticket_contract_ = new QLabel(tk);
    ticket_contract_->setObjectName(QStringLiteral("futTicketContract"));
    tv->addWidget(ticket_contract_);
    auto* r1 = new QHBoxLayout;
    r1->setSpacing(6);
    buy_btn_ = new QPushButton(QStringLiteral("BUY"), tk);
    buy_btn_->setObjectName(QStringLiteral("futBuy"));
    sell_btn_ = new QPushButton(QStringLiteral("SELL"), tk);
    sell_btn_->setObjectName(QStringLiteral("futSell"));
    auto* side = new QButtonGroup(this);
    for (auto* b : {buy_btn_, sell_btn_}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        side->addButton(b);
        r1->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this]() { render_ticket(); });
    }
    buy_btn_->setChecked(true);
    qty_ = new QSpinBox(tk);
    qty_->setRange(1, 999);
    qty_->setSuffix(QStringLiteral(" contract"));
    connect(qty_, &QSpinBox::valueChanged, this, [this](int n) {
        qty_->setSuffix(n == 1 ? QStringLiteral(" contract") : QStringLiteral(" contracts"));
        render_ticket();
    });
    r1->addWidget(qty_);
    r1->addStretch(1);
    tv->addLayout(r1);
    auto* r1b = new QHBoxLayout;
    r1b->setSpacing(6);
    auto make_price = [&](const QString& hint) {
        auto* e = new QLineEdit(tk);
        e->setPlaceholderText(hint);
        auto* validator = new QDoubleValidator(0.0, 1e9, 7, e);
        // Parsed with the C locale below, so validated with it: under a
        // comma-decimal system locale "71,50" was accepted here and then read
        // as no stop at all.
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::StandardNotation);
        e->setValidator(validator);
        e->setMinimumWidth(130);
        connect(e, &QLineEdit::textChanged, this, [this](const QString&) { render_ticket(); });
        return e;
    };
    stop_ = make_price(QStringLiteral("stop (optional)"));
    target_ = make_price(QStringLiteral("target (optional)"));
    r1b->addWidget(stop_);
    r1b->addWidget(target_);
    r1b->addStretch(1);
    tv->addLayout(r1b);
    ticket_math_ = new QLabel(tk);
    ticket_math_->setObjectName(QStringLiteral("futNote"));
    ticket_math_->setTextFormat(Qt::RichText);
    ticket_math_->setWordWrap(true);
    tv->addWidget(ticket_math_);
    auto* r2 = new QHBoxLayout;
    place_btn_ = new QPushButton(QStringLiteral("PLACE PAPER ORDER"), tk);
    place_btn_->setObjectName(QStringLiteral("futPlace"));
    place_btn_->setCursor(Qt::PointingHandCursor);
    connect(place_btn_, &QPushButton::clicked, this, &FuturesScreen::place_order);
    r2->addWidget(place_btn_);
    ticket_status_ = new QLabel(tk);
    ticket_status_->setObjectName(QStringLiteral("futNote"));
    ticket_status_->setWordWrap(true);
    r2->addWidget(ticket_status_, 1);
    tv->addLayout(r2);
    account_ = new QLabel(tk);
    account_->setObjectName(QStringLiteral("futNote"));
    account_->setWordWrap(true);
    account_->setToolTip(QStringLiteral(
        "A simulated account. Orders fill at the contract's last print (about ten minutes behind the exchange), "
        "with no commission. Margin is modelled as 5% of notional — not any exchange's requirement. "
        "A stop or target entered with an order stays on the position: it is closed at the first print at or "
        "beyond the level — not at the level itself — and only while this screen is open to see the print."));
    tv->addWidget(account_);
    // Seven columns in a narrow pane: side and size share one ("LONG 2").
    positions_ = make_table({QStringLiteral("CONTRACT"), QStringLiteral("POSITION"),
                             QStringLiteral("ENTRY"), QStringLiteral("LAST"), QStringLiteral("P&L"),
                             QStringLiteral("STOP / TARGET"), QString()},
                            tk);
    positions_->horizontalHeaderItem(PContract)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The button column keeps the width its label needs; the name takes the slack.
    positions_->horizontalHeader()->setSectionResizeMode(PContract, QHeaderView::Stretch);
    positions_->horizontalHeader()->setSectionResizeMode(PClose, QHeaderView::Fixed);
    positions_->setColumnWidth(PClose, 68);
    positions_->setSelectionMode(QAbstractItemView::NoSelection);
    tv->addWidget(positions_, 1);
    tabs_->addTab(tk, QStringLiteral("TICKET"));

    // Contract terms
    terms_ = new QLabel(tabs_);
    terms_->setObjectName(QStringLiteral("futTerms"));
    terms_->setTextFormat(Qt::RichText);
    terms_->setWordWrap(true);
    terms_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    terms_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tabs_->addTab(terms_, QStringLiteral("CONTRACT"));
    sp->setProperty("futSource",
                    QStringList{QStringLiteral("Yahoo daily closes"),
                                QStringLiteral("Each month's daily close from Yahoo Finance; the spread is computed "
                                               "here, on the sessions both months traded.")});
    pos->setProperty("futSource",
                     QStringList{QStringLiteral("CFTC Commitments of Traders · weekly"),
                                 QStringLiteral("The CFTC's Commitments of Traders report: positions as of each "
                                                "Tuesday, published the following Friday.")});
    tk->setProperty("futSource",
                    QStringList{QStringLiteral("paper account · fills at the last Yahoo print"),
                                QStringLiteral("A simulated account kept on this computer. Nothing is sent to a broker.")});
    terms_->setProperty("futSource",
                        QStringList{QStringLiteral("CME Group / ICE specifications"),
                                    QStringLiteral("Contract terms from the exchanges' published specifications, "
                                                   "checked against live prints on 2026-11-16. Margin is not shown: "
                                                   "exchanges change it often and there is no feed for it.")});
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { update_detail_source(); });

    auto* row = new QHBoxLayout;
    row->addWidget(panel_title(QStringLiteral("DETAIL"), this));
    row->addStretch(1);
    auto* frame = new futures::PanelFrame(QStringLiteral("detail"), row, tabs_, this);
    panels_.insert(frame->id(), frame);
    return frame;
}

void FuturesScreen::apply_theme() {
    using namespace ui::colors;
    const QString family = QString(ui::fonts::DATA_FAMILY);
    const int px = ui::fonts::font_px(-2);
    setStyleSheet(
        QStringLiteral(
            "finterm--screens--FuturesScreen{background:%1;}"
            "QWidget#futHeader{background:%2;border-bottom:1px solid %3;}"
            "QWidget#futStrip{background:%1;border-bottom:1px solid %3;}"
            "QWidget#futPanelBar{background:%2;border-bottom:1px solid %3;border-top:1px solid %3;}"
            "QLabel{background:transparent;border:none;font-family:'%4';font-size:%5px;color:%6;}"
            "QLabel#futTag{color:%7;font-weight:700;letter-spacing:1px;}"
            "QLabel#futPanelTitle{color:%8;font-weight:700;letter-spacing:1px;}"
            "QLabel#futStripName{color:%9;font-weight:700;font-size:%10px;}"
            "QLabel#futStripPrice{color:%9;font-weight:700;font-size:%10px;}"
            "QLabel#futStripChange{font-weight:700;font-size:%10px;}"
            "QLabel#futStripFacts{color:%8;}"
            "QLabel#futNote{color:%8;padding:4px 8px;}"
            "QLabel#futTerms{color:%6;padding:10px;}"
            "QLabel#futTicketContract{color:%9;font-weight:700;}"
            "QPushButton#futSeg{background:transparent;color:%8;border:none;padding:3px 8px;font-family:'%4';"
            "font-size:%5px;font-weight:700;}"
            "QPushButton#futSeg:hover{color:%9;}"
            "QPushButton#futSeg:checked{color:%7;border-bottom:2px solid %7;}"
            "QSplitter::handle{background:%3;}"
            "QSplitter::handle:hover{background:%7;}"
            "QLabel#futSource{color:%8;font-weight:400;}"
            "QWidget#futDropMark{background:transparent;border:2px solid %7;}")
            .arg(BG_BASE(), BG_RAISED(), BORDER_DIM(), family)
            .arg(px)
            .arg(TEXT_SECONDARY(), AMBER(), TEXT_TERTIARY(), TEXT_PRIMARY())
            .arg(ui::fonts::font_px(1)) +
        QStringLiteral(
            "QTableWidget{background:%1;alternate-background-color:%2;border:none;font-family:'%3';font-size:%4px;"
            "selection-background-color:%5;}"
            "QHeaderView::section{background:%1;color:%6;border:none;border-bottom:1px solid %7;padding:3px 5px;"
            "font-family:'%3';font-size:%4px;font-weight:700;}"
            "QTableWidget::item{padding:0 5px;}"
            "QTabWidget::pane{border:0;background:%1;}"
            "QTabBar::tab{background:%8;color:%6;padding:5px 14px;border:0;border-bottom:2px solid transparent;"
            "font-family:'%3';font-size:%4px;font-weight:700;letter-spacing:1px;}"
            "QTabBar::tab:selected{color:%9;border-bottom:2px solid %9;}"
            "QSpinBox,QLineEdit{background:%8;color:%10;border:1px solid %7;padding:3px 6px;font-family:'%3';"
            "font-size:%4px;}")
            .arg(BG_BASE(), ROW_ALT(), family)
            .arg(px)
            .arg(BG_HOVER(), TEXT_TERTIARY(), BORDER_DIM(), BG_RAISED(), AMBER(), TEXT_PRIMARY()) +
        QStringLiteral(
            "QPushButton#futBuy,QPushButton#futSell,QPushButton#futPlace,QPushButton#futClose{background:%1;"
            "color:%2;border:1px solid %3;padding:4px 12px;font-family:'%4';font-size:%5px;font-weight:700;}"
            "QPushButton#futBuy:checked{background:%6;color:%7;border-color:%7;}"
            "QPushButton#futSell:checked{background:%8;color:%9;border-color:%9;}"
            "QPushButton#futPlace{color:%10;border-color:%10;}"
            "QPushButton#futPlace:disabled{color:%2;border-color:%3;}"
            "QPushButton#futClose{padding:0 8px;}")
            .arg(BG_RAISED(), TEXT_TERTIARY(), BORDER_MED(), family)
            .arg(px)
            .arg(POSITIVE_BG(), POSITIVE(), NEGATIVE_BG(), NEGATIVE(), AMBER()));
    if (curve_chart_)
        curve_chart_->update();
    if (cot_chart_)
        cot_chart_->update();
    if (spread_chart_)
        spread_chart_->update();
    render_board();
    render_strip();
    render_curve();
    render_positions();
}

// ── state ──────────────────────────────────────────────────────────────────

void FuturesScreen::restore_state(const QVariantMap& state) {
    const QString root = state.value(QStringLiteral("root")).toString();
    const QString period = state.value(QStringLiteral("period")).toString();
    for (const auto& p : kPeriods)
        if (period == QLatin1String(p.label))
            period_ = period;
    for (auto* b : period_buttons_->buttons())
        b->setChecked(b->text() == period_);
    if (const auto* s = find_contract(root)) {
        root_ = s->root;
        const QString group = state.value(QStringLiteral("group")).toString();
        bool listed = false;
        for (const auto* c : contracts_in(group))
            listed = listed || c->root == root_;
        set_group(listed ? group : s->group);
    }
}

QVariantMap FuturesScreen::save_state() const {
    return {{QStringLiteral("root"), root_}, {QStringLiteral("group"), group_}, {QStringLiteral("period"), period_}};
}

void FuturesScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    shown_ = true;
    subscribe_board();
    curve_timer_->start();
    fetch_curve();
    if (canvas_->candles().isEmpty())
        load_chart();
    if (cot_for_ != root_) {
        cot_retry_.reset(); // coming back gives a failed read fresh retries
        fetch_cot();
    }
    if (spread_failed_) { // a failed spread is tried again on return, with fresh retries
        spread_failed_ = false;
        spread_for_.clear();
        spread_retry_.reset();
    }
    fetch_spread();
    load_brackets();
    reload_positions();
    render_positions();
    sync_stream();
}

void FuturesScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    if (layout_save_ && layout_save_->isActive()) { // a divider moved just before leaving
        layout_save_->stop();
        save_layout();
    }
    // Paused while hidden: no quote subscriptions, no curve polling, no stream.
    shown_ = false;
    unsubscribe_board();
    curve_timer_->stop();
    sync_stream();
    // Prices seen before the screen was put away are not prices on return:
    // positions show a dash, and no level is acted on, until a new print.
    marks_.clear();
    mark_ts_.clear();
    mark_seen_ms_.clear();
}

// ── board ──────────────────────────────────────────────────────────────────

void FuturesScreen::set_group(const QString& group) {
    const bool was_subscribed = board_subscribed_;
    unsubscribe_board();
    group_ = group;
    for (auto* b : group_buttons_->buttons())
        b->setChecked(b->property("group").toString() == group_);
    const auto list = contracts_in(group_);
    board_->setRowCount(list.size());
    QString root = root_;
    bool listed = false;
    for (const auto* c : list)
        listed = listed || c->root == root_;
    if (!listed)
        root = list.first()->root;
    if (was_subscribed || shown_)
        subscribe_board();
    render_board();
    set_root(root);
}

void FuturesScreen::subscribe_board() {
    if (board_subscribed_)
        return;
    auto& hub = datahub::DataHub::instance();
    QStringList symbols;
    for (const auto* c : contracts_in(group_)) {
        const QString sym = continuous_symbol(c->root);
        symbols << sym;
        hub.subscribe(this, QStringLiteral("market:quote:") + sym, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            quotes_.insert(sym, v.value<services::QuoteData>());
            board_render_.request();
        });
    }
    board_subscribed_ = true;
    // subscribe() replays only quotes younger than the hub's TTL; show the
    // last known ones meanwhile rather than a column of dashes.
    if (services::seed_last_known_quotes(symbols, quotes_) > 0)
        board_render_.request();
}

void FuturesScreen::unsubscribe_board() {
    if (!board_subscribed_)
        return;
    datahub::DataHub::instance().unsubscribe(this);
    board_subscribed_ = false;
}

void FuturesScreen::render_board() {
    if (!board_)
        return;
    const auto list = contracts_in(group_);
    if (board_->rowCount() != list.size())
        board_->setRowCount(list.size());
    const QColor primary(ui::colors::TEXT_PRIMARY()), secondary(ui::colors::TEXT_SECONDARY());
    for (int r = 0; r < list.size(); ++r) {
        const auto* c = list[r];
        const auto it = quotes_.constFind(continuous_symbol(c->root));
        const bool has = it != quotes_.constEnd() && it->price > 0;
        const int dp = price_decimals(c->tick);
        put(board_, r, BRoot, c->root, QColor(ui::colors::AMBER()), false);
        put(board_, r, BName, c->name, secondary, false);
        cell(board_, r, BName)->setToolTip(QStringLiteral("%1 — %2, quoted in %3").arg(c->name, c->size, c->unit));
        put(board_, r, BLast, has ? num(it->price, dp) : fmt::placeholder(), primary);
        put(board_, r, BChg, has ? signed_num(it->change, dp) : fmt::placeholder(), tone(has ? it->change : kNaN));
        put(board_, r, BPct, has && std::isfinite(it->change_pct) ? fmt::format_percent(it->change_pct, 2, true)
                                                                   : fmt::placeholder(),
            tone(has ? it->change_pct : kNaN));
        put(board_, r, BVol, has && it->volume > 0 ? fmt::format_compact_volume(it->volume) : fmt::placeholder(),
            secondary);
        if (c->root == root_ && board_->currentRow() != r)
            board_->selectRow(r);
    }
    // The strip falls back to the board's quote until the curve arrives.
    if (curve_.contracts.isEmpty())
        render_strip();
}

// ── selected market ────────────────────────────────────────────────────────

void FuturesScreen::set_root(const QString& root) {
    const bool changed = root != root_ || curve_.root != root;
    root_ = root;
    if (!changed)
        return;
    ++curve_seq_;
    curve_in_flight_ = false;
    curve_ = {};
    past_.clear();
    past_for_.clear();
    past_failed_at_ = 0;
    front_live_ = {};
    chart_symbol_.clear();
    ticket_symbol_.clear();
    ticket_status_->clear();
    stop_->clear();
    target_->clear();
    cot_for_.clear();
    spread_retry_.reset(); // a new market gets its own retries
    cot_retry_.reset();
    spread_for_.clear();
    spread_failed_ = false;
    spread_summary_->clear();
    spread_chart_->set_message(QStringLiteral("Loading the calendar spread…"));
    months_->setRowCount(0);
    curve_chart_->set_message(QStringLiteral("Loading the %1 curve…").arg(root_));
    cot_chart_->set_message(QStringLiteral("Loading positioning…"));
    cot_summary_->clear();
    render_board();
    render_strip();
    render_contract_terms();
    render_ticket();
    if (!shown_)
        return; // showEvent loads everything
    status_->set_loading(QStringLiteral("loading %1…").arg(root_));
    fetch_curve();
    fetch_cot();
    load_chart();
}

void FuturesScreen::fetch_curve() {
    if (curve_in_flight_ || root_.isEmpty())
        return;
    curve_in_flight_ = true;
    const int seq = curve_seq_;
    services::futures::fetch_curve(root_, this, [this, seq](bool ok, Curve curve, QString err) {
        if (seq != curve_seq_)
            return; // another market was selected meanwhile
        curve_in_flight_ = false;
        if (!ok) {
            if (curve_.contracts.isEmpty()) {
                curve_chart_->set_message(QStringLiteral("The %1 curve did not load: %2").arg(root_, err));
                status_->set_error(err, [this]() { fetch_curve(); });
            } else {
                status_->set_stale(QStringLiteral("refresh failed — showing the last curve"));
            }
            return;
        }
        curve_ = curve;
        // The batch quote runs about ten minutes behind the stream. Where a
        // live print of the front month is newer than the read, the print
        // stands — otherwise the strip would snap back on every refresh.
        if (const int fi = curve_.index_of(curve_.front);
            fi >= 0 && front_live_.symbol == curve_.front && front_live_.ts > curve_.contracts[fi].ts) {
            auto& c = curve_.contracts[fi];
            c.price = front_live_.price;
            c.prev_close = front_live_.prev_close;
            c.change = front_live_.change;
            c.pct = front_live_.pct;
            c.ts = front_live_.ts;
            c.stale = false;
        }
        // A mark is the newest print known for a contract. The snapshot gives
        // one only for a month that has printed lately (a stale month's
        // "last" is days old: no P&L is struck and nothing is closed at it),
        // and never replaces a stream print that is newer than itself.
        for (const auto& c : curve_.contracts) {
            if (c.stale || c.ts < mark_ts_.value(c.symbol, 0))
                continue;
            marks_.insert(c.symbol, c.price);
            mark_ts_.insert(c.symbol, c.ts);
            mark_seen_ms_.insert(c.symbol, QDateTime::currentMSecsSinceEpoch());
        }
        // EVENT-STAMP: when the curve was read — an instant, shown in local time.
        status_->set_idle(QStringLiteral("Yahoo · ~10 min delayed · %1")
                              .arg(QDateTime::fromSecsSinceEpoch(curve_.as_of).toString(QStringLiteral("HH:mm:ss"))));
        render_curve();
        render_strip();
        render_ticket();
        check_brackets();
        render_positions();
        sync_stream();
        fetch_spread();
        if (chart_symbol_.isEmpty())
            chart_title_->setText(QStringLiteral("%1 CONTINUOUS").arg(root_));
        QStringList symbols;
        const int anchor = std::max(0, curve_.index_of(curve_.front));
        // The front month first: the past sessions are picked off its bars.
        symbols << curve_.contracts[anchor].symbol;
        for (const auto& c : curve_.contracts)
            if (c.symbol != symbols.first())
                symbols << c.symbol;
        // After a refused download, not again for five minutes: it is a
        // thirty-symbol request that holds the app-wide download gate.
        const bool backing_off = past_failed_at_ > 0 && QDateTime::currentSecsSinceEpoch() - past_failed_at_ < 300;
        if (symbols.join(QLatin1Char(',')) != past_for_ && !backing_off) {
            past_for_ = symbols.join(QLatin1Char(','));
            fetch_curve_history();
        }
    });
}

void FuturesScreen::fetch_curve_history() {
    const int seq = curve_seq_;
    services::futures::fetch_curve_history(past_for_.split(QLatin1Char(',')), this,
                                           [this, seq](bool ok, QVector<PastCurve> past, QString err) {
                                               if (seq != curve_seq_)
                                                   return;
                                               if (!ok) {
                                                   // The curve stands without its comparisons; retry with the next one.
                                                   LOG_WARN("Futures", QStringLiteral("curve history: %1").arg(err));
                                                   past_for_.clear();
                                                   past_failed_at_ = QDateTime::currentSecsSinceEpoch();
                                                   return;
                                               }
                                               past_ = past;
                                               render_curve();
                                           });
}

void FuturesScreen::render_strip() {
    if (!strip_name_)
        return;
    const auto* s = spec();
    strip_name_->setText(s ? QStringLiteral("%1 · %2").arg(s->root, s->name) : root_);
    double price = kNaN, change = kNaN, pct = kNaN;
    QStringList facts;
    const CurveSummary sum = summarize(curve_);
    if (sum.anchor >= 0) {
        const auto& front = curve_.contracts[sum.anchor];
        price = front.price;
        change = front.change;
        pct = front.pct;
        facts << QStringLiteral("front <b>%1</b> (%2)").arg(front.month_label(), front.code());
        if (s && s->rate_quoted) {
            facts << QStringLiteral("implies <b>%1%</b>").arg(num(implied_rate(front.price), 3));
            if (sum.far_month >= 0) {
                facts << QStringLiteral("%1 implies <b>%2%</b>")
                             .arg(curve_.contracts[sum.far_month].month_label(),
                                  num(implied_rate(curve_.contracts[sum.far_month].price), 3));
                // The question a stock investor asks of this curve: how much easing or tightening is priced?
                const double bp = priced_change_bp(front.price, curve_.contracts[sum.far_month].price);
                if (std::isfinite(bp) && std::abs(bp) >= 1.0)
                    // Between the two MONTHS named, not from today's policy rate:
                    // the front contract already averages what is expected in it.
                    facts << QStringLiteral("<span style='color:%1'><b>%2 bp %3</b></span> from %4 to %5")
                                 .arg(bp < 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE(),
                                      QString::number(std::abs(bp), 'f', 0),
                                      bp < 0 ? QStringLiteral("lower") : QStringLiteral("higher"), front.month_label(),
                                      curve_.contracts[sum.far_month].month_label());
            }
        } else {
            if (sum.shape != Shape::Unknown && sum.far_month >= 0)
                facts << QStringLiteral("<span style='color:%1'><b>%2</b></span> · %3 %4 vs front")
                             .arg(sum.shape == Shape::Backwardation ? ui::colors::POSITIVE()
                                  : sum.shape == Shape::Contango    ? ui::colors::NEGATIVE()
                                                                    : ui::colors::TEXT_SECONDARY(),
                                  shape_label(sum.shape), curve_.contracts[sum.far_month].month_label(),
                                  fmt::format_percent(sum.far_carry.spread_pct, 2, true));
            if (std::isfinite(sum.roll_yield_pct))
                facts << QStringLiteral("roll yield <b>%1</b>/yr (%2→%3)")
                             .arg(fmt::format_percent(sum.roll_yield_pct, 1, true), front.month_label(),
                                  curve_.contracts[sum.next].month_label());
        }
        if (front.expiry.isValid()) {
            const QString last = QStringLiteral("front's last day %1 (%2d)")
                                     .arg(front.expiry.toString(QStringLiteral("d MMM")))
                                     .arg(QDate::currentDate().daysTo(front.expiry));
            facts << (rolls_soon(front, QDate::currentDate())
                          ? QStringLiteral("<span style='color:%1'><b>%2 — rolls soon</b></span>").arg(ui::colors::WARNING(), last)
                          : last);
        }
        double oi = 0;
        bool any_oi = false;
        for (const auto& c : curve_.contracts)
            if (std::isfinite(c.oi)) {
                oi += c.oi;
                any_oi = true;
            }
        if (any_oi)
            facts << QStringLiteral("open interest %1 across %2 months").arg(fmt::format_compact(oi, 2)).arg(
                curve_.contracts.size());
    } else {
        const auto it = quotes_.constFind(continuous_symbol(root_));
        if (it != quotes_.constEnd() && it->price > 0) {
            price = it->price;
            change = it->change;
            pct = it->change_pct;
        }
    }
    strip_price_->setText(price_text(price));
    strip_change_->setText(std::isfinite(change) && std::isfinite(pct)
                               ? QStringLiteral("%1  (%2)").arg(signed_num(change, s ? price_decimals(s->tick) : 2),
                                                                fmt::format_percent(pct, 2, true))
                               : QString());
    strip_change_->setStyleSheet(QStringLiteral("color:%1;").arg(tone(change).name()));
    strip_facts_->setText(facts.join(QStringLiteral("  ·  ")));
    strip_facts_->setToolTip(
        QStringLiteral("Roll yield: what holding the front month and rolling into the next earns a long, a year — "
                       "positive when the curve is in backwardation. The shape is read about a year out."));
}

void FuturesScreen::render_curve() {
    if (!curve_chart_ || curve_.contracts.isEmpty())
        return;
    const auto* s = spec();
    const bool as_rate = s && s->rate_quoted;
    const int dp = s ? price_decimals(s->tick) : 2;
    curve_chart_->set_curve(curve_, past_, as_rate, dp);
    curve_title_->setText(as_rate ? QStringLiteral("FORWARD CURVE · rate implied by each month (100 − price)")
                                  : QStringLiteral("FORWARD CURVE · %1").arg(s ? s->unit : QString()));
    months_->horizontalHeaderItem(MCarry)->setText(as_rate ? QStringLiteral("IMPLIED") : QStringLiteral("CARRY/YR"));

    const CurveSummary sum = summarize(curve_);
    const QColor primary(ui::colors::TEXT_PRIMARY()), secondary(ui::colors::TEXT_SECONDARY()),
        dim(ui::colors::TEXT_TERTIARY());
    const QDate today = QDate::currentDate();
    const QString selected = chart_symbol_.isEmpty() && sum.anchor >= 0 ? curve_.contracts[sum.anchor].symbol
                                                                        : chart_symbol_;
    months_->setRowCount(curve_.contracts.size());
    for (int r = 0; r < curve_.contracts.size(); ++r) {
        const auto& c = curve_.contracts[r];
        const bool is_front = r == sum.anchor;
        const Carry carry = sum.anchor >= 0 && !is_front ? carry_between(curve_.contracts[sum.anchor], c) : Carry{};
        put(months_, r, MMonth, c.month_label() + (is_front ? QStringLiteral(" ◂") : QString()),
            is_front ? QColor(ui::colors::AMBER()) : primary, false);
        cell(months_, r, MMonth)->setToolTip(is_front ? QStringLiteral("The front month: the one the continuous series follows")
                                                      : QString());
        put(months_, r, MCode, c.code(), secondary, false);
        put(months_, r, MLast, num(c.price, dp), c.stale ? dim : primary);
        // An old print's change is against an old close: not today's move.
        put(months_, r, MChg, c.stale ? fmt::placeholder() : signed_num(c.change, dp), c.stale ? dim : tone(c.change));
        put(months_, r, MPct,
            !c.stale && std::isfinite(c.pct) ? fmt::format_percent(c.pct, 2, true) : fmt::placeholder(),
            c.stale ? dim : tone(c.pct));
        // A spread between a live price and an old print is not a spread.
        put(months_, r, MSpread, is_front || c.stale ? fmt::placeholder() : signed_num(carry.spread, dp), secondary);
        put(months_, r, MCarry,
            as_rate ? num(implied_rate(c.price), 3) + QStringLiteral("%")
            : !c.stale && std::isfinite(carry.annualized_pct) ? fmt::format_percent(carry.annualized_pct, 1, true)
                                                              : fmt::placeholder(),
            c.stale ? dim : secondary);
        put(months_, r, MVol, std::isfinite(c.volume) ? whole(c.volume) : fmt::placeholder(), secondary);
        put(months_, r, MOi, std::isfinite(c.oi) ? whole(c.oi) : fmt::placeholder(), secondary);
        put(months_, r, MExpiry, c.expiry.isValid() ? c.expiry.toString(QStringLiteral("d MMM yy")) : fmt::placeholder(),
            secondary);
        put(months_, r, MDays, c.expiry.isValid() ? QString::number(today.daysTo(c.expiry)) : fmt::placeholder(),
            secondary);
        if (c.stale)
            cell(months_, r, MLast)->setToolTip(QStringLiteral("No print for more than a day and a half — this is an "
                                                               "old trade, not where the month is worth now"));
        else
            cell(months_, r, MLast)->setToolTip({});
        if (c.symbol == selected && months_->currentRow() != r)
            months_->selectRow(r);
    }
}

// ── calendar spread ────────────────────────────────────────────────────────


namespace {
// A failed read is asked again on its own, further apart each time: a
// network stall at launch (2026-11-16, 30 s of timeouts) otherwise left the
// chart, spread and positioning panes on their error for good.
constexpr int kRetryDelaysMs[] = {5000, 15000, 45000, 120000};
constexpr int kRetries = int(sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]));
} // namespace

int FuturesScreen::schedule_retry(Retry& r, std::function<void()> again) {
    if (r.attempt >= kRetries)
        return 0;
    const int delay = kRetryDelaysMs[r.attempt++];
    const int gen = ++r.gen;
    Retry* rp = &r; // a member: lives exactly as long as this screen
    QTimer::singleShot(delay, this, [this, rp, gen, again = std::move(again)]() {
        if (gen == rp->gen && shown_)
            again();
    });
    return delay / 1000;
}

void FuturesScreen::fetch_spread() {
    if (curve_.contracts.isEmpty())
        return; // nothing is known yet: the pane keeps saying it is loading
    const auto pair = spread_pair(curve_, ticket_symbol_);
    if (pair.first.isEmpty()) {
        spread_for_.clear();
        spread_summary_->clear();
        spread_chart_->set_message(QStringLiteral("%1 lists no second month to spread against.").arg(root_));
        return;
    }
    const QString key = pair.first + QLatin1Char('|') + pair.second;
    if (key == spread_for_ || !shown_)
        return;
    if (key != spread_retry_key_) {
        spread_retry_.reset(); // another pair is another read with its own retries
        spread_retry_key_ = key;
    }
    spread_for_ = key;
    const int ia = curve_.index_of(pair.first), ib = curve_.index_of(pair.second);
    const QString title = QStringLiteral("<b>%1 − %2</b> · %3 less %4")
                              .arg(curve_.contracts[ib].code(), curve_.contracts[ia].code(),
                                   curve_.contracts[ib].month_label(), curve_.contracts[ia].month_label());
    spread_summary_->setText(title);
    spread_chart_->set_message(QStringLiteral("Loading the calendar spread…"));
    services::futures::fetch_spread_history(pair.first, pair.second, this,
                                            [this, key, title](bool ok, SpreadHistory history, QString err) {
        if (key != spread_for_)
            return; // another pair was picked meanwhile
        if (!ok) {
            // spread_for_ stays: the same pair is not asked for again on every
            // curve refresh. Another pick, or coming back to the screen, asks.
            spread_failed_ = true;
            const int in_s = schedule_retry(spread_retry_, [this, key]() {
                if (key == spread_for_ && spread_failed_) {
                    spread_for_.clear();
                    fetch_spread();
                }
            });
            spread_chart_->set_message(
                in_s > 0 ? QStringLiteral("The spread did not load: %1 — trying again in %2 s").arg(err, QString::number(in_s))
                         : QStringLiteral("The spread did not load: %1").arg(err));
            return;
        }
        spread_failed_ = false;
        spread_retry_.reset();
        const auto* s = spec();
        const int dp = s ? price_decimals(s->tick) : 2;
        spread_chart_->set_history(history, dp);
        const SpreadStats st = spread_stats(history);
        QString text = title + QStringLiteral("<br>Now <b>%1</b> (%2 over %3 sessions) · range %4 to %5 · average %6")
                                   .arg(signed_num(st.last, dp), signed_num(st.change, dp))
                                   .arg(st.sessions)
                                   .arg(signed_num(st.low, dp), signed_num(st.high, dp), signed_num(st.mean, dp));
        if (std::isfinite(st.range_pct))
            text += QStringLiteral(" · at <b>%1%</b> of that range").arg(QString::number(st.range_pct, 'f', 0));
        if (s && std::isfinite(st.last))
            text += QStringLiteral("<br>One spread (long the later month, short the nearer) is worth %1 per 1.00; "
                                   "it has moved %2 a contract pair over the window.")
                        .arg(dollars(s->point_value), dollars(st.change * s->point_value));
        spread_summary_->setText(text);
    });
}

// ── positioning ────────────────────────────────────────────────────────────

void FuturesScreen::fetch_cot() {
    const auto* s = spec();
    cot_for_ = root_;
    if (!s || s->cftc.isEmpty()) {
        cot_summary_->clear();
        cot_chart_->set_message(QStringLiteral("No Commitments of Traders report is paired with %1.").arg(root_));
        return;
    }
    const QString root = root_;
    services::futures::fetch_cot(s->cftc, this, [this, root](bool ok, CotReport report, QString err) {
        if (root != root_)
            return;
        if (!ok) {
            cot_for_.clear(); // also retried when the screen is next shown
            cot_summary_->clear();
            const int in_s = schedule_retry(cot_retry_, [this, root]() {
                if (root == root_ && cot_for_.isEmpty())
                    fetch_cot();
            });
            cot_chart_->set_message(
                in_s > 0 ? QStringLiteral("Positioning did not load: %1 — trying again in %2 s").arg(err, QString::number(in_s))
                         : QStringLiteral("Positioning did not load: %1").arg(err));
            return;
        }
        cot_retry_.reset();
        render_cot(report);
    });
}

void FuturesScreen::render_cot(const CotReport& report) {
    cot_chart_->set_report(report);
    const CotSummary s = summarize_cot(report);
    auto net = [](double v) {
        if (!std::isfinite(v))
            return fmt::placeholder();
        return QStringLiteral("net %1 <b>%2</b>").arg(v >= 0 ? QStringLiteral("long") : QStringLiteral("short"),
                                                      whole(std::abs(v)));
    };
    auto wow = [](double v) {
        return std::isfinite(v) ? QStringLiteral(" (%1 w/w)").arg(signed_num(v, 0)) : QString();
    };
    QStringList parts;
    parts << QStringLiteral("<b>%1</b> · positions held %2").arg(report.market.toHtmlEscaped(),
                                                                 s.date.toString(QStringLiteral("ddd d MMM yyyy")));
    QString spec_line = QStringLiteral("Large speculators %1%2").arg(net(s.nc_net), wow(s.nc_net_change));
    if (std::isfinite(s.nc_net_pct_oi))
        spec_line += QStringLiteral(", %1 of open interest").arg(fmt::format_percent(s.nc_net_pct_oi, 1));
    if (std::isfinite(s.nc_range_pct))
        spec_line += QStringLiteral(" — at <b>%1%</b> of their %2-week range")
                         .arg(QString::number(s.nc_range_pct, 'f', 0))
                         .arg(s.weeks);
    parts << spec_line;
    parts << QStringLiteral("Commercials %1%2 · open interest <b>%3</b>%4")
                 .arg(net(s.c_net), wow(s.c_net_change), whole(s.oi), wow(s.oi_change));
    cot_summary_->setText(parts.join(QStringLiteral("<br>")));
    cot_summary_->setToolTip(QStringLiteral(
        "CFTC Commitments of Traders, legacy futures-only report. Published Fridays for the Tuesday before. "
        "The range reading places the latest speculator net between the lowest (0%) and highest (100%) of the window."));
}

// ── chart ──────────────────────────────────────────────────────────────────

void FuturesScreen::set_chart_symbol(const QString& symbol) {
    chart_symbol_ = symbol;
    ticket_symbol_ = symbol;
    ticket_status_->clear();
    load_chart();
    render_curve();
    render_ticket();
    fetch_spread();
}

void FuturesScreen::load_chart(bool retry) {
    if (!retry)
        chart_retry_.reset();
    const auto& def = period_def(period_);
    const QString sym = chart_symbol_.isEmpty() ? continuous_symbol(root_) : chart_symbol_;
    const int idx = curve_.index_of(chart_symbol_);
    chart_title_->setText(chart_symbol_.isEmpty()
                              ? QStringLiteral("%1 CONTINUOUS").arg(root_)
                              : QStringLiteral("%1 · %2").arg(chart_symbol_.section(QLatin1Char('.'), 0, 0),
                                                              idx >= 0 ? curve_.contracts[idx].month_label() : QString()));
    chart_title_->setToolTip(chart_symbol_.isEmpty()
                                 ? QStringLiteral("The front month, stitched: each roll to the next month shows as a "
                                                  "jump the size of the calendar spread. Pick a month off the curve "
                                                  "for a series without them.")
                                 : QStringLiteral("One delivery month — no roll gaps, thin in its early life."));
    continuous_btn_->setVisible(!chart_symbol_.isEmpty());
    if (!shown_)
        return;
    const int seq = ++chart_seq_;
    canvas_->set_intraday(def.intraday);
    canvas_->clear();
    chart_overlay_->show_loading(QStringLiteral("LOADING %1…").arg(sym));
    QPointer<FuturesScreen> self = this;
    services::MarketDataService::instance()
        .fetch_bars( // D4-exempt: one-shot chart read; needs the ok flag and chart_seq_ supersession
            sym, QString::fromLatin1(def.period), QString::fromLatin1(def.interval),
            [self, seq](const services::MarketDataService::BarsReply& r) {
                if (!self || seq != self->chart_seq_)
                    return;
                const bool ok = r.ok;
                const auto& pts = r.bars;
                self->chart_overlay_->hide_loading();
                if (!ok || pts.isEmpty()) {
                    self->canvas_->clear();
                    self->canvas_->set_placeholder_state(ok ? ResearchCandleCanvas::PlaceholderState::NoData
                                                            : ResearchCandleCanvas::PlaceholderState::Error);
                    // An empty answer is an answer; a failure is asked again
                    // (unless another chart is picked meanwhile), and says so.
                    const int in_s = ok ? 0 : self->schedule_retry(self->chart_retry_, [self, seq]() {
                        if (self && seq == self->chart_seq_)
                            self->load_chart(/*retry=*/true);
                    });
                    if (in_s > 0)
                        self->chart_overlay_->show_loading(QStringLiteral("NO ANSWER — TRYING AGAIN IN %1 S").arg(in_s));
                    return;
                }
                self->chart_retry_.reset();
                self->canvas_->set_candles(pts, QString()); // Candle is the app's one bar type
            });
}

// ── contract terms ─────────────────────────────────────────────────────────

void FuturesScreen::render_contract_terms() {
    const auto* s = spec();
    if (!s) {
        terms_->clear();
        return;
    }
    const int dp = price_decimals(s->tick);
    auto row = [](const QString& k, const QString& v) {
        return QStringLiteral("<tr><td style='color:%1;padding:3px 18px 3px 0'>%2</td><td><b>%3</b></td></tr>")
            .arg(ui::colors::TEXT_TERTIARY(), k, v);
    };
    QString html = QStringLiteral("<table>");
    html += row(QStringLiteral("Contract"), QStringLiteral("%1 — %2").arg(s->root, s->name));
    html += row(QStringLiteral("One contract"), s->size);
    html += row(QStringLiteral("Quoted in"), s->unit);
    html += row(QStringLiteral("Minimum tick"), QStringLiteral("%1 = %2").arg(num(s->tick, std::max(dp, 2)),
                                                                              dollars(s->tick * s->point_value)));
    html += row(QStringLiteral("A 1.00 move"), dollars(s->point_value) + QStringLiteral(" per contract"));
    html += row(QStringLiteral("Positioning report"),
                s->cftc.isEmpty() ? QStringLiteral("none") : QStringLiteral("CFTC market %1").arg(s->cftc));
    html += QStringLiteral("</table><p style='color:%1'>Contract terms are the exchange's specifications. "
                           "Margin is not shown: exchanges revise it often and there is no feed for it here — "
                           "ask the broker.</p>")
                .arg(ui::colors::TEXT_TERTIARY());
    terms_->setText(html);
}

// ── ticket and paper account ───────────────────────────────────────────────

const CurveContract* FuturesScreen::ticket_contract() const {
    int idx = curve_.index_of(ticket_symbol_);
    if (idx < 0)
        idx = curve_.index_of(curve_.front);
    if (idx < 0 && !curve_.contracts.isEmpty())
        idx = 0;
    return idx >= 0 ? &curve_.contracts[idx] : nullptr;
}

double FuturesScreen::last_price(const QString& contract_symbol) const {
    const double v = marks_.value(contract_symbol, kNaN);
    return v > 0 ? v : kNaN;
}

void FuturesScreen::render_ticket() {
    if (!ticket_math_)
        return;
    const auto* s = spec();
    const CurveContract* c = ticket_contract();
    if (!s || !c) {
        ticket_contract_->setText(QStringLiteral("%1 — waiting for the curve").arg(root_));
        ticket_math_->clear();
        place_btn_->setEnabled(false);
        return;
    }
    const int dp = price_decimals(s->tick);
    ticket_contract_->setText(QStringLiteral("%1 · %2 · last %3%4")
                                  .arg(c->code(), c->month_label(), num(c->price, dp),
                                       c->stale ? QStringLiteral(" (no recent print)") : QString()));
    Ticket t;
    t.is_long = buy_btn_->isChecked();
    t.contracts = qty_->value();
    t.entry = c->price;
    t.stop = ticket_level(stop_);
    t.target = ticket_level(target_);
    const TicketResult r = evaluate(t, *s);
    QStringList parts;
    parts << QStringLiteral("Notional <b>%1</b>").arg(dollars(r.notional));
    parts << QStringLiteral("each tick <b>%1</b>").arg(dollars(r.tick_value));
    if (std::isfinite(r.risk_usd))
        parts << QStringLiteral("risk <span style='color:%1'><b>%2</b></span> (%3 ticks)")
                     .arg(ui::colors::NEGATIVE(), dollars(r.risk_usd), num(r.risk_ticks, 0));
    if (std::isfinite(r.reward_usd))
        parts << QStringLiteral("reward <span style='color:%1'><b>%2</b></span> (%3 ticks)")
                     .arg(ui::colors::POSITIVE(), dollars(r.reward_usd), num(r.reward_ticks, 0));
    if (std::isfinite(r.reward_to_risk))
        parts << QStringLiteral("reward : risk <b>%1</b>").arg(num(r.reward_to_risk, 2));
    QString text = parts.join(QStringLiteral(" · "));
    if (rolls_soon(*c, QDate::currentDate()))
        text += QStringLiteral("<br><span style='color:%1'>%2 stops trading %3 (%4 days). A position held to then is "
                               "settled or delivered — close it or roll it to a later month first.</span>")
                    .arg(ui::colors::WARNING(), c->code(), c->expiry.toString(QStringLiteral("d MMM")))
                    .arg(QDate::currentDate().daysTo(c->expiry));
    if (!r.problem.isEmpty())
        text += QStringLiteral("<br><span style='color:%1'>%2</span>").arg(ui::colors::WARNING(), r.problem);
    ticket_math_->setText(text);
    // A stale month has no price worth filling at.
    place_btn_->setEnabled(r.problem.isEmpty() && !c->stale);
    place_btn_->setToolTip(c->stale ? QStringLiteral("This month has not printed for more than a day and a half")
                                    : QString());
}

QString FuturesScreen::paper_portfolio_id() {
    if (!portfolio_id_.isEmpty())
        return portfolio_id_;
    try {
        if (const auto found = trading::pt_find_portfolio(kPaperName, kPaperExchange))
            portfolio_id_ = found->id;
        else
            portfolio_id_ = trading::pt_create_portfolio(kPaperName, kPaperBalance, QStringLiteral("USD"),
                                                         kPaperLeverage, QStringLiteral("cross"), 0.0, kPaperExchange)
                                .id;
    } catch (const std::exception& e) {
        LOG_WARN("Futures", QStringLiteral("paper account unavailable: %1").arg(QString::fromUtf8(e.what())));
    }
    return portfolio_id_;
}

void FuturesScreen::place_order() {
    const auto* s = spec();
    const CurveContract* c = ticket_contract();
    const QString pid = paper_portfolio_id();
    if (!s || !c || c->stale || pid.isEmpty()) {
        ticket_status_->setText(pid.isEmpty() ? QStringLiteral("The paper account could not be opened.")
                                              : QStringLiteral("No price to fill at."));
        return;
    }
    const bool is_long = buy_btn_->isChecked();
    const double contracts = qty_->value();
    const QString symbol = c->symbol;
    const double fill = c->price;
    const int dp = price_decimals(s->tick);
    // The paper engine prices a position as quantity × price, so a futures
    // position is stored in units of the quoted price: contracts × point value.
    const double units = contracts * s->point_value;
    try {
        const auto order = trading::pt_place_order(pid, symbol, is_long ? QStringLiteral("buy") : QStringLiteral("sell"),
                                                   QStringLiteral("market"), units, fill);
        trading::pt_fill_order(order.id, fill);
    } catch (const std::exception& e) {
        ticket_status_->setText(QStringLiteral("Order refused: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    QString status = QStringLiteral("Filled: %1 %2 %3 @ %4")
                         .arg(is_long ? QStringLiteral("bought") : QStringLiteral("sold"))
                         .arg(contracts)
                         .arg(c->code(), num(fill, dp));
    const Bracket typed{ticket_level(stop_), ticket_level(target_), is_long, {}};
    reload_positions(); // also drops levels that no longer have their position

    // What the order left open in this contract decides what the levels mean.
    const trading::PtPosition* pos = nullptr;
    for (const auto& p : std::as_const(open_))
        if (p.symbol == symbol)
            pos = &p;
    if (typed.any()) {
        const bool same_side = pos && (pos->side == QLatin1String("long")) == is_long;
        // Levels typed with an order that only reduced, closed or reversed a
        // position were sized for a trade that did not open: they are not kept.
        const QString problem = !same_side ? QStringLiteral("the order did not open or add to a position on its own side")
                                           : bracket_problem(is_long, fill, typed);
        if (problem.isEmpty()) {
            Bracket b = typed;
            b.opened_at = pos->opened_at;
            brackets_.insert(symbol, b);
            save_brackets();
        } else {
            status += QStringLiteral(" · stop/target not set: %1").arg(problem);
        }
    }
    // Left in the fields they would be measured against the next entry price.
    stop_->clear();
    target_->clear();
    ticket_status_->setText(status);
    render_positions();
    sync_stream();
}

double FuturesScreen::ticket_level(const QLineEdit* field) {
    bool ok = false;
    const double v = QLocale::c().toDouble(field->text().trimmed(), &ok);
    return ok && v > 0 ? v : kNaN;
}

void FuturesScreen::load_brackets() {
    // Once per session: the map in memory is the truth after that, and a
    // failed read on some later tab switch must not be saved back as "none".
    if (brackets_loaded_)
        return;
    const auto r = SettingsRepository::instance().get(kBracketsKey);
    if (r.is_err())
        return; // tried again on the next show
    brackets_loaded_ = true;
    const QJsonObject o = QJsonDocument::fromJson(r.value().toUtf8()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject b = it.value().toObject();
        Bracket br{json_num(b, "stop"), json_num(b, "target"), b.value(QLatin1String("long")).toBool(true),
                   b.value(QLatin1String("opened_at")).toString()};
        if (br.any())
            brackets_.insert(it.key(), br);
    }
}

void FuturesScreen::save_brackets() {
    if (!brackets_loaded_)
        return; // never write a map that was not read first
    QJsonObject o;
    for (auto it = brackets_.constBegin(); it != brackets_.constEnd(); ++it) {
        QJsonObject b{{"long", it->is_long}, {"opened_at", it->opened_at}};
        if (std::isfinite(it->stop))
            b.insert(QStringLiteral("stop"), it->stop);
        if (std::isfinite(it->target))
            b.insert(QStringLiteral("target"), it->target);
        o.insert(it.key(), b);
    }
    SettingsRepository::instance().set(kBracketsKey, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)),
                                       QStringLiteral("futures"));
}

void FuturesScreen::reconcile_brackets() {
    // Levels live only as long as the position they were set for. Whatever
    // closed it — the CLOSE button, an opposite order, an agent, a reset —
    // the book is what says so, and levels without their position go.
    if (!brackets_loaded_ || !book_ok_)
        return;
    bool changed = false;
    for (auto it = brackets_.begin(); it != brackets_.end();) {
        bool kept = false;
        for (const auto& p : std::as_const(open_))
            kept = kept || (p.symbol == it.key() && it->belongs_to(p.side == QLatin1String("long"), p.opened_at));
        if (kept) {
            ++it;
        } else {
            it = brackets_.erase(it);
            changed = true;
        }
    }
    if (changed)
        save_brackets();
}

void FuturesScreen::check_brackets() {
    // Only while the screen is open to see the print, and only on a print
    // that is still news. The fill is at that print, not at the level: a
    // market that gaps through a stop fills worse than the stop.
    if (!shown_)
        return;
    struct Hit {
        QString symbol;
        BracketHit which;
        double px;
    };
    QVector<Hit> hits;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& p : std::as_const(open_)) {
        const auto it = brackets_.constFind(p.symbol);
        const bool is_long = p.side == QLatin1String("long");
        if (it == brackets_.constEnd() || !it->belongs_to(is_long, p.opened_at))
            continue;
        if (!mark_is_fresh(mark_seen_ms_.value(p.symbol, 0), now))
            continue;
        const double px = last_price(p.symbol);
        const BracketHit which = bracket_hit(is_long, px, *it);
        if (which != BracketHit::None)
            hits.append({p.symbol, which, px});
    }
    QStringList said;
    for (const Hit& h : std::as_const(hits)) {
        const auto* s = find_contract(root_of(h.symbol));
        const QString what = h.which == BracketHit::Stop ? QStringLiteral("Stop reached") : QStringLiteral("Target reached");
        const QString code = h.symbol.section(QLatin1Char('.'), 0, 0);
        said << (close_position(h.symbol, /*report=*/false)
                     ? QStringLiteral("%1: %2 closed at %3").arg(what, code, num(h.px, s ? price_decimals(s->tick) : 2))
                     : QStringLiteral("%1 on %2, but the close was refused — the position and its levels stand").arg(what, code));
    }
    if (!said.isEmpty())
        ticket_status_->setText(said.join(QStringLiteral(" · ")));
}

bool FuturesScreen::close_position(const QString& symbol, bool report) {
    const QString pid = paper_portfolio_id();
    const double px = last_price(symbol);
    auto say = [this, report](const QString& text) {
        if (report)
            ticket_status_->setText(text);
    };
    if (pid.isEmpty() || !(px > 0)) {
        say(QStringLiteral("No price for %1 yet — cannot close.").arg(symbol));
        return false;
    }
    bool closed = false;
    try {
        for (const auto& pos : trading::pt_get_positions(pid)) {
            if (pos.symbol != symbol || !(pos.quantity > 0))
                continue;
            const auto order =
                trading::pt_place_order(pid, symbol, pos.side == QLatin1String("long") ? QStringLiteral("sell")
                                                                                        : QStringLiteral("buy"),
                                        QStringLiteral("market"), pos.quantity, px, std::nullopt, true);
            trading::pt_fill_order(order.id, px);
            closed = true;
        }
    } catch (const std::exception& e) {
        say(QStringLiteral("Close refused: %1").arg(QString::fromUtf8(e.what())));
        reload_positions();
        render_positions();
        return false;
    }
    if (closed) {
        const auto* closed_spec = find_contract(root_of(symbol));
        say(QStringLiteral("Closed %1 @ %2")
                .arg(symbol.section(QLatin1Char('.'), 0, 0), num(px, closed_spec ? price_decimals(closed_spec->tick) : 2)));
    }
    // A click that arrives after the position is already gone says nothing.
    reload_positions(); // drops the closed position's levels with it
    render_positions();
    sync_stream();
    return closed;
}

void FuturesScreen::reload_positions() {
    // The book changes only when an order fills. It is read from the paper
    // store here — on show and after each fill — and painted from memory on
    // every tick, rather than three database reads per price print.
    book_ok_ = false;
    book_error_.clear();
    open_.clear();
    account_stats_.clear();
    const QString pid = paper_portfolio_id();
    if (pid.isEmpty())
        return;
    try {
        balance_ = trading::pt_get_portfolio(pid).balance;
        for (const auto& p : trading::pt_get_positions(pid))
            if (p.quantity > 0)
                open_.append(p);
        book_ok_ = true;
    } catch (const std::exception& e) {
        book_error_ = QString::fromUtf8(e.what());
        return;
    }
    reconcile_brackets();
    try {
        const auto stats = trading::pt_get_stats(pid);
        const qint64 closed = stats.winning_trades + stats.losing_trades;
        if (closed > 0)
            account_stats_ = QStringLiteral(" · realised P&L %1 over %2 closed trade%3")
                                 .arg(dollars(stats.total_pnl))
                                 .arg(closed)
                                 .arg(closed == 1 ? QString() : QStringLiteral("s"));
    } catch (const std::exception&) {
        // the account line stands without it
    }
}

void FuturesScreen::render_positions() {
    if (!positions_ || !shown_)
        return;
    if (!book_ok_) {
        account_->setText(book_error_.isEmpty() ? QStringLiteral("Paper account unavailable.")
                                                : QStringLiteral("Paper account unavailable: %1").arg(book_error_));
        return;
    }
    const QVector<trading::PtPosition>& open = open_;
    const double balance = balance_;
    const QColor primary(ui::colors::TEXT_PRIMARY()), secondary(ui::colors::TEXT_SECONDARY());
    double open_pnl = 0;
    bool all_marked = true;
    positions_->setRowCount(open.size());
    for (int r = 0; r < open.size(); ++r) {
        const auto& p = open[r];
        const auto* s = find_contract(root_of(p.symbol));
        const int dp = s ? price_decimals(s->tick) : 2;
        const double last = last_price(p.symbol);
        // Known to this session only once it has printed; registering it here
        // is what lets the stream handler keep it marked.
        if (!marks_.contains(p.symbol))
            marks_.insert(p.symbol, kNaN);
        const double dir = p.side == QLatin1String("long") ? 1.0 : -1.0;
        const double pnl = last > 0 ? (last - p.entry_price) * p.quantity * dir : kNaN;
        if (std::isfinite(pnl))
            open_pnl += pnl;
        else
            all_marked = false;
        put(positions_, r, PContract, p.symbol.section(QLatin1Char('.'), 0, 0), primary, false);
        put(positions_, r, PPosition,
            s ? QStringLiteral("%1 %2").arg(p.side.toUpper(), num(contracts_from_units(p.quantity, *s), 0)) : p.side.toUpper(),
            tone(dir));
        put(positions_, r, PEntry, num(p.entry_price, dp), secondary);
        put(positions_, r, PLast, num(last, dp), primary);
        put(positions_, r, PPnl, std::isfinite(pnl) ? dollars(pnl) : fmt::placeholder(), tone(pnl));
        const Bracket br = brackets_.value(p.symbol);
        put(positions_, r, PLevels,
            br.any() ? QStringLiteral("%1 / %2").arg(num(br.stop, dp), num(br.target, dp)) : fmt::placeholder(), secondary);
        auto* btn = qobject_cast<QPushButton*>(positions_->cellWidget(r, PClose));
        if (!btn || btn->property("symbol").toString() != p.symbol) {
            btn = new QPushButton(QStringLiteral("CLOSE"), positions_);
            btn->setObjectName(QStringLiteral("futClose"));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setProperty("symbol", p.symbol);
            const QString sym = p.symbol;
            // Queued: closing rebuilds this table, which deletes the button.
            connect(btn, &QPushButton::clicked, this, [this, sym]() { close_position(sym, true); }, Qt::QueuedConnection);
            positions_->setCellWidget(r, PClose, btn);
        }
        btn->setEnabled(last > 0);
    }
    QString text = QStringLiteral("Paper account · cash %1").arg(dollars(balance)) + account_stats_;
    if (!open.isEmpty())
        text += QStringLiteral(" · open P&L %1%2").arg(dollars(open_pnl),
                                                         all_marked ? QString() : QStringLiteral(" (priced positions only)"));
    text += QStringLiteral(" · fills at the last print, no commission, margin modelled at 5% of notional");
    account_->setText(text);
}

void FuturesScreen::sync_stream() {
    QStringList symbols;
    // The front month, so the strip moves with the tape between curve reads.
    if (shown_ && !curve_.front.isEmpty())
        symbols << curve_.front;
    if (shown_)
        for (const auto& p : std::as_const(open_))
            if (!symbols.contains(p.symbol))
                symbols << p.symbol;
    services::MarketDataService::instance().set_stream_extras(this, symbols);
}

} // namespace finterm::screens

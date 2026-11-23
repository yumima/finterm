#include "screens/futures/FuturesWidgets.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "screens/dashboard/canvas/SelectionLink.h"
#include "screens/futures/FuturesCharts.h"
#include "screens/futures/FuturesUi.h"
#include "screens/markets/MarketChartDialog.h"
#include "services/futures/FuturesHub.h"
#include "services/markets/IndexFutures.h"
#include "services/markets/LastKnownQuote.h"
#include "storage/repositories/SettingsRepository.h"
#include "trading/PaperTrading.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace finterm::screens::widgets {

using namespace services::futures;
using namespace futures_ui;
namespace fmt = ui::formatting;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
// The paper account (as on the old FUTURES screen — the same account).
// Margin is a modelled 5% of notional: a parameter of the simulation.
constexpr double kPaperBalance = 250'000.0;
constexpr double kPaperLeverage = 20.0;
const QString kPaperName = QStringLiteral("Futures Paper");
const QString kPaperExchange = QStringLiteral("futures");
const QString kBracketsKey = QStringLiteral("futures.paper_brackets");

/// The paper account's stop / target levels — ONE map for every ticket
/// (each FUTURES page and dashboard tile has a detail widget, all on the same
/// account): read once, saved whole, and every ticket repaints on a change.
struct BracketBook {
    QHash<QString, Bracket> map;
    bool loaded = false;
    QVector<QPointer<FuturesDetailWidget>> tickets;
};
BracketBook& book() {
    static BracketBook b;
    return b;
}

enum BoardCol { BRoot, BName, BLast, BChg, BPct, BVol };
enum MonthCol { MMonth, MCode, MLast, MChg, MPct, MSpread, MCarry, MVol, MOi, MExpiry, MDays };
enum PosCol { PContract, PPosition, PEntry, PLast, PPnl, PLevels, PClose };

/// The futures root a selected symbol belongs to: a continuous contract
/// ("CL=F"), a delivery month ("CLZ26.NYM"), or an index with a future
/// (^GSPC → ES). Empty for anything else.
QString root_for(const QString& symbol) {
    if (const QString r = root_of(symbol); !r.isEmpty())
        return r;
    if (const QString r = root_of_contract(symbol); !r.isEmpty() && find_contract(r))
        return r;
    if (const auto* f = services::index_future_for(symbol); f && find_contract(f->root))
        return f->root;
    return {};
}

} // namespace

// ── base ───────────────────────────────────────────────────────────────────

FuturesWidgetBase::FuturesWidgetBase(const QString& title, const QJsonObject& cfg, QWidget* parent)
    : BaseWidget(title, parent, ui::colors::AMBER()), base_title_(title) {
    FuturesHub::instance().ensure_registered_with_hub();
    setStyleSheet(stylesheet());
    connect(this, &BaseWidget::refresh_requested, this, [this]() {
        datahub::DataHub::instance().request(FuturesHub::curve_topic(root_), true);
    });
    const QString r = cfg.value(QLatin1String("root")).toString();
    if (find_contract(r))
        root_ = r;
    retitle();
}

QJsonObject FuturesWidgetBase::config() const {
    return QJsonObject{{"root", root_}};
}

void FuturesWidgetBase::apply_config(const QJsonObject& cfg) {
    const QString r = cfg.value(QLatin1String("root")).toString();
    if (find_contract(r))
        set_root(r);
}

void FuturesWidgetBase::on_theme_changed() {
    setStyleSheet(stylesheet());
}

const ContractSpec* FuturesWidgetBase::spec() const {
    return find_contract(root_);
}

int FuturesWidgetBase::price_dp() const {
    const auto* s = spec();
    return s ? price_decimals(s->tick) : 2;
}

void FuturesWidgetBase::retitle() {
    const auto* s = spec();
    set_title(QStringLiteral("%1 · %2").arg(base_title_, s ? s->root + QStringLiteral(" ") + s->name.toUpper() : root_));
}

void FuturesWidgetBase::set_root(const QString& root) {
    if (root == root_ && (curve_.root == root || !subscribed_))
        return;
    const bool was = subscribed_;
    if (was)
        unsubscribe();
    root_ = root;
    contract_.clear();
    curve_ = {};
    past_.clear();
    curve_error_.clear();
    retitle();
    root_changed();
    if (was)
        subscribe();
    emit config_changed(config()); // reopens on the market last shown
}

void FuturesWidgetBase::pick_contract(const QString& contract_symbol) {
    const int i = curve_.index_of(contract_symbol);
    const QString label = i >= 0 ? QStringLiteral("%1 · %2").arg(curve_.contracts[i].code(), curve_.contracts[i].month_label())
                                 : contract_symbol;
    if (link_)
        link_->select(contract_symbol, label); // every widget here, this one included, follows
    else {
        contract_ = contract_symbol;
        contract_changed();
        MarketChartDialog::show_for(contract_symbol, label, this);
    }
}

void FuturesWidgetBase::on_selected(const QString& symbol) {
    const QString r = root_for(symbol);
    if (r.isEmpty())
        return; // not a futures market: these widgets stay on theirs
    const bool is_month = !root_of_contract(symbol).isEmpty();
    set_root(r);
    const QString picked = is_month ? symbol : QString();
    if (picked != contract_) {
        contract_ = picked;
        contract_changed();
    }
}

void FuturesWidgetBase::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!link_) {
        link_ = SelectionLink::of(this);
        if (link_) {
            connect(link_, &SelectionLink::selected, this,
                    [this](const QString& sym, const QString&) { on_selected(sym); });
            // Picked before this widget was first shown (its page built for it).
            if (link_->picked())
                on_selected(link_->symbol());
        }
    }
    if (!subscribed_)
        subscribe();
}

void FuturesWidgetBase::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (subscribed_)
        unsubscribe();
}

void FuturesWidgetBase::subscribe() {
    auto& hub = datahub::DataHub::instance();
    QPointer<FuturesWidgetBase> self = this;
    const QString root = root_;
    // Cache first: the curve another widget already holds paints at once.
    const QVariant held = hub.peek_raw(FuturesHub::curve_topic(root));
    if (held.canConvert<Curve>() && curve_.contracts.isEmpty()) {
        curve_ = held.value<Curve>();
        curve_changed();
    }
    hub.subscribe(this, FuturesHub::curve_topic(root), [self, root](const QVariant& v) {
        if (!self || root != self->root_ || !v.canConvert<Curve>())
            return;
        self->curve_ = v.value<Curve>();
        self->curve_error_.clear();
        self->curve_changed();
    });
    hub.subscribe_errors(this, FuturesHub::curve_topic(root), [self, root](const QString& err) {
        if (!self || root != self->root_)
            return;
        self->curve_error_ = err;
        self->curve_changed();
    });
    if (wants_past_) {
        const QVariant p = hub.peek_raw(FuturesHub::past_topic(root));
        if (p.canConvert<QVector<PastCurve>>())
            past_ = p.value<QVector<PastCurve>>();
        hub.subscribe(this, FuturesHub::past_topic(root), [self, root](const QVariant& v) {
            if (!self || root != self->root_ || !v.canConvert<QVector<PastCurve>>())
                return;
            self->past_ = v.value<QVector<PastCurve>>();
            self->curve_changed();
        });
    }
    subscribed_ = true;
    subscribe_more();
}

void FuturesWidgetBase::unsubscribe() {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    hub.unsubscribe_errors(this, FuturesHub::curve_topic(root_));
    subscribed_ = false;
}

// ── curve ──────────────────────────────────────────────────────────────────

FuturesCurveWidget::FuturesCurveWidget(const QJsonObject& cfg, QWidget* parent)
    : FuturesWidgetBase(QStringLiteral("FORWARD CURVE"), cfg, parent) {
    wants_past_ = true;
    auto* strip = new QWidget(this);
    auto* sv = new QVBoxLayout(strip);
    sv->setContentsMargins(8, 4, 8, 2);
    sv->setSpacing(2);
    auto* top = new QHBoxLayout;
    top->setSpacing(12);
    name_ = new QLabel(strip);
    name_->setObjectName(QStringLiteral("futStripName"));
    price_ = new QLabel(strip);
    price_->setObjectName(QStringLiteral("futStripPrice"));
    change_ = new QLabel(strip);
    change_->setObjectName(QStringLiteral("futStripChange"));
    top->addWidget(name_);
    top->addWidget(price_);
    top->addWidget(change_);
    top->addStretch(1);
    sv->addLayout(top);
    facts_ = new QLabel(strip);
    facts_->setObjectName(QStringLiteral("futStripFacts"));
    facts_->setTextFormat(Qt::RichText);
    facts_->setWordWrap(true);
    facts_->setToolTip(QStringLiteral("Roll yield: what holding the front month and rolling into the next earns a "
                                      "long, a year — positive when the curve is in backwardation. The shape is read "
                                      "about a year out."));
    sv->addWidget(facts_);
    content_layout()->addWidget(strip);
    chart_ = new futures::CurveChart(this);
    chart_->setToolTip(QStringLiteral("Every listed month now, against the previous close and where the same months "
                                      "settled a week and a month ago. Click a month: its chart and its spread to "
                                      "the front."));
    connect(chart_, &futures::CurveChart::contract_clicked, this, [this](const QString& s) { pick_contract(s); });
    content_layout()->addWidget(chart_, 1);
    root_changed();
}

void FuturesCurveWidget::root_changed() {
    chart_->set_message(QStringLiteral("Loading the %1 curve…").arg(root_));
    render_strip();
}

void FuturesCurveWidget::curve_changed() {
    if (curve_.contracts.isEmpty()) {
        chart_->set_message(curve_error_.isEmpty() ? QStringLiteral("Loading the %1 curve…").arg(root_)
                                                   : QStringLiteral("The %1 curve did not load: %2 — retrying")
                                                         .arg(root_, curve_error_));
    } else {
        const auto* s = spec();
        chart_->set_curve(curve_, past_, s && s->rate_quoted, price_dp());
    }
    render_strip();
}

void FuturesCurveWidget::render_strip() {
    const auto* s = spec();
    name_->setText(s ? QStringLiteral("%1 · %2").arg(s->root, s->name) : root_);
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
                const double bp = priced_change_bp(front.price, curve_.contracts[sum.far_month].price);
                if (std::isfinite(bp) && std::abs(bp) >= 1.0)
                    facts << QStringLiteral("<span style='color:%1'><b>%2 bp %3</b></span> from %4 to %5")
                                 .arg(bp < 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE(),
                                      QString::number(std::abs(bp), 'f', 0),
                                      bp < 0 ? QStringLiteral("lower") : QStringLiteral("higher"), front.month_label(),
                                      curve_.contracts[sum.far_month].month_label());
            }
        } else {
            if (sum.shape != services::futures::Shape::Unknown && sum.far_month >= 0)
                facts << QStringLiteral("<span style='color:%1'><b>%2</b></span> · %3 %4 vs front")
                             .arg(sum.shape == services::futures::Shape::Backwardation ? ui::colors::POSITIVE()
                                  : sum.shape == services::futures::Shape::Contango    ? ui::colors::NEGATIVE()
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
                          ? QStringLiteral("<span style='color:%1'><b>%2 — rolls soon</b></span>")
                                .arg(ui::colors::WARNING(), last)
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
            facts << QStringLiteral("open interest %1 across %2 months")
                         .arg(fmt::format_compact(oi, 2))
                         .arg(curve_.contracts.size());
    }
    const int dp = price_dp();
    price_->setText(num(price, dp));
    change_->setText(std::isfinite(change) && std::isfinite(pct)
                         ? QStringLiteral("%1  (%2)").arg(signed_num(change, dp), fmt::format_percent(pct, 2, true))
                         : QString());
    change_->setStyleSheet(QStringLiteral("color:%1;").arg(tone(change).name()));
    facts_->setText(facts.join(QStringLiteral("  ·  ")));
}

// ── months ─────────────────────────────────────────────────────────────────

FuturesMonthsWidget::FuturesMonthsWidget(const QJsonObject& cfg, QWidget* parent)
    : FuturesWidgetBase(QStringLiteral("CONTRACT MONTHS"), cfg, parent) {
    table_ = make_table({QStringLiteral("MONTH"), QStringLiteral("CONTRACT"), QStringLiteral("LAST"),
                         QStringLiteral("CHG"), QStringLiteral("%"), QStringLiteral("VS FRONT"),
                         QStringLiteral("CARRY/YR"), QStringLiteral("VOLUME"), QStringLiteral("OPEN INT"),
                         QStringLiteral("LAST DAY"), QStringLiteral("DAYS")},
                        this);
    table_->horizontalHeaderItem(MMonth)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeaderItem(MCode)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeaderItem(MSpread)->setToolTip(QStringLiteral("This month's price minus the front month's"));
    table_->horizontalHeaderItem(MCarry)->setToolTip(
        QStringLiteral("The spread to the front month as a percent of the front price, scaled to a year by the days "
                       "between the two last trading days. For Fed Funds and SOFR: the rate the price implies."));
    table_->horizontalHeaderItem(MExpiry)->setToolTip(QStringLiteral("Last trading day, as the vendor reports it"));
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row >= 0 && row < curve_.contracts.size())
            pick_contract(curve_.contracts[row].symbol);
    });
    content_layout()->addWidget(table_, 1);
}

void FuturesMonthsWidget::root_changed() {
    table_->setRowCount(0);
}

void FuturesMonthsWidget::curve_changed() {
    const auto* s = spec();
    const bool as_rate = s && s->rate_quoted;
    const int dp = price_dp();
    table_->horizontalHeaderItem(MCarry)->setText(as_rate ? QStringLiteral("IMPLIED") : QStringLiteral("CARRY/YR"));
    const CurveSummary sum = summarize(curve_);
    const QColor primary(ui::colors::TEXT_PRIMARY()), secondary(ui::colors::TEXT_SECONDARY()),
        dim(ui::colors::TEXT_TERTIARY());
    const QDate today = QDate::currentDate();
    const QString selected = contract_.isEmpty() && sum.anchor >= 0 ? curve_.contracts[sum.anchor].symbol : contract_;
    table_->setRowCount(curve_.contracts.size());
    for (int r = 0; r < curve_.contracts.size(); ++r) {
        const auto& c = curve_.contracts[r];
        const bool is_front = r == sum.anchor;
        const Carry carry = sum.anchor >= 0 && !is_front ? carry_between(curve_.contracts[sum.anchor], c) : Carry{};
        put(table_, r, MMonth, c.month_label() + (is_front ? QStringLiteral(" ◂") : QString()),
            is_front ? QColor(ui::colors::AMBER()) : primary, false);
        put(table_, r, MCode, c.code(), secondary, false);
        put(table_, r, MLast, num(c.price, dp), c.stale ? dim : primary);
        // An old print's change is against an old close: not today's move.
        put(table_, r, MChg, c.stale ? fmt::placeholder() : signed_num(c.change, dp), c.stale ? dim : tone(c.change));
        put(table_, r, MPct, !c.stale && std::isfinite(c.pct) ? fmt::format_percent(c.pct, 2, true) : fmt::placeholder(),
            c.stale ? dim : tone(c.pct));
        // A spread between a live price and an old print is not a spread.
        put(table_, r, MSpread, is_front || c.stale ? fmt::placeholder() : signed_num(carry.spread, dp), secondary);
        put(table_, r, MCarry,
            as_rate ? num(implied_rate(c.price), 3) + QStringLiteral("%")
            : !c.stale && std::isfinite(carry.annualized_pct) ? fmt::format_percent(carry.annualized_pct, 1, true)
                                                              : fmt::placeholder(),
            c.stale ? dim : secondary);
        put(table_, r, MVol, std::isfinite(c.volume) ? whole(c.volume) : fmt::placeholder(), secondary);
        put(table_, r, MOi, std::isfinite(c.oi) ? whole(c.oi) : fmt::placeholder(), secondary);
        put(table_, r, MExpiry, c.expiry.isValid() ? c.expiry.toString(QStringLiteral("d MMM yy")) : fmt::placeholder(),
            secondary);
        put(table_, r, MDays, c.expiry.isValid() ? QString::number(today.daysTo(c.expiry)) : fmt::placeholder(),
            secondary);
        cell(table_, r, MLast)->setToolTip(c.stale ? QStringLiteral("No print for more than a day and a half — an old "
                                                                    "trade, not where the month is worth now")
                                                   : QString());
        if (c.symbol == selected && table_->currentRow() != r)
            table_->selectRow(r);
    }
}

// ── detail: spread · positioning · ticket · contract ────────────────────────

FuturesDetailWidget::FuturesDetailWidget(const QJsonObject& cfg, QWidget* parent)
    : FuturesWidgetBase(QStringLiteral("FUTURES DETAIL"), cfg, parent) {
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
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
        "The later month's price minus the nearer one's, on the sessions both traded (Yahoo daily closes). Above "
        "zero the curve between them is in contango, below it in backwardation. Click a month on the curve or in "
        "the months table to pair it with the front."));
    spread_chart_ = new futures::SpreadChart(sp);
    sv->addWidget(spread_summary_);
    sv->addWidget(spread_chart_, 1);
    tabs_->addTab(sp, QStringLiteral("SPREAD"));
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
    tabs_->addTab(pos, QStringLiteral("POSITIONING"));
    // Ticket
    auto* tk = new QWidget(tabs_);
    build_ticket(tk);
    tabs_->addTab(tk, QStringLiteral("TICKET"));
    // Contract terms
    terms_ = new QLabel(tabs_);
    terms_->setObjectName(QStringLiteral("futTerms"));
    terms_->setTextFormat(Qt::RichText);
    terms_->setWordWrap(true);
    terms_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    terms_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tabs_->addTab(terms_, QStringLiteral("CONTRACT"));
    content_layout()->addWidget(tabs_, 1);

    connect(&services::MarketDataService::instance(), &services::MarketDataService::stream_ticks, this,
            &FuturesDetailWidget::on_ticks);
    book().tickets.removeAll(nullptr);
    book().tickets.append(this);
    root_changed();
}

void FuturesDetailWidget::build_ticket(QWidget* tk) {
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
        // Parsed with the C locale, so validated with it: under a comma-decimal
        // locale "71,50" was accepted here and then read as no stop at all.
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::StandardNotation);
        e->setValidator(validator);
        e->setMinimumWidth(110);
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
    connect(place_btn_, &QPushButton::clicked, this, &FuturesDetailWidget::place_order);
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
        "with no commission. Margin is modelled as 5% of notional — not any exchange's requirement. A stop or "
        "target entered with an order stays on the position: it is closed at the first print at or beyond the "
        "level — not at the level itself — and only while this widget is on screen to see the print."));
    tv->addWidget(account_);
    // Seven columns in a narrow pane: side and size share one ("LONG 2").
    positions_ = make_table({QStringLiteral("CONTRACT"), QStringLiteral("POSITION"), QStringLiteral("ENTRY"),
                             QStringLiteral("LAST"), QStringLiteral("P&L"), QStringLiteral("STOP / TARGET"), QString()},
                            tk);
    positions_->horizontalHeaderItem(PContract)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    positions_->horizontalHeader()->setSectionResizeMode(PContract, QHeaderView::Stretch);
    positions_->horizontalHeader()->setSectionResizeMode(PClose, QHeaderView::Fixed);
    positions_->setColumnWidth(PClose, 68);
    positions_->setSelectionMode(QAbstractItemView::NoSelection);
    positions_->setCursor(Qt::ArrowCursor);
    tv->addWidget(positions_, 1);
}

void FuturesDetailWidget::showEvent(QShowEvent* e) {
    FuturesWidgetBase::showEvent(e);
    load_brackets();
    reload_positions();
    render_positions();
    sync_stream();
}

void FuturesDetailWidget::hideEvent(QHideEvent* e) {
    FuturesWidgetBase::hideEvent(e); // drops every subscription, these included
    auto& hub = datahub::DataHub::instance();
    for (const QString& t : {spread_topic_, cot_topic_})
        if (!t.isEmpty())
            hub.unsubscribe_errors(this, t);
    spread_topic_.clear(); // re-followed on the next show (subscribe_more)
    cot_topic_.clear();
    sync_stream(); // withdraws
}

void FuturesDetailWidget::root_changed() {
    spread_summary_->clear();
    spread_chart_->set_message(QStringLiteral("Loading the calendar spread…"));
    cot_summary_->clear();
    cot_chart_->set_message(QStringLiteral("Loading positioning…"));
    ticket_status_->clear();
    stop_->clear();
    target_->clear();
    render_terms();
    render_ticket();
}

void FuturesDetailWidget::subscribe_more() {
    follow_cot();
    follow_spread();
}

void FuturesDetailWidget::curve_changed() {
    // A mark is the newest print known for a contract. The curve gives one only
    // for a month that printed lately, and never replaces a newer stream print.
    for (const auto& c : curve_.contracts) {
        if (c.stale || c.ts < mark_ts_.value(c.symbol, 0))
            continue;
        marks_.insert(c.symbol, c.price);
        mark_ts_.insert(c.symbol, c.ts);
        mark_seen_ms_.insert(c.symbol, QDateTime::currentMSecsSinceEpoch());
    }
    render_ticket();
    check_brackets();
    positions_render_.request();
    follow_spread();
}

void FuturesDetailWidget::contract_changed() {
    ticket_status_->clear();
    render_ticket();
    follow_spread();
}

void FuturesDetailWidget::follow_spread() {
    if (!subscribed_ || curve_.contracts.isEmpty())
        return;
    const auto pair = spread_pair(curve_, contract_);
    auto& hub = datahub::DataHub::instance();
    if (pair.first.isEmpty()) {
        if (!spread_topic_.isEmpty())
            hub.unsubscribe(this, spread_topic_);
        spread_topic_.clear();
        spread_summary_->clear();
        spread_chart_->set_message(QStringLiteral("%1 lists no second month to spread against.").arg(root_));
        return;
    }
    const QString topic = FuturesHub::spread_topic(pair.first, pair.second);
    if (topic == spread_topic_)
        return;
    if (!spread_topic_.isEmpty()) {
        hub.unsubscribe(this, spread_topic_);
        hub.unsubscribe_errors(this, spread_topic_);
    }
    spread_topic_ = topic;
    const int ia = curve_.index_of(pair.first), ib = curve_.index_of(pair.second);
    spread_title_ = QStringLiteral("<b>%1 − %2</b> · %3 less %4")
                        .arg(curve_.contracts[ib].code(), curve_.contracts[ia].code(),
                             curve_.contracts[ib].month_label(), curve_.contracts[ia].month_label());
    spread_summary_->setText(spread_title_);
    spread_chart_->set_message(QStringLiteral("Loading the calendar spread…"));
    QPointer<FuturesDetailWidget> self = this;
    const QVariant held = hub.peek_raw(topic);
    if (held.canConvert<SpreadHistory>())
        render_spread(held.value<SpreadHistory>());
    hub.subscribe(this, topic, [self, topic](const QVariant& v) {
        if (self && topic == self->spread_topic_ && v.canConvert<SpreadHistory>())
            self->render_spread(v.value<SpreadHistory>());
    });
    hub.subscribe_errors(this, topic, [self, topic](const QString& err) {
        if (self && topic == self->spread_topic_)
            self->spread_chart_->set_message(QStringLiteral("The spread did not load: %1 — retrying").arg(err));
    });
}

void FuturesDetailWidget::render_spread(const SpreadHistory& history) {
    const auto* s = spec();
    const int dp = price_dp();
    spread_chart_->set_history(history, dp);
    const SpreadStats st = spread_stats(history);
    QString text = spread_title_ + QStringLiteral("<br>Now <b>%1</b> (%2 over %3 sessions) · range %4 to %5 · average %6")
                                       .arg(signed_num(st.last, dp), signed_num(st.change, dp))
                                       .arg(st.sessions)
                                       .arg(signed_num(st.low, dp), signed_num(st.high, dp), signed_num(st.mean, dp));
    if (std::isfinite(st.range_pct))
        text += QStringLiteral(" · at <b>%1%</b> of that range").arg(QString::number(st.range_pct, 'f', 0));
    if (s && std::isfinite(st.last))
        text += QStringLiteral("<br>One spread (long the later month, short the nearer) is worth %1 per 1.00; it has "
                               "moved %2 a contract pair over the window.")
                    .arg(dollars(s->point_value), dollars(st.change * s->point_value));
    spread_summary_->setText(text);
}

void FuturesDetailWidget::follow_cot() {
    const auto* s = spec();
    auto& hub = datahub::DataHub::instance();
    if (!cot_topic_.isEmpty()) {
        hub.unsubscribe(this, cot_topic_);
        hub.unsubscribe_errors(this, cot_topic_);
    }
    cot_topic_.clear();
    if (!s || s->cftc.isEmpty()) {
        cot_summary_->clear();
        cot_chart_->set_message(QStringLiteral("No Commitments of Traders report is paired with %1.").arg(root_));
        return;
    }
    cot_topic_ = FuturesHub::cot_topic(s->cftc);
    const QString topic = cot_topic_;
    QPointer<FuturesDetailWidget> self = this;
    const QVariant held = hub.peek_raw(topic);
    if (held.canConvert<CotReport>())
        render_cot(held.value<CotReport>());
    hub.subscribe(this, topic, [self, topic](const QVariant& v) {
        if (self && topic == self->cot_topic_ && v.canConvert<CotReport>())
            self->render_cot(v.value<CotReport>());
    });
    hub.subscribe_errors(this, topic, [self, topic](const QString& err) {
        if (self && topic == self->cot_topic_)
            self->cot_chart_->set_message(QStringLiteral("Positioning did not load: %1 — retrying").arg(err));
    });
}

void FuturesDetailWidget::render_cot(const CotReport& report) {
    cot_chart_->set_report(report);
    const CotSummary s = summarize_cot(report);
    auto net = [](double v) {
        if (!std::isfinite(v))
            return fmt::placeholder();
        return QStringLiteral("net %1 <b>%2</b>")
            .arg(v >= 0 ? QStringLiteral("long") : QStringLiteral("short"), whole(std::abs(v)));
    };
    auto wow = [](double v) { return std::isfinite(v) ? QStringLiteral(" (%1 w/w)").arg(signed_num(v, 0)) : QString(); };
    QStringList parts;
    parts << QStringLiteral("<b>%1</b> · positions held %2")
                 .arg(report.market.toHtmlEscaped(), s.date.toString(QStringLiteral("ddd d MMM yyyy")));
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
        "CFTC Commitments of Traders, legacy futures-only report. Published Fridays for the Tuesday before. The range "
        "reading places the latest speculator net between the lowest (0%) and highest (100%) of the window."));
}

void FuturesDetailWidget::render_terms() {
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
    html += row(QStringLiteral("Minimum tick"),
                QStringLiteral("%1 = %2").arg(num(s->tick, std::max(dp, 2)), dollars(s->tick * s->point_value)));
    html += row(QStringLiteral("A 1.00 move"), dollars(s->point_value) + QStringLiteral(" per contract"));
    html += row(QStringLiteral("Positioning report"),
                s->cftc.isEmpty() ? QStringLiteral("none") : QStringLiteral("CFTC market %1").arg(s->cftc));
    html += QStringLiteral("</table><p style='color:%1'>Contract terms are the exchanges' specifications (CME Group / "
                           "ICE). Margin is not shown: exchanges revise it often and there is no feed for it here — "
                           "ask the broker.</p>")
                .arg(ui::colors::TEXT_TERTIARY());
    terms_->setText(html);
}

// ── ticket and paper account (as on the old FUTURES screen) ────────────────

const CurveContract* FuturesDetailWidget::ticket_contract() const {
    int idx = curve_.index_of(contract_);
    if (idx < 0)
        idx = curve_.index_of(curve_.front);
    if (idx < 0 && !curve_.contracts.isEmpty())
        idx = 0;
    return idx >= 0 ? &curve_.contracts[idx] : nullptr;
}

double FuturesDetailWidget::last_price(const QString& contract_symbol) const {
    const double v = marks_.value(contract_symbol, kNaN);
    return v > 0 ? v : kNaN;
}

void FuturesDetailWidget::render_ticket() {
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

QString FuturesDetailWidget::paper_portfolio_id() {
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

void FuturesDetailWidget::place_order() {
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
            book().map.insert(symbol, b);
            save_brackets();
        } else {
            status += QStringLiteral(" · stop/target not set: %1").arg(problem);
        }
    }
    stop_->clear();
    target_->clear();
    ticket_status_->setText(status);
    render_positions();
    sync_stream();
}

double FuturesDetailWidget::ticket_level(const QLineEdit* field) {
    bool ok = false;
    const double v = QLocale::c().toDouble(field->text().trimmed(), &ok);
    return ok && v > 0 ? v : kNaN;
}

void FuturesDetailWidget::load_brackets() {
    // Once per session: the map in memory is the truth after that, and a
    // failed read on a later show must not be saved back as "none".
    if (book().loaded)
        return;
    const auto r = SettingsRepository::instance().get(kBracketsKey);
    if (r.is_err())
        return;
    book().loaded = true;
    const QJsonObject o = QJsonDocument::fromJson(r.value().toUtf8()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject b = it.value().toObject();
        Bracket br{json_num(b, "stop"), json_num(b, "target"), b.value(QLatin1String("long")).toBool(true),
                   b.value(QLatin1String("opened_at")).toString()};
        if (br.any())
            book().map.insert(it.key(), br);
    }
}

void FuturesDetailWidget::save_brackets() {
    if (!book().loaded)
        return; // never write a map that was not read first
    QJsonObject o;
    for (auto it = book().map.constBegin(); it != book().map.constEnd(); ++it) {
        QJsonObject b{{"long", it->is_long}, {"opened_at", it->opened_at}};
        if (std::isfinite(it->stop))
            b.insert(QStringLiteral("stop"), it->stop);
        if (std::isfinite(it->target))
            b.insert(QStringLiteral("target"), it->target);
        o.insert(it.key(), b);
    }
    SettingsRepository::instance().set(kBracketsKey, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)),
                                       QStringLiteral("futures"));
    for (const auto& t : book().tickets) // every ticket shows the same levels
        if (t)
            t->positions_render_.request();
}

void FuturesDetailWidget::reconcile_brackets() {
    // Levels live only as long as the position they were set for.
    if (!book().loaded || !book_ok_)
        return;
    bool changed = false;
    for (auto it = book().map.begin(); it != book().map.end();) {
        bool kept = false;
        for (const auto& p : std::as_const(open_))
            kept = kept || (p.symbol == it.key() && it->belongs_to(p.side == QLatin1String("long"), p.opened_at));
        if (kept) {
            ++it;
        } else {
            it = book().map.erase(it);
            changed = true;
        }
    }
    if (changed)
        save_brackets();
}

void FuturesDetailWidget::check_brackets() {
    // Only while on screen to see the print, and only on a print that is still
    // news. The fill is at that print, not at the level.
    if (!isVisible())
        return;
    struct Hit {
        QString symbol;
        BracketHit which;
        double px;
    };
    QVector<Hit> hits;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& p : std::as_const(open_)) {
        const auto it = book().map.constFind(p.symbol);
        const bool is_long = p.side == QLatin1String("long");
        if (it == book().map.constEnd() || !it->belongs_to(is_long, p.opened_at))
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
        const auto* s = find_contract(root_of_contract(h.symbol));
        const QString what = h.which == BracketHit::Stop ? QStringLiteral("Stop reached") : QStringLiteral("Target reached");
        const QString code = h.symbol.section(QLatin1Char('.'), 0, 0);
        said << (close_position(h.symbol, /*report=*/false)
                     ? QStringLiteral("%1: %2 closed at %3").arg(what, code, num(h.px, s ? price_decimals(s->tick) : 2))
                     : QStringLiteral("%1 on %2, but the close was refused — the position and its levels stand")
                           .arg(what, code));
    }
    if (!said.isEmpty())
        ticket_status_->setText(said.join(QStringLiteral(" · ")));
}

bool FuturesDetailWidget::close_position(const QString& symbol, bool report) {
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
            const auto order = trading::pt_place_order(
                pid, symbol, pos.side == QLatin1String("long") ? QStringLiteral("sell") : QStringLiteral("buy"),
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
        const auto* cs = find_contract(root_of_contract(symbol));
        say(QStringLiteral("Closed %1 @ %2")
                .arg(symbol.section(QLatin1Char('.'), 0, 0), num(px, cs ? price_decimals(cs->tick) : 2)));
    }
    reload_positions(); // drops the closed position's levels with it
    render_positions();
    sync_stream();
    return closed;
}

void FuturesDetailWidget::reload_positions() {
    // The book changes only when an order fills: read on show and after each
    // fill, painted from memory on every tick.
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

void FuturesDetailWidget::render_positions() {
    if (!positions_ || !isVisible())
        return;
    if (!book_ok_) {
        account_->setText(book_error_.isEmpty() ? QStringLiteral("Paper account unavailable.")
                                                : QStringLiteral("Paper account unavailable: %1").arg(book_error_));
        return;
    }
    const QColor primary(ui::colors::TEXT_PRIMARY()), secondary(ui::colors::TEXT_SECONDARY());
    double open_pnl = 0;
    bool all_marked = true;
    positions_->setRowCount(open_.size());
    for (int r = 0; r < open_.size(); ++r) {
        const auto& p = open_[r];
        const auto* s = find_contract(root_of_contract(p.symbol));
        const int dp = s ? price_decimals(s->tick) : 2;
        const double last = last_price(p.symbol);
        const double dir = p.side == QLatin1String("long") ? 1.0 : -1.0;
        const double pnl = last > 0 ? (last - p.entry_price) * p.quantity * dir : kNaN;
        if (std::isfinite(pnl))
            open_pnl += pnl;
        else
            all_marked = false;
        put(positions_, r, PContract, p.symbol.section(QLatin1Char('.'), 0, 0), primary, false);
        put(positions_, r, PPosition,
            s ? QStringLiteral("%1 %2").arg(p.side.toUpper(), num(contracts_from_units(p.quantity, *s), 0))
              : p.side.toUpper(),
            tone(dir));
        put(positions_, r, PEntry, num(p.entry_price, dp), secondary);
        put(positions_, r, PLast, num(last, dp), primary);
        put(positions_, r, PPnl, std::isfinite(pnl) ? dollars(pnl) : fmt::placeholder(), tone(pnl));
        const Bracket br = book().map.value(p.symbol);
        put(positions_, r, PLevels,
            br.any() ? QStringLiteral("%1 / %2").arg(num(br.stop, dp), num(br.target, dp)) : fmt::placeholder(),
            secondary);
        auto* btn = qobject_cast<QPushButton*>(positions_->cellWidget(r, PClose));
        if (!btn || btn->property("symbol").toString() != p.symbol) {
            btn = new QPushButton(QStringLiteral("CLOSE"), positions_);
            btn->setObjectName(QStringLiteral("futClose"));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setProperty("symbol", p.symbol);
            const QString sym = p.symbol;
            // Queued: closing rebuilds this table, which deletes the button.
            connect(btn, &QPushButton::clicked, this, [this, sym]() { close_position(sym, true); },
                    Qt::QueuedConnection);
            positions_->setCellWidget(r, PClose, btn);
        }
        btn->setEnabled(last > 0);
    }
    QString text = QStringLiteral("Paper account · cash %1").arg(dollars(balance_)) + account_stats_;
    if (!open_.isEmpty())
        text += QStringLiteral(" · open P&L %1%2")
                    .arg(dollars(open_pnl), all_marked ? QString() : QStringLiteral(" (priced positions only)"));
    text += QStringLiteral(" · fills at the last print, no commission, margin modelled at 5% of notional");
    account_->setText(text);
}

void FuturesDetailWidget::on_ticks(const QVector<services::StreamTick>& ticks) {
    if (!isVisible())
        return;
    bool moved = false;
    for (const auto& t : ticks) {
        bool held = false;
        for (const auto& p : std::as_const(open_))
            held = held || p.symbol == t.symbol;
        if (!held || !(t.price > 0))
            continue;
        const qint64 ts = t.time_ms > 0 ? t.time_ms / 1000 : QDateTime::currentSecsSinceEpoch();
        if (ts < mark_ts_.value(t.symbol, 0))
            continue;
        marks_.insert(t.symbol, t.price);
        mark_ts_.insert(t.symbol, ts);
        mark_seen_ms_.insert(t.symbol, QDateTime::currentMSecsSinceEpoch());
        moved = true;
    }
    if (moved) {
        check_brackets();
        positions_render_.request();
    }
}

void FuturesDetailWidget::sync_stream() {
    // The open positions' contracts, so their P&L and levels follow the tape
    // (the front month streams through FuturesHub).
    QStringList symbols;
    if (isVisible())
        for (const auto& p : std::as_const(open_))
            if (!symbols.contains(p.symbol))
                symbols << p.symbol;
    services::MarketDataService::instance().set_stream_extras(this, symbols);
}

// ── board ──────────────────────────────────────────────────────────────────

FuturesBoardWidget::FuturesBoardWidget(const QJsonObject& cfg, QWidget* parent)
    : BaseWidget(QStringLiteral("FUTURES BOARD"), parent, ui::colors::AMBER()) {
    setStyleSheet(stylesheet());
    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(4, 0, 4, 0);
    bl->setSpacing(0);
    groups_ = new QButtonGroup(this);
    for (const auto& g : services::futures::groups()) {
        auto* b = new QPushButton(g, bar);
        b->setObjectName(QStringLiteral("futSeg"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("group", g);
        groups_->addButton(b);
        bl->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, g]() {
            set_group(g);
            emit config_changed(config());
        });
    }
    bl->addStretch(1);
    content_layout()->addWidget(bar);
    bar_ = bar;
    table_ = make_table({QStringLiteral("ROOT"), QStringLiteral("MARKET"), QStringLiteral("LAST"), QStringLiteral("CHG"),
                         QStringLiteral("%"), QStringLiteral("VOLUME")},
                        this);
    table_->horizontalHeader()->setSectionResizeMode(BName, QHeaderView::Stretch);
    table_->horizontalHeaderItem(BRoot)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeaderItem(BName)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        const auto list = contracts_in(group_);
        if (row < 0 || row >= list.size())
            return;
        selected_root_ = list[row]->root;
        // The screen's chart, curve, months and detail follow.
        MarketChartDialog::show_for(continuous_symbol(list[row]->root),
                                    list[row]->root + QStringLiteral(" · ") + list[row]->name, this);
    });
    content_layout()->addWidget(table_, 1);
    auto* note = new QLabel(QStringLiteral("Continuous front month (Yahoo, ~10 min delayed). Click a market: the "
                                           "screen's chart, curve, months and detail follow."),
                            this);
    note->setObjectName(QStringLiteral("futNote"));
    note->setWordWrap(true);
    content_layout()->addWidget(note);
    connect(this, &BaseWidget::refresh_requested, this, [this]() {
        QStringList topics;
        for (const auto* c : contracts_in(group_))
            topics << QStringLiteral("market:quote:") + continuous_symbol(c->root);
        datahub::DataHub::instance().request(topics, true);
    });
    apply_config(cfg);
}

QJsonObject FuturesBoardWidget::config() const {
    QJsonObject o{{"group", group_}};
    if (fixed_)
        o["fixed"] = true;
    return o;
}

void FuturesBoardWidget::apply_config(const QJsonObject& cfg) {
    const QString g = cfg.value(QLatin1String("group")).toString();
    // A board on a FUTURES page: the header picks the group — no tabs here.
    fixed_ = cfg.value(QLatin1String("fixed")).toBool(false);
    bar_->setVisible(!fixed_);
    set_group(services::futures::groups().contains(g) ? g : services::futures::groups().first());
    set_title(QStringLiteral("FUTURES BOARD · ") + group_);
}

void FuturesBoardWidget::on_theme_changed() {
    setStyleSheet(stylesheet());
    render_.request();
}

void FuturesBoardWidget::set_group(const QString& group) {
    const bool was = subscribed_;
    if (was) {
        datahub::DataHub::instance().unsubscribe(this);
        subscribed_ = false;
    }
    group_ = group;
    set_title(QStringLiteral("FUTURES BOARD · ") + group_);
    for (auto* b : groups_->buttons())
        b->setChecked(b->property("group").toString() == group_);
    table_->setRowCount(contracts_in(group_).size());
    if (was)
        subscribe();
    render_.request();
}

void FuturesBoardWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!link_) {
        link_ = SelectionLink::of(this);
        if (link_) {
            // The board's selected root is a mark: it follows clicks and,
            // before any, the market the page's chart draws.
            connect(link_, &SelectionLink::marked, this, [this](const QString& sym) {
                const QString r = root_for(sym);
                if (!r.isEmpty()) {
                    selected_root_ = r;
                    render_.request();
                }
            });
            if (const QString r = root_for(link_->symbol()); !r.isEmpty())
                selected_root_ = r;
        }
    }
    if (!subscribed_)
        subscribe();
}

void FuturesBoardWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (subscribed_) {
        datahub::DataHub::instance().unsubscribe(this);
        subscribed_ = false;
    }
}

void FuturesBoardWidget::subscribe() {
    auto& hub = datahub::DataHub::instance();
    QStringList symbols;
    QPointer<FuturesBoardWidget> self = this;
    for (const auto* c : contracts_in(group_)) {
        const QString sym = continuous_symbol(c->root);
        symbols << sym;
        hub.subscribe(this, QStringLiteral("market:quote:") + sym, [self, sym](const QVariant& v) {
            if (!self || !v.canConvert<services::QuoteData>())
                return;
            self->quotes_.insert(sym, v.value<services::QuoteData>());
            self->render_.request();
        });
    }
    subscribed_ = true;
    // Cache first: the last known quotes while the hub refreshes.
    if (services::seed_last_known_quotes(symbols, quotes_) > 0)
        render_.request();
}

void FuturesBoardWidget::render() {
    const auto list = contracts_in(group_);
    if (table_->rowCount() != list.size())
        table_->setRowCount(list.size());
    const QColor primary(ui::colors::TEXT_PRIMARY()), secondary(ui::colors::TEXT_SECONDARY());
    for (int r = 0; r < list.size(); ++r) {
        const auto* c = list[r];
        const auto it = quotes_.constFind(continuous_symbol(c->root));
        const bool has = it != quotes_.constEnd() && it->price > 0;
        const bool stale = has && services::last_known_is_stale(continuous_symbol(c->root));
        const int dp = price_decimals(c->tick);
        put(table_, r, BRoot, c->root, QColor(ui::colors::AMBER()), false);
        put(table_, r, BName, c->name, secondary, false);
        cell(table_, r, BName)->setToolTip(QStringLiteral("%1 — %2, quoted in %3").arg(c->name, c->size, c->unit));
        put(table_, r, BLast, has ? num(it->price, dp) : fmt::placeholder(), stale ? secondary : primary);
        put(table_, r, BChg, has ? signed_num(it->change, dp) : fmt::placeholder(),
            stale ? secondary : tone(has ? it->change : kNaN));
        put(table_, r, BPct,
            has && std::isfinite(it->change_pct) ? fmt::format_percent(it->change_pct, 2, true) : fmt::placeholder(),
            stale ? secondary : tone(has ? it->change_pct : kNaN));
        put(table_, r, BVol, has && it->volume > 0 ? fmt::format_compact_volume(it->volume) : fmt::placeholder(),
            secondary);
        if (c->root == selected_root_ && table_->currentRow() != r)
            table_->selectRow(r);
    }
}

} // namespace finterm::screens::widgets

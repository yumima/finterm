#include "screens/ownership/ScanPanels.h"

#include "screens/ownership/FirmDetailPanel.h"
#include "screens/ownership/OwnershipUi.h"
#include "services/ownership/OwnershipService.h"
#include "ui/components/TooltipText.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace finterm::screens {

using namespace finterm::ownership;
using namespace finterm::screens::ownership_ui;

namespace {

QString control_style() {
    return QString("QComboBox,QSpinBox{color:%1;background:%2;border:1px solid %3;padding:2px 6px;"
                   "font-size:12px;} QCheckBox{color:%4;font-size:12px;}")
        .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(), ui::colors::BORDER_DIM(),
             ui::colors::TEXT_SECONDARY());
}

QLabel* control_label(const QString& t) {
    auto* l = new QLabel(t);
    l->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    return l;
}

void connect_open(QTableWidget* t, QObject* owner, std::function<void(const QString&)> fn) {
    QObject::connect(t, &QTableWidget::cellClicked, owner, [t, fn](int r, int) {
        auto* it = t->item(r, 0);
        const QString sym = it ? it->data(Qt::UserRole).toString() : QString();
        if (!sym.isEmpty())
            fn(sym);
    });
}

QTableWidgetItem* ticker_cell(const QString& sym, const QString& name) {
    auto* it = cell(sym.isEmpty() ? fmt::placeholder() : sym, sym.isEmpty() ? QString() : ui::colors::AMBER());
    it->setData(Qt::UserRole, sym);
    it->setToolTip(ui::tooltip_wrap(sym.isEmpty() ? QStringLiteral("%1 — the filing named no ticker").arg(name)
                                                  : QStringLiteral("%1 — click to open in Equity Research").arg(name)));
    return it;
}

} // namespace

// ── INSIDER BUYS ─────────────────────────────────────────────────────────────

InsiderBuysPanel::InsiderBuysPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(control_style());
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto title = section_title(QStringLiteral("INSIDER BUYS"));
    title_note_ = title.right;
    root->addWidget(title.widget);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    bar->addWidget(control_label(QStringLiteral("Window")));
    window_ = new QComboBox;
    window_->addItem(QStringLiteral("7 days"), 7);
    window_->addItem(QStringLiteral("14 days"), 14);
    window_->addItem(QStringLiteral("30 days"), 30);
    window_->setCurrentIndex(2);
    bar->addWidget(window_);
    bar->addWidget(control_label(QStringLiteral("Min insiders")));
    min_insiders_ = new QSpinBox;
    min_insiders_->setRange(1, 6);
    min_insiders_->setValue(2);
    min_insiders_->setToolTip(ui::tooltip_wrap(
        QStringLiteral("Minimum distinct buyers that make a cluster.")));
    bar->addWidget(min_insiders_);
    bar->addWidget(control_label(QStringLiteral("Min value")));
    min_value_ = new QComboBox;
    min_value_->addItem(QStringLiteral("$0"), 0.0);
    min_value_->addItem(QStringLiteral("$25K"), 25'000.0);
    min_value_->addItem(QStringLiteral("$100K"), 100'000.0);
    min_value_->addItem(QStringLiteral("$1M"), 1'000'000.0);
    min_value_->setCurrentIndex(1);
    bar->addWidget(min_value_);
    exclude_ten_pct_ = new QCheckBox(QStringLiteral("exclude 10% owners"));
    exclude_ten_pct_->setChecked(true);
    exclude_ten_pct_->setToolTip(ui::tooltip_wrap(
        QStringLiteral("10% owner, not a company insider")));
    bar->addWidget(exclude_ten_pct_);
    exclude_plan_ = new QCheckBox(QStringLiteral("exclude 10b5-1 plans"));
    exclude_plan_->setChecked(true);
    exclude_plan_->setToolTip(ui::tooltip_wrap(
        QStringLiteral("Plan trade, decided months earlier")));
    bar->addWidget(exclude_plan_);
    bar->addStretch(1);
    root->addLayout(bar);
    for (auto* w : {static_cast<QWidget*>(window_), static_cast<QWidget*>(min_value_)})
        connect(static_cast<QComboBox*>(w), &QComboBox::activated, this, [this](int) { refresh(); });
    connect(min_insiders_, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    connect(exclude_ten_pct_, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    connect(exclude_plan_, &QCheckBox::toggled, this, [this](bool) { refresh(); });

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("Insiders"),
                         QStringLiteral("Roles"), QStringLiteral("Value"), QStringLiteral("Shares"),
                         QStringLiteral("Avg price"), QStringLiteral("Stake +"), QStringLiteral("Traded"),
                         QStringLiteral("Filed"), QStringLiteral("1w"), QStringLiteral("1m")});
    connect_open(table_, this, [this](const QString& s) { emit stock_activated(s); });
    root->addWidget(table_, 1);
    foot_ = note_label();
    root->addWidget(foot_);

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::insider_buys_updated, this, [this]() { render(); });
    connect(&svc, &services::OwnershipService::form4_status_changed, this, [this](const QString&) { render(); });
    render();
}

void InsiderBuysPanel::refresh() {
    InsiderBuyQuery q;
    q.days = window_->currentData().toInt();
    q.min_insiders = min_insiders_->value();
    q.min_value = min_value_->currentData().toDouble();
    q.exclude_ten_pct = exclude_ten_pct_->isChecked();
    q.exclude_plan = exclude_plan_->isChecked();
    services::OwnershipService::instance().load_insider_buys(q);
    render();
}

void InsiderBuysPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const auto& ib = svc.insider_buys();

    QStringList head;
    head << QStringLiteral("open-market purchases (code P), last %1 days, one row per issuer").arg(ib.query.days);
    if (ib.loaded) {
        head << QStringLiteral("%1 of %2 business days read").arg(ib.days_scanned).arg(ib.days_wanted);
        if (ib.last_scanned.isValid())
            head << QStringLiteral("latest filings %1").arg(ib.last_scanned.toString(QStringLiteral("d MMM")));
    }
    if (svc.form4_scanning() || !svc.form4_status().isEmpty())
        head << svc.form4_status();
    title_note_->setText(head.join(QStringLiteral(" · ")));

    if (!ib.loaded) {
        table_->setRowCount(0);
        foot_->setText(svc.insider_buys_loading() ? QStringLiteral("Reading the Form 4 store…") : QString());
        return;
    }
    if (!ib.error.isEmpty()) {
        table_->setRowCount(0);
        foot_->setText(ib.error);
        return;
    }
    table_->setUpdatesEnabled(false);
    table_->setRowCount(ib.rows.size());
    for (int i = 0; i < ib.rows.size(); ++i) {
        const auto& r = ib.rows[i];
        table_->setItem(i, 0, ticker_cell(r.symbol, r.issuer));
        table_->setItem(i, 1, cell(r.issuer));
        auto* n = num_cell(QString::number(r.insiders), r.insiders >= 2 ? ui::colors::GREEN() : QString());
        n->setToolTip(ui::tooltip_wrap(
            QStringLiteral("%1 distinct buyer%2, %3 trade%4%5")
                .arg(r.insiders)
                .arg(r.insiders == 1 ? QString() : QStringLiteral("s"))
                .arg(r.trades)
                .arg(r.trades == 1 ? QString() : QStringLiteral("s"))
                .arg(r.plan_unknown > 0
                         ? QStringLiteral("\n%1 row(s) read before the 10b5-1 column existed").arg(r.plan_unknown)
                         : QString())));
        table_->setItem(i, 2, n);
        auto* roles = cell(r.roles.join(QStringLiteral(", ")), ui::colors::TEXT_SECONDARY());
        roles->setToolTip(ui::tooltip_wrap(r.roles.join(QStringLiteral("\n"))));
        table_->setItem(i, 3, roles);
        table_->setItem(i, 4, num_cell(fmt::format_compact(r.value), ui::colors::GREEN()));
        table_->setItem(i, 5, num_cell(fmt::format_compact(r.shares)));
        table_->setItem(i, 6, num_cell(r.avg_price ? fmt::format_money(*r.avg_price) : fmt::placeholder()));
        auto* stake = num_cell(r.stake_increase ? pct_or_dash(r.stake_increase, 0, true) : fmt::placeholder(),
                               r.stake_increase && *r.stake_increase >= 0.25 ? ui::colors::AMBER() : QString());
        stake->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Shares bought ÷ shares held before")));
        table_->setItem(i, 7, stake);
        table_->setItem(i, 8,
                        cell(r.first_trade == r.last_trade || !r.first_trade.isValid()
                                 ? date_or_dash(r.last_trade)
                                 : QStringLiteral("%1 – %2").arg(r.first_trade.toString(QStringLiteral("d MMM")),
                                                                 r.last_trade.toString(QStringLiteral("d MMM")))));
        table_->setItem(i, 9, cell(date_or_dash(r.last_filed), ui::colors::TEXT_SECONDARY()));
        auto ret = [&](const std::optional<double>& v, const QDate& due) {
            auto* it = num_cell(v ? pct_or_dash(v, 1, true) : fmt::placeholder(), signed_colour(v));
            if (!v)
                it->setToolTip(ui::tooltip_wrap(
                    due > QDate::currentDate()
                        ? QStringLiteral("Not yet — the window ends %1.").arg(due.toString(QStringLiteral("d MMM")))
                    : ib.returns_ok ? QStringLiteral("No close on file for this window.")
                                    : QStringLiteral("Prices loading.")));
            return it;
        };
        table_->setItem(i, 10, ret(r.ret_1w, r.last_trade.addDays(7)));
        table_->setItem(i, 11, ret(r.ret_1m, r.last_trade.addMonths(1)));
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(1, qMin(qMax(table_->columnWidth(1), 160), 260));
    table_->setColumnWidth(3, qMin(table_->columnWidth(3), 200));
    fill_width(table_, {1, 3}); // Company and the (capped) Roles share the spare width

    QStringList notes;
    notes << QStringLiteral("%1 issuers").arg(ib.rows.size());
    if (ib.days_scanned < ib.days_wanted)
        notes << QStringLiteral("the store is still being filled — %1 business days of the window are unread")
                     .arg(ib.days_wanted - ib.days_scanned);
    notes << QStringLiteral("sells are never ranked: insiders sell for a dozen reasons and buy for one");
    notes << QStringLiteral("1w / 1m: the stock since the last trade, after the fact");
    foot_->setText(notes.join(QStringLiteral(" · ")));
}

// ── SHORT-CONSTRAINED ────────────────────────────────────────────────────────

ShortRankPanel::ShortRankPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(control_style());
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto title = section_title(QStringLiteral("SHORT-CONSTRAINED"));
    title_note_ = title.right;
    auto* bar = new QHBoxLayout;
    bar->addWidget(title.widget, 1);
    bar->addWidget(control_label(QStringLiteral("Rank by")));
    sort_ = new QComboBox;
    sort_->addItem(QStringLiteral("short interest ÷ 13F shares"), QStringLiteral("sirio"));
    sort_->addItem(QStringLiteral("days to cover"), QStringLiteral("dtc"));
    sort_->addItem(QStringLiteral("change vs prior settlement"), QStringLiteral("change"));
    sort_->addItem(QStringLiteral("shares short"), QStringLiteral("short"));
    connect(sort_, &QComboBox::activated, this, [this](int) { refresh(); });
    bar->addWidget(sort_);
    root->addLayout(bar);

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("SI ÷ 13F"),
                         QStringLiteral("Days to cover"), QStringLiteral("Shares short"), QStringLiteral("vs prior"),
                         QStringLiteral("13F shares"), QStringLiteral("Holders"), QStringLiteral("Avg daily vol")});
    connect_open(table_, this, [this](const QString& s) { emit stock_activated(s); });
    root->addWidget(table_, 1);
    foot_ = note_label();
    root->addWidget(foot_);

    connect(&services::OwnershipService::instance(), &services::OwnershipService::short_rank_updated, this,
            [this]() { render(); });
    render();
}

void ShortRankPanel::refresh() {
    services::OwnershipService::instance().load_short_rank(sort_->currentData().toString());
    render();
}

void ShortRankPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const auto& sr = svc.short_rank();
    if (!sr.loaded) {
        table_->setRowCount(0);
        title_note_->setText(
            svc.short_rank_loading()
                ? QStringLiteral("fetching the latest FINRA settlement — ~15,000 symbols, three pages…")
                : QString());
        foot_->clear();
        return;
    }
    if (!sr.error.isEmpty()) {
        table_->setRowCount(0);
        title_note_->clear();
        foot_->setText(sr.error);
        return;
    }
    title_note_->setText(QStringLiteral("FINRA settlement %1 · published ~%2 · 13F shares as of %3")
                             .arg(sr.settlement.toString(QStringLiteral("d MMM yyyy")),
                                  sr.published_after.toString(QStringLiteral("d MMM")),
                                  sr.quarter.isValid() ? sr.quarter.toString(QStringLiteral("d MMM yyyy"))
                                                       : QStringLiteral("(no index)")));

    // Decile cut points so a reading can be placed: the flag on the stock
    // page fires at the top decile, and this is where that decile starts.
    const double top_decile = sr.sirio_deciles.size() >= 9 ? sr.sirio_deciles[8] : -1.0;

    table_->setUpdatesEnabled(false);
    table_->setRowCount(sr.rows.size());
    for (int i = 0; i < sr.rows.size(); ++i) {
        const auto& r = sr.rows[i];
        table_->setItem(i, 0, ticker_cell(r.symbol, r.name));
        table_->setItem(i, 1, cell(r.name));
        auto* sirio = num_cell(r.sirio ? QString::number(*r.sirio, 'f', 3) : fmt::placeholder(),
                               r.sirio && top_decile > 0 && *r.sirio >= top_decile ? ui::colors::AMBER() : QString());
        sirio->setToolTip(ui::tooltip_wrap(
            QStringLiteral("Shares short ÷ 13F shares · amber = top decile")));
        table_->setItem(i, 2, sirio);
        table_->setItem(i, 3,
                        num_cell(r.days_to_cover ? QString::number(*r.days_to_cover, 'f', 1) : fmt::placeholder(),
                                 r.days_to_cover && *r.days_to_cover >= 5.0 ? ui::colors::AMBER() : QString()));
        table_->setItem(i, 4, num_cell(compact_or_dash(r.shares_short)));
        table_->setItem(
            i, 5,
            num_cell(pct_or_dash(r.change_pct, 0, true),
                     r.change_pct ? (*r.change_pct > 0 ? ui::colors::RED() : ui::colors::GREEN()) : QString()));
        table_->setItem(i, 6, num_cell(compact_or_dash(r.inst_shares)));
        table_->setItem(i, 7, num_cell(QString::number(r.holders)));
        table_->setItem(i, 8, num_cell(compact_or_dash(r.avg_daily_volume), ui::colors::TEXT_SECONDARY()));
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(1, qMin(qMax(table_->columnWidth(1), 160), 260));
    fill_width(table_, {1});

    foot_->setText(QStringLiteral("%1 symbols on the settlement date · %2 joined to a 13F denominator · "
                                  "%3 funds and ETFs dropped by name · holders ≥ 50 · a risk screen, not a "
                                  "portfolio: the effect is small-cap and value-weighted zero, and squeeze "
                                  "cost at high utilisation eats two-thirds of it (Schultz 2024)")
                       .arg(sr.symbols)
                       .arg(sr.joined)
                       .arg(sr.funds_dropped));
}

// ── 13F MOVERS ───────────────────────────────────────────────────────────────

MoversPanel::MoversPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(control_style());
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto title = section_title(QStringLiteral("13F MOVERS"));
    title_note_ = title.right;
    auto* bar = new QHBoxLayout;
    bar->addWidget(title.widget, 1);
    bar->addWidget(control_label(QStringLiteral("Sort")));
    sort_ = new QComboBox;
    sort_->addItem(QStringLiteral("holders gained"), QStringLiteral("breadth_up"));
    sort_->addItem(QStringLiteral("holders lost"), QStringLiteral("breadth_down"));
    sort_->addItem(QStringLiteral("most new positions"), QStringLiteral("new"));
    sort_->addItem(QStringLiteral("most closed positions"), QStringLiteral("closed"));
    sort_->addItem(QStringLiteral("focused books buying"), QStringLiteral("focused_buying"));
    sort_->addItem(QStringLiteral("focused books selling"), QStringLiteral("focused_selling"));
    sort_->addItem(QStringLiteral("most concentrated"), QStringLiteral("concentration"));
    connect(sort_, &QComboBox::activated, this, [this](int) { refresh(); });
    bar->addWidget(sort_);
    root->addLayout(bar);

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("Holders"),
                         QStringLiteral("Δ holders"), QStringLiteral("New"), QStringLiteral("Closed"),
                         QStringLiteral("Added"), QStringLiteral("Reduced"), QStringLiteral("Focused net shares"),
                         QStringLiteral("Top-10 %"), QStringLiteral("13F value")});
    connect_open(table_, this, [this](const QString& s) { emit stock_activated(s); });
    root->addWidget(table_, 1);
    foot_ = note_label();
    root->addWidget(foot_);

    connect(&services::OwnershipService::instance(), &services::OwnershipService::movers_updated, this,
            [this]() { render(); });
    render();
}

void MoversPanel::refresh() {
    services::OwnershipService::instance().load_movers(sort_->currentData().toString());
    render();
}

void MoversPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const auto& m = svc.movers();
    if (!m.loaded) {
        table_->setRowCount(0);
        title_note_->setText(svc.movers_loading()
                                 ? QStringLiteral("computing breadth across every filer — the first pass over a "
                                                  "quarter pair takes about a minute, then it is cached")
                                 : QString());
        foot_->clear();
        return;
    }
    if (!m.error.isEmpty()) {
        table_->setRowCount(0);
        title_note_->clear();
        foot_->setText(m.error);
        return;
    }
    title_note_->setText(QStringLiteral("Q%1 %2 vs Q%3 %4 · filed by %5 · positions up to 135 days old")
                             .arg((m.quarter.month() - 1) / 3 + 1)
                             .arg(m.quarter.year())
                             .arg((m.prior_quarter.month() - 1) / 3 + 1)
                             .arg(m.prior_quarter.year())
                             .arg(m.quarter.addDays(45).toString(QStringLiteral("d MMM yyyy"))));

    table_->setUpdatesEnabled(false);
    table_->setRowCount(m.rows.size());
    for (int i = 0; i < m.rows.size(); ++i) {
        const auto& r = m.rows[i];
        table_->setItem(i, 0, ticker_cell(r.ticker, r.name));
        auto* name = cell(r.name + (r.fund ? QStringLiteral("  (fund)") : QString()),
                          r.fund ? ui::colors::TEXT_SECONDARY() : QString());
        table_->setItem(i, 1, name);
        table_->setItem(i, 2, num_cell(QLocale().toString(r.holders)));
        auto* d =
            num_cell((r.delta_holders > 0 ? QStringLiteral("+") : QString()) + QLocale().toString(r.delta_holders),
                     r.delta_holders > 0   ? ui::colors::GREEN()
                     : r.delta_holders < 0 ? ui::colors::RED()
                                           : QString());
        if (r.holders_prior < r.holders / 10)
            d->setToolTip(ui::tooltip_wrap(
                QStringLiteral("%1 holders a quarter ago — likely a new listing or mapping")
                    .arg(r.holders_prior)));
        table_->setItem(i, 3, d);
        table_->setItem(i, 4, num_cell(QLocale().toString(r.new_holders), ui::colors::GREEN()));
        table_->setItem(i, 5, num_cell(QLocale().toString(r.closed), ui::colors::RED()));
        table_->setItem(i, 6, num_cell(QLocale().toString(r.added)));
        table_->setItem(i, 7, num_cell(QLocale().toString(r.reduced)));
        QString net = fmt::placeholder();
        if (r.focused_net_shares)
            net = (*r.focused_net_shares >= 0 ? QStringLiteral("+") : QStringLiteral("−")) +
                  fmt::format_compact(std::fabs(*r.focused_net_shares));
        auto* fn = num_cell(net, signed_colour(r.focused_net_shares));
        fn->setToolTip(ui::tooltip_wrap(
            QStringLiteral(
                "Net shares across focused books · %1 holders")
                .arg(r.focused_holders)));
        table_->setItem(i, 8, fn);
        table_->setItem(i, 9,
                        num_cell(pct_or_dash(r.top10_share, 0),
                                 r.top10_share && *r.top10_share >= 0.6 ? ui::colors::AMBER() : QString()));
        table_->setItem(i, 10, num_cell(fmt::format_compact(r.value), ui::colors::TEXT_SECONDARY()));
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(1, qMin(qMax(table_->columnWidth(1), 160), 260));
    fill_width(table_, {1});

    foot_->setText(QStringLiteral("%1 securities with 20 or more holders · funds and ETFs dropped by name · "
                                  "breadth change is one of two ownership signals that replicates (Chen, Hong "
                                  "& Stein: 0.67%/mo, half of it in the short leg) · only filers present in "
                                  "both quarters are compared")
                       .arg(m.rows.size()));
}

// ── BIG MOVES ────────────────────────────────────────────────────────────────

BigMovesPanel::BigMovesPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(control_style());
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto title = section_title(QStringLiteral("BIG MOVES"));
    title_note_ = title.right;
    auto* bar = new QHBoxLayout;
    bar->addWidget(title.widget, 1);
    side_ = new QComboBox;
    side_->addItem(QStringLiteral("buys and sells"), QStringLiteral("both"));
    side_->addItem(QStringLiteral("buys"), QStringLiteral("buys"));
    side_->addItem(QStringLiteral("sells"), QStringLiteral("sells"));
    bar->addWidget(side_);
    bar->addWidget(control_label(QStringLiteral("Rank by")));
    sort_ = new QComboBox;
    sort_->addItem(QStringLiteral("est. $ size"), QStringLiteral("value"));
    sort_->addItem(QStringLiteral("share of the manager's book"), QStringLiteral("weight"));
    bar->addWidget(sort_);
    active_only_ = new QCheckBox(QStringLiteral("active books only"));
    active_only_->setChecked(true);
    bar->addWidget(active_only_);
    for (auto* c : {side_, sort_})
        connect(c, &QComboBox::activated, this, [this](int) { refresh(); });
    connect(active_only_, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    root->addLayout(bar);

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("Manager"),
                         QStringLiteral("Move"), QStringLiteral("Est. $"), QStringLiteral("% of book"),
                         QStringLiteral("Shares now"), QStringLiteral("Shares before"), QStringLiteral("Filed for")});
    table_->horizontalHeaderItem(4)->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "Shares changed × the price the filing implies. 13F shows quarter-end holdings, not trades, so this "
        "is an estimate of the dollars that moved.")));
    table_->horizontalHeaderItem(5)->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "The move as a share of the manager's own 13F book — the book it went into for a buy, the one it came "
        "out of for a sale.")));
    connect_open(table_, this, [this](const QString& s) { emit stock_activated(s); });
    root->addWidget(table_, 1);
    foot_ = note_label();
    root->addWidget(foot_);

    connect(&services::OwnershipService::instance(), &services::OwnershipService::big_moves_updated, this,
            [this]() { render(); });
    render();
}

void BigMovesPanel::refresh() {
    services::OwnershipService::instance().load_big_moves(side_->currentData().toString(),
                                                          sort_->currentData().toString(),
                                                          active_only_->isChecked());
    render();
}

void BigMovesPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const auto& b = svc.big_moves();
    if (!b.loaded || svc.big_moves_loading()) {
        if (!b.loaded)
            table_->setRowCount(0);
        title_note_->setText(svc.big_moves_loading()
                                 ? QStringLiteral("comparing every filer's newest filing with its last — the first "
                                                  "pass after new data takes a few minutes, then it is cached")
                                 : QString());
        if (!b.loaded)
            foot_->clear();
        return;
    }
    if (!b.error.isEmpty()) {
        table_->setRowCount(0);
        title_note_->clear();
        foot_->setText(b.error);
        return;
    }
    QDate newest;
    for (const auto& r : b.rows)
        newest = std::max(newest, r.quarter);
    title_note_->setText(newest.isValid()
                             ? QStringLiteral("each manager's newest filing vs its previous one · newest Q%1 %2 · "
                                              "positions up to 135 days old")
                                   .arg((newest.month() - 1) / 3 + 1)
                                   .arg(newest.year())
                             : QString());

    table_->setUpdatesEnabled(false);
    table_->setRowCount(b.rows.size());
    for (int i = 0; i < b.rows.size(); ++i) {
        const auto& r = b.rows[i];
        table_->setItem(i, 0, ticker_cell(r.ticker, r.issuer));
        table_->setItem(i, 1, cell(r.issuer));
        auto* mgr = cell(r.manager);
        mgr->setToolTip(ui::tooltip_wrap(QStringLiteral("%1 · 13F book %2 across %3 names")
                                             .arg(r.manager, fmt::format_compact(r.book_value))
                                             .arg(r.position_count)));
        table_->setItem(i, 2, mgr);
        const bool buy = r.flow >= 0;
        const QString move = r.kind == QLatin1String("new")      ? QStringLiteral("NEW")
                             : r.kind == QLatin1String("exited") ? QStringLiteral("EXITED")
                             : r.kind == QLatin1String("added")  ? QStringLiteral("ADDED")
                                                                 : QStringLiteral("TRIMMED");
        table_->setItem(i, 3, cell(move, buy ? ui::colors::GREEN() : ui::colors::RED()));
        table_->setItem(i, 4, num_cell((buy ? QStringLiteral("+$") : QStringLiteral("−$")) +
                                           fmt::format_compact(std::fabs(r.flow)),
                                       buy ? ui::colors::GREEN() : ui::colors::RED()));
        table_->setItem(i, 5, num_cell(r.book_pct ? QStringLiteral("%1%").arg(*r.book_pct, 0, 'f', 1)
                                                  : fmt::placeholder(),
                                       r.book_pct && *r.book_pct >= 10.0 ? ui::colors::AMBER() : QString()));
        table_->setItem(i, 6, num_cell(r.shares > 0 ? fmt::format_compact(r.shares) : fmt::placeholder()));
        auto* before = num_cell(r.prior_shares > 0 ? fmt::format_compact(r.prior_shares) : fmt::placeholder(),
                                ui::colors::TEXT_SECONDARY());
        before->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Held at the previous filing (%1), restated in today's shares when a split or CUSIP change came between")
                                                .arg(r.prior_quarter.toString(QStringLiteral("d MMM yyyy")))));
        table_->setItem(i, 7, before);
        // A quarter still being filed is a sample of its filers, not all of them.
        const bool partial = b.partial_quarters.contains(r.quarter.toString(Qt::ISODate));
        auto* q = num_cell(QStringLiteral("Q%1 %2%3")
                               .arg((r.quarter.month() - 1) / 3 + 1)
                               .arg(r.quarter.year())
                               .arg(partial ? QStringLiteral(" · filing") : QString()),
                           partial ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY());
        if (partial)
            q->setToolTip(ui::tooltip_wrap(QStringLiteral(
                "This quarter's 13Fs are still coming in: the move is real, but other managers' moves in the "
                "same quarter may not be filed yet")));
        table_->setItem(i, 8, q);
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(1, qMin(qMax(table_->columnWidth(1), 160), 240));
    table_->setColumnWidth(2, qMin(qMax(table_->columnWidth(2), 160), 260));
    fill_width(table_, {1, 2});

    foot_->setText(
        QStringLiteral("%1 moves of %2 or more · %3 · splits and CUSIP changes undone; a newly listed stock is "
                       "not counted as bought; filers whose book changed shape (new filing entity, units error) "
                       "left out · funds and ETFs dropped by name · 13F is filed up to 45 days after quarter end")
            .arg(b.rows.size())
            .arg(fmt::format_compact(b.floor))
            .arg(b.active_only ? QStringLiteral("books of %1 names or fewer").arg(b.max_positions)
                               : QStringLiteral("all books, index giants included")));
}

// ── LARGEST FUNDS ────────────────────────────────────────────────────────────

namespace {

QString quarter_label(const QDate& q) {
    return q.isValid() ? QStringLiteral("Q%1 %2").arg((q.month() - 1) / 3 + 1).arg(q.year()) : fmt::placeholder();
}

QString signed_compact(double v) {
    return (v >= 0 ? QStringLiteral("+") : QStringLiteral("−")) + fmt::format_compact(std::fabs(v));
}

// Column of each field, named once: the click handler and render() both use it.
enum FundCol {
    kRank,
    kManager,
    kQuarter,
    kBook,
    kBookChange,
    kPositions,
    kNew,
    kAdded,
    kTrimmed,
    kExited,
    kNetFlow,
    kTopBuy,
    kTopSell,
    kFundCols
};

} // namespace

LargestFundsPanel::LargestFundsPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(control_style());
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto title = section_title(QStringLiteral("LARGEST FUNDS"));
    title_note_ = title.right;
    auto* bar = new QHBoxLayout;
    bar->addWidget(title.widget, 1);
    pull_status_ = control_label(QString());
    bar->addWidget(pull_status_);
    pull_btn_ = new QPushButton(QStringLiteral("PULL NEWEST FROM EDGAR"));
    pull_btn_->setCursor(Qt::PointingHandCursor);
    pull_btn_->setStyleSheet(QString("QPushButton{color:%1;background:transparent;border:1px solid %2;"
                                     "padding:3px 10px;font-size:12px;font-weight:700;}"
                                     "QPushButton:hover{color:%3;border-color:%3;}"
                                     "QPushButton:disabled{color:%4;border-color:%4;}")
                                 .arg(ui::colors::AMBER(), ui::colors::BORDER_DIM(), ui::colors::TEXT_PRIMARY(),
                                      ui::colors::TEXT_TERTIARY()));
    pull_btn_->setToolTip(ui::tooltip_wrap(
        QStringLiteral("Read the newest filings for the 100 largest filers")));
    connect(pull_btn_, &QPushButton::clicked, this,
            []() { services::OwnershipService::instance().pull_latest_filings(100); });
    bar->addWidget(pull_btn_);
    root->addLayout(bar);

    table_ =
        make_table({QStringLiteral("#"), QStringLiteral("Manager"), QStringLiteral("Filed for"),
                    QStringLiteral("13F book"), QStringLiteral("Δ book"), QStringLiteral("Names"),
                    QStringLiteral("New"), QStringLiteral("Added"), QStringLiteral("Trimmed"), QStringLiteral("Exited"),
                    QStringLiteral("Est. net flow"), QStringLiteral("Biggest buy (est. $)"),
                    QStringLiteral("Biggest sell (est. $)")});
    for (int c : {static_cast<int>(kTopBuy), static_cast<int>(kTopSell)})
        table_->horizontalHeaderItem(c)->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "The single largest position change by estimated dollars: shares changed × the price the filing "
            "implies. Splits and CUSIP changes between the filings are undone first, and a stock that listed "
            "since the last filing is not counted as bought.")));
    // A ticker in the buy/sell columns opens that stock; anywhere else on the
    // row opens the filer's book.
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        if (c == kTopBuy || c == kTopSell) {
            if (auto* it = table_->item(r, c)) {
                const QString sym = it->data(Qt::UserRole).toString();
                if (!sym.isEmpty()) {
                    emit stock_activated(sym);
                    return;
                }
            }
        }
        if (auto* it = table_->item(r, kManager)) {
            const QString cik = it->data(Qt::UserRole).toString();
            if (!cik.isEmpty()) {
                selected_cik_ = cik;
                selected_from_search_ = false;
                detail_->set_firm(cik, it->data(Qt::UserRole + 1).toString());
            }
        }
    });

    // Ranking on the left, the picked fund's book on the right. A plain
    // splitter: the reader decides how much room each side gets.
    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    auto* left = new QWidget;
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(6);
    lv->addWidget(table_, 1);
    loading_ = new ui::LoadingOverlay(table_);
    foot_ = note_label();
    lv->addWidget(foot_);
    split->addWidget(left);
    detail_ = new FirmDetailPanel;
    connect(detail_, &FirmDetailPanel::navigate_to_symbol, this,
            [this](const QString& ticker) { emit stock_activated(ticker); });
    split->addWidget(detail_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::top_firms_updated, this, [this]() { render(); });
    connect(&svc, &services::OwnershipService::latest_pull_status, this, [this](const QString& t) {
        pull_status_->setText(t);
        render();
    });
    render();
}

void LargestFundsPanel::show_fund(const QString& cik) {
    if (cik.isEmpty())
        return;
    selected_cik_ = cik;
    selected_from_search_ = true;
    // A ranked fund opens at the quarter its row describes, like a click on
    // the row would; an unranked one at its newest complete quarter.
    const auto& t = services::OwnershipService::instance().top_firms();
    for (int i = 0; i < t.firms.size(); ++i) {
        if (t.firms[i].cik != cik)
            continue;
        table_->selectRow(i);
        detail_->set_firm(cik, t.firms[i].quarter.toString(Qt::ISODate));
        return;
    }
    table_->clearSelection();
    detail_->set_firm(cik);
}

void LargestFundsPanel::refresh() {
    services::OwnershipService::instance().load_top_firms();
    render();
}

void LargestFundsPanel::render() {
    auto& svc = services::OwnershipService::instance();
    pull_btn_->setEnabled(!svc.pulling_latest());
    // Up for the first load and for a reload after an EDGAR pull — the old
    // ranking stays visible underneath until the new one replaces it.
    if (svc.top_firms_loading())
        loading_->show_loading(QStringLiteral("RANKING THE LARGEST FUNDS…"));
    else
        loading_->hide_loading();
    const auto& t = svc.top_firms();
    if (!t.loaded) {
        table_->setRowCount(0);
        title_note_->setText(svc.top_firms_loading() ? QStringLiteral("ranking every filer's newest book…")
                                                     : QString());
        foot_->clear();
        return;
    }
    if (!t.error.isEmpty()) {
        table_->setRowCount(0);
        title_note_->clear();
        foot_->setText(t.error);
        return;
    }
    title_note_->setText(QStringLiteral("top %1 by US-listed stock book · newest filing %2")
                             .arg(t.firms.size())
                             .arg(quarter_label(t.newest_quarter)));

    auto move_cell = [](const std::optional<FirmMoveTop>& m) {
        if (!m)
            return cell(fmt::placeholder());
        const QString label = m->ticker.isEmpty() ? m->issuer : m->ticker;
        auto* it = cell(QStringLiteral("%1  %2").arg(label, signed_compact(m->value)),
                        m->ticker.isEmpty() ? ui::colors::TEXT_SECONDARY() : ui::colors::AMBER());
        it->setData(Qt::UserRole, m->ticker);
        it->setToolTip(
            ui::tooltip_wrap(QStringLiteral("%1 — %2 estimated%3")
                                 .arg(m->issuer, signed_compact(m->value),
                                      m->ticker.isEmpty() ? QString() : QStringLiteral(". Click to open it."))));
        return it;
    };

    table_->setUpdatesEnabled(false);
    table_->setRowCount(t.firms.size());
    for (int i = 0; i < t.firms.size(); ++i) {
        const auto& f = t.firms[i];
        table_->setItem(i, kRank, num_cell(QString::number(i + 1), ui::colors::TEXT_TERTIARY()));
        auto* name = cell(f.manager, ui::colors::TEXT_PRIMARY());
        name->setData(Qt::UserRole, f.cik);
        name->setData(Qt::UserRole + 1, f.quarter.toString(Qt::ISODate));
        name->setToolTip(ui::tooltip_wrap(QStringLiteral("CIK %1 — click to show the whole book")
                                              .arg(f.cik)));
        table_->setItem(i, kManager, name);
        const bool partial = t.partial_quarters.contains(f.quarter.toString(Qt::ISODate));
        auto* q = cell(quarter_label(f.quarter) + (partial ? QStringLiteral(" ·EDGAR") : QString()),
                       partial ? ui::colors::CYAN() : ui::colors::TEXT_SECONDARY());
        table_->setItem(i, kQuarter, q);
        table_->setItem(i, kBook, num_cell(QStringLiteral("$") + fmt::format_compact(f.book_value)));
        if (f.prior_book_value && *f.prior_book_value > 0) {
            const double chg = (f.book_value - *f.prior_book_value) / *f.prior_book_value * 100.0;
            auto* c = num_cell(QStringLiteral("%1%2%").arg(chg >= 0 ? "+" : "").arg(QString::number(chg, 'f', 1)),
                               chg >= 0 ? ui::colors::GREEN() : ui::colors::RED());
            c->setToolTip(ui::tooltip_wrap(
                QStringLiteral("Book value against %1")
                    .arg(quarter_label(*f.prior_quarter))));
            table_->setItem(i, kBookChange, c);
        } else {
            auto* c = cell(QStringLiteral("first filing"), ui::colors::TEXT_TERTIARY());
            c->setToolTip(ui::tooltip_wrap(
                QStringLiteral("No earlier filing under this CIK")));
            table_->setItem(i, kBookChange, c);
        }
        table_->setItem(i, kPositions, num_cell(QLocale().toString(f.position_count)));
        if (f.has_moves) {
            table_->setItem(i, kNew, num_cell(QLocale().toString(f.new_positions), ui::colors::GREEN()));
            table_->setItem(i, kAdded, num_cell(QLocale().toString(f.added)));
            table_->setItem(i, kTrimmed, num_cell(QLocale().toString(f.trimmed)));
            table_->setItem(i, kExited, num_cell(QLocale().toString(f.exited), ui::colors::RED()));
            const double net = f.bought_value - f.sold_value;
            auto* nf =
                num_cell((net >= 0 ? QStringLiteral("+$") : QStringLiteral("−$")) + fmt::format_compact(std::fabs(net)),
                         net >= 0 ? ui::colors::GREEN() : ui::colors::RED());
            nf->setToolTip(ui::tooltip_wrap(
                QStringLiteral("Estimated: bought $%1, sold $%2")
                    .arg(fmt::format_compact(f.bought_value), fmt::format_compact(f.sold_value))));
            table_->setItem(i, kNetFlow, nf);
            table_->setItem(i, kTopBuy, move_cell(f.top_buy));
            table_->setItem(i, kTopSell, move_cell(f.top_sell));
        } else {
            for (int c = kNew; c < kFundCols; ++c)
                table_->setItem(i, c, cell(fmt::placeholder(), ui::colors::TEXT_TERTIARY()));
        }
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(kManager, qMin(qMax(table_->columnWidth(kManager), 180), 280));
    fill_width(table_, {kManager});
    // Keep the open fund's row and its book in step across a reload. After an
    // EDGAR pull the row can describe a newer quarter; set_firm follows it
    // there (and is a no-op when nothing changed). A fund that has left the
    // ranking takes its highlight and its book with it, rather than leaving
    // another fund's row highlighted beside the old book.
    if (!selected_cik_.isEmpty()) {
        int found = -1;
        for (int i = 0; i < t.firms.size(); ++i)
            if (t.firms[i].cik == selected_cik_) {
                found = i;
                break;
            }
        if (found >= 0) {
            table_->selectRow(found);
            detail_->set_firm(selected_cik_, t.firms[found].quarter.toString(Qt::ISODate));
        } else if (selected_from_search_) {
            // A searched fund need not be ranked; keep its book open.
            table_->clearSelection();
        } else {
            table_->clearSelection();
            selected_cik_.clear();
            detail_->set_firm(QString());
        }
    }

    foot_->setText(
        QStringLiteral("13F covers the US-listed long stock books of managers with $100M+ under SEC filing rules — "
                       "most of the world's largest asset managers and sovereign funds (e.g. Norges Bank), but not "
                       "their bonds, private assets or non-US listings. Moves compare each filer's two newest "
                       "filings; ·EDGAR marks a quarter pulled straight from EDGAR ahead of the bulk index."));
}

} // namespace finterm::screens

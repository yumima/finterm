#include "screens/ownership/ScanPanels.h"

#include "screens/ownership/OwnershipUi.h"
#include "services/ownership/OwnershipService.h"
#include "ui/components/TooltipText.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

using namespace fincept::ownership;
using namespace fincept::screens::ownership_ui;

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
    it->setToolTip(ui::tooltip_wrap(sym.isEmpty()
                                        ? QStringLiteral("%1 — the filing named no ticker").arg(name)
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
    min_insiders_->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "A cluster is a group-by with a floor you set, not a constant. Two distinct buyers is "
        "OpenInsider's default; Lakonishok & Lee measured three.")));
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
    exclude_ten_pct_->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "A 10% beneficial owner is a holder, not an insider in the sense the evidence is about; "
        "their purchase ranking inverts (Lakonishok & Lee).")));
    bar->addWidget(exclude_ten_pct_);
    exclude_plan_ = new QCheckBox(QStringLiteral("exclude 10b5-1 plans"));
    exclude_plan_->setChecked(true);
    exclude_plan_->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "A plan trade was decided months before it printed. The flag is on filings since 2023; "
        "older rows are unknown and are kept.")));
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
        n->setToolTip(ui::tooltip_wrap(QStringLiteral("%1 distinct buyer%2, %3 trade%4%5")
                                           .arg(r.insiders).arg(r.insiders == 1 ? QString() : QStringLiteral("s"))
                                           .arg(r.trades).arg(r.trades == 1 ? QString() : QStringLiteral("s"))
                                           .arg(r.plan_unknown > 0
                                                    ? QStringLiteral("\n%1 row(s) read before the 10b5-1 column existed")
                                                          .arg(r.plan_unknown)
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
            "How much the largest buy grew the buyer's own holding: shares bought over shares held "
            "before. A $50K buy from someone holding $50M is noise; the same buy doubling a stake is not.")));
        table_->setItem(i, 7, stake);
        table_->setItem(i, 8, cell(r.first_trade == r.last_trade || !r.first_trade.isValid()
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
                         QStringLiteral("Days to cover"), QStringLiteral("Shares short"),
                         QStringLiteral("vs prior"), QStringLiteral("13F shares"), QStringLiteral("Holders"),
                         QStringLiteral("Avg daily vol")});
    connect_open(table_, this, [this](const QString& s) { emit stock_activated(s); });
    root->addWidget(table_, 1);
    foot_ = note_label();
    root->addWidget(foot_);

    connect(&services::OwnershipService::instance(), &services::OwnershipService::short_rank_updated,
            this, [this]() { render(); });
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
        title_note_->setText(svc.short_rank_loading()
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
        sirio->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Shares short over shares held by 13F filers — Drechsler & Drechsler's proxy for the "
            "borrow fee (1.51%/mo four-factor alpha, 1980–2013). Amber: top decile. A value near or "
            "above 1 means the short exceeds the institutional book — an ADR or a name whose "
            "holders are mostly not 13F filers.")));
        table_->setItem(i, 2, sirio);
        table_->setItem(i, 3, num_cell(r.days_to_cover ? QString::number(*r.days_to_cover, 'f', 1) : fmt::placeholder(),
                                       r.days_to_cover && *r.days_to_cover >= 5.0 ? ui::colors::AMBER() : QString()));
        table_->setItem(i, 4, num_cell(compact_or_dash(r.shares_short)));
        table_->setItem(i, 5, num_cell(pct_or_dash(r.change_pct, 0, true),
                                       r.change_pct ? (*r.change_pct > 0 ? ui::colors::RED() : ui::colors::GREEN()) : QString()));
        table_->setItem(i, 6, num_cell(compact_or_dash(r.inst_shares)));
        table_->setItem(i, 7, num_cell(QString::number(r.holders)));
        table_->setItem(i, 8, num_cell(compact_or_dash(r.avg_daily_volume), ui::colors::TEXT_SECONDARY()));
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(1, qMin(qMax(table_->columnWidth(1), 160), 260));

    foot_->setText(QStringLiteral("%1 symbols on the settlement date · %2 joined to a 13F denominator · "
                                  "%3 funds and ETFs dropped by name · holders ≥ 50 · a risk screen, not a "
                                  "portfolio: the effect is small-cap and value-weighted zero, and squeeze "
                                  "cost at high utilisation eats two-thirds of it (Schultz 2024)")
                       .arg(sr.symbols).arg(sr.joined).arg(sr.funds_dropped));
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
                         QStringLiteral("Added"), QStringLiteral("Reduced"),
                         QStringLiteral("Focused net shares"), QStringLiteral("Top-10 %"),
                         QStringLiteral("13F value")});
    connect_open(table_, this, [this](const QString& s) { emit stock_activated(s); });
    root->addWidget(table_, 1);
    foot_ = note_label();
    root->addWidget(foot_);

    connect(&services::OwnershipService::instance(), &services::OwnershipService::movers_updated,
            this, [this]() { render(); });
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
                             .arg((m.quarter.month() - 1) / 3 + 1).arg(m.quarter.year())
                             .arg((m.prior_quarter.month() - 1) / 3 + 1).arg(m.prior_quarter.year())
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
        auto* d = num_cell((r.delta_holders > 0 ? QStringLiteral("+") : QString()) + QLocale().toString(r.delta_holders),
                           r.delta_holders > 0 ? ui::colors::GREEN() : r.delta_holders < 0 ? ui::colors::RED() : QString());
        if (r.holders_prior < r.holders / 10)
            d->setToolTip(ui::tooltip_wrap(QStringLiteral(
                "%1 holders a quarter ago — a new listing, a spin-off or a newly mapped CUSIP, not a "
                "wave of buying.").arg(r.holders_prior)));
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
        fn->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Net shares across books of fewer than a thousand names, among filers present in both "
            "quarters. Broad and index books rebalance; only a focused book decides. %1 focused holders.")
                                            .arg(r.focused_holders)));
        table_->setItem(i, 8, fn);
        table_->setItem(i, 9, num_cell(pct_or_dash(r.top10_share, 0),
                                       r.top10_share && *r.top10_share >= 0.6 ? ui::colors::AMBER() : QString()));
        table_->setItem(i, 10, num_cell(fmt::format_compact(r.value), ui::colors::TEXT_SECONDARY()));
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(1, qMin(qMax(table_->columnWidth(1), 160), 260));

    foot_->setText(QStringLiteral("%1 securities with 20 or more holders · funds and ETFs dropped by name · "
                                  "breadth change is one of two ownership signals that replicates (Chen, Hong "
                                  "& Stein: 0.67%/mo, half of it in the short leg) · only filers present in "
                                  "both quarters are compared")
                       .arg(m.rows.size()));
}

} // namespace fincept::screens

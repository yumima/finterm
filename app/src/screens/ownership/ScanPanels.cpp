#include "screens/ownership/ScanPanels.h"

#include "screens/ownership/FirmDetailPanel.h"
#include "screens/ownership/OwnershipUi.h"
#include "services/ownership/OwnershipService.h"
#include "ui/components/TooltipText.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QUrl>
#include <QDesktopServices>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include "ui/widgets/RememberedControls.h"
#include "ui/widgets/RememberedSplitter.h"
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
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
    ui::remember_choice(window_, QStringLiteral("ownership.insider.window"));
    ui::remember_choice(min_insiders_, QStringLiteral("ownership.insider.min_insiders"));
    ui::remember_choice(min_value_, QStringLiteral("ownership.insider.min_value"));
    ui::remember_choice(exclude_ten_pct_, QStringLiteral("ownership.insider.exclude_ten_pct"));
    ui::remember_choice(exclude_plan_, QStringLiteral("ownership.insider.exclude_plan"));
    connect(exclude_ten_pct_, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    connect(exclude_plan_, &QCheckBox::toggled, this, [this](bool) { refresh(); });

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("Insiders"),
                         QStringLiteral("Roles"), QStringLiteral("Value"), QStringLiteral("Shares"),
                         QStringLiteral("Avg price"), QStringLiteral("Stake +"), QStringLiteral("Traded"),
                         QStringLiteral("Filed"), QStringLiteral("1w"), QStringLiteral("1m")});
    busy_ = new ui::LoadingOverlay(table_);
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
    busy_->set_loading(!ib.loaded && svc.insider_buys_loading(), QStringLiteral("READING FORM 4 FILINGS\u2026"));

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
    ui::remember_choice(sort_, QStringLiteral("ownership.short.sort"));
    connect(sort_, &QComboBox::activated, this, [this](int) { refresh(); });
    bar->addWidget(sort_);
    root->addLayout(bar);

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("SI ÷ 13F"),
                         QStringLiteral("Days to cover"), QStringLiteral("Shares short"), QStringLiteral("vs prior"),
                         QStringLiteral("13F shares"), QStringLiteral("Holders"), QStringLiteral("Avg daily vol")});
    busy_ = new ui::LoadingOverlay(table_);
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
    busy_->set_loading(!sr.loaded && svc.short_rank_loading(), QStringLiteral("FETCHING FINRA SHORT INTEREST\u2026"));
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
    ui::remember_choice(sort_, QStringLiteral("ownership.movers.sort"));
    connect(sort_, &QComboBox::activated, this, [this](int) { refresh(); });
    bar->addWidget(sort_);
    root->addLayout(bar);

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("Holders"),
                         QStringLiteral("Δ holders"), QStringLiteral("New"), QStringLiteral("Closed"),
                         QStringLiteral("Added"), QStringLiteral("Reduced"), QStringLiteral("Focused net shares"),
                         QStringLiteral("Top-10 %"), QStringLiteral("13F value")});
    busy_ = new ui::LoadingOverlay(table_);
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
    busy_->set_loading(!m.loaded && svc.movers_loading(), QStringLiteral("COMPUTING 13F BREADTH\u2026"));
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
    bar->addWidget(control_label(QStringLiteral("Filed")));
    filed_within_ = new QComboBox;
    filed_within_->addItem(QStringLiteral("any time"), 0);
    filed_within_->addItem(QStringLiteral("last 7 days"), 7);
    filed_within_->addItem(QStringLiteral("last 30 days"), 30);
    filed_within_->addItem(QStringLiteral("last 90 days"), 90);
    filed_within_->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "By the day the 13F reached SEC — how new the news is, not how old the position is")));
    bar->addWidget(filed_within_);
    active_only_ = new QCheckBox(QStringLiteral("active books only"));
    active_only_->setChecked(true);
    bar->addWidget(active_only_);
    for (auto* c : {side_, sort_, filed_within_})
        connect(c, &QComboBox::activated, this, [this](int) { refresh(); });
    connect(active_only_, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    ui::remember_choice(side_, QStringLiteral("ownership.big_moves.side"));
    ui::remember_choice(sort_, QStringLiteral("ownership.big_moves.sort"));
    ui::remember_choice(filed_within_, QStringLiteral("ownership.big_moves.filed_within"));
    ui::remember_choice(active_only_, QStringLiteral("ownership.big_moves.active_only"));
    root->addLayout(bar);

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("Manager"),
                         QStringLiteral("Move"), QStringLiteral("Est. $"), QStringLiteral("% of book"),
                         QStringLiteral("Shares now"), QStringLiteral("Shares before"), QStringLiteral("Filed for"),
                         QStringLiteral("Filed"), QStringLiteral("Traded between")});
    busy_ = new ui::LoadingOverlay(table_);
    table_->horizontalHeaderItem(10)->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "The stock's low and high between the two filings: the shares changed hands somewhere in this range. "
        "A 13F gives no trade prices or dates, so no average price is made up.")));
    // The filing date opens the filing itself on EDGAR: every row can be checked at its source.
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        if (c != 9)
            return;
        if (auto* it = table_->item(r, c)) {
            const QString url = it->data(Qt::UserRole).toString();
            if (!url.isEmpty())
                QDesktopServices::openUrl(QUrl(url));
        }
    });
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
                                                          active_only_->isChecked(),
                                                          filed_within_->currentData().toInt());
    render();
}

void BigMovesPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const auto& b = svc.big_moves();
    busy_->set_loading(!b.loaded && svc.big_moves_loading(), QStringLiteral("COMPARING 13F FILINGS\u2026"));
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
        auto* filed = num_cell(r.filed.isValid() ? r.filed.toString(QStringLiteral("d MMM yy")) : fmt::placeholder(),
                               r.url.isEmpty() ? QString() : ui::colors::CYAN());
        filed->setData(Qt::UserRole, r.url);
        if (!r.url.isEmpty())
            filed->setToolTip(ui::tooltip_wrap(QStringLiteral("Open this 13F on SEC EDGAR")));
        table_->setItem(i, 9, filed);
        table_->setItem(i, 10, num_cell(r.range_low && r.range_high
                                            ? QStringLiteral("$%1 – $%2")
                                                  .arg(QLocale().toString(*r.range_low, 'f', *r.range_low < 10 ? 2 : 0),
                                                       QLocale().toString(*r.range_high, 'f', *r.range_high < 10 ? 2 : 0))
                                            : fmt::placeholder(),
                                        ui::colors::TEXT_SECONDARY()));
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

// ── 5% STAKES ────────────────────────────────────────────────────────────────

StakesPanel::StakesPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(control_style());
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);
    auto title = section_title(QStringLiteral("5% STAKES"));
    title_note_ = title.right;
    auto* bar = new QHBoxLayout;
    bar->addWidget(title.widget, 1);
    kind_ = new QComboBox;
    kind_->addItem(QStringLiteral("13D — may seek influence"), QStringLiteral("13d"));
    kind_->addItem(QStringLiteral("13D and 13G"), QStringLiteral("all"));
    kind_->addItem(QStringLiteral("13G — passive"), QStringLiteral("13g"));
    bar->addWidget(kind_);
    bar->addWidget(control_label(QStringLiteral("Filed")));
    days_ = new QComboBox;
    days_->addItem(QStringLiteral("last 7 days"), 7);
    days_->addItem(QStringLiteral("last 30 days"), 30);
    days_->setCurrentIndex(1);
    bar->addWidget(days_);
    new_only_ = new QCheckBox(QStringLiteral("new stakes only"));
    bar->addWidget(new_only_);
    for (auto* c : {kind_, days_})
        connect(c, &QComboBox::activated, this, [this](int) { refresh(); });
    connect(new_only_, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    ui::remember_choice(kind_, QStringLiteral("ownership.stakes.kind"));
    ui::remember_choice(days_, QStringLiteral("ownership.stakes.days"));
    ui::remember_choice(new_only_, QStringLiteral("ownership.stakes.new_only"));
    root->addLayout(bar);

    table_ = make_table({QStringLiteral("Ticker"), QStringLiteral("Company"), QStringLiteral("Holder"),
                         QStringLiteral("Form"), QStringLiteral("% of class"), QStringLiteral("Change"),
                         QStringLiteral("Shares"), QStringLiteral("Event"), QStringLiteral("Filed")});
    busy_ = new ui::LoadingOverlay(table_);
    table_->horizontalHeaderItem(7)->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "The date the holder crossed the threshold or the stake changed — the filing is due 5 business days after "
        "for a 13D")));
    connect_open(table_, this, [this](const QString& s) { emit stock_activated(s); });
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        if (c != 8)
            return;
        if (auto* it = table_->item(r, c); it && !it->data(Qt::UserRole).toString().isEmpty())
            QDesktopServices::openUrl(QUrl(it->data(Qt::UserRole).toString()));
    });
    root->addWidget(table_, 1);
    foot_ = note_label();
    root->addWidget(foot_);
    connect(&services::OwnershipService::instance(), &services::OwnershipService::stakes_updated, this,
            [this]() { render(); });
    render();
}

void StakesPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // Emptied by a scan that read new filings while another tab was up.
    if (!services::OwnershipService::instance().stakes().loaded &&
        !services::OwnershipService::instance().stakes_loading())
        refresh();
}

void StakesPanel::refresh() {
    services::OwnershipService::instance().load_stakes(days_->currentData().toInt(), kind_->currentData().toString(),
                                                       new_only_->isChecked());
    render();
}

void StakesPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const auto& st = svc.stakes();
    busy_->set_loading(!st.loaded && svc.stakes_loading(), QStringLiteral("READING 13D / 13G FILINGS\u2026"));
    if (!st.loaded) {
        table_->setRowCount(0);
        title_note_->setText(svc.stakes_loading() ? QStringLiteral("reading the local 13D / 13G store…") : QString());
        if (!svc.stakes_loading() && isVisible())
            refresh(); // emptied by a scan that read new filings: read again while on screen
        return;
    }
    if (!st.error.isEmpty()) {
        table_->setRowCount(0);
        foot_->setText(st.error);
        return;
    }
    title_note_->setText(st.read_through.isValid()
                             ? QStringLiteral("EDGAR read through %1 · a 13D is due 5 business days after the stake")
                                   .arg(st.read_through.toString(QStringLiteral("d MMM yyyy")))
                             : QStringLiteral("the first read of EDGAR runs a few minutes after launch"));
    table_->setUpdatesEnabled(false);
    table_->setRowCount(st.rows.size());
    for (int i = 0; i < st.rows.size(); ++i) {
        const auto& s = st.rows[i];
        table_->setItem(i, 0, ticker_cell(s.ticker, s.company));
        table_->setItem(i, 1, cell(s.company));
        table_->setItem(i, 2, cell(s.filer));
        const QString form = (s.activist ? QStringLiteral("13D") : QStringLiteral("13G")) +
                             (s.amendment ? QStringLiteral(" amend.") : QStringLiteral(" new"));
        table_->setItem(i, 3, cell(form, s.activist ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY()));
        table_->setItem(i, 4, num_cell(s.pct ? QStringLiteral("%1%").arg(*s.pct, 0, 'f', 2) : fmt::placeholder()));
        QString chg = fmt::placeholder();
        QString col;
        if (s.pct && s.prev_pct) {
            const double d = *s.pct - *s.prev_pct;
            chg = (d >= 0 ? QStringLiteral("+") : QStringLiteral("−")) + QString::number(std::fabs(d), 'f', 2) +
                  QStringLiteral(" pts");
            col = d > 0 ? ui::colors::GREEN() : d < 0 ? ui::colors::RED() : QString();
        } else if (!s.amendment) {
            chg = QStringLiteral("new");
            col = ui::colors::GREEN();
        }
        table_->setItem(i, 5, num_cell(chg, col));
        table_->setItem(i, 6, num_cell(s.shares ? fmt::format_compact(*s.shares) : fmt::placeholder()));
        table_->setItem(i, 7, num_cell(s.event.isValid() ? s.event.toString(QStringLiteral("d MMM")) : fmt::placeholder(),
                                       ui::colors::TEXT_SECONDARY()));
        auto* filed = num_cell(s.filed.toString(QStringLiteral("d MMM yy")), ui::colors::CYAN());
        filed->setData(Qt::UserRole, s.url);
        filed->setToolTip(ui::tooltip_wrap(QStringLiteral("Open this filing on SEC EDGAR")));
        table_->setItem(i, 8, filed);
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(1, qMin(qMax(table_->columnWidth(1), 160), 240));
    table_->setColumnWidth(2, qMin(qMax(table_->columnWidth(2), 160), 260));
    fill_width(table_, {1, 2});
    foot_->setText(QStringLiteral("%1 filings · percentage and shares as the filing states them (structured filings "
                                  "since Dec 2024; older free-text ones show none) · a group filing shows the "
                                  "group's stake · 13G amendments are passive holders restating and are not "
                                  "listed")
                       .arg(st.rows.size()));
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
        if (r == add_row_) {
            begin_add();
            return;
        }
        // The ★ of a fund you added takes it off your list.
        if (c == kRank) {
            if (auto* it = table_->item(r, kRank); it && !it->data(Qt::UserRole).toString().isEmpty()) {
                services::OwnershipService::instance().set_my_fund(it->data(Qt::UserRole).toString(), {}, false);
                return;
            }
        }
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
    ui::remember_splitter(split, QStringLiteral("ownership.largest_funds"));
    root->addWidget(split, 1);

    // Right-click any fund: keep it in your list (it stays when it drops out
    // of the top 20), or take it off.
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int r = table_->rowAt(pos.y());
        auto* name = r >= 0 && r != add_row_ ? table_->item(r, kManager) : nullptr;
        if (!name)
            return;
        const QString cik = name->data(Qt::UserRole).toString();
        const QString manager = name->text();
        auto& svc = services::OwnershipService::instance();
        const bool mine = svc.is_my_fund(cik);
        QMenu menu(this);
        menu.addAction(mine ? QStringLiteral("Remove from my list") : QStringLiteral("Keep in my list"),
                       this, [cik, manager, mine]() {
                           services::OwnershipService::instance().set_my_fund(cik, manager, !mine);
                       });
        menu.addAction(QStringLiteral("Add a fund…"), this, [this]() {
            table_->scrollToBottom();
            begin_add();
        });
        menu.exec(table_->viewport()->mapToGlobal(pos));
    });

    // Adding a fund: suggestions from the filer search, under the field.
    add_results_ = new QListWidget(this);
    add_results_->setWindowFlags(Qt::ToolTip); // floats over the table, takes no focus
    add_results_->setStyleSheet(QString("QListWidget{background:%1;border:1px solid %2;color:%3;font-size:12px;}"
                                        "QListWidget::item{padding:4px 8px;}"
                                        "QListWidget::item:selected{background:%4;color:%3;}")
                                    .arg(ui::colors::BG_RAISED(), ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(),
                                         ui::colors::BG_SURFACE()));
    add_results_->hide();
    connect(add_results_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) { pick_result(add_results_->row(it)); });
    add_debounce_ = new QTimer(this);
    add_debounce_->setSingleShot(true);
    add_debounce_->setInterval(250);
    connect(add_debounce_, &QTimer::timeout, this, [this]() {
        if (!add_edit_)
            return;
        const QString q = add_edit_->text().trimmed();
        if (q.length() < 2) {
            add_results_->hide();
            return;
        }
        services::OwnershipService::instance().search_firms(q);
    });

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::firms_found, this, [this]() {
        if (!add_edit_ || !add_edit_->hasFocus())
            return; // the screen's own filer search asked, not us
        add_results_->clear();
        auto& s = services::OwnershipService::instance();
        for (const auto& m : s.last_firm_results()) {
            const bool have = s.is_my_fund(m.cik) ||
                              std::any_of(s.top_firms().firms.begin(), s.top_firms().firms.end(),
                                          [&](const ownership::TopFirm& f) { return f.cik == m.cik; });
            auto* it = new QListWidgetItem(QStringLiteral("%1   ·   $%2 · %3 names%4")
                                               .arg(m.name, fmt::format_compact(m.book_value))
                                               .arg(m.position_count)
                                               .arg(have ? QStringLiteral("   (in the list)") : QString()));
            it->setData(Qt::UserRole, m.cik);
            it->setData(Qt::UserRole + 1, m.name);
            if (have)
                it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
            add_results_->addItem(it);
        }
        if (add_results_->count() == 0) {
            add_results_->addItem(QStringLiteral("No 13F filer by that name"));
            add_results_->item(0)->setFlags(Qt::NoItemFlags);
        }
        const QPoint at = add_edit_->mapToGlobal(QPoint(0, add_edit_->height()));
        add_results_->setFixedWidth(qMax(add_edit_->width(), 420));
        add_results_->setFixedHeight(qMin(add_results_->count(), 10) * 26 + 4);
        add_results_->move(at);
        add_results_->show();
    });
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

void LargestFundsPanel::begin_add() {
    if (add_row_ < 0 || add_edit_)
        return;
    add_edit_ = new QLineEdit;
    add_edit_->setPlaceholderText(QStringLiteral("Type a fund or manager name — Enter adds the first match, Esc cancels"));
    add_edit_->setStyleSheet(QString("QLineEdit{color:%1;background:%2;border:1px solid %3;padding:2px 8px;font-size:12px;}")
                                 .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(), ui::colors::AMBER()));
    add_edit_->installEventFilter(this);
    connect(add_edit_, &QLineEdit::textEdited, this, [this](const QString&) { add_debounce_->start(); });
    connect(add_edit_, &QLineEdit::returnPressed, this, [this]() {
        for (int i = 0; i < add_results_->count(); ++i)
            if (add_results_->isVisible() && (add_results_->item(i)->flags() & Qt::ItemIsEnabled)) {
                pick_result(add_results_->currentRow() >= 0 &&
                                    (add_results_->currentItem()->flags() & Qt::ItemIsEnabled)
                                ? add_results_->currentRow()
                                : i);
                return;
            }
    });
    table_->setCellWidget(add_row_, 0, add_edit_);
    add_edit_->setFocus();
}

void LargestFundsPanel::end_add() {
    add_results_->hide();
    if (!add_edit_)
        return;
    QLineEdit* e = add_edit_;
    add_edit_ = nullptr;
    if (add_row_ >= 0)
        table_->removeCellWidget(add_row_, 0);
    e->deleteLater();
}

void LargestFundsPanel::pick_result(int row) {
    auto* it = add_results_->item(row);
    if (!it || !(it->flags() & Qt::ItemIsEnabled))
        return;
    const QString cik = it->data(Qt::UserRole).toString();
    const QString name = it->data(Qt::UserRole + 1).toString();
    end_add();
    services::OwnershipService::instance().set_my_fund(cik, name, true);
}

bool LargestFundsPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == add_edit_) {
        if (ev->type() == QEvent::KeyPress) {
            auto* k = static_cast<QKeyEvent*>(ev);
            if (k->key() == Qt::Key_Escape) {
                end_add();
                return true;
            }
            if ((k->key() == Qt::Key_Down || k->key() == Qt::Key_Up) && add_results_->isVisible()) {
                const int n = add_results_->count();
                int r = add_results_->currentRow() + (k->key() == Qt::Key_Down ? 1 : -1);
                add_results_->setCurrentRow(qBound(0, r, n - 1));
                return true;
            }
        } else if (ev->type() == QEvent::FocusOut) {
            // Clicking a suggestion moves the mouse, not the focus (the list
            // takes none); anything else ends the add.
            QTimer::singleShot(150, this, [this]() {
                if (add_edit_ && !add_edit_->hasFocus() && !add_results_->underMouse())
                    end_add();
            });
        }
    }
    return QWidget::eventFilter(obj, ev);
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
    if (!t.loaded || !t.error.isEmpty()) {
        end_add();
        add_row_ = -1;
    }
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
    const int yours = std::count_if(t.firms.begin(), t.firms.end(), [](const ownership::TopFirm& f) { return !f.ranked; });
    title_note_->setText(QStringLiteral("top %1 by US-listed stock book%2 · newest filing %3")
                             .arg(t.top)
                             .arg(yours ? QStringLiteral(" + %1 of yours").arg(yours) : QString())
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

    // A reload can land while a fund is being typed (the sync finishing, a
    // pass the ranking asked for): the field is rebuilt with the row, but
    // what was typed, and the suggestions, are carried over.
    const bool adding = add_edit_ != nullptr;
    const QString typed = adding ? add_edit_->text() : QString();
    const bool suggesting = adding && add_results_->isVisible();
    end_add();
    table_->setUpdatesEnabled(false);
    table_->clearSpans();
    table_->setRowCount(t.firms.size() + 1); // + the "add a fund" row
    for (int i = 0; i < t.firms.size(); ++i) {
        const auto& f = t.firms[i];
        // Ranked: its place. Yours and outside the top: a star in its place.
        auto* rank = f.ranked ? num_cell(QString::number(i + 1) + (f.mine ? QStringLiteral(" ★") : QString()),
                                         f.mine ? ui::colors::AMBER() : ui::colors::TEXT_TERTIARY())
                              : num_cell(QStringLiteral("★"), ui::colors::AMBER());
        if (f.mine) {
            rank->setData(Qt::UserRole, f.cik);
            rank->setToolTip(ui::tooltip_wrap(QStringLiteral("In your list — click the ★ to remove %1").arg(f.manager)));
        }
        table_->setItem(i, kRank, rank);
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
    // The last row adds a fund: click it and it becomes a search field.
    add_row_ = t.firms.size();
    auto* add = cell(QStringLiteral("＋  Add a fund…"), ui::colors::AMBER());
    add->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "Add any 13F filer to this list by name. It stays below the top %1, marked ★, until you click the ★ "
        "or right-click it to remove it.")
                                         .arg(t.top)));
    table_->setItem(add_row_, 0, add);
    for (int c = 1; c < kFundCols; ++c)
        table_->setItem(add_row_, c, cell(QString(), ui::colors::TEXT_TERTIARY()));
    table_->setSpan(add_row_, 0, 1, kFundCols);
    if (adding) {
        begin_add();
        add_edit_->setText(typed);
        if (suggesting)
            add_debounce_->start();
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(kRank, qMax(table_->columnWidth(kRank), 44));
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

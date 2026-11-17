#include "ui/alerts/AlertsDialog.h"

#include "core/events/EventBus.h"
#include "services/alerts/AlertService.h"
#include "ui/theme/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace finterm::ui {

using alerts::AlertService;
using alerts::Kind;
using alerts::Rule;
using alerts::Scope;
using alerts::Session;

namespace {

QString dialog_ss() {
    return QStringLiteral(
               "QDialog{background:%1;color:%2;}"
               "QLabel{color:%2;background:transparent;}"
               "QLineEdit,QComboBox,QDoubleSpinBox{background:%3;color:%2;border:1px solid %4;"
               "border-radius:3px;padding:4px 6px;min-height:20px;}"
               "QLineEdit:focus,QComboBox:focus,QDoubleSpinBox:focus{border:1px solid %5;}"
               "QComboBox QAbstractItemView{background:%3;color:%2;selection-background-color:%6;}"
               "QCheckBox{color:%2;spacing:8px;}"
               "QTableWidget{background:%3;color:%2;border:1px solid %4;gridline-color:%4;"
               "selection-background-color:%6;selection-color:%2;}"
               "QHeaderView::section{background:%1;color:%7;border:none;border-bottom:1px solid %4;"
               "padding:4px 6px;font-size:11px;font-weight:700;}"
               "QPushButton{background:%3;color:%2;border:1px solid %4;border-radius:3px;padding:5px 14px;}"
               "QPushButton:hover{border-color:%5;}"
               "QPushButton:disabled{color:%7;}"
               "QPushButton#primary{background:%5;color:%8;font-weight:700;border:none;}")
        .arg(colors::BG_BASE(), colors::TEXT_PRIMARY(), colors::BG_SURFACE(), colors::BORDER_MED(), colors::AMBER(),
             colors::BG_HOVER(), colors::TEXT_SECONDARY(), colors::TEXT_ON_ACCENT());
}

QString kind_label(Kind k) {
    switch (k) {
        case Kind::PriceAbove:
            return QStringLiteral("Price crosses above");
        case Kind::PriceBelow:
            return QStringLiteral("Price crosses below");
        case Kind::DayMove:
            return QStringLiteral("Day move beyond ±%");
        case Kind::InsiderCluster:
            return QStringLiteral("Insider cluster buy (2+ insiders)");
        case Kind::StakeFiling:
            return QStringLiteral("5% stake filed (13D / 13G)");
        case Kind::FundMove:
            return QStringLiteral("Big 13F move by an active manager");
        case Kind::FundFiled:
            return QStringLiteral("Fund files its 13F");
    }
    return {};
}

const char* kAlwaysOnNote = "Alerts are checked only while finterm is running — there is no background "
                            "service, so nothing is watched while the app is closed.";

} // namespace

void open_in_equity_research(const QString& symbol) {
    if (symbol.trimmed().isEmpty())
        return;
    EventBus::instance().publish("nav.split_alongside", QVariantMap{{"screen_id", "equity_research"}});
    EventBus::instance().publish("equity_research.load_symbol", QVariantMap{{"symbol", symbol.trimmed().toUpper()}});
}

void open_alerts_manager(QWidget* parent) {
    static QPointer<AlertsDialog> dlg;
    if (!dlg) {
        dlg = new AlertsDialog(parent ? parent->window() : nullptr);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
    }
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void open_add_alert(QWidget* parent, const QString& symbol, double price) {
    auto& svc = AlertService::instance();
    if (!svc.running()) {
        QMessageBox::information(parent, QStringLiteral("Alerts"),
                                 QStringLiteral("Alerts start once you are signed in and the terminal is unlocked."));
        return;
    }
    if (!std::isfinite(price) || price <= 0)
        price = svc.current_price(symbol).value_or(std::numeric_limits<double>::quiet_NaN());
    AlertEditDialog dlg(parent, std::nullopt, symbol, price);
    if (dlg.exec() == QDialog::Accepted)
        svc.add_rule(dlg.rule());
}

// ── Add / edit ───────────────────────────────────────────────────────────────

AlertEditDialog::AlertEditDialog(QWidget* parent, const std::optional<Rule>& existing, const QString& symbol,
                                 double price)
    : QDialog(parent), existing_(existing), price_(price) {
    setWindowTitle(existing ? QStringLiteral("Edit alert") : QStringLiteral("Add alert"));
    setModal(true);
    setMinimumWidth(460);
    setStyleSheet(dialog_ss());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    symbol_ = new QLineEdit;
    symbol_->setPlaceholderText(QStringLiteral("e.g. AAPL"));
    form->addRow(QStringLiteral("Symbol"), symbol_);

    kind_ = new QComboBox;
    // FundFiled is set from a fund's page (FOLLOW), not typed here: its
    // subject is a fund, not a ticker.
    for (Kind k : {Kind::PriceAbove, Kind::PriceBelow, Kind::DayMove, Kind::InsiderCluster, Kind::StakeFiling,
                   Kind::FundMove})
        kind_->addItem(kind_label(k), int(k));
    form->addRow(QStringLiteral("When"), kind_);

    level_lbl_ = new QLabel(QStringLiteral("Level"));
    level_ = new QDoubleSpinBox;
    level_->setRange(0.0, 1e7);
    {
        // Precision follows the magnitude being edited — the rule's own level
        // first (an edit must not round it), else the live price.
        const double ref = existing && std::isfinite(existing->level) ? existing->level : price;
        level_->setDecimals(!std::isfinite(ref) || ref >= 1.0 ? 2 : ref >= 0.01 ? 4 : 8);
    }
    level_->setSingleStep(std::isfinite(price) && price > 0 ? std::max(0.01, price * 0.005) : 0.5);
    form->addRow(level_lbl_, level_);

    pct_lbl_ = new QLabel(QStringLiteral("Threshold"));
    pct_ = new QDoubleSpinBox;
    pct_->setRange(0.0, 100.0);
    pct_->setDecimals(2);
    pct_->setSuffix(QStringLiteral(" %"));
    pct_->setValue(5.0);
    form->addRow(pct_lbl_, pct_);

    session_lbl_ = new QLabel(QStringLiteral("Prints"));
    session_ = new QComboBox;
    session_->addItem(QStringLiteral("Regular session only"), int(Session::Regular));
    session_->addItem(QStringLiteral("Including pre-, post-market and overnight"), int(Session::Extended));
    form->addRow(session_lbl_, session_);
    root->addLayout(form);

    hint_ = new QLabel;
    hint_->setWordWrap(true);
    hint_->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(colors::TEXT_SECONDARY()));
    root->addWidget(hint_);

    auto* note = new QLabel(QString::fromUtf8(kAlwaysOnNote));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(colors::TEXT_TERTIARY()));
    root->addWidget(note);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    ok_ = new QPushButton(existing ? QStringLiteral("Save") : QStringLiteral("Add alert"));
    ok_->setObjectName(QStringLiteral("primary"));
    ok_->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(ok_);
    root->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok_, &QPushButton::clicked, this, &QDialog::accept);

    if (existing) {
        symbol_->setText(existing->symbol);
        kind_->setCurrentIndex(kind_->findData(int(existing->kind)));
        if (std::isfinite(existing->level))
            level_->setValue(existing->level);
        if (std::isfinite(existing->pct))
            pct_->setValue(existing->pct);
        session_->setCurrentIndex(session_->findData(int(existing->session)));
        // An all-names insider rule is the manager's toggle, not a symbol rule.
        kind_->setEnabled(existing->scope != Scope::OwnNames);
        symbol_->setEnabled(existing->scope != Scope::OwnNames);
    } else {
        symbol_->setText(symbol.trimmed().toUpper());
        if (std::isfinite(price) && price > 0)
            level_->setValue(price);
    }

    connect(kind_, &QComboBox::currentIndexChanged, this, &AlertEditDialog::sync_fields);
    connect(symbol_, &QLineEdit::textChanged, this, &AlertEditDialog::validate);
    connect(level_, &QDoubleSpinBox::valueChanged, this, &AlertEditDialog::validate);
    connect(pct_, &QDoubleSpinBox::valueChanged, this, &AlertEditDialog::validate);
    connect(session_, &QComboBox::currentIndexChanged, this, &AlertEditDialog::validate);
    sync_fields();
}

Rule AlertEditDialog::rule() const {
    Rule r = existing_.value_or(Rule{});
    r.kind = Kind(kind_->currentData().toInt());
    if (r.scope != Scope::OwnNames)
        r.symbol = symbol_->text().trimmed().toUpper();
    // Only the field the kind uses is stored; the others stay absent (NaN),
    // never a confident 0.
    r.level = r.is_price() ? level_->value() : alerts::kNaN;
    r.pct = r.kind == Kind::DayMove ? pct_->value() : alerts::kNaN;
    r.session = Session(session_->currentData().toInt());
    return r;
}

void AlertEditDialog::sync_fields() {
    const Kind k = Kind(kind_->currentData().toInt());
    const bool price = k == Kind::PriceAbove || k == Kind::PriceBelow;
    level_lbl_->setVisible(price);
    level_->setVisible(price);
    session_lbl_->setVisible(price);
    session_->setVisible(price);
    pct_lbl_->setVisible(k == Kind::DayMove);
    pct_->setVisible(k == Kind::DayMove);
    validate();
}

void AlertEditDialog::validate() {
    const Rule r = rule();
    ok_->setEnabled(r.valid());
    const QString now = std::isfinite(price_) && price_ > 0
                            ? QStringLiteral("Now %1. ").arg(alerts::fmt_price(price_))
                            : QStringLiteral("No live price for this symbol yet. ");
    QString text;
    switch (r.kind) {
        case Kind::PriceAbove:
        case Kind::PriceBelow: {
            const bool above = r.kind == Kind::PriceAbove;
            if (!std::isfinite(r.level) || r.level <= 0) {
                text = now + QStringLiteral("Set the price level to watch.");
                break;
            }
            text = now + QStringLiteral("Fires once when the price reaches %1, then re-arms only after it is "
                                        "back %2 %3 (a %4% band), so a price wobbling on the level does not "
                                        "fire again.")
                             .arg(alerts::fmt_price(r.level), above ? QStringLiteral("below") : QStringLiteral("above"),
                                  alerts::fmt_price(alerts::rearm_price(r)),
                                  QString::number(alerts::kRearmBandFrac * 100.0, 'g', 3));
            if (std::isfinite(price_) && price_ > 0 && (above ? price_ >= r.level : price_ <= r.level))
                text += QStringLiteral(" The price is already %1 the level: nothing fires until it has been "
                                       "back across first.")
                            .arg(above ? QStringLiteral("above") : QStringLiteral("below"));
            text += r.session == Session::Extended
                        ? QStringLiteral(" Watches pre-, post-market and overnight prints too; each print is "
                                         "checked against the quote's own close before it is used.")
                        : QStringLiteral(" Regular-session quotes only.");
            break;
        }
        case Kind::DayMove:
            text = now + QStringLiteral("Fires once per session when the day's change against the previous close "
                                        "is beyond ±%1% (up or down). Regular-session quote; a quote that could "
                                        "not be fetched is never read as a 0% day.")
                             .arg(QString::number(pct_->value(), 'g', 4));
            break;
        case Kind::InsiderCluster:
            text = QStringLiteral("Fires once per Form 4 filing when %1 or more insiders bought on the open market "
                                  "within %2 days (10% owners and 10b5-1 plan trades excluded) — the Ownership "
                                  "screen's definition. Reads the local Form 4 store the Ownership feature keeps "
                                  "current; filings older than %3 days are history, not alerts.")
                       .arg(ownership::kClusterMinInsiders)
                       .arg(ownership::kClusterWindowDays)
                       .arg(alerts::kInsiderFreshDays);
            break;
        case Kind::StakeFiling:
            text = QStringLiteral("Fires once per filing when someone files a 13D or a new 13G — a stake of 5% or "
                                  "more — or amends a 13D by %1 point or more (or across 10%). 13G amendments are "
                                  "passive holders restating a position and never fire. Filings older than %2 "
                                  "days are history, not alerts.")
                       .arg(alerts::kStakeMovePts, 0, 'f', 0)
                       .arg(alerts::kInsiderFreshDays);
            break;
        case Kind::FundMove:
            text = QStringLiteral("Fires once per filing when an active manager (1,000 names or fewer) changes its "
                                  "stake by $25M or more, as BIG MOVES shows it — splits, CUSIP changes and new "
                                  "listings already undone. 13F positions are up to 135 days old when filed.");
            break;
        case Kind::FundFiled:
            text = QStringLiteral("Fires once when the fund files its next 13F, with its biggest buy and sell.");
            break;
    }
    hint_->setText(text);
}

// ── Manager ──────────────────────────────────────────────────────────────────

AlertsDialog::AlertsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Alerts"));
    setModal(false);
    resize(1140, 480);
    setStyleSheet(dialog_ss());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("ALERTS"));
    title->setStyleSheet(QStringLiteral("color:%1;font-size:13px;font-weight:700;").arg(colors::AMBER()));
    root->addWidget(title);

    notes_ = new QLabel;
    notes_->setWordWrap(true);
    notes_->setTextFormat(Qt::RichText);
    notes_->setStyleSheet(QStringLiteral("font-size:12px;"));
    root->addWidget(notes_);

    own_insider_ = new QCheckBox(QStringLiteral("Alert me on insider cluster buys for all my watchlist and "
                                                "portfolio names"));
    root->addWidget(own_insider_);
    connect(own_insider_, &QCheckBox::toggled, this,
            [](bool on) { AlertService::instance().set_own_names_insider(on); });
    own_stakes_ = new QCheckBox(QStringLiteral("… and on 5% stakes (13D / 13G) filed in them"));
    root->addWidget(own_stakes_);
    connect(own_stakes_, &QCheckBox::toggled, this,
            [](bool on) { AlertService::instance().set_own_names(Kind::StakeFiling, on); });
    own_moves_ = new QCheckBox(QStringLiteral("… and on big 13F moves in them by active managers"));
    root->addWidget(own_moves_);
    connect(own_moves_, &QCheckBox::toggled, this,
            [](bool on) { AlertService::instance().set_own_names(Kind::FundMove, on); });

    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("RULE"), QStringLiteral("SYMBOL"), QStringLiteral("STATE"), QStringLiteral("LAST FIRED")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    root->addWidget(table_, 1);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { edit_selected(); });

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("Add…"));
    add->setObjectName(QStringLiteral("primary"));
    edit_btn_ = new QPushButton(QStringLiteral("Edit…"));
    pause_btn_ = new QPushButton(QStringLiteral("Pause"));
    delete_btn_ = new QPushButton(QStringLiteral("Delete"));
    open_btn_ = new QPushButton(QStringLiteral("Open in Equity Research"));
    auto* close = new QPushButton(QStringLiteral("Close"));
    buttons->addWidget(add);
    buttons->addWidget(edit_btn_);
    buttons->addWidget(pause_btn_);
    buttons->addWidget(delete_btn_);
    buttons->addSpacing(12);
    buttons->addWidget(open_btn_);
    buttons->addStretch();
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, [this]() {
        auto& svc = AlertService::instance();
        if (!svc.running())
            return;
        AlertEditDialog dlg(this, std::nullopt, QString(), std::numeric_limits<double>::quiet_NaN());
        if (dlg.exec() == QDialog::Accepted)
            svc.add_rule(dlg.rule());
    });
    connect(edit_btn_, &QPushButton::clicked, this, &AlertsDialog::edit_selected);
    connect(pause_btn_, &QPushButton::clicked, this, [this]() {
        const auto r = AlertService::instance().rule(selected_id());
        if (r)
            AlertService::instance().set_paused(r->id, !r->paused);
    });
    connect(delete_btn_, &QPushButton::clicked, this, [this]() {
        const auto r = AlertService::instance().rule(selected_id());
        if (!r)
            return;
        if (QMessageBox::question(this, QStringLiteral("Delete alert"),
                                  QStringLiteral("Delete \"%1\"%2?")
                                      .arg(alerts::describe(*r),
                                           r->symbol.isEmpty() ? QString() : QStringLiteral(" on ") + r->symbol)) ==
            QMessageBox::Yes)
            AlertService::instance().remove_rule(r->id);
    });
    connect(open_btn_, &QPushButton::clicked, this, [this]() {
        const auto r = AlertService::instance().rule(selected_id());
        if (r && !r->symbol.isEmpty())
            open_in_equity_research(r->symbol);
    });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &AlertsDialog::update_buttons);

    // One rebuild per burst: a tick batch or an insider pass can change many
    // rules at once.
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setSingleShot(true);
    refresh_timer_->setInterval(0);
    connect(refresh_timer_, &QTimer::timeout, this, &AlertsDialog::refresh);
    connect(&AlertService::instance(), &AlertService::rules_changed, refresh_timer_,
            qOverload<>(&QTimer::start));
    connect(&AlertService::instance(), &AlertService::state_changed, refresh_timer_,
            qOverload<>(&QTimer::start));
    refresh();
}

void AlertsDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    AlertService::instance().refresh_delivery_switches();
    refresh();
}

QString AlertsDialog::selected_id() const {
    const int row = table_->currentRow();
    if (row < 0 || !table_->item(row, 0))
        return {};
    return table_->item(row, 0)->data(Qt::UserRole).toString();
}

void AlertsDialog::edit_selected() {
    auto& svc = AlertService::instance();
    const auto r = svc.rule(selected_id());
    // A followed fund has nothing to edit (its subject is a fund, set from the
    // fund's page): pause or remove it. Opening the editor would save it back
    // as whatever kind the editor's list starts with.
    if (!r || r->kind == Kind::FundFiled)
        return;
    AlertEditDialog dlg(this, r, r->symbol,
                        svc.current_price(r->symbol).value_or(std::numeric_limits<double>::quiet_NaN()));
    if (dlg.exec() == QDialog::Accepted)
        svc.update_rule(dlg.rule());
}

void AlertsDialog::refresh_notes() {
    auto& svc = AlertService::instance();
    QStringList lines;
    lines << QStringLiteral("<span style='color:%1;'>%2</span>")
                 .arg(colors::TEXT_SECONDARY(), QString::fromUtf8(kAlwaysOnNote));
    if (!svc.running())
        lines << QStringLiteral("<span style='color:%1;'>Sign in and unlock the terminal to manage alerts.</span>")
                     .arg(colors::WARNING());
    bool any_price = false, any_insider = false;
    for (const auto& r : svc.rules()) {
        if (r.paused)
            continue;
        any_price |= !is_filing_kind(r.kind);
        any_insider |= is_filing_kind(r.kind);
    }
    if (any_price && !svc.price_delivery_on())
        lines << QStringLiteral("<span style='color:%1;'>Price Alerts are switched off in Settings › Notifications: "
                                "price and day-move rules are not being checked.</span>")
                     .arg(colors::WARNING());
    if (any_insider && !svc.insider_delivery_on())
        lines << QStringLiteral("<span style='color:%1;'>Insider Buys on Holdings is switched off in Settings › "
                                "Notifications: insider rules are not being checked.</span>")
                     .arg(colors::WARNING());
    if (any_insider) {
        const QDate d = QDate::fromString(svc.form4_scanned_to(), Qt::ISODate);
        lines << QStringLiteral("<span style='color:%1;'>%2</span>")
                     .arg(colors::TEXT_SECONDARY(),
                          d.isValid()
                              ? QStringLiteral("Insider rules read the local Form 4 store (read to %1), which the "
                                               "Ownership feature keeps current with its daily EDGAR read.")
                                    .arg(d.toString(QStringLiteral("d MMM yyyy")))
                              : QStringLiteral("Insider rules read the local Form 4 store. If it has never been "
                                               "built, open OWNERSHIP once — its daily EDGAR read fills it."));
    }
    lines << QStringLiteral("<span style='color:%1;'>Delivered to the ALERTS bell, the in-app toast and any "
                            "channel enabled in Settings › Notifications. No SEC 8-K rule: the app does not read "
                            "per-company filing lists in the background.</span>")
                 .arg(colors::TEXT_TERTIARY());
    notes_->setText(lines.join(QStringLiteral("<br>")));
}

void AlertsDialog::refresh() {
    auto& svc = AlertService::instance();
    refresh_notes();
    {
        QSignalBlocker block(own_insider_);
        own_insider_->setChecked(svc.own_names_insider_on());
        own_insider_->setEnabled(svc.running());
    }
    for (auto [box, kind] : {std::pair{own_stakes_, Kind::StakeFiling}, std::pair{own_moves_, Kind::FundMove}}) {
        QSignalBlocker block(box);
        box->setChecked(svc.own_names_on(kind));
        box->setEnabled(svc.running());
    }
    const QString keep = selected_id();
    QSignalBlocker block(table_);
    const auto rules = svc.rules();
    table_->setRowCount(0);
    table_->setRowCount(rules.size());
    int select = -1;
    for (int i = 0; i < rules.size(); ++i) {
        const Rule& r = rules[i];
        const auto st = svc.state(r.id);
        auto* c0 = new QTableWidgetItem(alerts::describe(r));
        c0->setData(Qt::UserRole, r.id);
        auto* c1 = new QTableWidgetItem(r.scope == Scope::OwnNames ? QStringLiteral("ALL OWN NAMES") : r.symbol);
        auto* c2 = new QTableWidgetItem(alerts::state_label(r, st));
        const QString fired =
            st.last_fired.isValid() ? st.last_fired.toLocalTime().toString(QStringLiteral("d MMM HH:mm"))
                                    : QStringLiteral("—");
        auto* c3 = new QTableWidgetItem(fired);
        c0->setToolTip(alerts::describe(r));
        c2->setToolTip(c2->text());
        if (r.paused)
            c2->setForeground(QColor(colors::TEXT_TERTIARY()));
        else if (r.is_price() && st.arm == alerts::Arm::Tripped && !st.last_fired.isValid())
            c2->setForeground(QColor(colors::TEXT_SECONDARY())); // waiting to re-arm, never fired
        else if (st.last_fired.isValid())
            c2->setForeground(QColor(colors::AMBER()));
        else
            c2->setForeground(QColor(colors::POSITIVE()));
        table_->setItem(i, 0, c0);
        table_->setItem(i, 1, c1);
        table_->setItem(i, 2, c2);
        table_->setItem(i, 3, c3);
        if (r.id == keep)
            select = i;
    }
    if (select >= 0)
        table_->selectRow(select);
    update_buttons();
}

void AlertsDialog::update_buttons() {
    const auto cur = AlertService::instance().rule(selected_id());
    edit_btn_->setEnabled(cur.has_value());
    pause_btn_->setEnabled(cur.has_value());
    pause_btn_->setText(cur && cur->paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    delete_btn_->setEnabled(cur.has_value());
    open_btn_->setEnabled(cur && !cur->symbol.isEmpty());
}

} // namespace finterm::ui

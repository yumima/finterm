#include "screens/ownership/SmartMoneyPanel.h"

#include "services/ownership/OwnershipService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

namespace {

/// Colour by what the manager did, not by whether it is "good". An exit is not
/// a bad thing, it is a fact.
QString action_colour(const QString& action) {
    if (action == QLatin1String("new") || action == QLatin1String("added"))
        return ui::colors::GREEN();
    if (action == QLatin1String("trimmed") || action == QLatin1String("exited"))
        return ui::colors::RED();
    return ui::colors::TEXT_SECONDARY();
}

/// A style that makes the weight a conviction reading, versus one that does
/// not. The distinction is the difference between a useful screen and a
/// misleading one, so it drives the row's emphasis.
bool weight_reads_as_conviction(const QString& style) {
    return style.contains(QStringLiteral("concentrated"), Qt::CaseInsensitive) ||
           style.contains(QStringLiteral("activist"), Qt::CaseInsensitive);
}

QTableWidgetItem* cell(const QString& text, const QString& colour = {}) {
    auto* it = new QTableWidgetItem(text);
    if (!colour.isEmpty())
        it->setForeground(QColor(colour));
    return it;
}

} // namespace

SmartMoneyPanel::SmartMoneyPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    load_btn_ = new QPushButton(QStringLiteral("LOAD 13F POSITIONS"));
    load_btn_->setToolTip(
        QStringLiteral("Reads each tracked manager's own 13F from EDGAR. Several "
                       "round-trips per manager, so this takes a while."));
    connect(load_btn_, &QPushButton::clicked, this, [this]() {
        if (symbol_.isEmpty())
            return;
        services::OwnershipService::instance().load_smart_money(symbol_);
        render();
    });
    bar->addWidget(load_btn_);
    status_ = new QLabel;
    bar->addWidget(status_, 1);
    root->addLayout(bar);

    table_ = new QTableWidget;
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({QStringLiteral("Manager"), QStringLiteral("Style"),
                                       QStringLiteral("% of their book"),
                                       QStringLiteral("Shares"), QStringLiteral("Value"),
                                       QStringLiteral("Last move"), QStringLiteral("As of")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setMinimumHeight(160);
    root->addWidget(table_, 1);

    // Stated once, here, rather than repeated on every row — and stated at all,
    // because a weight column with no context invites being read as a share of
    // the manager's fund.
    caveat_ = new QLabel(QStringLiteral(
        "13F covers long US equities only — no shorts, no bonds, no cash, no leverage — so a "
        "weight is a share of the manager's disclosed equity book, not of their fund. For a "
        "hedged multi-strategy book it is one leg of a position and does not read as conviction. "
        "Filed 45 days after quarter end, so a position opened early in the quarter is up to 135 "
        "days old here."));
    caveat_->setWordWrap(true);
    caveat_->setStyleSheet(QString("color:%1;font-size:11px;").arg(ui::colors::TEXT_DIM()));
    root->addWidget(caveat_);

    connect(&services::OwnershipService::instance(),
            &services::OwnershipService::snapshot_updated, this, [this](const QString& sym) {
                if (sym.compare(symbol_, Qt::CaseInsensitive) == 0)
                    render();
            });
    render();
}

void SmartMoneyPanel::set_symbol(const QString& symbol, bool auto_fetch) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym == symbol_)
        return;
    symbol_ = sym;
    if (auto_fetch && !sym.isEmpty())
        services::OwnershipService::instance().load_smart_money(sym);
    render();
}

void SmartMoneyPanel::render() {
    auto& svc = services::OwnershipService::instance();
    if (symbol_.isEmpty()) {
        status_->setText(QStringLiteral("No symbol selected."));
        table_->setRowCount(0);
        load_btn_->setEnabled(false);
        return;
    }
    load_btn_->setEnabled(true);

    const auto snap = svc.snapshot(symbol_);
    const auto& rows = snap.smart_money;

    if (!snap.smart_money_error.isEmpty()) {
        status_->setText(QStringLiteral("13F lookup failed: ") + snap.smart_money_error);
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::RED()));
    } else if (svc.is_loading(symbol_) && !snap.smart_money_ok) {
        status_->setText(QStringLiteral("Reading manager filings from EDGAR — this takes a "
                                        "minute or two."));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    } else if (snap.smart_money_ok && rows.isEmpty()) {
        // A real answer, and a different one from "we did not look".
        status_->setText(QStringLiteral("None of the tracked managers reported a position in %1 "
                                        "in their latest 13F.").arg(symbol_));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    } else if (!snap.smart_money_ok) {
        status_->setText(QStringLiteral("Not loaded for %1.").arg(symbol_));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    } else {
        status_->setText(QStringLiteral("%1 of the tracked managers hold %2.")
                             .arg(rows.size()).arg(symbol_));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
    }

    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& p = rows[i];
        const bool conviction = weight_reads_as_conviction(p.style);

        table_->setItem(i, 0, cell(p.manager));
        auto* style_item = cell(p.style, conviction ? QString() : ui::colors::TEXT_DIM());
        style_item->setToolTip(
            conviction
                ? QStringLiteral("A long, concentrated or activist book — the weight reads as a "
                                 "deliberate position size.")
                : QStringLiteral("A hedged or index book — 13F shows only the long equity leg, "
                                 "so this weight is not a conviction reading."));
        table_->setItem(i, 1, style_item);

        // The weight is emphasised only where it means something. Everywhere
        // else it is still shown — it is a real number — just not highlighted
        // as though it were a signal.
        auto* w = cell(p.weight ? fmt::format_percent(*p.weight * 100.0) : fmt::placeholder(),
                       conviction ? ui::colors::AMBER() : QString());
        if (p.book_total && p.position_count > 0) {
            w->setToolTip(QStringLiteral("Of a %1 disclosed equity book across %2 positions.")
                              .arg(fmt::format_compact(*p.book_total))
                              .arg(p.position_count));
        }
        table_->setItem(i, 2, w);

        table_->setItem(i, 3, cell(p.shares ? fmt::format_compact(*p.shares)
                                            : fmt::placeholder()));
        table_->setItem(i, 4, cell(p.value ? fmt::format_money(*p.value) : fmt::placeholder()));

        QString move = p.action.isEmpty() ? fmt::placeholder() : p.action;
        if (p.shares_delta && *p.shares_delta != 0.0) {
            move += QStringLiteral("  %1%2")
                        .arg(*p.shares_delta > 0 ? QStringLiteral("+") : QStringLiteral("-"),
                             fmt::format_compact(std::abs(*p.shares_delta)));
        }
        auto* move_item = cell(move, action_colour(p.action));
        if (p.action == QLatin1String("held")) {
            move_item->setToolTip(QStringLiteral("Present in both quarters with no material "
                                                 "change — which is itself an answer, and a "
                                                 "different one from 'we could not tell'."));
        }
        table_->setItem(i, 5, move_item);

        table_->setItem(i, 6, cell(p.period.isValid()
                                       ? p.period.toString(QStringLiteral("yyyy-MM-dd"))
                                       : fmt::placeholder()));
    }
}

} // namespace fincept::screens

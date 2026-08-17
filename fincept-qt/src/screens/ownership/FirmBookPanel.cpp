#include "screens/ownership/FirmBookPanel.h"

#include "services/ownership/OwnershipService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

namespace {

/// Colour by what the filer did. An exit is not "bad", it is a decision — the
/// same green/red convention the insider timeline and the holder list use, so
/// the whole screen reads one way.
QString action_colour(const QString& action) {
    if (action == QLatin1String("new") || action == QLatin1String("added"))
        return ui::colors::GREEN();
    if (action == QLatin1String("trimmed") || action == QLatin1String("exited"))
        return ui::colors::RED();
    return ui::colors::TEXT_SECONDARY();
}

QTableWidgetItem* cell(const QString& text, const QString& colour = {}) {
    auto* it = new QTableWidgetItem(text);
    if (!colour.isEmpty())
        it->setForeground(QColor(colour));
    return it;
}

} // namespace

FirmBookPanel::FirmBookPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(6);

    // A search box, not a curated dropdown. There are 10,647 filers in a
    // quarter; the old twenty-firm list existed only because the universe was
    // not indexed, and it quietly answered "what does this firm own" with "of
    // the firms someone picked, this one".
    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("Search 13F filers — e.g. Berkshire, Baupost"));
    search_->setMinimumWidth(200);
    bar->addWidget(search_, 1);

    firm_ = new QComboBox;
    firm_->setMinimumWidth(230);
    bar->addWidget(firm_, 1);

    status_ = new QLabel;
    status_->setWordWrap(true);
    bar->addWidget(status_, 1);
    root->addLayout(bar);

    caveat_ = new QLabel(QStringLiteral(
        "13F covers long US equities only — no shorts, no bonds, no cash, no leverage — so these "
        "weights are shares of the manager's disclosed equity book, not of their fund."));
    caveat_->setWordWrap(true);
    caveat_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(caveat_);

    positions_ = new QTableWidget;
    positions_->setColumnCount(6);
    positions_->setHorizontalHeaderLabels({QStringLiteral("Issuer"), QStringLiteral("Ticker"),
                                           QStringLiteral("% of book"), QStringLiteral("Shares"),
                                           QStringLiteral("Value"), QStringLiteral("Move")});
    positions_->verticalHeader()->setVisible(false);
    positions_->setSelectionBehavior(QAbstractItemView::SelectRows);
    positions_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    positions_->setAlternatingRowColors(true);
    positions_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    positions_->horizontalHeader()->setStretchLastSection(true);
    positions_->setMinimumHeight(170);
    // The ticker comes from the index, so a drill-through is exact rather than
    // a guess made from the issuer name.
    connect(positions_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* it = positions_->item(row, 1)) {
            if (!it->text().isEmpty())
                emit navigate_to_symbol(it->text());
        }
    });
    root->addWidget(positions_, 1);

    // Debounced: a query per keystroke over 10,647 filers is wasted work.
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(250);
    connect(debounce_, &QTimer::timeout, this, [this]() {
        services::OwnershipService::instance().search_firms(search_->text());
    });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { debounce_->start(); });

    connect(firm_, &QComboBox::currentIndexChanged, this, [this](int i) {
        const QString cik = firm_->itemData(i).toString();
        if (!cik.isEmpty())
            services::OwnershipService::instance().load_book(cik);
        render();
    });

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::firms_found, this, [this]() { reload_firms(); });
    connect(&svc, &services::OwnershipService::book_updated, this, [this](const QString& cik) {
        if (cik == firm_->currentData().toString())
            render();
    });
    connect(&svc, &services::OwnershipService::index_changed, this, [this](const QString&) {
        services::OwnershipService::instance().search_firms(search_->text());
        render();
    });

    // Seed with the largest books, so an untouched panel over an indexed
    // universe is useful rather than blank.
    svc.search_firms(QString());
    render();
}

void FirmBookPanel::reload_firms() {
    const QString keep = firm_->currentData().toString();
    {
        QSignalBlocker block(firm_);
        firm_->clear();
        const auto results = services::OwnershipService::instance().last_firm_results();
        if (results.isEmpty()) {
            firm_->addItem(QStringLiteral("No filers — build the 13F index"), QString());
            firm_->setEnabled(false);
            render();
            return;
        }
        firm_->setEnabled(true);
        for (const auto& m : results)
            firm_->addItem(m.name, m.cik);
        const int idx = firm_->findData(keep);
        if (idx >= 0)
            firm_->setCurrentIndex(idx);
    }
    const QString cik = firm_->currentData().toString();
    if (!cik.isEmpty())
        services::OwnershipService::instance().load_book(cik);
    render();
}

void FirmBookPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const QString cik = firm_->currentData().toString();
    if (cik.isEmpty()) {
        status_->setText(svc.index_ready() ? QStringLiteral("Pick a filer.")
                                           : QStringLiteral("No 13F index yet."));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        positions_->setRowCount(0);
        return;
    }

    const auto b = svc.book(cik);
    if (svc.is_book_loading(cik)) {
        status_->setText(QStringLiteral("Reading the index…"));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        return;
    }
    if (!b.error.isEmpty()) {
        status_->setText(b.error);
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::AMBER()));
        positions_->setRowCount(0);
        return;
    }
    if (b.positions.isEmpty()) {
        status_->setText(QStringLiteral("No positions indexed for this filer."));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        positions_->setRowCount(0);
        return;
    }

    QString head = QStringLiteral("%1 positions · %2 · %3")
                       .arg(b.position_count)
                       .arg(fmt::format_money(b.total_value),
                            b.period.toString(QStringLiteral("MMM yyyy")));
    if (b.prior_period.isValid())
        head += QStringLiteral(" vs ") + b.prior_period.toString(QStringLiteral("MMM yyyy"));
    status_->setText(head);
    status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));

    // Exits are appended after the held positions: a name the filer no longer
    // owns has no weight and no place in a weight-ordered list, but "what did
    // they get out of" is half the question this panel answers.
    positions_->setRowCount(b.positions.size() + b.exits.size());
    int r = 0;
    auto put = [&](const BookPosition& p, bool exited) {
        const QString action = exited ? QStringLiteral("exited") : p.action;
        const QString col = action_colour(action);
        positions_->setItem(r, 0, cell(p.issuer));
        positions_->setItem(r, 1, cell(p.ticker));
        positions_->setItem(r, 2, cell(p.weight ? fmt::format_percent(*p.weight * 100.0, 2)
                                                : fmt::placeholder(),
                                       exited ? col : ui::colors::AMBER()));
        positions_->setItem(r, 3, cell(p.shares ? fmt::format_compact(*p.shares)
                                                : fmt::placeholder()));
        positions_->setItem(r, 4, cell(p.value ? fmt::format_money(*p.value)
                                               : fmt::placeholder()));
        QString move = action.isEmpty() ? fmt::placeholder() : action;
        if (p.shares_delta && *p.shares_delta != 0.0) {
            move += QStringLiteral("  %1%2")
                        .arg(*p.shares_delta > 0 ? QStringLiteral("+") : QStringLiteral("-"),
                             fmt::format_compact(std::abs(*p.shares_delta)));
        }
        positions_->setItem(r, 5, cell(move, col));
        ++r;
    };
    for (const auto& p : b.positions)
        put(p, false);
    for (const auto& p : b.exits)
        put(p, true);
}

} // namespace fincept::screens

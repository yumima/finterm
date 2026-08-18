#include "screens/ownership/FirmBookPanel.h"

#include "services/ownership/OwnershipService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
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

    // Which fifty. Ranking the universe by book value answers "who manages the
    // most money" and returns fund complexes and pension books; ranking the
    // concentrated books answers "who is running a book with a view in it",
    // which is what a trader means by the big money. Both are real readings of
    // the same filings, so the reader chooses rather than us deciding.
    ranking_ = new QComboBox;
    ranking_->addItem(QStringLiteral("Largest books"));
    ranking_->addItem(QStringLiteral("High conviction"));
    ranking_->setToolTip(QStringLiteral(
        "Largest books — the biggest disclosed equity books, which includes fund complexes "
        "and pension funds whose quarterly change is largely a rebalance.\n"
        "High conviction — books of 10 to 150 names, where a quarterly change is a decision. "
        "This is the list that surfaces Berkshire, TCI, Viking and the like."));
    bar->addWidget(ranking_);
    connect(ranking_, &QComboBox::currentIndexChanged, this, [this]() {
        services::OwnershipService::instance().search_firms(search_->text(), current_ranking());
    });


    status_ = new QLabel;
    status_->setWordWrap(true);
    bar->addWidget(status_, 1);
    root->addLayout(bar);

    // No caveat here: the same sentence already sits under the holders tile,
    // and printing it twice on one screen is noise rather than emphasis.

    // The ranked list, above the selected firm's book. A dropdown could not
    // show what each firm DID last quarter, and "who is holding what and what
    // are they doing" needs both halves visible at once.
    firms_ = new QTableWidget;
    firms_->setColumnCount(6);
    firms_->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Firm"),
                                       QStringLiteral("Book"), QStringLiteral("Names"),
                                       QStringLiteral("Last quarter"),
                                       QStringLiteral("Largest position")});
    firms_->verticalHeader()->setVisible(false);
    firms_->setSelectionBehavior(QAbstractItemView::SelectRows);
    firms_->setSelectionMode(QAbstractItemView::SingleSelection);
    firms_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    firms_->setAlternatingRowColors(true);
    // Size to content, then stay interactive — a fixed default made the
    // first column need a horizontal scrollbar to be read at all.
    firms_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    firms_->horizontalHeader()->setSectionsMovable(true);
    // The name column is capped and elided. Sized to content it grows to fit
    // "DZ BANK AG Deutsche Zentral Genossenschafts Bank, Frankfurt am Main"
    // and pushes what the firm DID off the pane — which is the column the
    // table exists for.
    firms_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    firms_->horizontalHeader()->setStretchLastSection(true);
    firms_->setMinimumHeight(150);
    connect(firms_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int r = firms_->currentRow();
        if (r < 0)
            return;
        if (auto* it = firms_->item(r, 1)) {
            selected_cik_ = it->data(Qt::UserRole).toString();
            if (!selected_cik_.isEmpty())
                services::OwnershipService::instance().load_book(selected_cik_);
            render();
        }
    });
    root->addWidget(firms_, 2);

    positions_ = new QTableWidget;
    positions_->setColumnCount(9);
    positions_->setHorizontalHeaderLabels({QStringLiteral("Issuer"), QStringLiteral("Ticker"),
                                           QStringLiteral("% of book"), QStringLiteral("Shares"),
                                           QStringLiteral("Value"), QStringLiteral("Move"),
                                           QStringLiteral("Since Q-end"),
                                           QStringLiteral("3M"), QStringLiteral("6M")});
    positions_->verticalHeader()->setVisible(false);
    positions_->setSelectionBehavior(QAbstractItemView::SelectRows);
    positions_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    positions_->setAlternatingRowColors(true);
    // Interactive, sized once per load. Same widths as ResizeToContents, but
    // measured on demand instead of on every layout pass — this table is now
    // rendered twice per book (once on the filing, once when prices land), so
    // the repeat measuring is worth avoiding.
    positions_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    positions_->horizontalHeader()->setSectionsMovable(true);
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
    // The holdings table had no caption, so "Issuer / Ticker / % of book" sat
    // under a list of firms with nothing saying whose holdings they were. The
    // selected firm's name belongs on the table, not only in the row highlight.
    book_title_ = new QLabel;
    book_title_->setStyleSheet(QString("color:%1;font-size:12px;font-weight:600;"
                                       "padding:3px 0 1px 0;")
                                   .arg(ui::colors::AMBER()));
    book_title_->setWordWrap(true);
    root->addWidget(book_title_);
    root->addWidget(positions_, 1);

    // Debounced: a query per keystroke over 10,647 filers is wasted work.
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(250);
    connect(debounce_, &QTimer::timeout, this, [this]() {
        services::OwnershipService::instance().search_firms(search_->text(), current_ranking());
    });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { debounce_->start(); });

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::firms_found, this, [this]() { reload_firms(); });
    connect(&svc, &services::OwnershipService::book_updated, this, [this](const QString& cik) {
        if (cik == selected_cik_)
            render();
    });
    connect(&svc, &services::OwnershipService::index_changed, this, [this](const QString&) {
        services::OwnershipService::instance().search_firms(search_->text(), current_ranking());
        render();
    });

    // Seed with the largest books, so an untouched panel over an indexed
    // universe is useful rather than blank.
    svc.search_firms(QString(), current_ranking());
    render();
}

services::OwnershipService::FirmRanking FirmBookPanel::current_ranking() const {
    using R = services::OwnershipService::FirmRanking;
    return ranking_ && ranking_->currentIndex() == 1 ? R::Concentrated : R::LargestBooks;
}

void FirmBookPanel::reload_firms() {
    const auto results = services::OwnershipService::instance().last_firm_results();
    firms_->setRowCount(results.size());
    firms_->setUpdatesEnabled(false);
    for (int i = 0; i < results.size(); ++i) {
        const auto& m = results[i];
        firms_->setItem(i, 0, cell(QString::number(i + 1), ui::colors::TEXT_SECONDARY()));
        auto* nm = cell(m.name);
        nm->setToolTip(m.name);   // the full name is a hover away
        nm->setData(Qt::UserRole, m.cik);
        firms_->setItem(i, 1, nm);
        firms_->setItem(i, 2, cell(fmt::format_compact(m.book_value)));
        firms_->setItem(i, 3, cell(QString::number(m.position_count),
                                   ui::colors::TEXT_SECONDARY()));

        // Direction as words with colour, not a bare net number: a firm that
        // opened four and exited sixteen is doing something a single figure
        // cannot express.
        QString act = fmt::placeholder();
        QString col;
        if (m.has_activity) {
            QStringList parts;
            if (m.opened)  parts << QStringLiteral("%1 new").arg(m.opened);
            if (m.added)   parts << QStringLiteral("%1 added").arg(m.added);
            if (m.trimmed) parts << QStringLiteral("%1 cut").arg(m.trimmed);
            if (m.exited)  parts << QStringLiteral("%1 exited").arg(m.exited);
            act = parts.isEmpty() ? QStringLiteral("no change") : parts.join(QStringLiteral(" · "));
            const int up = m.opened + m.added, down = m.trimmed + m.exited;
            col = up > down * 1.15 ? ui::colors::GREEN()
                                   : (down > up * 1.15 ? ui::colors::RED() : QString());
        }
        firms_->setItem(i, 4, cell(act, col));

        QString top = m.top_ticker.isEmpty() ? m.top_name : m.top_ticker;
        if (m.top_weight)
            top += QStringLiteral("  %1").arg(fmt::format_percent(*m.top_weight * 100.0, 1));
        firms_->setItem(i, 5, cell(top, ui::colors::TEXT_SECONDARY()));
    }
    firms_->setUpdatesEnabled(true);
    firms_->resizeColumnsToContents();
    firms_->setColumnWidth(1, qMin(firms_->columnWidth(1), 210));
    if (!results.isEmpty() && selected_cik_.isEmpty()) {
        firms_->selectRow(0);   // the largest book is a sensible landing place
    }
    render();
}

void FirmBookPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const QString cik = selected_cik_;
    if (cik.isEmpty()) {
        status_->setText(svc.index_ready() ? QStringLiteral("Pick a filer.")
                                           : QStringLiteral("No 13F index yet."));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        positions_->setRowCount(0);
        book_title_->clear();
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

    book_title_->setText(b.manager.isEmpty()
                             ? QStringLiteral("Holdings")
                             : b.manager + QStringLiteral(" — disclosed equity holdings"));

    QString head = QStringLiteral("%1 positions · %2 · %3")
                       .arg(b.position_count)
                       .arg(fmt::format_compact(b.total_value),
                            b.period.toString(QStringLiteral("MMM yyyy")));
    if (b.prior_period.isValid())
        head += QStringLiteral(" vs ") + b.prior_period.toString(QStringLiteral("MMM yyyy"));
    if (b.book_return_since_quarter_end && b.return_coverage >= ownership::kMinReturnCoverage) {
        // "Since quarter end", never "since filed": 13F is due 45 days after the
        // quarter closes and the SEC data sets carry no filing date at all, so
        // part of this window predates disclosure. Calling it "since filed"
        // would imply a return a reader could have captured.
        head += QStringLiteral(" · book %1 since %2")
                    .arg(fmt::format_percent(*b.book_return_since_quarter_end * 100.0, 1, true),
                         b.period.toString(QStringLiteral("d MMM")));
        if (b.return_coverage < 0.95)
            head += QStringLiteral(" (on %1 of book value)")
                        .arg(fmt::format_percent(b.return_coverage * 100.0, 0));
        // The quarter-end return depends on which quarter this filer is in; the
        // trailing three months is the same window for every firm, so it is the
        // one that can be compared across the ranked list.
        if (b.book_return_3m)
            head += QStringLiteral(" · %1 over 3M")
                        .arg(fmt::format_percent(*b.book_return_3m * 100.0, 1, true));
    } else if (!b.return_error.isEmpty()) {
        head += QStringLiteral(" · returns unavailable (") + b.return_error + QStringLiteral(")");
    } else if (b.book_return_since_quarter_end) {
        // Priced, but over too thin a slice to summarise. The per-position
        // columns are still exact, so point at them rather than inventing a
        // book number.
        head += QStringLiteral(" · book return not shown — priced names cover only %1 of "
                               "book value")
                    .arg(fmt::format_percent(b.return_coverage * 100.0, 0));
    }
    status_->setText(head);
    status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));

    // Exits are appended after the held positions: a name the filer no longer
    // owns has no weight and no place in a weight-ordered list, but "what did
    // they get out of" is half the question this panel answers.
    positions_->setRowCount(b.positions.size() + b.exits.size());
    positions_->setUpdatesEnabled(false);
    int r = 0;
    auto put = [&](const BookPosition& p, bool exited) {
        const QString action = exited ? QStringLiteral("exited") : p.action;
        const QString col = action_colour(action);
        auto* issuer_cell = cell(p.issuer);
        issuer_cell->setToolTip(p.issuer);   // the column is capped; the full name is a hover away
        positions_->setItem(r, 0, issuer_cell);
        positions_->setItem(r, 1, cell(p.ticker));
        positions_->setItem(r, 2, cell(p.weight ? fmt::format_percent(*p.weight * 100.0, 2)
                                                : fmt::placeholder(),
                                       exited ? col : ui::colors::AMBER()));
        positions_->setItem(r, 3, cell(p.shares ? fmt::format_compact(*p.shares)
                                                : fmt::placeholder()));
        positions_->setItem(r, 4, cell(p.value ? fmt::format_compact(*p.value)
                                               : fmt::placeholder()));
        QString move = action.isEmpty() ? fmt::placeholder() : action;
        if (p.shares_delta && *p.shares_delta != 0.0) {
            move += QStringLiteral("  %1%2")
                        .arg(*p.shares_delta > 0 ? QStringLiteral("+") : QStringLiteral("-"),
                             fmt::format_compact(std::abs(*p.shares_delta)));
        }
        positions_->setItem(r, 5, cell(move, col));

        // Price performance of the position as disclosed. The holding is a
        // quarter-end photograph and the price is daily, so these are what the
        // disclosed shares have done — not a claim about trading since.
        auto ret = [&](const std::optional<double>& v) {
            auto* it = cell(v ? fmt::format_percent(*v * 100.0, 1, true) : fmt::placeholder(),
                            v ? (*v >= 0 ? ui::colors::GREEN() : ui::colors::RED()) : QString());
            if (!v) {
                // Four different reasons produce an empty cell; saying the
                // wrong one is worse than saying nothing.
                if (exited)
                    it->setToolTip(QStringLiteral("Exited positions are not priced — there is no "
                                                 "position left to value."));
                else if (p.ticker.isEmpty())
                    it->setToolTip(QStringLiteral("This CUSIP is not mapped to a ticker yet, so "
                                                 "it cannot be priced."));
                else if (!p.priced)
                    it->setToolTip(QStringLiteral("Outside the priced set — the largest positions "
                                                 "by value are priced, this one is further down "
                                                 "the book."));
                else
                    it->setToolTip(QStringLiteral("Priced, but the daily history does not reach "
                                                 "back over this window."));
            }
            return it;
        };
        positions_->setItem(r, 6, ret(p.ret_since_quarter_end));
        positions_->setItem(r, 7, ret(p.ret_3m));
        positions_->setItem(r, 8, ret(p.ret_6m));
        ++r;
    };
    for (const auto& p : b.positions)
        put(p, false);
    for (const auto& p : b.exits)
        put(p, true);
    positions_->setUpdatesEnabled(true);
    positions_->resizeColumnsToContents();
    // Nine columns do not fit a third of the screen at content width, and the
    // ones that get pushed off the right edge are the returns — the reason the
    // row is worth reading. Cap the issuer name (the only unbounded column) so
    // the numeric tail stays on-pane; the full name is in the tooltip and the
    // pane itself is user-resizable.
    positions_->setColumnWidth(0, qMin(positions_->columnWidth(0), 150));
}

} // namespace fincept::screens

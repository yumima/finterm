#include "screens/ownership/FirmDetailPanel.h"

#include "services/ownership/OwnershipService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
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

FirmDetailPanel::FirmDetailPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(status_);

    book_title_ = new QLabel;
    book_title_->setStyleSheet(QString("color:%1;font-size:12px;font-weight:600;"
                                       "padding:3px 0 1px 0;")
                                   .arg(ui::colors::AMBER()));
    book_title_->setWordWrap(true);
    root->addWidget(book_title_);

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
    // measured on demand instead of on every layout pass — this table renders
    // twice per book, once on the filing and again when prices land.
    positions_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    positions_->horizontalHeader()->setSectionsMovable(true);
    positions_->horizontalHeader()->setStretchLastSection(true);
    // A holding is a security, and the question that follows "they own this"
    // is "who else does". One click, because a holding a reader is looking at
    // is already the thing they are asking about.
    // cellClicked, NOT itemSelectionChanged: the selection signal also fires
    // when the table is repopulated for a different firm, with the current row
    // left over from the previous book — so filling a new book could announce a
    // holding nobody clicked and swap the pane to it. Navigation must come from
    // a real gesture.
    connect(positions_, &QTableWidget::cellClicked, this, [this](int r, int) {
        if (populating_ || r < 0)
            return;
        auto* it = positions_->item(r, 1);
        const QString ticker = it ? it->text().trimmed() : QString();
        if (!ticker.isEmpty())
            emit navigate_to_symbol(ticker);
    });
    root->addWidget(positions_, 1);

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::book_updated, this, [this](const QString& cik) {
        if (cik == cik_)
            render();
    });
    render();
}

void FirmDetailPanel::set_firm(const QString& cik) {
    if (cik == cik_)
        return;
    cik_ = cik;
    if (!cik_.isEmpty())
        services::OwnershipService::instance().load_book(cik_);
    render();
}

void FirmDetailPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const QString cik = cik_;
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
        // Leaving the previous firm's rows up while a different filer loads
        // attributes one manager's holdings to another for as long as the read
        // takes. Blank is honest; wrong is not.
        positions_->setRowCount(0);
        book_title_->clear();
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
    populating_ = true;
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
    populating_ = false;
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

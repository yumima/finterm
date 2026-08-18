#include "screens/ownership/FirmBookPanel.h"

#include "services/ownership/OwnershipService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>


namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

namespace {

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

    // Typing runs a query against 10,647 filers, so it is debounced rather than
    // fired per keystroke.
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(220);
    connect(debounce_, &QTimer::timeout, this, [this]() {
        services::OwnershipService::instance().search_firms(search_->text(), current_ranking());
    });
    connect(search_, &QLineEdit::textChanged, this, [this]() { debounce_->start(); });


    bar->addStretch(1);
    root->addLayout(bar);

    // No caveat here: the same sentence already sits under the holders tile,
    // and printing it twice on one screen is noise rather than emphasis.

    // The ranked list, above the selected firm's book. A dropdown could not
    // show what each firm DID last quarter, and "who is holding what and what
    // are they doing" needs both halves visible at once.
    firms_ = new QTableWidget;
    firms_->setColumnCount(7);
    firms_->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Firm"),
                                       QStringLiteral("Book"), QStringLiteral("Top holding"),
                                       QStringLiteral("Biggest add"),
                                       QStringLiteral("Biggest trim"),
                                       QStringLiteral("Biggest exit")});
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
            const QString cik = it->data(Qt::UserRole).toString();
            if (!cik.isEmpty()) {
                selected_cik_ = cik;
                emit firm_selected(cik);
            }
        }
    });
    // Selection alone cannot bring a book back once the reader has drilled into
    // one of its holdings: the row is still current, so no selection change is
    // emitted. A click always re-announces the firm.
    connect(firms_, &QTableWidget::cellClicked, this, [this](int r, int) {
        if (auto* it = firms_->item(r, 1)) {
            const QString cik = it->data(Qt::UserRole).toString();
            if (!cik.isEmpty()) {
                selected_cik_ = cik;
                emit firm_selected(cik);
            }
        }
    });
    root->addWidget(firms_, 1);

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::firms_found, this, [this]() { reload_firms(); });
    connect(&svc, &services::OwnershipService::index_changed, this, [this](const QString&) {
        services::OwnershipService::instance().search_firms(search_->text(), current_ranking());
    });

    // Seed with the largest books, so an untouched panel over an indexed
    // universe is useful rather than blank.
    svc.search_firms(QString(), current_ranking());
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

        QString top = m.top_ticker.isEmpty() ? m.top_name : m.top_ticker;
        if (m.top_weight)
            top += QStringLiteral("  %1").arg(fmt::format_percent(*m.top_weight * 100.0, 1));
        auto* topit = cell(top, ui::colors::TEXT_SECONDARY());
        topit->setToolTip(QStringLiteral("%1 · %2 of a %3 book across %4 names")
                              .arg(m.top_name,
                                   m.top_weight ? fmt::format_percent(*m.top_weight * 100.0, 1)
                                                : fmt::placeholder(),
                                   fmt::format_compact(m.book_value))
                              .arg(m.position_count));
        firms_->setItem(i, 3, topit);

        // What they DID, by name and in money. Four counts — "110 new · 236
        // added · 163 cut · 58 exited" — are nearly identical for every large
        // filer and answer nothing; "added $10.5B of GOOGL, cut $9.5B of CVX"
        // is the row a reader stops on. Counts move to the tooltip, where they
        // are context rather than the headline.
        QStringList counts;
        if (m.opened)  counts << QStringLiteral("%1 new").arg(m.opened);
        if (m.added)   counts << QStringLiteral("%1 added").arg(m.added);
        if (m.trimmed) counts << QStringLiteral("%1 cut").arg(m.trimmed);
        if (m.exited)  counts << QStringLiteral("%1 exited").arg(m.exited);
        const QString tip = counts.isEmpty()
                                ? QStringLiteral("No change against the prior quarter.")
                                : counts.join(QStringLiteral(" · "));

        auto move_cell = [&](const ownership::Manager::Move& mv, const QString& colour) {
            auto* it = cell(mv.valid ? QStringLiteral("%1  %2").arg(
                                           mv.label(), fmt::format_compact(mv.value))
                                     : (m.has_activity ? QStringLiteral("—")
                                                       : fmt::placeholder()),
                            mv.valid ? colour : QString());
            it->setToolTip(mv.valid ? QStringLiteral("%1 — %2 at this quarter's price\n%3")
                                          .arg(mv.issuer, fmt::format_compact(mv.value), tip)
                                    : tip);
            return it;
        };
        firms_->setItem(i, 4, move_cell(m.top_add, ui::colors::GREEN()));
        firms_->setItem(i, 5, move_cell(m.top_trim, ui::colors::RED()));
        firms_->setItem(i, 6, move_cell(m.top_exit, ui::colors::RED()));
    }
    firms_->setUpdatesEnabled(true);
    firms_->resizeColumnsToContents();
    // Seven columns at content width do not fit half a screen, and the ones
    // that fall off the right edge are the three that answer "what is this
    // firm doing" — the reason to scan the list at all. Every free-text column
    // is capped and elided; the full value is in the tooltip.
    const int caps[] = {0, 175, 0, 140, 132, 132, 132};
    for (int c = 1; c < firms_->columnCount(); ++c) {
        if (caps[c] > 0)
            firms_->setColumnWidth(c, qMin(firms_->columnWidth(c), caps[c]));
    }
    if (!results.isEmpty() && selected_cik_.isEmpty()) {
        firms_->selectRow(0);   // the largest book is a sensible landing place
    }
}

} // namespace fincept::screens

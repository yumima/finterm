#include "screens/ownership/HoldersTable.h"

#include "screens/ownership/OwnershipUi.h"
#include "ui/components/ExternalLink.h"
#include "ui/components/TooltipText.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

using namespace fincept::ownership;
using namespace fincept::screens::ownership_ui;

namespace {

QString tier_label(const Holder& h) {
    const QString n = QLocale().toString(h.position_count);
    if (h.tier == QLatin1String("index"))
        return QStringLiteral("Index · %1 names").arg(n);
    if (h.tier == QLatin1String("broad"))
        return QStringLiteral("Broad · %1").arg(n);
    return QStringLiteral("Focused · %1").arg(n);
}

QString action_colour(const QString& a) {
    if (a == QLatin1String("added") || a == QLatin1String("new"))
        return ui::colors::GREEN();
    if (a == QLatin1String("trimmed"))
        return ui::colors::RED();
    return ui::colors::TEXT_SECONDARY();
}

/// The filter chips are a segmented control: one button lit at a time.
QPushButton* chip(const QString& text, const QString& key) {
    auto* b = new QPushButton(text);
    b->setCheckable(true);
    b->setProperty("key", key);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QString("QPushButton{color:%1;background:transparent;border:1px solid %2;"
                             "padding:2px 10px;font-size:12px;}"
                             "QPushButton:checked{color:%3;border-color:%4;background:%5;}"
                             "QPushButton:hover{color:%3;}")
                         .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                              ui::colors::TEXT_PRIMARY(), ui::colors::AMBER(),
                              ui::colors::BG_RAISED()));
    return b;
}

} // namespace

HoldersTable::HoldersTable(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto title = section_title(QStringLiteral("HOLDERS"));
    title_note_ = title.right;

    auto* bar = new QHBoxLayout;
    bar->setSpacing(6);
    bar->addWidget(title.widget, 1);
    filters_ = new QButtonGroup(this);
    filters_->setExclusive(true);
    for (const auto& [text, key] : {std::pair{QStringLiteral("All"), QStringLiteral("all")},
                                    std::pair{QStringLiteral("New"), QStringLiteral("new")},
                                    std::pair{QStringLiteral("Added"), QStringLiteral("added")},
                                    std::pair{QStringLiteral("Reduced"), QStringLiteral("trimmed")},
                                    std::pair{QStringLiteral("Focused books"), QStringLiteral("focused")}}) {
        auto* b = chip(text, key);
        filters_->addButton(b);
        bar->addWidget(b);
        if (key == QLatin1String("all"))
            b->setChecked(true);
    }
    connect(filters_, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* b) {
        filter_ = b->property("key").toString();
        render();
    });
    bar->addSpacing(12);
    sort_ = new QComboBox;
    sort_->addItem(QStringLiteral("by position size"), false);
    sort_->addItem(QStringLiteral("by % of their book (books ≥ $1B)"), true);
    sort_->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "Position size is who owns the most of this company. Weight is how much of the "
        "filer's own book this is — a different question, answered with a floor on book size "
        "so that a two-person adviser with one name in the account does not outrank Berkshire.")));
    connect(sort_, &QComboBox::activated, this,
            [this](int i) { emit sort_requested(sort_->itemData(i).toBool()); });
    bar->addWidget(sort_);
    root->addLayout(bar);

    table_ = make_table({QStringLiteral("Holder"), QStringLiteral("Type"), QStringLiteral("Shares"),
                         QStringLiteral("% out"), QStringLiteral("Δ shares"), QStringLiteral("Δ %"),
                         QStringLiteral("% of their book"), QStringLiteral("Book"),
                         QStringLiteral("As of")});
    table_->setMinimumHeight(90);
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int) {
        auto* it = table_->item(r, 0);
        if (!it)
            return;
        const QString cik = it->data(Qt::UserRole).toString();
        if (!cik.isEmpty())
            emit holder_activated(cik, it->data(Qt::UserRole + 1).toString());
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        // A 13D/13G badge on the row: double-click opens the filing itself.
        auto* it = table_->item(r, 0);
        const QString url = it ? it->data(Qt::UserRole + 2).toString() : QString();
        if (!url.isEmpty())
            ui::open_external_link(url);
    });
    root->addWidget(table_, 1);

    foot_ = note_label();
    root->addWidget(foot_);
    clear();
}

void HoldersTable::clear() {
    snap_ = {};
    loading_ = false;
    table_->setRowCount(0);
    title_note_->clear();
    foot_->clear();
}

void HoldersTable::set_snapshot(const OwnershipSnapshot& snap, bool loading) {
    snap_ = snap;
    loading_ = loading;
    // Keep the control on what the rows actually are in.
    const bool by_weight = snap.summary.sort == QLatin1String("weight");
    if (sort_->currentData().toBool() != by_weight)
        sort_->setCurrentIndex(by_weight ? 1 : 0);
    render();
}

void HoldersTable::render() {
    const auto& s = snap_;
    const auto& sum = s.summary;

    if (!s.holders_ok) {
        table_->setRowCount(0);
        title_note_->setText(loading_ && s.holders_error.isEmpty()
                                 ? QStringLiteral("reading the 13F index…")
                                 : s.holders_error.isEmpty()
                                       ? QStringLiteral("no 13F index built — build one from the OWNERSHIP screen")
                                       : s.holders_error.left(140));
        foot_->clear();
        return;
    }

    title_note_->setText(QStringLiteral("%1 filers · Q%2 %3 · filed by %4")
                             .arg(QLocale().toString(sum.holder_count))
                             .arg((sum.quarter.month() - 1) / 3 + 1).arg(sum.quarter.year())
                             .arg(sum.filed_by().toString(QStringLiteral("d MMM yyyy"))));

    // Stakes by filer name, so a 13D/13G can sit on the holder's row. The
    // match is on the name the schedule was filed under, which is the best
    // the two sources offer; a miss leaves the row unbadged, never mis-badged.
    QHash<QString, const BeneficialStake*> stakes;
    for (const auto& st : s.stakes) {
        if (st.filer.isEmpty() || !st.subject_verified)
            continue;
        const QString key = st.filer.toLower().left(12);
        if (!stakes.contains(key) || st.filed_date > stakes.value(key)->filed_date)
            stakes.insert(key, &st);
    }

    QVector<const Holder*> rows;
    for (const auto& h : s.holders) {
        if (filter_ == QLatin1String("focused") && !h.is_focused())
            continue;
        if (filter_ != QLatin1String("all") && filter_ != QLatin1String("focused") &&
            h.action != filter_)
            continue;
        rows.push_back(&h);
    }

    const auto& shares_out = s.vendor.shares_outstanding;
    table_->setUpdatesEnabled(false);
    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const Holder& h = *rows[i];
        // The badge leads the name: the name column is capped and elided,
        // and a badge on the end of a long filer name is a badge nobody sees.
        QString name = h.manager;
        const auto* st = stakes.value(h.manager.toLower().left(12), nullptr);
        if (st)
            name = (st->activist ? QStringLiteral("[13D]  ") : QStringLiteral("[13G]  ")) + name;
        auto* who = cell(name, st && st->activist ? ui::colors::AMBER() : QString());
        who->setData(Qt::UserRole, h.cik);
        who->setData(Qt::UserRole + 1, h.manager);
        QString tip = QStringLiteral("%1 — click to open this filer's book").arg(h.manager);
        if (st) {
            who->setData(Qt::UserRole + 2, st->url);
            tip += QStringLiteral("\n%1 filed %2 — double-click to open the filing")
                       .arg(st->form, st->filed_date.toString(QStringLiteral("d MMM yyyy")));
        }
        if (!h.note.isEmpty())
            tip += QStringLiteral("\n") + h.note;
        who->setToolTip(ui::tooltip_wrap(tip));
        table_->setItem(i, 0, who);

        auto* tier = cell(tier_label(h), h.is_focused() ? QString() : ui::colors::TEXT_SECONDARY());
        tier->setToolTip(ui::tooltip_wrap(
            h.tier == QLatin1String("index")
                ? QStringLiteral("A named passive complex. Its quarterly change is an index rebalance, not a view.")
                : h.tier == QLatin1String("broad")
                      ? QStringLiteral("A book of a thousand or more names — tracking a benchmark or running client mandates, not expressing a view on this company.")
                      : QStringLiteral("A book narrow enough that a position in it is a decision.")));
        table_->setItem(i, 1, tier);

        table_->setItem(i, 2, num_cell(compact_or_dash(h.shares)));
        std::optional<double> pct_out;
        if (h.shares && shares_out && *shares_out > 0)
            pct_out = *h.shares / *shares_out;
        table_->setItem(i, 3, num_cell(pct_or_dash(pct_out, 2)));

        const QString col = action_colour(h.action);
        QString delta = fmt::placeholder();
        if (h.action == QLatin1String("new"))
            delta = QStringLiteral("new");
        else if (h.action == QLatin1String("first seen"))
            delta = QStringLiteral("first seen");
        else if (h.shares_delta && std::fabs(*h.shares_delta) >= 1.0)
            delta = (*h.shares_delta > 0 ? QStringLiteral("+") : QStringLiteral("−")) +
                    fmt::format_compact(std::fabs(*h.shares_delta));
        else if (h.action == QLatin1String("held"))
            delta = QStringLiteral("held");
        auto* d = num_cell(delta, col);
        if (h.action == QLatin1String("first seen"))
            d->setToolTip(ui::tooltip_wrap(QStringLiteral(
                "No prior-quarter filing from this filer, so an opening position cannot be told "
                "apart from a first appearance.")));
        table_->setItem(i, 4, d);
        table_->setItem(i, 5, num_cell(h.pct_change ? pct_or_dash(h.pct_change, 1, true)
                                                    : fmt::placeholder(), col));
        table_->setItem(i, 6, num_cell(pct_or_dash(h.weight, 2),
                                       h.weight && *h.weight >= 0.05 ? ui::colors::AMBER() : QString()));
        table_->setItem(i, 7, num_cell(compact_or_dash(h.book_total)));
        auto* asof = cell(QStringLiteral("Q%1 %2")
                              .arg((sum.quarter.month() - 1) / 3 + 1).arg(sum.quarter.year()),
                          ui::colors::TEXT_SECONDARY());
        asof->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Quarter end the position was reported for. 13F is due 45 days later; the SEC data "
            "sets carry no per-filing date.")));
        table_->setItem(i, 8, asof);
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(0, qMin(qMax(table_->columnWidth(0), 220), 320));

    QStringList notes;
    notes << QStringLiteral("showing %1 of %2 filers%3")
                 .arg(rows.size())
                 .arg(QLocale().toString(sum.holder_count))
                 .arg(filter_ == QLatin1String("all") ? QString()
                                                      : QStringLiteral(" (filtered)"));
    if (sum.option_holders > 0)
        notes << QStringLiteral("%1 hold options only — not counted as long").arg(sum.option_holders);
    if (sum.sort == QLatin1String("weight"))
        notes << QStringLiteral("ranked by weight among books of $%1 or more")
                     .arg(fmt::format_compact(sum.min_book_value));
    notes << QStringLiteral("click a holder for its whole book");
    foot_->setText(notes.join(QStringLiteral(" · ")));
}

} // namespace fincept::screens

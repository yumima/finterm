// src/screens/power_trader/LeaderboardPanel.cpp
#include "screens/power_trader/LeaderboardPanel.h"

#include "ui/theme/Theme.h"

#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::screens {

static const QStringList kLeaderCols = {"#", "NAME", "PTY", "CHB", "ALPHA", "RET", "TRD"};

// Party hex colors — these are semantic constants (D/R/I are fixed political
// identities), not theme tokens.
static constexpr const char* kPartyD = "#3b82f6"; // blue
static constexpr const char* kPartyR = "#ef4444"; // red
static constexpr const char* kPartyI = "#eab308"; // yellow

static const char* party_color(const QString& p) {
    if (p == QStringLiteral("D")) return kPartyD;
    if (p == QStringLiteral("R")) return kPartyR;
    return kPartyI;
}

LeaderboardPanel::LeaderboardPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void LeaderboardPanel::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Section header
    auto* hdr_label = new QLabel(QStringLiteral("LEADERBOARD"), this);
    hdr_label->setStyleSheet(
        QString("QLabel { background:%1; color:%2; font-size:9px; font-weight:700;"
                " letter-spacing:1.5px; padding:6px 10px; border-bottom:1px solid %3; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_TERTIARY(), ui::colors::BORDER_DIM()));
    layout->addWidget(hdr_label);

    table_ = new QTableWidget(this);
    table_->setColumnCount(kLeaderCols.size());
    table_->setHorizontalHeaderLabels(kLeaderCols);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setSortingEnabled(true);
    table_->sortByColumn(4, Qt::DescendingOrder); // ALPHA col

    auto* h = table_->horizontalHeader();
    h->setMinimumSectionSize(20);
    h->setStretchLastSection(false);
    // Fixed width for rank + party + chamber; NAME stretches
    h->setSectionResizeMode(0, QHeaderView::Fixed);   // #
    h->resizeSection(0, 28);
    h->setSectionResizeMode(1, QHeaderView::Stretch);  // NAME
    h->setSectionResizeMode(2, QHeaderView::Fixed);    // PTY
    h->resizeSection(2, 32);
    h->setSectionResizeMode(3, QHeaderView::Fixed);    // CHB
    h->resizeSection(3, 44);
    h->setSectionResizeMode(4, QHeaderView::Fixed);    // ALPHA
    h->resizeSection(4, 52);
    h->setSectionResizeMode(5, QHeaderView::Fixed);    // RET
    h->resizeSection(5, 52);
    h->setSectionResizeMode(6, QHeaderView::Fixed);    // TRD
    h->resizeSection(6, 36);

    table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:11px; font-family:Consolas,monospace;"
                "  gridline-color:transparent; }"
                "QTableWidget::item { padding:3px 6px; border-bottom:1px solid %3; }"
                "QTableWidget::item:selected { background:rgba(217,119,6,0.18); color:%2; }"
                "QTableWidget::item:hover { background:%4; }"
                "QScrollBar:vertical { width:4px; background:%1; }"
                "QScrollBar::handle:vertical { background:%3; min-height:16px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BORDER_DIM(), ui::colors::BG_HOVER()));

    h->setStyleSheet(
        QString("QHeaderView::section { background:%1; color:%2; border:none;"
                "  border-bottom:2px solid %3; border-right:1px solid %4;"
                "  padding:4px 6px; font-size:9px; font-weight:700; letter-spacing:0.5px; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::AMBER(), ui::colors::BORDER_DIM()));

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= table_->rowCount())
            return;
        auto* item = table_->item(row, 1);
        if (!item)
            return;
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            emit member_selected(id);
    });

    layout->addWidget(table_);
}

void LeaderboardPanel::set_members(const QVector<power_trader::CongressMember>& members) {
    members_ = members;
    // Sort by alpha descending
    std::stable_sort(members_.begin(), members_.end(),
                     [](const power_trader::CongressMember& a, const power_trader::CongressMember& b) {
                         return a.alpha_ytd > b.alpha_ytd;
                     });
    populate_table();
}

void LeaderboardPanel::set_selected(const QString& member_id) {
    selected_id_ = member_id;
    for (int r = 0; r < table_->rowCount(); ++r) {
        auto* item = table_->item(r, 1);
        if (item && item->data(Qt::UserRole).toString() == member_id) {
            table_->selectRow(r);
            return;
        }
    }
}

void LeaderboardPanel::populate_table() {
    table_->setSortingEnabled(false);
    table_->setRowCount(0);

    const int limit = qMin(members_.size(), 50);
    table_->setRowCount(limit);

    for (int r = 0; r < limit; ++r) {
        const auto& m = members_[r];
        table_->setRowHeight(r, 26);

        auto set_item = [&](int col, const QString& text, const char* color = nullptr,
                            Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(r, col, item);
        };

        // Rank
        set_item(0, QString::number(r + 1),
                 ui::colors::TEXT_TERTIARY, Qt::AlignCenter);

        // Name — store member_id in UserRole
        auto* name_item = new QTableWidgetItem(m.full_name);
        name_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        name_item->setForeground(QColor(ui::colors::CYAN()));
        name_item->setData(Qt::UserRole, m.id);
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(r, 1, name_item);

        // Party
        set_item(2, m.party, party_color(m.party), Qt::AlignCenter);

        // Chamber
        set_item(3,
                 m.chamber == power_trader::MemberChamber::Senate ? QStringLiteral("SEN")
                                                                   : QStringLiteral("HSE"),
                 ui::colors::TEXT_SECONDARY, Qt::AlignCenter);

        // Alpha YTD
        const bool alpha_pos = m.alpha_ytd >= 0;
        set_item(4,
                 QString("%1%2%").arg(alpha_pos ? "+" : "").arg(m.alpha_ytd, 0, 'f', 1),
                 alpha_pos ? ui::colors::POSITIVE : ui::colors::NEGATIVE);

        // Return YTD
        const bool ret_pos = m.portfolio_return_ytd >= 0;
        set_item(5,
                 QString("%1%2%").arg(ret_pos ? "+" : "").arg(m.portfolio_return_ytd, 0, 'f', 1),
                 ret_pos ? ui::colors::POSITIVE : ui::colors::NEGATIVE);

        // Trades YTD
        set_item(6, QString::number(m.trade_count_ytd),
                 ui::colors::TEXT_SECONDARY, Qt::AlignCenter);

        if (m.id == selected_id_)
            table_->selectRow(r);
    }

    table_->setSortingEnabled(true);
}

QString LeaderboardPanel::format_pct(double v) {
    return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(v, 0, 'f', 1);
}

} // namespace fincept::screens

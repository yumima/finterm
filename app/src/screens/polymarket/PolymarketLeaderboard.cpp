#include "screens/polymarket/PolymarketLeaderboard.h"

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

namespace finterm::screens::polymarket {

using namespace finterm::ui;
using namespace finterm::services::polymarket;

PolymarketLeaderboard::PolymarketLeaderboard(QWidget* parent) : QWidget(parent) {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* header_bar = new QWidget;
    header_bar->setFixedHeight(30);
    header_bar->setStyleSheet(
        QString("background: %1; border-bottom: 1px solid %2;").arg(colors::BG_RAISED(), colors::BORDER_DIM()));
    auto* hbl = new QHBoxLayout(header_bar);
    hbl->setContentsMargins(12, 0, 8, 0);
    auto* header = new QLabel("LEADERBOARD");
    header->setStyleSheet(QString("color: %1; font-size: 9px; font-weight: 700; letter-spacing: 0.8px; "
                                  "background: transparent;")
                              .arg(colors::TEXT_SECONDARY()));
    hbl->addWidget(header);
    hbl->addStretch(1);
    vl->addWidget(header_bar);

    table_ = new QTableWidget;
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"#", "TRADER", "PNL", "VOLUME", "TRADES"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setShowGrid(false);
    table_->setStyleSheet(QString("QTableWidget { background: %1; color: %2; border: none; font-size: 10px; }"
                                  "QTableWidget::item { padding: 3px 8px; border-bottom: 1px solid %3; }"
                                  "QTableWidget::item:selected { background: %4; color: %2; }"
                                  "QHeaderView::section { background: %5; color: %6; border: none;"
                                  "  border-bottom: 1px solid %3; padding: 5px 8px;"
                                  "  font-size: 8px; font-weight: 700; letter-spacing: 0.5px; }"
                                  "QScrollBar:vertical { background: %1; width: 4px; border: none; }"
                                  "QScrollBar::handle:vertical { background: %3; min-height: 20px; }"
                                  "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
                              .arg(colors::BG_BASE(), colors::TEXT_PRIMARY(), colors::BORDER_DIM(), colors::BG_HOVER(),
                                   colors::BG_RAISED(), colors::TEXT_SECONDARY()));
    vl->addWidget(table_, 1);
}

void PolymarketLeaderboard::set_loading(bool loading) {
    if (loading) {
        table_->setRowCount(0);
    }
}

} // namespace finterm::screens::polymarket

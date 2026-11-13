#include "ui/tables/DataTable.h"

#include "ui/tables/TickFlashDelegate.h"
#include "ui/theme/Theme.h"

#include <QHeaderView>

namespace fincept::ui {

DataTable::DataTable(QWidget* parent) : QTableWidget(parent) {
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setShowGrid(false);
    verticalHeader()->setVisible(false);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Explicit font-size on items so DataTable renders at the same size across
    // every dashboard widget that uses it (Indices, Commodities, Forex, Crypto).
    // Without this, items inherit from the qApp font which can drift if a child
    // QSS context strips inheritance — Commodities and Indices were rendering at
    // visibly different sizes when nested under different parent stylesheets.
    setStyleSheet(QString("QTableWidget { background: %1; alternate-background-color: %2; "
                          "gridline-color: %3; border: none; font-size: %4px; }"
                          "QTableWidget::item { padding: 4px 8px; height: 26px; font-size: %4px; }"
                          "QTableWidget::item:selected { background: #111111; }")
                      .arg(colors::DARK(), colors::ROW_ALT(), colors::BORDER())
                      .arg(fonts::font_px(0)));
}

void DataTable::set_headers(const QStringList& headers) {
    setColumnCount(headers.size());
    setHorizontalHeaderLabels(headers);
}

void DataTable::set_data(const QVector<QStringList>& rows) {
    setRowCount(0);
    for (const auto& row : rows) {
        add_row(row);
    }
}

void DataTable::add_row(const QStringList& row) {
    int r = rowCount();
    insertRow(r);
    for (int c = 0; c < row.size() && c < columnCount(); ++c) {
        auto* item = new QTableWidgetItem(row[c]);
        item->setForeground(QColor(colors::WHITE()));
        setItem(r, c, item);
    }
    setRowHeight(r, 26);
}

bool DataTable::set_rows(const QVector<QStringList>& rows) {
    bool same_shape = rowCount() == rows.size();
    for (int r = 0; same_shape && r < rows.size(); ++r)
        same_shape = rows[r].size() <= columnCount();
    if (!same_shape) {
        set_data(rows);
        return true;
    }
    bool width_may_change = false;
    for (int r = 0; r < rows.size(); ++r) {
        const QStringList& row = rows[r];
        for (int c = 0; c < row.size(); ++c) {
            QTableWidgetItem* it = item(r, c);
            if (!it) {
                it = new QTableWidgetItem(row[c]);
                it->setForeground(QColor(colors::WHITE()));
                setItem(r, c, it);
                width_may_change = true;
            } else if (it->text() != row[c]) {
                width_may_change = width_may_change || it->text().size() != row[c].size();
                it->setText(row[c]);
            }
        }
    }
    return width_may_change;
}

void DataTable::enable_tick_flash(int key_column) {
    TickFlashDelegate::install(this, key_column);
}

void DataTable::clear_data() {
    setRowCount(0);
}

void DataTable::set_column_widths(const QVector<int>& widths) {
    for (int i = 0; i < widths.size() && i < columnCount(); ++i) {
        setColumnWidth(i, widths[i]);
    }
}

void DataTable::set_cell_color(int row, int col, const QString& color) {
    auto* it = item(row, col);
    const QColor c(color);
    if (it && it->foreground().color() != c) // live ticks re-colour every row: skip no-ops
        it->setForeground(c);
}

} // namespace fincept::ui

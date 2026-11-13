#pragma once
#include <QStringList>
#include <QTableWidget>
#include <QVector>

namespace finterm::ui {

/// Reusable data table with alternating rows.
class DataTable : public QTableWidget {
    Q_OBJECT
  public:
    explicit DataTable(QWidget* parent = nullptr);

    void set_headers(const QStringList& headers);
    void set_data(const QVector<QStringList>& rows);
    void add_row(const QStringList& row);
    void clear_data();
    /// A faint green/red cue on a cell whose number just changed (live quote
    /// tables). Needs a column whose text is unique per row (the symbol).
    void enable_tick_flash(int key_column = 0);
    /// Show `rows`. Same shape as now (row count, and every row fits the
    /// columns) → only cells whose text changed are touched: no item churn,
    /// no relayout, and only those cells repaint — the live-tick path. Any
    /// other shape → a plain rebuild. Returns true when a cell may now need a
    /// different width (rebuilt, or a text's length changed) — re-fit then.
    bool set_rows(const QVector<QStringList>& rows);
    void set_column_widths(const QVector<int>& widths);

    // Color a specific cell
    void set_cell_color(int row, int col, const QString& color);
};

} // namespace finterm::ui

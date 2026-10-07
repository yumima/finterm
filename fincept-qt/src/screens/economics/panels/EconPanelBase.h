// src/screens/economics/panels/EconPanelBase.h
// Base class for all economics source panels.
// Provides: stat cards + empty/loading/error state + data table + CSV export.
// Subclasses call build_base_ui() from their constructor.
// For panels with a left selector sidebar, pass the right-side container
// to build_base_ui(container) instead.
#pragma once

#include "services/economics/EconomicsService.h"

#include <QDate>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

class EconPanelBase : public QWidget {
    Q_OBJECT
  public:
    explicit EconPanelBase(const QString& source_id, const QString& color, QWidget* parent = nullptr);

    /// Called by EconomicsScreen when user switches to this panel.
    virtual void activate() = 0;

  protected:
    // ── Subclass API ──────────────────────────────────────────────────────────
    /// Add source-specific controls into the toolbar layout.
    virtual void build_controls(QHBoxLayout* toolbar_layout) = 0;
    virtual void on_fetch() = 0;
    virtual void on_result(const QString& request_id, const services::EconomicsResult& result) = 0;

    // ── UI construction ───────────────────────────────────────────────────────
    /// Build toolbar + stat cards + table stack into the given container widget.
    /// Call this from the subclass constructor (pass 'this' or a right-side pane).
    void build_base_ui(QWidget* container);

    // ── State helpers ─────────────────────────────────────────────────────────
    void show_loading(const QString& msg = "Fetching data…");
    void show_error(const QString& msg);
    void show_empty(const QString& msg = "Select parameters and click FETCH");
    void show_table();

    /// Populate the shared table + update stat cards.
    /// value_key: the row field holding the headline series value. Empty means
    ///   the table has no single series value (cross-sectional / multi-column),
    ///   so the value stat cards show "—" instead of guessing a column.
    /// date_key: the row field the series is ordered by. Rows are sorted
    ///   oldest→newest on it before LATEST/CHANGE are computed (sources such as
    ///   BLS and FiscalData send newest-first). Empty means no time order, so
    ///   LATEST/CHANGE show "—".
    void display(const QJsonArray& rows, const QString& title = {}, const QString& value_key = {},
                 const QString& date_key = {});
    void export_csv();

    /// Per-provider accent color stylesheet (uses live theme tokens).
    QString panel_style() const;

    /// Called when theme changes — re-applies panel_style(). Override for extra widgets.
    virtual void refresh_panel_theme();

    // ── Shared utilities ──────────────────────────────────────────────────────
    /// Filter a QListWidget by hiding items that don't contain text (case-insensitive).
    static void filter_list(QListWidget* list, const QString& text);

    // ── Reusable token-based style helpers for sidebar panels ─────────────────
    static QString sidebar_style();      // left panel background + border
    static QString section_hdr_style();  // section header bar (e.g. "COUNTRY")
    static QString section_lbl_style();  // section header label text
    static QString search_input_style(); // search/filter QLineEdit
    static QString ctrl_label_style();   // toolbar control labels ("YEARS", "PRESET")
    QString list_style() const;          // QListWidget using source accent color
    static QString notice_style();       // warning/info notice label

    // ── Shared widgets (accessible to subclasses) ─────────────────────────────
    QLabel* stat_latest_ = nullptr;
    QLabel* stat_change_ = nullptr;
    QLabel* stat_min_ = nullptr;
    QLabel* stat_max_ = nullptr;
    QLabel* stat_avg_ = nullptr;
    QLabel* stat_count_ = nullptr;
    QPushButton* fetch_btn_ = nullptr;
    QPushButton* export_btn_ = nullptr;

    QString source_id_;
    QString color_;

  private:
    void update_stats(const QJsonArray& rows, const QString& value_key, const QString& date_key);

    QWidget* container_ = nullptr; // root widget that owns panel_style()
    QWidget* cards_row_ = nullptr;
    QWidget* title_bar_ = nullptr;
    QStackedWidget* stack_ = nullptr; // 0=empty/status  1=table
    QLabel* empty_lbl_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* title_lbl_ = nullptr;
    QLabel* row_count_ = nullptr;
};

} // namespace fincept::screens

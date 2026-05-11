#pragma once

#include <QHash>
#include <QHBoxLayout>
#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QLabel;

namespace fincept::knowledge {

class CategoryColumn;
class AbbreviationsColumn;
class GroupedPane;
class RailWidget;

/// KNOWLEDGE tab — 4-pane cockpit:
///
///   [ BASICS  : Glossary | Concepts | Abbreviations | Regulators ]
///   [ PRACTICE: Formulas | Tracks   | Playbooks | Cases | Interviews ]
///   [ CONTEXT : finterm rail tied to last selection              ]
///   [ QUANT   : Foundations | Risk · Return | Factors | Execution
///                | Backtesting | Strategies | Practices          ]
///
/// Within each grouped pane, sub-tabs are segmented buttons. Each
/// sub-pane (CategoryColumn / AbbreviationsColumn) keeps its own
/// last-viewed entry — switching sub-tabs preserves the picker /
/// body state. The Context rail follows whichever sub-tab was most
/// recently activated across all groups.
class KnowledgeScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KnowledgeScreen(QWidget* parent = nullptr);

    /// Public entry point used by HelpHint navigator and external callers.
    /// Routes the entry into its proper category column, switching the
    /// hosting super-pane's sub-tab to it as needed.
    void open_entry(const QString& entry_id);

  signals:
    /// Forward navigation requests to MainWindow / dock router. `ticker`
    /// is non-empty when the action wants the target screen to load a
    /// specific symbol (caller pushes onto SymbolContext).
    void navigate_to_screen(const QString& screen_id, const QString& ticker = {});

  private:
    void build_layout();
    void on_category_active(CategoryColumn* col, const QString& entry_id);
    void on_search(const QString& query);

    QLineEdit* search_ = nullptr;
    QLabel* breadcrumb_ = nullptr;
    QLabel* count_label_ = nullptr;

    GroupedPane* basics_pane_   = nullptr;
    GroupedPane* practice_pane_ = nullptr;
    GroupedPane* quant_pane_    = nullptr;

    QHash<QString, CategoryColumn*> category_cols_;
    AbbreviationsColumn* abbrev_col_ = nullptr;
    RailWidget* rail_ = nullptr;

    // ── Recently viewed ───────────────────────────────────────────────────────
    QWidget*         recent_bar_ = nullptr;
    QHBoxLayout*     recent_hl_  = nullptr;
    QVector<QString> recently_viewed_;      // entry IDs, most recent first, max 8
    static constexpr int kMaxRecent = 8;
    void update_recent_bar();
};

} // namespace fincept::knowledge

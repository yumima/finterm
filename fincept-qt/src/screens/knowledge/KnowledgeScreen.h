#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QLabel;

namespace fincept::knowledge {

class CategoryColumn;
class AbbreviationsColumn;
class RailWidget;

/// KNOWLEDGE tab — 5-column cockpit:
///   [GLOSSARY] [CONCEPTS] [PLAYBOOKS] [ABBREVIATIONS] [FINTERM RAIL]
/// Each category column has its own picker; the rail follows whichever
/// column was most recently focused.
class KnowledgeScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KnowledgeScreen(QWidget* parent = nullptr);

    /// Public entry point used by HelpHint navigator and external callers.
    /// Routes the entry into its proper category column.
    void open_entry(const QString& entry_id);

  signals:
    /// Forward navigation requests to MainWindow / dock router. `ticker`
    /// is non-empty when the action wants the target screen to load a
    /// specific symbol (caller pushes onto SymbolContext).
    void navigate_to_screen(const QString& screen_id, const QString& ticker = {});

  private:
    void build_layout();
    void on_column_active(CategoryColumn* col, const QString& entry_id);
    void on_search(const QString& query);
    void set_active_column(QWidget* col);

    QLineEdit* search_ = nullptr;
    QLabel* breadcrumb_ = nullptr;
    QLabel* count_label_ = nullptr;

    QVector<CategoryColumn*> category_cols_;     ///< one per category
    AbbreviationsColumn* abbrev_col_ = nullptr;
    RailWidget* rail_ = nullptr;
    QWidget* active_col_ = nullptr;              ///< column currently driving the rail
};

} // namespace fincept::knowledge

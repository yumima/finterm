#pragma once

#include "screens/knowledge/KnowledgeTypes.h"

#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;

namespace fincept::knowledge {

class EntryBodyView;

/// One vertical pane in the cockpit. Header (label + entry-picker dropdown)
/// over a body view of the currently-selected entry. Emits a signal when
/// the active entry changes so the shared rail can update.
class CategoryColumn : public QWidget {
    Q_OBJECT
  public:
    explicit CategoryColumn(const KnowledgeCategory& category, QWidget* parent = nullptr);

    QString category_id() const { return category_id_; }
    QString active_entry_id() const { return active_entry_id_; }

    /// Programmatically open an entry (used by external search / cross-refs).
    /// Returns true if the entry belongs to this column and was opened.
    bool open_entry(const QString& entry_id);

    /// Toggle a "this column drives the rail" highlight on the header.
    void set_active(bool active);

  signals:
    /// Emitted whenever the active entry changes (incl. initial selection)
    /// or the user clicks anywhere in the column to focus it.
    void entry_activated(const QString& entry_id);

    /// Emitted when a [[id]] cross-reference is clicked in the body markdown.
    void open_entry_external(const QString& entry_id);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void set_active_entry_internal(const QString& entry_id);

    QString category_id_;
    QString active_entry_id_;
    QComboBox* picker_ = nullptr;
    EntryBodyView* body_ = nullptr;
    QLabel* header_label_ = nullptr;
};

} // namespace fincept::knowledge

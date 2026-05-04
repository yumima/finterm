#pragma once

#include "screens/knowledge/KnowledgeTypes.h"

#include <QString>
#include <QWidget>

class QLabel;
class QTextBrowser;
class QVBoxLayout;

namespace fincept::knowledge {

/// Body-only renderer for an entry: compact header (difficulty, title,
/// subtitle), warnings, and the markdown body. NO right rail — meant to
/// live inside a CategoryColumn in the cockpit layout.
class EntryBodyView : public QWidget {
    Q_OBJECT
  public:
    explicit EntryBodyView(QWidget* parent = nullptr);

    /// Swap the displayed entry. Pass an empty optional to show a placeholder.
    void set_entry(const KnowledgeEntry* entry);
    const KnowledgeEntry* current_entry() const { return current_; }

  signals:
    /// Click on a [[id]] cross-reference inside the markdown body.
    void open_entry(const QString& entry_id);

  private:
    void rebuild();

    const KnowledgeEntry* current_ = nullptr;
    QVBoxLayout* root_ = nullptr;
    QWidget* content_ = nullptr;
};

} // namespace fincept::knowledge

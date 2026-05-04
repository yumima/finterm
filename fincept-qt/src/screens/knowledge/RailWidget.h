#pragma once

#include "screens/knowledge/KnowledgeTypes.h"

#include <QString>
#include <QWidget>

class QVBoxLayout;

namespace fincept::knowledge {

/// Right-rail panel: Live in finterm, Try in finterm, Where in finterm,
/// Related, Further reading, AI Tutor. Driven by a single "active entry"
/// and rebuilds when set_entry() is called.
class RailWidget : public QWidget {
    Q_OBJECT
  public:
    explicit RailWidget(QWidget* parent = nullptr);

    void set_entry(const KnowledgeEntry* entry);

  signals:
    void open_entry(const QString& entry_id);                       ///< click on RELATED chip
    void request_action(const QString& screen_id, const QString& ticker); ///< Try / Where in finterm

  private:
    void rebuild();

    const KnowledgeEntry* current_ = nullptr;
    QVBoxLayout* root_ = nullptr;
    QWidget* content_ = nullptr;
};

} // namespace fincept::knowledge

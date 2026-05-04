#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace fincept::knowledge {

/// One vertical pane in the cockpit dedicated to the flat abbreviations
/// dictionary. Header (label + filter input) over a scrollable list.
/// Clicking an abbreviation that has a matching glossary entry asks the
/// shared rail to switch context to that entry.
class AbbreviationsColumn : public QWidget {
    Q_OBJECT
  public:
    explicit AbbreviationsColumn(QWidget* parent = nullptr);

    QString last_selected_entry_id() const { return last_entry_id_; }

  signals:
    /// Emitted when the user clicks an abbreviation that maps to a known
    /// knowledge entry — payload is the entry id. The cockpit forwards
    /// this to the rail for context switching.
    void entry_activated(const QString& entry_id);

  private:
    void rebuild_list(const QString& filter = {});

    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    QString last_entry_id_;
};

} // namespace fincept::knowledge

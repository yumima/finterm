#pragma once

#include <QPointer>
#include <QPushButton>
#include <QString>
#include <functional>

class QWidget;

namespace fincept::knowledge {

/// Small "?" button placed next to a metric label in any screen. Clicking
/// shows a lightweight popover with the entry's title and abbreviation
/// expansion + a button that jumps to the  KNOWLEDGE tab pre-scrolled to the
/// full entry. The actual navigation is wired via set_navigator() once at
/// app startup so widgets don't need to know about MainWindow.
class HelpHint : public QPushButton {
    Q_OBJECT
  public:
    explicit HelpHint(QString entry_id, QWidget* parent = nullptr);

    /// Navigator callback: called with an entry id when the user clicks
    /// "Open in LEARNING". Set once by MainWindow after constructing the
    /// KNOWLEDGE tab.
    static void set_navigator(std::function<void(QString)> nav);

  private:
    void show_popover();
    QString entry_id_;
};

} // namespace fincept::knowledge

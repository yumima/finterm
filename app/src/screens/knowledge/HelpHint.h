#pragma once

#include <QPushButton>
#include <QString>

class QWidget;

namespace finterm::knowledge {

/// Small "?" button placed next to a metric label in any screen. Clicking
/// shows a lightweight popover with the entry's title and abbreviation
/// expansion + a button that jumps to the  KNOWLEDGE tab pre-scrolled to the
/// full entry (ContentLoader::request_open + a nav.switch_screen to it, so
/// widgets don't need to know about MainWindow).
class HelpHint : public QPushButton {
    Q_OBJECT
  public:
    explicit HelpHint(QString entry_id, QWidget* parent = nullptr);

  private:
    void show_popover();
    QString entry_id_;
};

} // namespace finterm::knowledge

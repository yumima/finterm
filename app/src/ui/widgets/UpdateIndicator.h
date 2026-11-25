#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

namespace finterm::ui {

/// The status bar's update slot. It shows one of three things:
///  - a newer version was found: "UPDATE vX AVAILABLE", which opens the
///    install prompt when clicked (a check never opens a dialog by itself);
///    then "DOWNLOADING vX…" and "vX INSTALLER STARTED" as it proceeds.
///  - automatic checks were never decided: a one-time question,
///    "AUTO UPDATE?  TURN ON · NO THANKS". Either answer is stored
///    and the question does not come back. About › Check for updates
///    automatically changes it later.
///  - otherwise nothing.
/// Dev builds (no release version) show nothing.
class UpdateIndicator : public QWidget {
    Q_OBJECT
  public:
    explicit UpdateIndicator(QWidget* parent = nullptr);

  private:
    void refresh();
    void refresh_theme();

    QLabel* question_ = nullptr;
    QPushButton* primary_ = nullptr;   ///< TURN ON, or the offer itself
    QPushButton* secondary_ = nullptr; ///< NO THANKS
};

} // namespace finterm::ui

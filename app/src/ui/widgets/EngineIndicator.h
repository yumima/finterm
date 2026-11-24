#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

namespace finterm::ui {

/// The status bar's one question about hearth: "USE HEARTH FOR LOCAL AI?
/// CONNECT… · NO THANKS", when one was found and the user was never asked.
/// Either answer is kept (EngineLinkRepository) and the question does not come
/// back; Settings › AI Config changes it later. Otherwise it shows nothing.
class EngineIndicator : public QWidget {
    Q_OBJECT
  public:
    explicit EngineIndicator(QWidget* parent = nullptr);

  private:
    void refresh();
    void refresh_theme();
    void open_dialog();

    QLabel* question_ = nullptr;
    QPushButton* yes_ = nullptr;
    QPushButton* no_ = nullptr;
};

} // namespace finterm::ui

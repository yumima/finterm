#pragma once
// DataStatusIndicator — the status bar's data slot. "READY" while data loads;
// when some does not, what is failing ("DATA: Yahoo Finance rate-limited"),
// and a click opens the panel that says, per problem, what failed, why, and
// what to do about it — with Retry and Copy details (for a support request).

#include <QPushButton>

class QFrame;
class QVBoxLayout;

namespace finterm::ui {

class DataStatusIndicator : public QPushButton {
    Q_OBJECT
  public:
    explicit DataStatusIndicator(QWidget* parent = nullptr);

  private:
    void refresh();
    void refresh_theme();
    void toggle_panel();
    void fill_panel();

    QFrame* panel_ = nullptr;
    QVBoxLayout* panel_list_ = nullptr;
};

} // namespace finterm::ui

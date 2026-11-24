#pragma once
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;

namespace finterm::screens {

/// hearth as the user sees it: what was found, and what can be done about it.
/// In Settings › AI Config, and in the question's dialog (EngineIndicator);
/// the first-run screen shows it without the actions, there being no user yet
/// whose answer it could keep.
class EnginePanel : public QWidget {
    Q_OBJECT
  public:
    enum class Mode { Full, LookOnly };
    explicit EnginePanel(Mode mode = Mode::Full, QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void refresh();
    void run_primary();
    void get_hearth();
    void say(const QString& text, bool problem);

    Mode mode_;
    QLabel* headline_ = nullptr;
    QLabel* detail_ = nullptr;
    QLabel* note_ = nullptr; ///< what the last action came to
    QPushButton* primary_ = nullptr;
    QPushButton* secondary_ = nullptr; ///< disconnect
    QCheckBox* for_chat_ = nullptr;
    QCheckBox* start_with_app_ = nullptr;
};

} // namespace finterm::screens

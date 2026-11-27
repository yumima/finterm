#pragma once
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace finterm::screens {

class PinRecoveryPanel;

/// Lock screen — the signed-in user's PIN unlocks the terminal (at start with
/// a saved session, and after inactivity). Obsidian design: sharp corners,
/// monospace, no shadows.
///
/// Pages:
///   0 — unlock: whose terminal it is, the PIN, the wait after wrong PINs,
///       and "Forgot PIN?" / "Switch user"
///   1 — forgot: the ways back in (PinRecoveryPanel)
class LockScreen : public QWidget {
    Q_OBJECT
  public:
    explicit LockScreen(QWidget* parent = nullptr);

    /// Show the unlock page.
    void activate();
    void show_unlock();

  signals:
    /// The PIN was accepted (or reset with the recovery key).
    void unlocked();
    /// Sign out to the user picker: another user, or a new one.
    void switch_user_requested();

  private:
    QWidget* pages_ = nullptr; ///< the card: one page shown at a time
    QWidget* unlock_page_ = nullptr;

    QLabel* unlock_user_ = nullptr;
    QLineEdit* unlock_pin_input_ = nullptr;
    QPushButton* unlock_btn_ = nullptr;
    QLabel* unlock_error_ = nullptr;
    QLabel* unlock_lockout_label_ = nullptr;
    PinRecoveryPanel* recovery_ = nullptr;
    QWidget* recovery_page_ = nullptr;

    QTimer* lockout_timer_ = nullptr;

    void build_unlock_page();
    void build_recovery_page();
    void show_page(QWidget* page);
    void on_unlock_submit();
    void update_lockout_display();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
};

} // namespace finterm::screens

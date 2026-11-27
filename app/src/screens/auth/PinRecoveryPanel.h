#pragma once
// PinRecoveryPanel — "Forgot your PIN?", on the lock screen and on the
// sign-in screen alike. The ways back in, from the one that keeps everything
// to the one that keeps nothing:
//   1. a recovery key sets a new PIN (all data kept);
//   2. sign in as someone else, or create a new user;
//   3. reset this user (their data on this computer goes);
//   4. start fresh (finterm set up again; the old data moved to a backup).

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace finterm::screens {

class PinRecoveryPanel : public QWidget {
    Q_OBJECT
  public:
    explicit PinRecoveryPanel(QWidget* parent = nullptr);

    /// Show the choices for `username`. `signed_in`: the lock screen (the
    /// user's session is open), else the sign-in screen.
    void start(const QString& username, bool signed_in);

  signals:
    /// Back to the PIN entry.
    void back();
    /// A new PIN was set with the recovery key.
    void pin_reset(const QString& username, const QString& new_pin);
    /// Pick another user, or create one.
    void switch_user();
    /// `username` was deleted.
    void user_reset(const QString& username);

  private:
    void build_choices();
    void build_key_form();
    void on_reset_with_key();
    void on_reset_user();
    void on_start_fresh();
    void show_page(QWidget* page);

    QString username_;
    bool signed_in_ = false;

    QWidget* choices_ = nullptr;
    QWidget* key_form_ = nullptr;
    QLabel* for_user_ = nullptr;
    QWidget* key_btn_ = nullptr;
    QLabel* key_note_ = nullptr;
    QLineEdit* key_input_ = nullptr;
    QLineEdit* new_pin_ = nullptr;
    QLineEdit* new_pin2_ = nullptr;
    QLabel* key_error_ = nullptr;
};

} // namespace finterm::screens

#pragma once
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

namespace fincept::screens {

/// Local-only registration: pick a username + set a 4-6 digit PIN.
/// No email, no OTP, no MFA — everything is stored in the local auth.db.
class RegisterScreen : public QWidget {
    Q_OBJECT
  public:
    explicit RegisterScreen(QWidget* parent = nullptr);

  signals:
    void navigate_login();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    QLineEdit*   username_     = nullptr;
    QLineEdit*   pin_          = nullptr;
    QLineEdit*   pin_confirm_  = nullptr;
    QPushButton* create_btn_   = nullptr;
    QPushButton* back_btn_     = nullptr;
    QLabel*      error_label_  = nullptr;

    void build_form();

  private slots:
    void on_create();
};

} // namespace fincept::screens

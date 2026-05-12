#pragma once
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

namespace fincept::screens {

/// Local login: pick a user from the list (sourced from the local auth.db),
/// then punch in their 4-6 digit PIN. No email, no MFA, no remote auth.
/// Falls through to a "first-run" panel that nudges new users into signup
/// when the local user database is empty.
class LoginScreen : public QWidget {
    Q_OBJECT
  public:
    explicit LoginScreen(QWidget* parent = nullptr);

  signals:
    void navigate_register();
    /// Deprecated in the local-only flow but kept so MainWindow's existing
    /// wiring compiles. Never emitted in normal use.
    void navigate_forgot_password();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    QStackedWidget* pages_         = nullptr;

    // ── Picker page ───────────────────────────────────────────────────────
    QWidget*      picker_page_     = nullptr;
    QListWidget*  user_list_       = nullptr;
    QPushButton*  create_btn_      = nullptr;
    QLabel*       picker_empty_    = nullptr;

    // ── PIN page ──────────────────────────────────────────────────────────
    QWidget*      pin_page_        = nullptr;
    QLabel*       pin_username_lbl_ = nullptr;
    QLineEdit*    pin_input_       = nullptr;
    QPushButton*  pin_unlock_btn_  = nullptr;
    QPushButton*  pin_back_btn_    = nullptr;
    QPushButton*  pin_delete_btn_  = nullptr;
    QLabel*       pin_error_       = nullptr;
    QString       active_username_;

    void build_picker_page();
    void build_pin_page();
    void refresh_user_list();
    void show_picker();
    void show_pin_pad(const QString& username);

  private slots:
    void on_user_selected(QListWidgetItem* item);
    void on_unlock();
    void on_delete_user();
    void on_login_succeeded();
    void on_login_failed(const QString& error);
};

} // namespace fincept::screens

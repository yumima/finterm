#pragma once
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace fincept::screens {

/// Settings → Account. finterm is local-only: the account is a row in
/// auth.db on this machine, so the page shows only what is stored there
/// plus where this install keeps its data.
/// Sections: Overview, Security
class ProfileScreen : public QWidget {
    Q_OBJECT
  public:
    explicit ProfileScreen(QWidget* parent = nullptr);

  private:
    QStackedWidget* sections_ = nullptr;
    QList<QPushButton*> nav_buttons_;

    // Header
    QLabel* username_header_ = nullptr;

    // Overview
    QLabel* ov_username_ = nullptr;
    QWidget* ov_email_row_ = nullptr;
    QLabel* ov_email_ = nullptr;
    QLabel* ov_member_since_ = nullptr;
    QLabel* ov_data_profile_ = nullptr;
    QLabel* ov_data_folder_ = nullptr;
    QLabel* ov_account_dbs_ = nullptr;

    // Security
    QLabel* sec_api_key_ = nullptr;
    QPushButton* sec_show_key_btn_ = nullptr;
    QLabel* sec_pin_ = nullptr;
    bool api_key_visible_ = false;

    void build_header(QVBoxLayout* root);
    void build_tab_nav(QVBoxLayout* root);
    QWidget* build_overview();
    QWidget* build_security();

    void refresh_all();
    void hide_api_key();

    // Helpers
    QWidget* make_panel(const QString& title);
    QWidget* make_data_row(const QString& label, QLabel*& value_out);

    void show_logout_confirm();
    void show_regen_confirm();
    void show_delete_account_dialog();

  private slots:
    void on_section_changed(int index);
};

} // namespace fincept::screens

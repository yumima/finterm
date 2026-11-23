#pragma once
#include "ui/navigation/CommandBar.h"

#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace finterm::ui {

/// Combined toolbar: File/Navigate/View/Help menus + command bar + LIVE + the
/// market and local clocks + user info + settings + logout.
/// A single row.
class ToolBar : public QWidget {
    Q_OBJECT
  public:
    explicit ToolBar(QWidget* parent = nullptr);

    void refresh_user_display();

  signals:
    void navigate_to(const QString& tab_id);
    void dock_command(const QString& action, const QString& primary, const QString& secondary);
    void action_triggered(const QString& action);
    void logout_clicked();
    void workbench_toggled();

  protected:
    void resizeEvent(QResizeEvent* e) override;

  private slots:
    void update_clock();
    /// The clocks' widths: the longest text each can show.
    void size_clocks();

  private:
    QMenuBar* menu_bar_ = nullptr;
    QLabel* clock_label_ = nullptr;  ///< LOCAL date time zone
    QLabel* market_label_ = nullptr; ///< NYSE·NASDAQ time, session, countdown to the bell
    QLabel* market_sep_ = nullptr;
    QLabel* clock_sep_ = nullptr;
    QLabel* live_sep_ = nullptr;
    QLabel* user_label_ = nullptr;
    QLabel* live_dot_ = nullptr;
    QLabel* live_label_ = nullptr;
    QPushButton* settings_btn_ = nullptr;
    QPushButton* workbench_btn_ = nullptr;
    QPushButton* logout_btn_ = nullptr;
    QTimer* clock_timer_ = nullptr;
    QVector<QLabel*> separators_;

    CommandBar* command_bar_ = nullptr;

    void refresh_theme();
    void apply_responsive_layout(int width);

    QMenu* build_file_menu();
    QMenu* build_navigate_menu();
    QMenu* build_view_menu();
    QMenu* build_help_menu();
};

} // namespace finterm::ui

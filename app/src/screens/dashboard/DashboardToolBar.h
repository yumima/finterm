#pragma once
#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QWidget>

namespace finterm::screens {

/// Top toolbar for dashboard — terminal identity, clock, widget count, actions.
class DashboardToolBar : public QWidget {
    Q_OBJECT
  public:
    explicit DashboardToolBar(QWidget* parent = nullptr);

    void set_widget_count(int count);

    /// Shown in the dock's tab row ("Dashboard [x]  FINTERM | clock | …"):
    /// transparent and borderless at the row's height; off = its own 26 px bar.
    void set_in_tab_row(bool on);

  signals:
    void add_widget_clicked();
    void save_layout_clicked();
    void reset_layout_clicked();
    void toggle_pulse_clicked();
    void toggle_compact_clicked();

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    void update_clock();
    void refresh_theme();

    QWidget* left_container_ = nullptr;
    QWidget* right_container_ = nullptr;
    QPushButton* clock_btn_ = nullptr;
    bool clock_is_utc_ = true;
    QLabel* widget_count_ = nullptr;
    QPushButton* pulse_btn_ = nullptr;
    QPushButton* compact_btn_ = nullptr;
    QTimer clock_timer_;
    bool in_tab_row_ = false;
    QString applied_theme_key_; ///< what refresh_theme() last applied (skip if unchanged)
};

} // namespace finterm::screens

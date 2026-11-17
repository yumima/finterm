#pragma once
// The ALERTS manager (reachable from the status-bar bell) and the add/edit
// dialog (also opened from a watchlist row's and the Equity Research header's
// "Add alert…"). The rules themselves live in alerts::AlertService.

#include "services/alerts/AlertRules.h"

#include <QDialog>

#include <limits>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace finterm::ui {

/// Open a symbol in Equity Research beside the current screen — the same
/// gesture every other "open in research" uses.
void open_in_equity_research(const QString& symbol);

/// The manager, one instance, non-modal.
void open_alerts_manager(QWidget* parent);

/// "Add alert…" prefilled with `symbol` and, when known, its current price
/// (NaN → the service's last polled price, else blank).
void open_add_alert(QWidget* parent, const QString& symbol,
                    double price = std::numeric_limits<double>::quiet_NaN());

class AlertEditDialog : public QDialog {
    Q_OBJECT
  public:
    /// `existing` set → edit that rule; else a new rule for `symbol`.
    AlertEditDialog(QWidget* parent, const std::optional<alerts::Rule>& existing, const QString& symbol,
                    double price);
    alerts::Rule rule() const;

  private:
    void sync_fields();
    void validate();

    std::optional<alerts::Rule> existing_;
    double price_ = std::numeric_limits<double>::quiet_NaN();
    QLineEdit* symbol_ = nullptr;
    QComboBox* kind_ = nullptr;
    QLabel* level_lbl_ = nullptr;
    QDoubleSpinBox* level_ = nullptr;
    QLabel* pct_lbl_ = nullptr;
    QDoubleSpinBox* pct_ = nullptr;
    QLabel* session_lbl_ = nullptr;
    QComboBox* session_ = nullptr;
    QLabel* hint_ = nullptr;
    QPushButton* ok_ = nullptr;
};

class AlertsDialog : public QDialog {
    Q_OBJECT
  public:
    explicit AlertsDialog(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void refresh();
    void refresh_notes();
    QString selected_id() const;
    void edit_selected();
    void update_buttons();

    QLabel* notes_ = nullptr;
    QCheckBox* own_insider_ = nullptr;
    QCheckBox* own_stakes_ = nullptr;
    QCheckBox* own_moves_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* edit_btn_ = nullptr;
    QPushButton* pause_btn_ = nullptr;
    QPushButton* delete_btn_ = nullptr;
    QPushButton* open_btn_ = nullptr;
    QTimer* refresh_timer_ = nullptr;
};

} // namespace finterm::ui

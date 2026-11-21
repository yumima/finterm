#pragma once
// ConditionsView — Markets ▸ CONDITIONS: every market-condition widget
// (screens/dashboard/widgets/ConditionWidgets.h — the very classes the
// dashboard offers) on one page, in three resizable columns.
//
// Edited the way the rest of the app is: each widget's own list in place
// (+ ADD / −); a widget's × hides it from the page and "+ WIDGET" in the
// header brings it back. Widget configs, the hidden set and the column
// sizes persist in settings ("markets_conditions.*").

#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QWidget>

class QPushButton;
class QSplitter;

namespace finterm::screens::widgets {
class BaseWidget;
}

namespace finterm::screens {

class ConditionsView : public QWidget {
    Q_OBJECT
  public:
    explicit ConditionsView(QWidget* parent = nullptr);
    /// A "+ WIDGET" control for the Markets header (shown in CONDITIONS mode).
    QPushButton* add_button() const { return add_btn_; }
    void refresh_all();

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void build();
    void equalise();
    void save_hidden();
    void show_add_menu();
    QJsonObject load_cfg(const QString& id) const;
    void save_cfg(const QString& id, const QJsonObject& cfg);

    QStringList hidden_;
    QSplitter* cols_ = nullptr;
    QHash<QString, widgets::BaseWidget*> widgets_;
    QPushButton* add_btn_ = nullptr;
    QString split_key_;      ///< settings key of the current arrangement's sizes
    bool equalised_ = false; ///< first-show sizing done for this arrangement
};

} // namespace finterm::screens

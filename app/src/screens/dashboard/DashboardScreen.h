#pragma once
#include "screens/dashboard/DashboardToolBar.h"
#include "screens/dashboard/MarketPulsePanel.h"
#include "screens/dashboard/canvas/DashboardCanvas.h"
#include "services/markets/MarketDataService.h"

#include <QHash>
#include <QPointer>
#include <QVBoxLayout>
#include <QHideEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>
#include <QTimer>
#include <QWidget>

namespace finterm::screens {

/// Main dashboard screen — toolbar, draggable widget grid, market pulse.
/// (No status bar or market tape of its own: the main window's StatusBar and
/// banner (PushpinBar) are on every screen.)
class DashboardScreen : public QWidget {
    Q_OBJECT
  public:
    explicit DashboardScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

  private:
    void refresh_theme();
    /// The toolbar in the dock's tab row while shown; back in the screen when not.
    void attach_toolbar();
    void detach_toolbar();
    void build_default_layout();
    void save_layout();
    void restore_layout();

    QPointer<DashboardToolBar> toolbar_; ///< may sit in a dock title bar (QPointer: ADS can delete it)
    QVBoxLayout* root_layout_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    DashboardCanvas* canvas_ = nullptr;
    QSplitter* content_split_ = nullptr;
    MarketPulsePanel* market_pulse_ = nullptr;
    QTimer* save_timer_ = nullptr;
    bool pulse_visible_ = true;
    bool layout_restored_ = false;
    bool split_sized_ = false;
    QString applied_theme_key_; // the theme refresh_theme() last applied (skip if unchanged)

};

} // namespace finterm::screens

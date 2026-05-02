#include "ui/navigation/DockToolBar.h"

namespace fincept::ui {

DockToolBar::DockToolBar(QWidget* parent) : QToolBar("Main Toolbar", parent) {
    // Stable objectName is required by QMainWindow::saveState / restoreState
    // to match the saved toolbar state back to this widget. Without it,
    // Qt logs `'objectName' not set for QToolBar ... 'Main Toolbar'` and
    // restored floating-toolbar state ends up as an orphan top-level
    // window titled "Fincept Terminal" — exactly the multi-window
    // visual bug we saw in practice.
    setObjectName("mainDockToolbar");
    setMovable(false);
    setFloatable(false);
    setAllowedAreas(Qt::TopToolBarArea);
    setFixedHeight(32);
    setStyleSheet("QToolBar{border:none;spacing:0;padding:0;margin:0;}");

    inner_ = new ToolBar(this);
    addWidget(inner_);
}

} // namespace fincept::ui

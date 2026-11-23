#include "screens/dashboard/DashboardScreen.h"

#include "screens/dashboard/canvas/AddWidgetDialog.h"
#include "screens/dashboard/canvas/LayoutCodec.h"
#include "screens/dashboard/canvas/DashboardTemplates.h"
#include "screens/dashboard/canvas/TemplatePicker.h"
#include "screens/dashboard/canvas/WidgetRegistry.h"
#include "services/markets/MarketDataService.h"
#include "services/notifications/NotificationService.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <DockAreaTabBar.h>
#include <DockAreaTitleBar.h>
#include <DockAreaWidget.h>
#include <DockWidget.h>

#include <QBoxLayout>
#include <QDataStream>
#include <QEvent>
#include <QHideEvent>
#include <QPalette>
#include <QPointer>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

void apply_solid_background(QWidget* widget, const QColor& color) {
    if (!widget)
        return;

    widget->setAttribute(Qt::WA_StyledBackground, true);
    widget->setAutoFillBackground(true);

    QPalette pal = widget->palette();
    pal.setColor(QPalette::Window, color);
    pal.setColor(QPalette::Base, color);
    pal.setColor(QPalette::Button, color);
    widget->setPalette(pal);
}

} // namespace

namespace finterm::screens {

DashboardScreen::DashboardScreen(QWidget* parent) : QWidget(parent) {
    apply_solid_background(this, QColor(ui::colors::BG_BASE()));
    refresh_theme();

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    root_layout_ = vl;

    // ── Top Toolbar ──
    // Shown in the dock's tab row beside "Dashboard [x]" (attach_toolbar), so
    // the canvas gets the row it used to take; here only when not docked.
    toolbar_ = new DashboardToolBar;
    vl->addWidget(toolbar_);

    // (The scrolling market tape is the banner at the top of every screen —
    // PushpinBar — not the dashboard's own.)

    // ── Main Content: Canvas (in scroll) + Market Pulse ──
    content_split_ = new QSplitter(Qt::Horizontal);
    content_split_->setHandleWidth(1);
    content_split_->setStyleSheet(QString("QSplitter::handle{background:%1;}QSplitter::handle:hover{background:%2;}")
                                      .arg(ui::colors::BORDER_DIM(), ui::colors::BORDER_MED()));

    canvas_ = new DashboardCanvas;

    // Scroll area wraps canvas — provides vertical scroll when content exceeds viewport
    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(false); // canvas controls its own size
    scroll_area_->setMinimumWidth(0);        // allow ADS to shrink this panel freely
    scroll_area_->setWidget(canvas_);
    scroll_area_->setStyleSheet(QString("QScrollArea{border:none;background:%1;}"
                                        "QScrollBar:vertical{width:6px;background:transparent;}"
                                        "QScrollBar::handle:vertical{background:%2;border-radius:3px;min-height:20px;}"
                                        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}")
                                    .arg(ui::colors::BG_BASE(), ui::colors::BORDER_MED()));
    scroll_area_->viewport()->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    // Sync canvas width to scroll viewport via event filter
    scroll_area_->viewport()->installEventFilter(this);

    market_pulse_ = new MarketPulsePanel;

    content_split_->addWidget(scroll_area_);
    content_split_->addWidget(market_pulse_);
    content_split_->setStretchFactor(0, 3);
    content_split_->setStretchFactor(1, 1);
    content_split_->setCollapsible(0, false);
    content_split_->setCollapsible(1, false);
    market_pulse_->setMinimumWidth(180);

    vl->addWidget(content_split_, 1);

    // The in-app notification toast lives with the bell on the app status bar,
    // so it shows on every screen, not only while the dashboard exists.

    // ── Debounced save timer (P3: not started in constructor) ──
    save_timer_ = new QTimer(this);
    save_timer_->setSingleShot(true);
    save_timer_->setInterval(800);
    connect(save_timer_, &QTimer::timeout, this, &DashboardScreen::save_layout);

    // ── Canvas signals → toolbar/statusbar ──
    connect(canvas_, &DashboardCanvas::widget_count_changed, toolbar_, &DashboardToolBar::set_widget_count);
    connect(canvas_, &DashboardCanvas::layout_changed, this, [this](const GridLayout&) { save_timer_->start(); });

    // ── Toolbar buttons ──
    connect(toolbar_, &DashboardToolBar::toggle_pulse_clicked, this, [this]() {
        pulse_visible_ = !pulse_visible_;
        market_pulse_->setVisible(pulse_visible_);
    });

    connect(toolbar_, &DashboardToolBar::add_widget_clicked, this, [this]() {
        auto* dlg = new AddWidgetDialog(this);
        connect(dlg, &AddWidgetDialog::widget_selected, canvas_, &DashboardCanvas::add_widget);
        dlg->exec();
        dlg->deleteLater();
    });

    connect(toolbar_, &DashboardToolBar::reset_layout_clicked, this, [this]() {
        auto* dlg = new TemplatePicker(this);
        connect(dlg, &TemplatePicker::template_selected, this, [this](const QString& tid) {
            // Clear saved state so fresh template is used
            finterm::SettingsRepository::instance().remove("dashboard_canvas_layout");
            canvas_->apply_template(tid);
        });
        dlg->exec();
        dlg->deleteLater();
    });

    connect(toolbar_, &DashboardToolBar::save_layout_clicked, this, &DashboardScreen::save_layout);

    connect(toolbar_, &DashboardToolBar::toggle_compact_clicked, this, [this]() {
        // Remember the row height before going compact so the toggle restores
        // exactly what the user was looking at — fit_rows_to_viewport may have
        // expanded rows past the default 60 to fill a tall window, and a fixed
        // 60 fallback would feel like the second click didn't restore the
        // previous view.
        static int saved_row_h = 0; // 0 = not currently compact
        if (saved_row_h == 0) {
            saved_row_h = canvas_->current_layout().row_h;
            canvas_->set_row_height(40);
        } else {
            canvas_->set_row_height(saved_row_h);
            saved_row_h = 0;
        }
    });

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });

    // Build the tiles on the next event-loop turn rather than on first show.
    // The router pre-builds this screen in the background after unlock; with
    // the restore left to showEvent, that pre-build was an empty shell and the
    // first DASHBOARD click still spent ~1 s constructing every tile (and
    // initialising FFmpeg for the video tile) before anything painted. Hidden
    // tiles subscribe to nothing until shown, so this costs no data traffic.
    QTimer::singleShot(0, this, [this]() {
        if (!layout_restored_) {
            layout_restored_ = true;
            restore_layout();
        }
    });
}

// ── Theme ─────────────────────────────────────────────────────────────────────

void DashboardScreen::refresh_theme() {
    // Called on every show. setStyleSheet() re-polishes the WHOLE subtree —
    // every tile, label and table re-matched against the stylesheet and its
    // text laid out again — even when the string is unchanged: 0.5 s per
    // return to the Dashboard with 11 tiles, 1.2 s with the user's 8 (one a
    // video tile), during which the tiles' already-seeded quotes could not
    // paint ("the indices take seconds to load"). Apply only what changed.
    const QString key = QString(ui::colors::BG_BASE()) + ui::colors::BORDER_DIM() + ui::colors::BORDER_MED();
    if (key == applied_theme_key_)
        return;
    applied_theme_key_ = key;
    const QColor bg(ui::colors::BG_BASE());
    apply_solid_background(this, bg);

    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    if (content_split_) {
        apply_solid_background(content_split_, bg);
        content_split_->setStyleSheet(
            QString("QSplitter{background:%1;}QSplitter::handle{background:%2;}QSplitter::handle:hover{background:%3;}")
                .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM(), ui::colors::BORDER_MED()));
    }
    if (scroll_area_) {
        apply_solid_background(scroll_area_, bg);
        scroll_area_->setStyleSheet(
            QString("QScrollArea{border:none;background:%1;}"
                    "QScrollBar:vertical{width:6px;background:transparent;}"
                    "QScrollBar::handle:vertical{background:%2;border-radius:3px;min-height:20px;}"
                    "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}")
                .arg(ui::colors::BG_BASE(), ui::colors::BORDER_MED()));
        // Ensure viewport doesn't paint its own opaque background over the canvas
        apply_solid_background(scroll_area_->viewport(), bg);
        scroll_area_->viewport()->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    }
}

// ── Show/Hide ─────────────────────────────────────────────────────────────────

// ── Toolbar in the tab row ────────────────────────────────────────────────────

void DashboardScreen::attach_toolbar() {
    if (!toolbar_)
        return;
    ads::CDockWidget* dock = nullptr;
    for (QWidget* w = parentWidget(); w && !dock; w = w->parentWidget())
        dock = qobject_cast<ads::CDockWidget*>(w);
    ads::CDockAreaTitleBar* bar =
        dock && dock->dockAreaWidget() ? dock->dockAreaWidget()->titleBar() : nullptr;
    if (!bar || !bar->tabBar()) {
        // Not docked: its own bar at the top of the screen.
        detach_toolbar();
        toolbar_->show();
        return;
    }
    if (toolbar_->parentWidget() != bar) {
        root_layout_->removeWidget(toolbar_);
        toolbar_->set_in_tab_row(true);
        // Right after the tab label, taking the row's free width.
        bar->insertWidget(bar->indexOf(bar->tabBar()) + 1, toolbar_);
        if (auto* box = qobject_cast<QBoxLayout*>(bar->layout()))
            box->setStretchFactor(toolbar_, 1);
    }
    toolbar_->show();
}

void DashboardScreen::detach_toolbar() {
    // Back into the screen whenever it isn't showing: the tab row belongs to
    // the dock area, which another screen may take over or ADS may delete
    // (a dock area deletes its children with it).
    if (!toolbar_ || toolbar_->parentWidget() == this)
        return;
    toolbar_->hide();
    toolbar_->set_in_tab_row(false);
    root_layout_->insertWidget(0, toolbar_);
}

void DashboardScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    detach_toolbar();
}

void DashboardScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh_theme();
    attach_toolbar();

    // Set splitter sizes on first show using actual pixel width.
    // Must be done here (not in constructor) because the widget has no size yet
    // during construction. MarketPulsePanel's large sizeHint otherwise causes it
    // to claim ~70% of the splitter before the user sees it.
    if (!split_sized_) {
        split_sized_ = true;
        const int total = content_split_->width();
        if (total > 400) {
            const int pulse_w = qBound(180, total / 4, 320);
            content_split_->setSizes({total - pulse_w, pulse_w});
        }
    }

    if (!layout_restored_) {
        layout_restored_ = true;
        restore_layout();
    }
}



// ── Event filter: sync canvas width to scroll viewport ────────────────────────

bool DashboardScreen::eventFilter(QObject* obj, QEvent* event) {
    if (obj == scroll_area_->viewport() && event->type() == QEvent::Resize) {
        const int vp_w = scroll_area_->viewport()->width();
        const int vp_h = scroll_area_->viewport()->height();

        // Sync canvas width to viewport — avoids triggering reflow on every
        // scroll tick by guarding on actual width change.
        if (vp_w > 0 && vp_w != canvas_->width()) {
            canvas_->setMinimumWidth(0);
            canvas_->setMaximumWidth(QWIDGETSIZE_MAX);
            canvas_->resize(vp_w, canvas_->height());
        }

        // Distribute tiles to fill the viewport height: scale row_h up when
        // the window is taller than the current content so tiles expand to
        // use the available space rather than leaving blank canvas at the bottom.
        if (vp_h > 0)
            canvas_->fit_rows_to_viewport(vp_h);
    }
    return QWidget::eventFilter(obj, event);
}

// ── Save / Restore layout via SettingsRepository ──────────────────────────────

void DashboardScreen::save_layout() {
    save_timer_->stop();
    finterm::SettingsRepository::instance().set("dashboard_canvas_layout",
                                                layout_codec::encode(canvas_->current_layout()), "dashboard");
}

void DashboardScreen::restore_layout() {
    auto result = finterm::SettingsRepository::instance().get("dashboard_canvas_layout");
    const auto layout = result.is_ok() ? layout_codec::decode(result.value()) : std::nullopt;
    if (!layout) {
        build_default_layout();
        return;
    }
    canvas_->load_layout(*layout);
}

void DashboardScreen::build_default_layout() {
    canvas_->apply_template("portfolio_manager");
}

} // namespace finterm::screens

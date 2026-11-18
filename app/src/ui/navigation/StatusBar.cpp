#include "ui/navigation/StatusBar.h"

#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"
#include "ui/widgets/NotifBell.h"
#include "ui/widgets/NotifPanel.h"
#include "ui/widgets/NotifToast.h"
#include "services/notifications/NotificationService.h"
#include "ui/alerts/AlertsDialog.h"

#include <QApplication>

#include <QCoreApplication>

namespace finterm::ui {

StatusBar::StatusBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(26);
    setObjectName("appStatusBar");
    all_bars().append(this);

    auto* hl = new QHBoxLayout(this);
    layout_ = hl;
    hl->setContentsMargins(12, 0, 12, 0);
    hl->setSpacing(0);

    auto mk = [](const QString& t, const QString& name) {
        auto* l = new QLabel(t);
        l->setObjectName(name);
        return l;
    };

    // One source for the version: main.cpp sets it from the build's
    // CMAKE_PROJECT_VERSION (FINTERM_VERSION_STRING).
    hl->addWidget(mk(QStringLiteral("v") + QCoreApplication::applicationVersion(), "sbVersion"));
    hl->addWidget(mk("  |  ", "sbSep"));
    const char* feeds[] = {"EQ", "FX", "CM", "FI", "CR"};
    for (auto& f : feeds) {
        hl->addWidget(mk(f, "sbFeed"));
        hl->addWidget(mk(" ", "sbSpacer"));
    }
    hl->addStretch();
    ready_label_ = mk("READY", "sbReady");
    hl->addWidget(ready_label_);
    hl->addWidget(mk("  |  ", "sbSep"));

    notif_bell_ = new NotifBell(this);
    hl->addWidget(notif_bell_);
    connect(notif_bell_, &NotifBell::bell_clicked, this, &StatusBar::toggle_notif_panel);
    connect(&notifications::NotificationService::instance(), &notifications::NotificationService::notification_received,
            this, [this](const notifications::NotificationRecord& rec) {
                // Not over the lock or sign-in screen (the shell is hidden
                // there), and only in the window the user is looking at.
                QWidget* win = window();
                if (!isVisible() || !win || win == this)
                    return;
                // One toast, in the main window the user is in: the one that
                // owns the active window (a dialog such as the Alerts manager
                // counts as its parent's), else — finterm in the background —
                // the first main window.
                QWidget* owner = QApplication::activeWindow();
                while (owner && owner->parentWidget())
                    owner = owner->parentWidget()->window();
                if (owner ? owner != win : primary_bar() != this)
                    return;
                if (!notif_toast_) {
                    notif_toast_ = new NotifToast(win);
                    connect(notif_toast_, &NotifToast::clicked, this, [](const QString& sym, int id) {
                        notifications::NotificationService::instance().mark_read(id);
                        open_in_equity_research(sym);
                    });
                }
                notif_toast_->show_notification(rec);
            });

    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
            [this](const ThemeTokens&) { refresh_theme(); });
    refresh_theme();
}

void StatusBar::refresh_theme() {
    setStyleSheet(QString("#appStatusBar { background:%1; border-top:1px solid %2; }"
                          "#sbVersion { color:%3; background:transparent; }"
                          "#sbSep { color:%2; background:transparent; }"
                          "#sbFeed { color:%3; background:transparent; }"
                          "#sbSpacer { background:transparent; }"
                          "#sbReady { color:%4; font-weight:700; background:transparent; }")
                      .arg(colors::BG_BASE())
                      .arg(colors::BORDER_DIM())
                      .arg(colors::TEXT_SECONDARY())
                      .arg(colors::POSITIVE()));
}

StatusBar* StatusBar::primary_bar() {
    // The oldest living bar: the first main window's.
    for (StatusBar* b : std::as_const(all_bars()))
        if (b)
            return b;
    return nullptr;
}

QList<QPointer<StatusBar>>& StatusBar::all_bars() {
    static QList<QPointer<StatusBar>> bars;
    bars.removeAll(nullptr);
    return bars;
}

void StatusBar::toggle_notif_panel() {
    // Lazy-create; the panel is a Qt::Popup parented to the window.
    if (!notif_panel_) {
        QWidget* root = window();
        notif_panel_ = new NotifPanel(root ? root : this);
    }
    if (notif_panel_->isVisible()) {
        notif_panel_->hide();
    } else {
        notif_panel_->anchor_to(notif_bell_);
        notif_panel_->show();
        notif_panel_->raise();
    }
}

void StatusBar::add_trailing_widget(QWidget* w) {
    layout_->addSpacing(8);
    layout_->addWidget(w, 0, Qt::AlignVCenter);
}

} // namespace finterm::ui

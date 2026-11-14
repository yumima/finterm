#include "ui/navigation/StatusBar.h"

#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"
#include "ui/widgets/NotifBell.h"
#include "ui/widgets/NotifPanel.h"

#include <QCoreApplication>

namespace finterm::ui {

StatusBar::StatusBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(26);
    setObjectName("appStatusBar");

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

void StatusBar::set_ready(bool ready) {
    ready_label_->setText(ready ? "READY" : "BUSY");
    ready_label_->setStyleSheet(QString("color:%1;font-weight:700;background:transparent;")
                                    .arg(ready ? colors::POSITIVE() : colors::TEXT_SECONDARY()));
}

} // namespace finterm::ui

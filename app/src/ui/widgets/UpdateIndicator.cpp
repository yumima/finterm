#include "ui/widgets/UpdateIndicator.h"

#include "services/updater/UpdateService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

namespace finterm::ui {

using services::UpdateService;

UpdateIndicator::UpdateIndicator(QWidget* parent) : QWidget(parent) {
    setObjectName("sbUpdate");
    auto* hl = new QHBoxLayout(this);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    question_ = new QLabel(QStringLiteral("CHECK FOR UPDATES DAILY?"));
    question_->setObjectName("sbUpdateQuestion");
    question_->setToolTip(QStringLiteral("Once a day, finterm would fetch its update list from github.com "
                                         "(sending only its version and platform). A new version is downloaded "
                                         "in the background and installs when you restart or quit finterm — "
                                         "a note here says when it is ready.\n"
                                         "Change it later in About › Check for updates automatically."));
    primary_ = new QPushButton;
    primary_->setObjectName("sbUpdatePrimary");
    secondary_ = new QPushButton(QStringLiteral("NO THANKS"));
    secondary_->setObjectName("sbUpdateSecondary");
    for (QPushButton* b : {primary_, secondary_}) {
        b->setFlat(true);
        b->setFixedHeight(20);
        b->setCursor(Qt::PointingHandCursor);
    }
    auto* sep = new QLabel(QStringLiteral("  |  "));
    sep->setObjectName("sbUpdateSep");

    hl->addWidget(question_);
    hl->addWidget(primary_);
    hl->addWidget(secondary_);
    hl->addWidget(sep);

    auto& svc = UpdateService::instance();
    connect(primary_, &QPushButton::clicked, this, [this]() {
        auto& s = UpdateService::instance();
        if (!s.last_result().isEmpty() && (!s.has_offer() || !s.last_result_ok())) {
            const QString text = s.last_result();
            const bool ok = s.last_result_ok();
            s.clear_last_result();
            if (!ok)
                QMessageBox::warning(window(), QStringLiteral("finterm update"), text);
            return;
        }
        if (s.has_offer()) {
            s.set_dialog_parent(window());
            s.show_offer();
        } else {
            s.set_auto_check(true);
        }
    });
    connect(secondary_, &QPushButton::clicked, this, []() { UpdateService::instance().set_auto_check(false); });
    connect(&svc, &UpdateService::offer_changed, this, &UpdateIndicator::refresh);
    connect(&svc, &UpdateService::auto_check_changed, this, &UpdateIndicator::refresh);
    connect(&svc, &UpdateService::stage_changed, this, &UpdateIndicator::refresh);
    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
            [this](const ThemeTokens&) { refresh_theme(); });
    refresh_theme();
    refresh();
}

void UpdateIndicator::refresh() {
    const auto& svc = UpdateService::instance();
    if (!svc.updates_supported()) {
        hide();
        return;
    }
    // What the last update did — said once, until clicked. A problem comes
    // before any offer (after a rollback the failed version is still offered).
    if (!svc.last_result().isEmpty() && (!svc.has_offer() || !svc.last_result_ok())) {
        question_->hide();
        secondary_->hide();
        primary_->setText(svc.last_result_ok() ? QStringLiteral("UPDATED TO v%1 ✓").arg(QCoreApplication::applicationVersion())
                                               : QStringLiteral("UPDATE PROBLEM — DETAILS"));
        primary_->setToolTip(svc.last_result());
        primary_->show();
        show();
        return;
    }
    if (svc.has_offer()) {
        question_->hide();
        secondary_->hide();
        const QString v = svc.offered_label();
        switch (svc.stage()) {
        case UpdateService::Stage::Downloading:
            primary_->setText(QStringLiteral("DOWNLOADING v%1…").arg(v));
            primary_->setToolTip(svc.can_update_in_place()
                                     ? QStringLiteral("finterm %1 is downloading in the background.").arg(v)
                                     : QStringLiteral("The finterm %1 installer is downloading; it opens when done.")
                                           .arg(v));
            break;
        case UpdateService::Stage::Ready:
            primary_->setText(QStringLiteral("v%1 READY · RESTART").arg(v));
            primary_->setToolTip(QStringLiteral("finterm %1 is downloaded and verified. Click to restart into it — "
                                                "or keep working: it installs when you quit.")
                                     .arg(v));
            break;
        case UpdateService::Stage::Handed:
            primary_->setText(QStringLiteral("v%1 INSTALLER STARTED").arg(v));
            primary_->setToolTip(QStringLiteral("Finish the installer, then restart finterm."));
            break;
        case UpdateService::Stage::Offered:
            primary_->setText(QStringLiteral("UPDATE v%1 AVAILABLE").arg(v));
            primary_->setToolTip(QStringLiteral("finterm %1 is out (you have %2). Click to see what's new and install.")
                                     .arg(v, svc.running_label()));
            break;
        }
        primary_->show();
        show();
        return;
    }
    if (svc.auto_check() == UpdateService::AutoCheck::Unasked) {
        question_->show();
        primary_->setText(QStringLiteral("TURN ON"));
        primary_->setToolTip(question_->toolTip());
        primary_->show();
        secondary_->setToolTip(QStringLiteral("Don't check automatically (not asked again). Help › Check for "
                                              "Updates still works; About › Check for updates automatically "
                                              "turns it on."));
        secondary_->show();
        show();
        return;
    }
    hide();
}

void UpdateIndicator::refresh_theme() {
    setStyleSheet(QString("#sbUpdateQuestion { color:%1; background:transparent; padding-right:6px; }"
                          "#sbUpdateSep { color:%2; background:transparent; }"
                          "QPushButton#sbUpdatePrimary { background:transparent; color:%3; border:none; "
                          "  font-size:11px; font-weight:bold; padding:0 4px; }"
                          "QPushButton#sbUpdateSecondary { background:transparent; color:%1; border:none; "
                          "  font-size:11px; padding:0 4px; }"
                          "QPushButton:hover { color:%4; text-decoration:underline; }")
                      .arg(colors::TEXT_SECONDARY())
                      .arg(colors::BORDER_DIM())
                      .arg(colors::AMBER())
                      .arg(colors::TEXT_PRIMARY()));
}

} // namespace finterm::ui

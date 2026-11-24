#include "ui/widgets/EngineIndicator.h"

#include "ai_chat/HearthService.h"
#include "auth/AuthManager.h"
#include "screens/settings/EnginePanel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace finterm::ui {

EngineIndicator::EngineIndicator(QWidget* parent) : QWidget(parent) {
    setObjectName("sbEngine");
    auto* hl = new QHBoxLayout(this);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    question_ = new QLabel(QStringLiteral("USE HEARTH FOR LOCAL AI?"));
    question_->setObjectName("sbEngineQuestion");
    question_->setToolTip(QStringLiteral("hearth, which runs AI models on this computer, was found. finterm uses it only "
                                         "if you connect it.\nChange it later in Settings › AI Config."));
    yes_ = new QPushButton(QStringLiteral("CONNECT…"));
    yes_->setObjectName("sbEnginePrimary");
    no_ = new QPushButton(QStringLiteral("NO THANKS"));
    no_->setObjectName("sbEngineSecondary");
    for (QPushButton* b : {yes_, no_}) {
        b->setFlat(true);
        b->setFixedHeight(20);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(question_->toolTip());
    }
    auto* sep = new QLabel(QStringLiteral("  |  "));
    sep->setObjectName("sbEngineSep");
    for (QWidget* w : std::initializer_list<QWidget*>{question_, yes_, no_, sep})
        hl->addWidget(w);

    auto& hearth = HearthService::instance();
    connect(yes_, &QPushButton::clicked, this, &EngineIndicator::open_dialog);
    connect(no_, &QPushButton::clicked, this, [this]() {
        if (const QString e = HearthService::instance().decline_engine(); !e.isEmpty())
            QMessageBox::warning(window(), QStringLiteral("hearth"), QStringLiteral("Could not save: ") + e);
    });
    connect(&hearth, &HearthService::detected, this, &EngineIndicator::refresh);
    connect(&hearth, &HearthService::link_changed, this, &EngineIndicator::refresh);
    // Another user's answer is another answer.
    connect(&auth::AuthManager::instance(), &auth::AuthManager::auth_state_changed, this, &EngineIndicator::refresh);
    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this, [this](const ThemeTokens&) { refresh_theme(); });
    refresh_theme();
    refresh();
}

void EngineIndicator::refresh() {
    auto& hearth = HearthService::instance();
    setVisible(auth::AuthManager::instance().is_authenticated() &&
               engine::should_ask(hearth.status().state, hearth.link()));
}

void EngineIndicator::open_dialog() {
    QDialog dlg(window());
    dlg.setWindowTitle(QStringLiteral("Local AI"));
    auto* vl = new QVBoxLayout(&dlg);
    vl->addWidget(new screens::EnginePanel(screens::EnginePanel::Mode::Full, &dlg));
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(box);
    dlg.setMinimumWidth(460);
    dlg.exec();
}

void EngineIndicator::refresh_theme() {
    setStyleSheet(QString("#sbEngineQuestion { color:%1; background:transparent; padding-right:6px; }"
                          "#sbEngineSep { color:%2; background:transparent; }"
                          "QPushButton#sbEnginePrimary { background:transparent; color:%3; border:none; "
                          "  font-size:11px; font-weight:bold; padding:0 4px; }"
                          "QPushButton#sbEngineSecondary { background:transparent; color:%1; border:none; "
                          "  font-size:11px; padding:0 4px; }"
                          "QPushButton:hover { color:%4; text-decoration:underline; }")
                      .arg(colors::TEXT_SECONDARY())
                      .arg(colors::BORDER_DIM())
                      .arg(colors::AMBER())
                      .arg(colors::TEXT_PRIMARY()));
}

} // namespace finterm::ui

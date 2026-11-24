#include "screens/settings/EnginePanel.h"

#include "ai_chat/EngineText.h"
#include "ai_chat/HearthService.h"
#include "storage/repositories/EngineLinkRepository.h"
#include "ui/components/ExternalLink.h"
#include "ui/theme/Theme.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace finterm::screens {

using engine::Action;

namespace {
engine::View current_view() {
    auto& hearth = HearthService::instance();
    const auto s = hearth.status();
    return engine::view_for({s.state, hearth.link(), s.version, s.contract, s.base_url, s.model, s.remote});
}

QString css(const QString& color) {
    return "color:" + color + ";background:transparent;";
}
} // namespace

EnginePanel::EnginePanel(Mode mode, QWidget* parent) : QWidget(parent), mode_(mode) {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(6);
    headline_ = new QLabel(this);
    detail_ = new QLabel(this);
    note_ = new QLabel(this);
    for (QLabel* l : {headline_, detail_, note_}) {
        l->setWordWrap(true);
        vl->addWidget(l);
    }
    note_->hide();

    for_chat_ = new QCheckBox(this);
    start_with_app_ = new QCheckBox(QStringLiteral("Start hearth when finterm opens"), this);
    const QString check_style =
        QString("QCheckBox{color:%1;spacing:8px;}QCheckBox::indicator{width:16px;height:16px;border:1px solid %2;"
                "border-radius:3px;background:%3;}QCheckBox::indicator:checked{background:%4;border-color:%4;}")
            .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED(), ui::colors::BG_RAISED(), ui::colors::AMBER());
    for (QCheckBox* c : {for_chat_, start_with_app_}) {
        c->setStyleSheet(check_style);
        vl->addWidget(c);
    }

    auto* row = new QHBoxLayout;
    primary_ = new QPushButton(this);
    secondary_ = new QPushButton(QStringLiteral("Disconnect"), this);
    for (QPushButton* b : {primary_, secondary_}) {
        b->setFixedHeight(28);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:3px;padding:0 12px;}"
                                 "QPushButton:hover{background:%4;}QPushButton:disabled{color:%5;}")
                             .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_BRIGHT(),
                                  ui::colors::BG_HOVER(), ui::colors::TEXT_DIM()));
        row->addWidget(b);
    }
    row->addStretch();
    vl->addLayout(row);

    auto& hearth = HearthService::instance();
    connect(&hearth, &HearthService::detected, this, &EnginePanel::refresh);
    connect(&hearth, &HearthService::link_changed, this, &EnginePanel::refresh);
    connect(&hearth, &HearthService::started, this, [this](bool ok, const QString& message) {
        if (!ok)
            say(message, true);
        refresh();
    });
    connect(primary_, &QPushButton::clicked, this, &EnginePanel::run_primary);
    connect(secondary_, &QPushButton::clicked, this, [this]() {
        const QString chat = EngineLinkRepository::instance().chat_provider();
        QString text = QStringLiteral("finterm stops using hearth for chat, memory and read-aloud. hearth itself stays "
                                      "installed, and goes on running.");
        if (chat.isEmpty() || chat == "hearth" || chat == "ollama")
            text += QStringLiteral("\n\nThe chat goes back to the provider it had before, or to none.");
        if (QMessageBox::question(window(), QStringLiteral("Disconnect hearth"), text,
                                  QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
            say(HearthService::instance().disconnect_engine(), true);
    });
    connect(start_with_app_, &QCheckBox::toggled, this, [this](bool on) {
        if (auto r = EngineLinkRepository::instance().set_start_with_app(on); r.is_err())
            say(QStringLiteral("Could not save: ") + QString::fromStdString(r.error()), true);
    });
    refresh();
}

void EnginePanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    HearthService::instance().detect();
}

void EnginePanel::say(const QString& text, bool problem) {
    note_->setText(text);
    note_->setStyleSheet(css(problem ? ui::colors::NEGATIVE() : ui::colors::TEXT_SECONDARY()));
    note_->setVisible(!text.isEmpty());
}

void EnginePanel::run_primary() {
    say({}, false);
    auto& hearth = HearthService::instance();
    switch (current_view().primary) {
    case Action::Connect:
        say(hearth.connect_engine(for_chat_->isChecked()), true);
        break;
    case Action::StartAndConnect:
        hearth.start_and_connect(for_chat_->isChecked());
        refresh();
        break;
    case Action::Start:
        hearth.start();
        refresh();
        break;
    case Action::FinishSetup:
        if (!hearth.linked()) // it is theirs, and running: what is left is in hearth
            say(hearth.connect_engine(for_chat_->isChecked()), true);
        ui::open_external_link(hearth.engine_base_url() + "/");
        break;
    case Action::HowToUpdate:
    case Action::Get:
        ui::open_external_link(QString::fromLatin1(HearthService::kHomepage));
        break;
    case Action::Recheck:
        hearth.detect();
        break;
    case Action::None:
        break;
    }
}

void EnginePanel::refresh() {
    auto& hearth = HearthService::instance();
    auto& repo = EngineLinkRepository::instance();
    const auto v = current_view();
    const bool full = mode_ == Mode::Full;
    const bool busy = hearth.starting();

    headline_->setText(busy ? QStringLiteral("Starting hearth…") : v.headline);
    headline_->setStyleSheet(css(v.good ? ui::colors::POSITIVE() : ui::colors::TEXT_PRIMARY()) + "font-weight:600;");
    detail_->setText(v.detail);
    detail_->setStyleSheet(css(ui::colors::TEXT_SECONDARY()));

    primary_->setText(v.primary_text);
    primary_->setVisible(full && v.primary != Action::None);
    primary_->setEnabled(!busy);
    secondary_->setVisible(full && v.can_disconnect);
    secondary_->setEnabled(!busy);

    // Chat with another provider stays there unless the user says otherwise.
    const QString chat = repo.chat_provider();
    const bool elsewhere = !chat.isEmpty() && chat != "hearth" && chat != "ollama";
    const bool connects =
        v.primary == Action::Connect || v.primary == Action::StartAndConnect || (v.primary == Action::FinishSetup && !v.can_disconnect);
    for_chat_->setText(QStringLiteral("Use it for chat too, instead of %1 (memory and read-aloud use it either way)").arg(chat));
    for_chat_->setVisible(full && connects && elsewhere);
    start_with_app_->setVisible(full && v.can_disconnect);
    QSignalBlocker block(start_with_app_);
    start_with_app_->setChecked(repo.start_with_app());
}

} // namespace finterm::screens

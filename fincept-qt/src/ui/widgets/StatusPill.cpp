// src/ui/widgets/StatusPill.cpp
#include "ui/widgets/StatusPill.h"

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QPushButton>

namespace fincept::ui {

StatusPill::StatusPill(QWidget* parent) : QWidget(parent) {
    auto* hl = new QHBoxLayout(this);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);

    dot_ = new QLabel(QStringLiteral("\xe2\x80\xa2"));   // • bullet
    dot_->setFixedWidth(8);
    hl->addWidget(dot_);

    label_ = new QLabel;
    label_->setStyleSheet(
        QString("color:%1;font-size:11px;font-family:'Consolas',monospace;")
            .arg(colors::TEXT_SECONDARY()));
    hl->addWidget(label_, 1);

    auto* rb = new QPushButton(QStringLiteral("\xe2\x86\xbb"));   // ↻ refresh
    rb->setFixedSize(18, 18);
    rb->setCursor(Qt::PointingHandCursor);
    rb->setToolTip("Retry");
    rb->setStyleSheet(
        QString("QPushButton{background:transparent;border:1px solid %1;"
                "border-radius:2px;color:%1;font-size:11px;}"
                "QPushButton:hover{background:%2;border-color:%3;color:%3;}")
            .arg(colors::BORDER_DIM(), colors::BG_RAISED(), colors::NEGATIVE()));
    rb->hide();
    retry_btn_ = rb;
    connect(rb, &QPushButton::clicked, this, [this]() {
        emit retry_requested();
        if (on_retry_) on_retry_();
    });
    hl->addWidget(retry_btn_);

    apply_state(State::Idle, QString());
}

void StatusPill::set_idle(const QString& message)    { apply_state(State::Idle, message); }
void StatusPill::set_loading(const QString& message) { apply_state(State::Loading, message); }
void StatusPill::set_stale(const QString& message)   { apply_state(State::Stale, message); }
void StatusPill::set_empty(const QString& message)   { apply_state(State::Empty, message); }

void StatusPill::set_error(const QString& message, std::function<void()> on_retry) {
    on_retry_ = std::move(on_retry);
    apply_state(State::Error, message);
}

void StatusPill::apply_state(State s, const QString& message) {
    state_ = s;
    QString dot_color;
    switch (s) {
        case State::Idle:    dot_color = colors::POSITIVE(); break;
        case State::Loading: dot_color = colors::AMBER();    break;
        case State::Stale:   dot_color = colors::WARNING();  break;
        case State::Error:   dot_color = colors::NEGATIVE(); break;
        case State::Empty:   dot_color = colors::TEXT_SECONDARY(); break;
    }
    dot_->setStyleSheet(QString("color:%1;font-size:14px;").arg(dot_color));
    label_->setText(message);
    if (retry_btn_) retry_btn_->setVisible(s == State::Error && on_retry_);
}

} // namespace fincept::ui

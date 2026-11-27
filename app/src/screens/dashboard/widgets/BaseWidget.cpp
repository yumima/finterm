#include "screens/dashboard/widgets/BaseWidget.h"

#include "core/errors/DataFailure.h"

#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QPalette>
#include <QSize>
#include <QStackedWidget>
#include <QStyle>

namespace finterm::screens::widgets {

BaseWidget::BaseWidget(const QString& title, QWidget* parent, const QString& accent_color)
    : QFrame(parent), accent_color_(accent_color.isEmpty() ? ui::colors::AMBER : accent_color) {
    setObjectName("dashboardBaseWidget");
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(ui::colors::BG_SURFACE()));
    setPalette(pal);

    setStyleSheet(QString("#dashboardBaseWidget { background: %1; border: 1px solid %2; border-radius: 2px; }")
                      .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_BRIGHT()));

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // ── Title bar ──
    title_bar_ = new QWidget(this);
    title_bar_->setFixedHeight(26);
    title_bar_->setStyleSheet(
        QString("background: %1; border-bottom: 1px solid %2;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(title_bar_);
    hl->setContentsMargins(8, 0, 4, 0);
    hl->setSpacing(6);
    title_bar_layout_ = hl;

    // Accent bar
    accent_bar_ = new QLabel;
    accent_bar_->setFixedSize(2, 10);
    accent_bar_->setStyleSheet(QString("background: %1; border-radius: 1px;").arg(accent_color_));
    hl->addWidget(accent_bar_);

    title_label_ = new QLabel(title);
    hl->addWidget(title_label_);

    // Loading indicator
    loading_label_ = new QLabel("...");
    loading_label_->setVisible(false);
    hl->addWidget(loading_label_);

    hl->addStretch();

    // Config (gear) button — hidden by default until subclass calls set_configurable(true)
    config_btn_ = new QPushButton;
    config_btn_->setFixedSize(20, 20);
    config_btn_->setText("");
    config_btn_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    config_btn_->setIconSize(QSize(12, 12));
    config_btn_->setToolTip("Configure widget");
    config_btn_->setCursor(Qt::PointingHandCursor);
    config_btn_->setVisible(false);
    config_btn_->setStyleSheet(
        QString("QPushButton { color: %1; background: %2; border: 1px solid %3; border-radius: 2px; "
                "padding: 0px; }"
                "QPushButton:hover { color: %4; border-color: %4; background: %5; }")
            .arg(accent_color_, ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BG_HOVER()));
    connect(config_btn_, &QPushButton::clicked, this, &BaseWidget::on_config_clicked);
    hl->addWidget(config_btn_);

    // Refresh button
    refresh_btn_ = new QPushButton;
    refresh_btn_->setFixedSize(20, 20);
    refresh_btn_->setText("");
    refresh_btn_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refresh_btn_->setIconSize(QSize(12, 12));
    refresh_btn_->setToolTip("Refresh widget data");
    refresh_btn_->setCursor(Qt::PointingHandCursor);
    refresh_btn_->setStyleSheet(
        QString("QPushButton { color: %1; background: %2; border: 1px solid %3; border-radius: 2px; "
                "padding: 0px; }"
                "QPushButton:hover { color: %4; border-color: %4; background: %5; }")
            .arg(accent_color_, ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BG_HOVER()));
    connect(refresh_btn_, &QPushButton::clicked, this, &BaseWidget::refresh_requested);
    hl->addWidget(refresh_btn_);

    // Close button
    auto* close_btn = new QPushButton;
    close_btn->setFixedSize(20, 20);
    close_btn->setText("");
    close_btn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close_btn->setIconSize(QSize(11, 11));
    close_btn->setToolTip("Close widget");
    close_btn->setCursor(Qt::PointingHandCursor);
    close_btn->setStyleSheet(
        QString("QPushButton { color: %1; background: %2; border: 1px solid %3; border-radius: 2px; "
                "padding: 0px; }"
                "QPushButton:hover { color: %4; border-color: %4; background: %5; }")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(),
                 ui::colors::NEGATIVE(), ui::colors::BG_HOVER()));
    connect(close_btn, &QPushButton::clicked, this, &BaseWidget::close_requested);
    hl->addWidget(close_btn);

    vl->addWidget(title_bar_);

    // Apply title-bar styles once here; refresh_base_theme() re-uses the same
    // method on theme changes so there is only one place to update font/colours.
    apply_title_bar_style();

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this](const ui::ThemeTokens&) {
        refresh_base_theme();
        on_theme_changed();
    });

    // ── Content ──
    content_ = new QWidget(this);
    content_->setStyleSheet("background: transparent;");
    content_layout_ = new QVBoxLayout(content_);
    content_layout_->setContentsMargins(0, 0, 0, 0);
    content_layout_->setSpacing(0);

    // The error is a separate page, never a replacement of the content: the
    // headline, then (for a failed load) why and what to do, Retry, and the
    // raw error behind Details.
    error_page_ = new QWidget(this);
    auto* ev = new QVBoxLayout(error_page_);
    ev->setContentsMargins(14, 10, 14, 10);
    ev->setSpacing(5);
    ev->addStretch();
    auto mk = [this](Qt::Alignment align) {
        auto* l = new QLabel(error_page_);
        l->setAlignment(align);
        l->setWordWrap(true);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return l;
    };
    error_label_ = mk(Qt::AlignCenter);
    error_reason_ = mk(Qt::AlignCenter);
    error_action_ = mk(Qt::AlignCenter);
    error_detail_ = mk(Qt::AlignLeft);
    for (QLabel* l : {error_label_, error_reason_, error_action_, error_detail_})
        ev->addWidget(l);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    error_retry_ = new QPushButton(QStringLiteral("Retry"), error_page_);
    error_details_btn_ = new QPushButton(QStringLiteral("Details"), error_page_);
    for (QPushButton* b : {error_retry_, error_details_btn_}) {
        b->setCursor(Qt::PointingHandCursor);
        buttons->addWidget(b);
    }
    buttons->addStretch();
    ev->addLayout(buttons);
    ev->addStretch();
    connect(error_retry_, &QPushButton::clicked, this, &BaseWidget::refresh_requested);
    connect(error_details_btn_, &QPushButton::clicked, this, [this]() {
        error_detail_->setVisible(!error_detail_->isVisible());
        error_details_btn_->setText(error_detail_->isVisible() ? QStringLiteral("Hide details")
                                                               : QStringLiteral("Details"));
    });

    body_ = new QStackedWidget(this);
    body_->setStyleSheet("background: transparent;");
    body_->addWidget(content_);
    body_->addWidget(error_page_);
    refresh_error_style();

    vl->addWidget(body_, 1);
}

void BaseWidget::refresh_error_style() {
    error_label_->setStyleSheet(QString("color:%1; font-size:11px; font-weight:700; background:transparent;")
                                    .arg(ui::colors::NEGATIVE()));
    error_reason_->setStyleSheet(
        QString("color:%1; font-size:11px; background:transparent;").arg(ui::colors::TEXT_PRIMARY()));
    error_action_->setStyleSheet(
        QString("color:%1; font-size:10px; background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    error_detail_->setStyleSheet(QString("color:%1; font-size:9px; font-family:monospace; background:%2;"
                                         " border:1px solid %3; padding:4px;")
                                     .arg(ui::colors::TEXT_TERTIARY(), ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
    const QString btn = QString("QPushButton{color:%1;background:transparent;border:1px solid %2;padding:2px 10px;"
                                "font-size:10px;}QPushButton:hover{color:%3;border-color:%3;}")
                            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER());
    error_retry_->setStyleSheet(btn);
    error_details_btn_->setStyleSheet(btn);
}

void BaseWidget::apply_title_bar_style() {
    if (title_bar_)
        title_bar_->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                                      .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    if (title_label_)
        // Qt's QSS subset doesn't support `text-transform`. Including it here
        // produced one parse warning per BaseWidget at startup — the resulting
        // file-IO from the logger delayed the first paint visibly. Titles are
        // already passed uppercased by widget constructors so removing it is a
        // no-op visually. (letter-spacing IS supported via QFont mapping.)
        title_label_->setStyleSheet(QString("color:%1;font-size:%2;font-weight:bold;letter-spacing:0.5px;"
                                            "background:transparent;")
                                        .arg(accent_color_, ui::fonts::ui_px()));
    if (accent_bar_)
        accent_bar_->setStyleSheet(QString("background:%1;border-radius:1px;").arg(accent_color_));
    if (loading_label_)
        loading_label_->setStyleSheet(
            QString("color:%1;font-size:%2;background:transparent;").arg(ui::colors::AMBER(), ui::fonts::ui_px()));
    if (refresh_btn_)
        refresh_btn_->setStyleSheet(
            QString("QPushButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;padding:0;}"
                    "QPushButton:hover{color:%4;border-color:%4;background:%5;}")
                .arg(accent_color_, ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_PRIMARY(),
                     ui::colors::BG_HOVER()));
}

void BaseWidget::refresh_base_theme() {
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(ui::colors::BG_SURFACE()));
    setPalette(pal);

    setStyleSheet(QString("#dashboardBaseWidget{background:%1;border:1px solid %2;border-radius:2px;}")
                      .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_BRIGHT()));
    apply_title_bar_style(); // single source of truth — no duplication
    refresh_error_style();
}

void BaseWidget::set_loading(bool loading) {
    loading_label_->setVisible(loading);
    loading_label_->setText(loading ? "LOADING..." : "");
}

void BaseWidget::set_error(const QString& error) {
    if (error.isEmpty())
        return;
    error_label_->setText(error);
    for (QWidget* w : {static_cast<QWidget*>(error_reason_), static_cast<QWidget*>(error_action_),
                       static_cast<QWidget*>(error_detail_), static_cast<QWidget*>(error_details_btn_)})
        w->hide();
    error_retry_->show();
    body_->setCurrentWidget(error_page_);
}

void BaseWidget::set_load_error(const QString& what, const QString& cause, const QString& source_hint) {
    const auto f = data_failure::classify(cause, source_hint);
    error_label_->setText(QStringLiteral("Couldn't load %1").arg(what));
    error_reason_->setText(f.reason);
    error_action_->setText(QStringLiteral("What to do: ") + f.action);
    error_detail_->setText(f.raw.isEmpty() ? QStringLiteral("(no error text)") : f.raw);
    error_reason_->show();
    error_action_->show();
    error_detail_->hide();
    error_details_btn_->setText(QStringLiteral("Details"));
    error_details_btn_->setVisible(!f.raw.isEmpty());
    error_retry_->show();
    body_->setCurrentWidget(error_page_);
}

void BaseWidget::clear_error() {
    body_->setCurrentWidget(content_);
}

bool BaseWidget::has_error() const {
    return body_->currentWidget() == error_page_;
}

void BaseWidget::set_title(const QString& title) {
    title_label_->setText(title);
}

void BaseWidget::set_configurable(bool configurable) {
    if (config_btn_)
        config_btn_->setVisible(configurable);
}

void BaseWidget::add_title_bar_control(QWidget* w) {
    if (!w || !title_bar_layout_)
        return;
    w->setParent(title_bar_);
    // Place it after the title (past the stretch), just left of the buttons.
    const int idx = config_btn_ ? title_bar_layout_->indexOf(config_btn_) : -1;
    if (idx >= 0)
        title_bar_layout_->insertWidget(idx, w);
    else
        title_bar_layout_->addWidget(w);
}

void BaseWidget::on_config_clicked() {
    QDialog* dlg = make_config_dialog(this);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Subclasses are expected to emit config_changed() inside the dialog's
    // accept flow; BaseWidget just opens the dialog modally.
    dlg->exec();
}

} // namespace finterm::screens::widgets

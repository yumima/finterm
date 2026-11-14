#include "screens/news/NewsPeekPane.h"

#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace finterm::screens {

NewsPeekPane::NewsPeekPane(QWidget* parent) : QWidget(parent) {
    setObjectName("newsPeekPane");
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* head = new QWidget(this);
    head->setObjectName("newsPeekHead");
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto* h = new QHBoxLayout(head);
    h->setContentsMargins(8, 3, 4, 3);
    h->setSpacing(6);
    title_ = new QLabel(head);
    title_->setObjectName("newsPeekTitle");
    h->addWidget(title_, 1);
    auto* full = new QPushButton(QStringLiteral("FULL SCREEN ↗"), head);
    full->setObjectName("newsPeekButton");
    full->setCursor(Qt::PointingHandCursor);
    full->setToolTip(QStringLiteral("Open this as its own screen, beside News"));
    connect(full, &QPushButton::clicked, this, [this]() {
        if (pop_out_)
            pop_out_();
    });
    h->addWidget(full);
    auto* close = new QPushButton(QStringLiteral("×"), head);
    close->setObjectName("newsPeekButton");
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(QStringLiteral("Close this pane"));
    connect(close, &QPushButton::clicked, this, &NewsPeekPane::close_requested);
    h->addWidget(close);
    root->addWidget(head);

    stack_ = new QStackedWidget(this);
    root->addWidget(stack_, 1);

    apply_styles();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { apply_styles(); });
}

void NewsPeekPane::apply_styles() {
    setStyleSheet(QStringLiteral("#newsPeekPane { border-left:1px solid %1; border-right:1px solid %1; }"
                                 "#newsPeekHead { background:%2; border-bottom:1px solid %1; }"
                                 "#newsPeekTitle { color:%3; font-weight:700; background:transparent; }"
                                 "QPushButton#newsPeekButton { color:%4; background:transparent; border:1px solid %1;"
                                 "  padding:1px 6px; }"
                                 "QPushButton#newsPeekButton:hover { color:%3; border-color:%3; }")
                      .arg(ui::colors::BORDER_DIM(), ui::colors::BG_SURFACE(), ui::colors::AMBER(),
                           ui::colors::TEXT_SECONDARY()));
}

QWidget* NewsPeekPane::show_view(const QString& key, const QString& title, const std::function<QWidget*()>& make,
                                 std::function<void()> pop_out) {
    QWidget* v = views_.value(key);
    if (!v) {
        v = make();
        views_.insert(key, v);
        // A full screen (Equity Research) has a minimum width far wider than a
        // pane beside the drawer: scroll inside the pane instead of squeezing
        // the drawer and the feed to nothing.
        auto* scroll = new QScrollArea(stack_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(v);
        stack_->addWidget(scroll);
        holders_.insert(key, scroll);
    }
    title_->setText(title);
    pop_out_ = std::move(pop_out);
    stack_->setCurrentWidget(holders_.value(key));
    show();
    return v;
}

} // namespace finterm::screens

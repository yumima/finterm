// src/screens/equity_research/EarningsCallWebPanel.cpp
#include "screens/equity_research/EarningsCallWebPanel.h"

#include "core/config/AppPaths.h"
#include "ui/web/MediaWebProfile.h"
#include "ui/theme/Theme.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#ifdef HAS_QT_WEBENGINE
#    include <QWebEngineNewWindowRequest>
#    include <QWebEnginePage>
#    include <QWebEngineProfile>
#    include <QWebEngineSettings>
#    include <QtWebEngineWidgets/QWebEngineView>
#endif

namespace finterm::screens {

namespace {

#ifdef HAS_QT_WEBENGINE
/// One persistent profile for every call host, for the life of the app:
/// cookies (a registration form filled once, a Q4 sign-in) survive across
/// calls and launches. Parented to the application so it is shut down — and
/// its cookie store flushed — on exit, after every view using it is gone.
QWebEngineProfile* calls_profile() {
    static QWebEngineProfile* profile = [] {
        auto* p = new QWebEngineProfile(QStringLiteral("finterm_calls"), QCoreApplication::instance());
        // With the finterm profile's personal data (AppPaths::user), not
        // machine-wide: a webcast sign-in belongs to this finterm user.
        p->setPersistentStoragePath(AppPaths::user() + QStringLiteral("/webcasts"));
        p->setCachePath(AppPaths::cache() + QStringLiteral("/webcasts"));
        p->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
        ui::configure_media_profile(p);
        return p;
    }();
    return profile;
}
#endif

} // namespace

EarningsCallWebPanel::EarningsCallWebPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1; border:0;").arg(ui::colors::BG_BASE()));
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(10);
    title_ = new QLabel;
    title_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px; background:transparent;")
            .arg(ui::colors::AMBER()));
    bar->addWidget(title_);
    bar->addStretch();
    open_external_ = new QLabel;
    open_external_->setTextFormat(Qt::RichText);
    open_external_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    open_external_->setOpenExternalLinks(true);
    open_external_->setStyleSheet("font-size:12px; background:transparent;");
    bar->addWidget(open_external_);
    auto* close = new QPushButton(QStringLiteral("✕ CLOSE"));
    close->setFlat(true);
    close->setCursor(Qt::PointingHandCursor);
    close->setStyleSheet(QString("QPushButton { color:%1; font-size:12px; font-weight:700; background:transparent; "
                                 "border:0; } QPushButton:hover { color:%2; }")
                             .arg(ui::colors::TEXT_SECONDARY(), ui::colors::NEGATIVE()));
    connect(close, &QPushButton::clicked, this, &EarningsCallWebPanel::close_call);
    bar->addWidget(close);
    layout_->addLayout(bar);
    hide();
}

void EarningsCallWebPanel::open(const QString& url, const QString& title) {
    url_ = url;
    title_->setText(title);
    open_external_->setText(QStringLiteral("<a style=\"color:%1; text-decoration:none;\" href=\"%2\">"
                                           "OPEN IN BROWSER ↗</a>")
                                .arg(ui::colors::CYAN(), url.toHtmlEscaped()));
#ifdef HAS_QT_WEBENGINE
    if (!view_) {
        view_ = new QWebEngineView(calls_profile(), this);
        // Tall enough for a webcast player and its registration form; the
        // slides the hosts show alongside fit at this size too.
        view_->setMinimumHeight(420);
        layout_->addWidget(view_);
        // "Launch webcast" / "Register" buttons often open a new window: open
        // it HERE (replacing the landing page), or the click does nothing.
        connect(view_->page(), &QWebEnginePage::newWindowRequested, view_,
                [this](QWebEngineNewWindowRequest& request) { request.openIn(view_->page()); });
    }
    view_->setUrl(QUrl(url));
    show();
    set_open(true);
#else
    // No embedded browser in this build: the system browser plays it.
    QDesktopServices::openUrl(QUrl(url));
#endif
}

void EarningsCallWebPanel::set_open(bool open) {
    if (open_ == open)
        return;
    open_ = open;
    emit open_changed(open_);
}

void EarningsCallWebPanel::close_call() {
#ifdef HAS_QT_WEBENGINE
    if (view_) {
        // Destroying the view ends the page and its audio, and frees the
        // renderer; the next call builds a fresh one on the same profile.
        layout_->removeWidget(view_);
        view_->deleteLater();
        view_ = nullptr;
    }
#endif
    hide();
    url_.clear();
    set_open(false);
}

} // namespace finterm::screens

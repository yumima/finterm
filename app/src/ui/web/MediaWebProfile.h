// src/ui/web/MediaWebProfile.h
#pragma once
#ifdef HAS_QT_WEBENGINE
#    include <QWebEngineProfile>
#    include <QWebEngineSettings>

namespace finterm::ui {

/// Settings every finterm web view that PLAYS media needs — the dashboard's
/// web video player and the earnings-call player. One place, so a host that
/// starts refusing the user agent is fixed for both.
inline void configure_media_profile(QWebEngineProfile* p) {
    // The user clicked play in finterm; the page's player may start without
    // a second click inside it.
    p->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    p->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    // The default "QtWebEngine/x.y.z" agent is refused by YouTube (and some
    // webcast hosts) as an automated browser. This one is honest about the
    // engine — it IS Chromium.
    p->setHttpUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
}

} // namespace finterm::ui
#endif

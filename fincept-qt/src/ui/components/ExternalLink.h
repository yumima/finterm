#pragma once
// Opening a link that came from outside the app.
//
// Article URLs arrive from news providers — Yahoo, RSS feeds, the live
// websocket — and are pasted straight into QDesktopServices::openUrl by
// several call sites. openUrl is not a browser call: it hands the URL to the
// desktop's scheme handler, which will happily open a file:// path in a file
// manager or launch whatever application has registered a custom scheme. A
// feed we do not control should never be able to reach that.
//
// Header-only and Qt-Core/Gui only, so any widget can use it without adding a
// dependency.

#include <QDesktopServices>
#include <QString>
#include <QUrl>

namespace fincept::ui {

/// Open @p url in the user's browser. Returns false without doing anything
/// when the URL is empty, malformed, or not http/https.
///
/// The scheme allow-list is the point: everything a news feed can legitimately
/// link to is a web page, so anything else is either a mistake in the feed or
/// someone trying to use it as a launcher.
inline bool open_external_link(const QString& url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return false;
    const QUrl parsed(trimmed, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.isRelative())
        return false;
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;
    return QDesktopServices::openUrl(parsed);
}

} // namespace fincept::ui

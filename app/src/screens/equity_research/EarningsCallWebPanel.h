// src/screens/equity_research/EarningsCallWebPanel.h
#pragma once
#include <QString>
#include <QWidget>

class QLabel;
class QVBoxLayout;
class QWebEngineView;

namespace finterm::screens {

/// The earnings call, played by its host's own page inside finterm.
///
/// Most calls are webcast on platforms (Q4, webcasts.com, Choruscall,
/// open-exchange, media-server) whose player is part of their page —
/// sometimes behind a short name/email form. Rather than dig the stream out
/// of their player (their private interfaces; not finterm's to use), the page
/// itself is shown here, in a dedicated persistent browser profile: a form
/// filled once is remembered for that host's later calls. finterm's bundled
/// Chromium plays AAC/H.264 through Media Source Extensions, which is what
/// these players stream (measured 2026-11-17).
///
/// The view is built on first open and destroyed on close — no browser
/// process while no call is open, and closing always stops the audio.
class EarningsCallWebPanel : public QWidget {
    Q_OBJECT
  public:
    explicit EarningsCallWebPanel(QWidget* parent = nullptr);
    /// Show `url` (the call's webcast page) and start loading it.
    void open(const QString& url, const QString& title);
    void close_call();
    /// Open = a call is loaded (and may be playing) — NOT visibility: the
    /// panel is invisible whenever another tab is in front, still playing.
    bool is_open() const { return open_; }
    /// False in a build without an embedded browser: LISTEN opens the
    /// system browser instead.
    static constexpr bool kEmbedded =
#ifdef HAS_QT_WEBENGINE
        true;
#else
        false;
#endif
    QString url() const { return url_; }

  signals:
    /// The ONE source of the open/closed state; the LISTEN button follows it.
    void open_changed(bool open);

  private:
    void set_open(bool open);
    bool open_ = false;
    QString url_;
    QLabel* title_ = nullptr;
    QLabel* open_external_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QWebEngineView* view_ = nullptr;
};

} // namespace finterm::screens

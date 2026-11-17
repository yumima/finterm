// src/screens/equity_research/EarningsCallAudio.h
#pragma once
#include <QPointer>
#include <QPushButton>
#include <QString>

class QAudioOutput;
class QMediaPlayer;
class QProcess;
class QTimer;

namespace finterm::services::video {
class LiveHlsProxy;
}

namespace finterm::screens {

/// "▶ LISTEN" — plays an earnings call inside finterm, audio only.
///
/// Only sources with nothing in front of them: an audio/video file the IR
/// site links, or YouTube (a call's video, or a channel's live stream on the
/// day — GOOGL, NFLX). The webcast platforms most companies use (Q4,
/// Choruscall, open-exchange, media-server) put a registration form or their
/// own player script in front of the stream; for those the button is
/// disabled and says so, and CALL ↗ opens the page in the browser.
///
/// YouTube is resolved with the bundled yt-dlp. A live stream goes through
/// LiveHlsProxy (the same relay the dashboard video uses: it trims the live
/// manifest and rotates the playback session before YouTube expires it). A
/// stream that stops silently — Qt's FFmpeg backend emits nothing when an
/// HLS stream dies — is caught by watching the position, and re-resolved.
class EarningsCallAudio : public QPushButton {
    Q_OBJECT
  public:
    explicit EarningsCallAudio(QWidget* parent = nullptr);
    ~EarningsCallAudio() override;

    /// `kind`: "media" | "youtube" | "youtube_live". An empty url disables
    /// the button with `unavailable` as its tooltip. Setting the same source
    /// again does not interrupt playback.
    void set_source(const QString& url, const QString& kind, const QString& unavailable);
    void stop();

  private:
    void toggle();
    void resolve(bool refresh_session);
    void play(const QString& stream_url, bool live);
    void on_stall_tick();
    void fail(const QString& why);
    void paint_state();

    QString url_;
    QString kind_;
    QString unavailable_;
    QString error_;
    QString painted_color_;
    bool active_ = false;    // the user asked for playback
    bool resolving_ = false;
    int stall_ticks_ = 0;
    int retries_ = 0;
    qint64 last_position_ = -1;

#ifdef HAS_QT_MULTIMEDIA
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* output_ = nullptr;
#endif
    QPointer<QProcess> proc_;
    QPointer<services::video::LiveHlsProxy> proxy_;
    QTimer* stall_timer_ = nullptr;
};

} // namespace finterm::screens

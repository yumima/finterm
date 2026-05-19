// src/services/video/LiveHlsProxy.h
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace fincept::services::video {

/// Local HTTP proxy that trims YouTube DVR-mode HLS playlists to a small
/// live-edge window before serving them to QMediaPlayer.
///
/// Why this exists:
/// YouTube's live HLS playlists carry the full DVR history (2.4-hour rolling
/// window). For a busy live channel like Bloomberg/CNBC that's ~2800 segment
/// URLs and ~3.5 MB of manifest. Qt 6.8.3's bundled libavformat HLS demuxer
/// re-fetches the entire playlist every TARGETDURATION (5 s). When the
/// upstream CDN edge is slow (we measured 250 KB/s on the user's connection
/// in mid-2026), the 3.5 MB download takes ~14 s — longer than the refresh
/// interval — so libavformat can never catch up. Audio + video pipelines
/// starve waiting for the next playlist; the user sees stutter/freeze.
///
/// This proxy:
///   1. Background-fetches the upstream playlist on its own pace
///      (kRefreshIntervalMs, default 4 s).
///   2. Parses the upstream and KEEPS ONLY THE LAST kEdgeSegmentsToKeep
///      segments (default 6). Updates #EXT-X-MEDIA-SEQUENCE to match the
///      new first segment so libavformat sees a valid continuous live
///      playlist, just shorter.
///   3. Caches the trimmed playlist in memory.
///   4. Serves the cached playlist over loopback HTTP at
///      `http://127.0.0.1:<port>/play.m3u8`. QMediaPlayer requests come
///      back in milliseconds since they're served from RAM — they don't
///      block on the upstream CDN.
///
/// The segment URLs INSIDE the trimmed playlist still point at YouTube's
/// CDN (googlevideo.com). QMediaPlayer fetches segments directly. Only the
/// manifest goes through us; the actual video bytes do not.
///
/// Lifecycle: VideoPlayerWidget creates one of these per live playback,
/// calls start(upstream_url), reads local_url() to give to QMediaPlayer,
/// and deletes it on stop. The proxy parents to its owner (typically the
/// widget) so widget destruction cleans up the QTcpServer / network /
/// timer.
class LiveHlsProxy : public QObject {
    Q_OBJECT
  public:
    explicit LiveHlsProxy(QObject* parent = nullptr);
    ~LiveHlsProxy() override;

    /// Start the proxy with the upstream HLS variant playlist URL (the
    /// thing yt-dlp -g returns). Binds an ephemeral localhost port and
    /// kicks off the first upstream fetch.
    /// Returns true on success, false if the local listen socket couldn't
    /// bind. On failure, error string is available via last_error().
    bool start(const QUrl& upstream);

    /// Stop accepting new connections, abort outstanding upstream fetches,
    /// and stop the refresh timer. Safe to call multiple times.
    void stop();

    /// The HTTP URL QMediaPlayer should setSource() to.
    /// Empty until start() succeeds.
    QUrl local_url() const { return local_url_; }

    /// Human-readable error from the most recent failure, if any.
    QString last_error() const { return last_error_; }

  signals:
    /// Emitted the first time we successfully fetch and trim the upstream
    /// playlist. The owner can wait for this before calling setSource on
    /// QMediaPlayer so the player's first GET hits a non-empty cache.
    void ready();

    /// Emitted when an upstream fetch fails non-transiently (network
    /// error, HTTP non-200). The owner may choose to surface this to the
    /// user. Transient errors during background refresh are not
    /// surfaced — we just keep the previous cached playlist.
    void upstream_error(const QString& message);

  private slots:
    void on_new_connection();
    void on_refresh_timer();
    void on_upstream_finished();

  private:
    /// Pure function: parse a YouTube DVR HLS variant playlist and return
    /// a trimmed variant containing the last N segments with the
    /// MEDIA-SEQUENCE adjusted. If parsing fails (not a recognized HLS
    /// playlist), returns the input unchanged.
    static QByteArray trim_playlist(const QByteArray& upstream, int keep_segments);

    /// Send an HTTP response on the socket with the current cached
    /// trimmed playlist, then close the connection. If the cache is still
    /// empty, returns 503 Service Unavailable.
    void serve_request(QTcpSocket* client);

    /// Tunables.
    static constexpr int kEdgeSegmentsToKeep   = 6;     ///< trimmed playlist size
    static constexpr int kRefreshIntervalMs    = 4000;  ///< background fetch period
    static constexpr int kFetchTimeoutMs       = 30000; ///< per-fetch timeout

    QTcpServer*           server_         = nullptr;
    QNetworkAccessManager* netman_        = nullptr;
    QTimer*               refresh_timer_  = nullptr;
    QNetworkReply*        in_flight_      = nullptr;  ///< most recent upstream fetch, may be running
    QUrl                  upstream_url_;
    QUrl                  local_url_;
    QByteArray            trimmed_cache_;
    QString               last_error_;
    bool                  first_ready_emitted_ = false;
};

} // namespace fincept::services::video

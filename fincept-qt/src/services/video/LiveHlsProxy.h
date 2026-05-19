// src/services/video/LiveHlsProxy.h
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
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
    Q_DISABLE_COPY_MOVE(LiveHlsProxy)
  public:
    explicit LiveHlsProxy(QObject* parent = nullptr);
    ~LiveHlsProxy() override;

    /// Pure function exposed for unit testing. Parses an HLS variant
    /// playlist and returns a new playlist containing only the last
    /// `keep_segments` segments, with #EXT-X-MEDIA-SEQUENCE adjusted so
    /// the trimmed playlist's first segment carries the original
    /// sequence number. If input isn't a recognized HLS playlist
    /// (missing #EXTM3U), returns input unchanged.
    static QByteArray trim_playlist(const QByteArray& upstream, int keep_segments);

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
    /// Send an HTTP response on the socket with the current cached
    /// trimmed playlist, then close the connection. If the cache is
    /// stale or never-fetched, returns 503 Service Unavailable.
    void serve_request(QTcpSocket* client);

    /// True if we have a cached playlist that is still fresh enough to
    /// serve. The freshness bound matters because YouTube's signed
    /// segment URLs in the cache expire ~6 hours after issue; we also
    /// want to detect prolonged upstream failure (e.g., 30s+ of refresh
    /// timeouts) and surface it as an error instead of silently serving
    /// segments QMediaPlayer can no longer fetch.
    bool cache_is_fresh() const;

    /// Tunables.
    static constexpr int kEdgeSegmentsToKeep   = 6;     ///< trimmed playlist size
    static constexpr int kRefreshIntervalMs    = 4000;  ///< background fetch period
    static constexpr int kFetchTimeoutMs       = 30000; ///< per-fetch timeout
    static constexpr int kMaxCacheAgeMs        = 30000; ///< drop cache after this without a successful refresh

    QTcpServer*           server_         = nullptr;
    QNetworkAccessManager* netman_        = nullptr;
    QTimer*               refresh_timer_  = nullptr;
    QNetworkReply*        in_flight_      = nullptr;  ///< most recent upstream fetch, may be running
    QUrl                  upstream_url_;
    QUrl                  local_url_;
    QByteArray            trimmed_cache_;
    /// Age of trimmed_cache_. Invalid (i.e., !isValid()) until first
    /// successful refresh.
    QElapsedTimer         cache_age_;
    QString               last_error_;
    bool                  first_ready_emitted_ = false;
    /// Latched once we emit upstream_error due to stale cache, so we
    /// don't spam the signal on every subsequent client request.
    bool                  stale_error_emitted_ = false;
};

} // namespace fincept::services::video

#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"

#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>

#ifdef HAS_QT_MULTIMEDIA
#    include "screens/dashboard/widgets/OffscreenVideoScaler.h"

#    include <QAudioOutput>
#    include <QImage>
#    include <QMediaPlayer>
#    include <QVideoFrame>
#    include <QVideoSink>
#endif

#ifdef HAS_QT_WEBENGINE
#    include <QtWebEngineWidgets/QWebEngineView>
#    include <QtWebEngineCore/QWebEngineSettings>
#    include <QtWebEngineCore/QWebEngineProfile>
#    include <QtWebEngineCore/QWebEnginePage>
#    include <QtWebEngineCore/QWebEnginePermission>
#endif

namespace fincept::services::video { class LiveHlsProxy; }

namespace fincept::screens::widgets {

#ifdef HAS_QT_MULTIMEDIA
/// Video display surface — plain QWidget + QPainter.
///
/// The previous QOpenGLWidget implementation suffered from high-frequency
/// flashing on Wayland because:
///   1. paintGL() cleared the buffer to BLACK before validating the frame,
///      so any transient toImage() / decode failure produced a black flash.
///   2. The QOpenGLWidget FBO → Wayland subsurface compositing path adds
///      another layer where frame state can desync from the compositor.
///
/// QPainter has neither problem:
///   - We cache the last-good QImage; if a frame fails to convert we just
///     keep showing the previous image instead of clearing to black.
///   - QPainter on Wayland uses the compositor's own GPU path. There is no
///     extra subsurface; no FBO; no `frameSwapped` loop driving paints between
///     real frame arrivals (which was also paying the cost of re-upload).
///
/// NVDEC decode still happens upstream in Qt's FFmpeg backend; the cost we
/// pay is the GPU→CPU image transfer that toImage() does, ~37 MB/s at 480p30
/// — negligible. We only repaint when a new frame actually arrives.
class VideoRenderWidget : public QWidget {
    Q_OBJECT
  public:
    explicit VideoRenderWidget(QWidget* parent = nullptr);

  public slots:
    /// Receive a decoded frame from the multimedia thread (queued → main thread).
    void present(const QVideoFrame& frame);

  public:
    /// Clear the cached frame and repaint to black (called on stop / error).
    void clear_frame();

  signals:
    /// User left-clicked on the video surface — owner connects to a
    /// play/pause toggle so the YouTube-style "click anywhere to
    /// pause" gesture works without going up to the controls bar.
    /// Not emitted for non-left buttons (right/middle stay free for
    /// context menus / other handlers).
    void clicked();

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    // Pre-scale the cached source frame to fit the current widget size,
    // letterboxed. Called only on frame arrival or widget resize so the per-
    // paint cost is just a blit. SmoothTransformation is applied once per
    // frame instead of once per paint.
    void rescale_for_widget();

    QImage last_image_;       ///< CPU-fallback source frame, kept across transient toImage() failures
    QImage scaled_image_;     ///< pre-scaled / GPU-rendered, letterboxed dst size
    QPoint scaled_origin_;    ///< top-left position to blit scaled_image_ at

    // Last decoded frame, kept so resizeEvent can re-run the GPU scaler at
    // the new widget size without waiting for the next live frame (matters
    // when playback is paused). Cheap copy — QVideoFrame is shared-ptr to
    // the underlying buffer.
    QVideoFrame last_frame_;

    // GPU YUV→RGB + scale. Owns an offscreen GL context — no wl_subsurface,
    // so safe on Wayland/Mutter. Returns null on non-NV12 frames or any GL
    // failure; present() falls back to the CPU toImage() path in that case.
    OffscreenVideoScaler scaler_;

    // Drop-late-frames flag. The decoder can outrun the main thread on weak
    // GPUs / busy event loops. If a frame arrives while we still haven't
    // painted the previous one, we drop it instead of queueing more paint
    // events behind whatever else the main thread is doing (typing,
    // clicks). This is the right behaviour for a live tile: latency over
    // completeness, since dropped frames are imperceptible.
    bool paint_pending_ = false;
};
#endif

/// Inline video/stream player widget.
/// Plays HLS/MP4 direct streams via Qt Multimedia.
/// For YouTube URLs, uses yt-dlp to extract the direct stream URL first.
///
/// Channel list is user-configurable via the gear icon. Seed defaults
/// (CNBC, Yahoo Finance, Euronews) are written once on first launch; from
/// then on the list is owned by the user and persisted globally via
/// SettingsRepository (key `video.channels`, category `video`).
class VideoPlayerWidget : public BaseWidget {
    Q_OBJECT
  public:
    /// Single configured channel. Color is auto-assigned at load time from a
    /// fixed palette — the user picks name+URL only; we don't burden them
    /// with theme choices.
    struct ChannelDef {
        QString name;
        QString url;
        QString color; // assigned by us, not persisted
    };

    explicit VideoPlayerWidget(QWidget* parent = nullptr);

  private slots:
    void play_preset(int index);
    void play_custom_url();
    void refresh_data();
    void stop_playback();
    void on_ytdlp_finished(int exit_code, QProcess::ExitStatus status);
    void on_ytdlp_error(QProcess::ProcessError error);
    void on_player_error();

  protected:
    void on_theme_changed() override;
    QDialog* make_config_dialog(QWidget* parent) override;
    // NB: we intentionally do NOT pause playback on hideEvent. The user
    // relies on the audio continuing while they navigate to other screens.
    // The per-frame cost is bounded by VideoRenderWidget's drop-late-frames
    // guard — when the widget isn't visible Qt suppresses paintEvent, the
    // guard never clears, and present() returns early after the first
    // frame. No paint work happens for hidden video.

  private:
    void apply_styles();
    void build_channel_list();
    void populate_channel_rows();    // (re)fills channel_rows_layout_ from channels_
    void build_player_view();
    /// Resume from Paused, picking the lightest correct recovery:
    ///   - short pauses & VOD: setSource(same) rebuilds the decoder
    ///     chain (Qt6 FFmpeg audio-detach fix) and keeps the cursor;
    ///   - long pauses on the live-proxy URL: full reset (stop →
    ///     clear source → re-set source → defensive output re-attach
    ///     → play) lands at the live edge, since the buffered
    ///     segment has rolled out of the proxy's window.
    /// **No-op on Stopped or Playing** — callers that want
    /// PLAY-after-STOP restart semantics handle that themselves so
    /// the unlock auto-resume can be a one-liner that honors
    /// `auto_paused_on_lock_`'s "only resume if still paused"
    /// contract.
    void resume_playback();
    void play_url(const QString& url, const QString& title);
    void resolve_youtube_and_play(const QString& youtube_url, const QString& title);
    QString resolve_ytdlp_program() const;
    void play_direct(const QString& stream_url);
    // Live HLS via local trimming proxy. Bypasses Qt 6.8.3's libavformat
    // HLS demuxer choking on YouTube's 3.5 MB DVR playlists: the proxy
    // fetches the upstream variant playlist, trims it to a 6-segment
    // live-edge window, and serves a 7 KB version locally. QMediaPlayer
    // sees a tiny playlist its existing HLS code handles in milliseconds.
    void play_via_proxy(const QString& hls_url);
    void stop_hls_proxy();
    void set_loading(bool loading);

    void load_channels();                                     // pull from SettingsRepository (+ seed defaults)
    void save_channels(const QVector<ChannelDef>& channels);  // persist to SettingsRepository
    void load_engine();                                       // pull video.engine ("gl"/"web", default gl)
    void save_engine(bool web);                               // persist video.engine
    /// Pull video.max_height from SettingsRepository. Default 1080. Values
    /// outside the allowed set (480/720/1080) clamp to the nearest valid
    /// option so a hand-edited setting can't break stream resolution.
    void load_max_height();
    /// Persist max_height_ to SettingsRepository under video.max_height.
    void save_max_height(int height);
    static QVector<ChannelDef> default_channels();            // first-run seed
    static void assign_colors(QVector<ChannelDef>& channels); // round-robin palette

    QStackedWidget* stack_       = nullptr; // page 0 = channel list, page 1 = GL player, page 2 = WebEngine
    QWidget*        list_page_   = nullptr;
    QWidget*        player_page_ = nullptr;
    // Web engine mode: page 2 of stack_, loading YouTube directly in Chromium.
    // No yt-dlp, no GL pipeline, no frame callbacks — compositor does everything.
#ifdef HAS_QT_WEBENGINE
    QWebEngineView* web_view_          = nullptr;
    void            build_web_view();
    void            play_web(const QString& url, const QString& title);
    void            play_web_video_id(const QString& video_id, const QString& title, const QString& source_url);
    void            resolve_live_id_and_play_web(const QString& channel_url, const QString& title);
    void            stop_web();
    /// Render a Spotify show / episode / playlist / track / album via the
    /// official open.spotify.com/embed/<type>/<id> iframe in QWebEngineView.
    /// No yt-dlp, no QMediaPlayer — Spotify's embed owns the playback UI and
    /// auth (free-tier preview vs Premium full-play). Routed through this
    /// widget so the title bar, lock-pause hook, and theme integration still
    /// apply.
    void            play_spotify_embed(const QString& type, const QString& id,
                                       const QString& title, const QString& source_url);
    /// True while the WebEngine surface is showing a Spotify embed (vs the
    /// YouTube iframe path). Drives the lock-auto-pause / unlock-resume
    /// branch in the terminal_locked_changed handler and the visibility of
    /// the GL pause button. Cleared in stop_playback().
    bool            web_is_spotify_    = false;
    /// True if the Spotify embed was auto-paused by terminal-lock. Mirrors
    /// the GL pipeline's auto_paused_on_lock_ contract — we only resume
    /// what we paused, never undo a user-initiated pause.
    bool            spotify_auto_paused_on_lock_ = false;
    // GL (yt-dlp + QPainter) is the default — works for any public stream,
    // including channels that block YouTube iframe embedding (CNBC, Yahoo, …).
    // WebEngine remains available via the config dialog for custom YouTube
    // videos. Loaded from SettingsRepository on construct.
    bool            use_web_engine_    = false;
#endif
    // Cap on the GL pipeline's preferred resolution — controls the height<=N
    // term in the yt-dlp -f format selector. Allowed: 480, 720, 1080.
    // Loaded on construct, mutated via the config dialog. The format chain
    // still falls back to lower resolutions if N isn't available.
    int             max_height_        = 1080;
    QLineEdit*      url_input_    = nullptr;
    QLabel*         now_playing_  = nullptr;
    QLabel*         status_label_ = nullptr;

    QString pending_title_;
    QString current_url_;
    QString current_title_;
    // Blocks refresh_data() from re-entering play_url() during startup.
    bool play_in_progress_ = false;

    // Widgets needing theme-aware restyling
    QScrollArea*         scroll_                   = nullptr;
    QVBoxLayout*         channel_rows_layout_      = nullptr; // wraps just the preset rows so we can rebuild them
    QVector<QPushButton*> channel_rows_;
    QVector<QLabel*>     channel_name_labels_;
    QVector<QLabel*>     channel_desc_labels_;
    QLabel*              channel_sep_              = nullptr;
    QLabel*              custom_header_            = nullptr;
    QPushButton*         play_btn_                 = nullptr;
    QLabel*              helper_label_             = nullptr;
    QLabel*              status_label_placeholder_ = nullptr;
    QWidget*             controls_                 = nullptr;
    QPushButton*         stop_btn_                 = nullptr;
    /// Pause/resume toggle for the GL pipeline's QMediaPlayer. Hidden when
    /// the WEB engine is active (QWebEngineView's iframe is cross-origin so
    /// pause via JS is unreliable). Text flips between "⏸ PAUSE" and
    /// "▶ PLAY" via the player's playbackStateChanged signal.
    QPushButton*         play_pause_btn_           = nullptr;
    /// True if the GL player was auto-paused by terminal-lock so that the
    /// follow-up unlock can resume only when we'd been the ones to pause it
    /// — never resume a user-initiated pause.
    bool                 auto_paused_on_lock_      = false;
    /// Recorded by the playbackStateChanged lambda whenever the player
    /// enters PausedState (manual or auto).  resume_playback() compares
    /// this against now to pick between the cheap setSource(same)
    /// workaround (short pauses, VOD) and the full reset (long pauses
    /// on the live-proxy URL where the buffered segment has rolled out
    /// of the proxy's window).
    QDateTime            paused_at_;

    QVector<ChannelDef> channels_;

#ifdef HAS_QT_MULTIMEDIA
    QMediaPlayer*      player_       = nullptr;
    VideoRenderWidget* video_widget_ = nullptr; // plain QWidget — no native surface
    QVideoSink*        video_sink_   = nullptr; // frame delivery pipe
    QAudioOutput*      audio_output_ = nullptr;
    // Local HLS trimming proxy for live streams. Null when no live
    // playback is active. Owned (parented) by this widget — also gets
    // cleaned up automatically on destruction.
    fincept::services::video::LiveHlsProxy* hls_proxy_ = nullptr;
    // Sticky flag set on play_url() entry: tells on_ytdlp_finished() whether
    // to route the resolved stream URL through the proxy (live) or
    // directly to QMediaPlayer (VOD, no proxy needed).
    bool               current_is_live_ = false;
#endif
};

inline BaseWidget* create_video_player_widget() {
    return new VideoPlayerWidget;
}

} // namespace fincept::screens::widgets

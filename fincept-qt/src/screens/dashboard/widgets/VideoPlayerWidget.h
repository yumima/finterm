#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"

#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>

#ifdef HAS_QT_MULTIMEDIA
#    include <QAudioOutput>
#    include <QMediaPlayer>
#    include <QVideoWidget>
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
    // QVideoWidget's RHI render path only fires for visible surfaces, so
    // there's no idle GPU paint cost for hidden video either — decode keeps
    // running (NVDEC is rounding-noise), present is skipped while hidden.

  private:
    void apply_styles();
    void build_channel_list();
    void populate_channel_rows();    // (re)fills channel_rows_layout_ from channels_
    void build_player_view();
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
    // GL (yt-dlp + QVideoWidget RHI render) is the default — works for any
    // public stream, including channels that block YouTube iframe embedding.
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

    QVector<ChannelDef> channels_;

#ifdef HAS_QT_MULTIMEDIA
    QMediaPlayer*      player_       = nullptr;
    // QVideoWidget renders decoded frames on the GPU via Qt RHI — frames stay
    // in video memory, no QImage readback, no QPainter blit. Compare to the
    // prior QPainter path which burned ~1 CPU core per stream pulling frames
    // back to host memory.
    QVideoWidget* video_widget_ = nullptr;
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

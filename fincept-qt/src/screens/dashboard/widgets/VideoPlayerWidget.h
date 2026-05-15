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

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QImage last_image_; // last successfully-decoded frame; kept across transient failures
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

  private:
    void apply_styles();
    void build_channel_list();
    void populate_channel_rows();    // (re)fills channel_rows_layout_ from channels_
    void build_player_view();
    void play_url(const QString& url, const QString& title);
    void resolve_youtube_and_play(const QString& youtube_url, const QString& title);
    QString resolve_ytdlp_program() const;
    void play_direct(const QString& stream_url);
    void set_loading(bool loading);

    void load_channels();                                     // pull from SettingsRepository (+ seed defaults)
    void save_channels(const QVector<ChannelDef>& channels);  // persist to SettingsRepository
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
    // GL (yt-dlp + QPainter) is the default — works for any public stream,
    // including channels that block YouTube iframe embedding (CNBC, Yahoo, …).
    // WebEngine remains available via the toggle for custom YouTube videos.
    bool            use_web_engine_    = false;
    QPushButton*    engine_toggle_btn_ = nullptr; // WEB ↔ GL toggle
#endif
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

    QVector<ChannelDef> channels_;

#ifdef HAS_QT_MULTIMEDIA
    QMediaPlayer*      player_       = nullptr;
    VideoRenderWidget* video_widget_ = nullptr; // plain QWidget — no native surface
    QVideoSink*        video_sink_   = nullptr; // frame delivery pipe
    QAudioOutput*      audio_output_ = nullptr;
#endif
};

inline BaseWidget* create_video_player_widget() {
    return new VideoPlayerWidget;
}

} // namespace fincept::screens::widgets

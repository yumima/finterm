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
#    include <QVideoFrame>
#    include <QVideoSink>
#endif

namespace fincept::screens::widgets {

#ifdef HAS_QT_MULTIMEDIA
/// Software-rendered video surface.
///
/// Receives QVideoFrame objects from QVideoSink via a queued signal and
/// paints them in paintEvent() using QPainter. Because this is an ordinary
/// QWidget there is no native Wayland wl_surface involved — no xdg-toplevel
/// is ever allocated for video output, eliminating the rogue-window bug that
/// QVideoWidget caused on stop()/play()/setSource() cycles.
///
/// The data path is purely logical:
///   QMediaPlayer → QVideoSink → (Qt::QueuedConnection) → present() → update()
/// Pipeline lifecycle changes (stop, setSource, play) are invisible to this
/// widget; it simply paints whatever frame was last delivered.
class VideoRenderWidget : public QWidget {
    Q_OBJECT
  public:
    explicit VideoRenderWidget(QWidget* parent = nullptr);

  public slots:
    /// Receive a decoded frame from the multimedia thread (queued → main thread).
    void present(const QVideoFrame& frame);

  protected:
    void paintEvent(QPaintEvent* e) override;

  private:
    QVideoFrame current_frame_;
};
#endif

/// Inline video/stream player widget.
/// Plays HLS/MP4 direct streams via Qt Multimedia.
/// For YouTube URLs, uses yt-dlp to extract the direct stream URL first.
class VideoPlayerWidget : public BaseWidget {
    Q_OBJECT
  public:
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

  private:
    void apply_styles();
    void build_channel_list();
    void build_player_view();
    void play_url(const QString& url, const QString& title);
    void resolve_youtube_and_play(const QString& youtube_url, const QString& title);
    QString resolve_ytdlp_program() const;
    void play_direct(const QString& stream_url);
    void set_loading(bool loading);

    QStackedWidget* stack_       = nullptr; // page 0 = channel list, page 1 = player
    QWidget*        list_page_   = nullptr;
    QWidget*        player_page_ = nullptr;
    QLineEdit*      url_input_   = nullptr;
    QLabel*         now_playing_ = nullptr;
    QLabel*         status_label_ = nullptr;

    QString pending_title_;
    QString current_url_;
    QString current_title_;
    // Blocks refresh_data() from re-entering play_url() during startup.
    bool play_in_progress_ = false;

    // Widgets needing theme-aware restyling
    QScrollArea*         scroll_                   = nullptr;
    QLabel*              channel_header_           = nullptr;
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

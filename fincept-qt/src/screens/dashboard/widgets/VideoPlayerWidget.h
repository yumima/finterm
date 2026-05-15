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
#    include <QImage>
#    include <QOpenGLBuffer>
#    include <QOpenGLFunctions>
#    include <QOpenGLShaderProgram>
#    include <QOpenGLTexture>
#    include <QOpenGLVertexArrayObject>
#    include <QOpenGLWidget>
#    include <QVideoFrame>
#    include <QVideoSink>
#endif

namespace fincept::screens::widgets {

#ifdef HAS_QT_MULTIMEDIA
/// GPU-rendered video surface.
///
/// Inherits QOpenGLWidget so the GL context is created on the NVIDIA RTX card
/// via PRIME render offload (__NV_PRIME_RENDER_OFFLOAD=1 in /etc/environment).
/// Each decoded QVideoFrame is uploaded as an OpenGL texture and rendered by
/// a trivial fragment shader — the GPU handles both texture sampling and
/// rasterisation, not the CPU.
///
/// Qt's FFmpeg backend automatically selects NVDEC (libnvcuvid.so) for
/// hardware-accelerated decode when CUDA is available on the system. The
/// resulting QVideoFrame may be CUDA-resident; QVideoFrame::toImage() handles
/// the CUDA→CPU transfer for the GL upload. At 480p this transfer is ~12 MB/s,
/// negligible on a modern PCIe bus.
///
/// No native Wayland wl_surface is created by this widget (QOpenGLWidget
/// embeds properly as a Wayland subsurface), so the xdg-toplevel rogue-window
/// bug that QVideoWidget caused does not apply here.
///
/// Data path:
///   NVDEC → QVideoFrame → (QueuedConnection) → present() → update()
///        → paintGL() → glTexSubImage2D → fragment shader → RTX → display
class VideoRenderWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
  public:
    explicit VideoRenderWidget(QWidget* parent = nullptr);
    ~VideoRenderWidget() override;

  public slots:
    /// Receive a decoded frame from the multimedia thread (queued → main thread).
    void present(const QVideoFrame& frame);

  protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

  private:
    QVideoFrame               current_frame_;
    QOpenGLTexture*           texture_ = nullptr;
    QOpenGLShaderProgram      program_;
    QOpenGLVertexArrayObject  vao_;
    QOpenGLBuffer             vbo_{QOpenGLBuffer::VertexBuffer};
    int                       pos_loc_ = -1;
    int                       uv_loc_  = -1;
    bool                      gl_ready_ = false;
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

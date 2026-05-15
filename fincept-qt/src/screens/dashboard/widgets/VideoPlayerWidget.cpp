#include "screens/dashboard/widgets/VideoPlayerWidget.h"

#include "core/logging/Logger.h"
#include "ui/theme/Theme.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QRect>
#include <QSurfaceFormat>
#include <QWindow>

#include <algorithm>
#include <QScrollArea>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::screens::widgets {

// ── VideoRenderWidget ─────────────────────────────────────────────────────────
// QOpenGLWidget — rendering runs on the RTX card via PRIME render offload.
// Frames decoded by NVDEC arrive via QueuedConnection, are uploaded as a GL
// texture, and rendered by a minimal fragment shader. No CPU rasterisation.

#ifdef HAS_QT_MULTIMEDIA

// ── GLSL shaders (OpenGL 3.2 core / GLSL 1.50) ───────────────────────────────
// Uses in/out instead of the deprecated attribute/varying/gl_FragColor syntax.
// #version 150 core is the minimum that mandates VAO usage — keeping shaders
// and context requirements in sync prevents silent failures on core profiles.
static const char kVertSrc[] =
    "#version 150 core\n"
    "in  vec2 a_pos;\n"
    "in  vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_uv = a_uv; }\n";

static const char kFragSrc[] =
    "#version 150 core\n"
    "uniform sampler2D u_tex;\n"
    "in  vec2 v_uv;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = texture(u_tex, v_uv); }\n";

// Full-screen quad: 4 vertices × (x,y,u,v)
static const float kQuad[] = {
    -1.f, -1.f,  0.f, 1.f,   // bottom-left  (GL y-up → UV y-down)
     1.f, -1.f,  1.f, 1.f,   // bottom-right
    -1.f,  1.f,  0.f, 0.f,   // top-left
     1.f,  1.f,  1.f, 0.f,   // top-right
};

// ─────────────────────────────────────────────────────────────────────────────

VideoRenderWidget::VideoRenderWidget(QWidget* parent)
    : QOpenGLWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // OpenGL 3.2 core profile — matches #version 150 core shaders and makes
    // VAO usage mandatory (no implicit default VAO), enforced in initializeGL().
    QSurfaceFormat fmt;
    fmt.setVersion(3, 2);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(fmt);

    // ── Continuous render loop (Qt-documented Wayland pattern) ───────────────
    // QOpenGLWidget renders to an offscreen FBO. The FBO is composited into
    // the parent window's backing store only when the Wayland compositor sends
    // a wl_surface_frame callback and Qt calls wl_surface_commit. Without an
    // active frame-callback loop, paintGL() runs but its output stays in the
    // FBO — invisible on screen, yet visible during resize (which Qt forces
    // through its own resize path that calls requestUpdate() internally).
    //
    // Qt's documented fix for continuous QOpenGLWidget rendering:
    //   connect(frameSwapped, update) — drives the next paint from the signal
    //   emitted *after* the compositor has accepted and presented a frame.
    // present() seeds the loop with window()->windowHandle()->requestUpdate()
    // which registers the first wl_surface_frame callback. Every subsequent
    // frame is self-driven by frameSwapped → update() → paintGL() → commit →
    // frameSwapped → …, vsync-synchronised by the compositor.
    //
    // isVisible() guard: prevents the loop from keeping a hidden widget (page 0
    // of the QStackedWidget) alive if the compositor happens to send a callback
    // while the page is off-screen (rare, but some compositors don't suppress).
    connect(this, &QOpenGLWidget::frameSwapped, this, [this]() {
        if (current_frame_.isValid() && isVisible())
            update(); // keep the loop alive while frames are arriving and visible
    });
}

VideoRenderWidget::~VideoRenderWidget() {
    // GL resources must be destroyed with the context active.
    makeCurrent();
    delete texture_;
    texture_ = nullptr;   // prevent double-free if makeCurrent() re-enters
    vao_.destroy();
    vbo_.destroy();
    doneCurrent();
}

void VideoRenderWidget::present(const QVideoFrame& frame) {
    current_frame_ = frame;
    // Seed the frameSwapped→update() loop with a Wayland frame-callback
    // request. QWindow::requestUpdate() registers a wl_surface_frame with
    // the compositor; when the callback fires Qt calls paintEvent() on all
    // dirty widgets, composites QOpenGLWidget FBOs into the backing store,
    // and calls wl_surface_commit — making the rendered frame visible.
    // frameSwapped() then fires and update() keeps the loop going.
    update(); // mark widget dirty so paintGL() is called when the frame arrives
    if (auto* wh = window()->windowHandle())
        wh->requestUpdate(); // register wl_surface_frame callback to start loop
}

void VideoRenderWidget::initializeGL() {
    initializeOpenGLFunctions();

    // Compile and link — check explicitly so a driver-side failure surfaces
    // as a log error rather than silent black screen with gl_ready_ = true.
    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kVertSrc)
     || !program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc)
     || !program_.link()) {
        LOG_ERROR("VideoGL", "Shader compile/link failed:\n" + program_.log());
        return; // gl_ready_ stays false; paintGL() is a safe no-op
    }

    pos_loc_ = program_.attributeLocation("a_pos");
    uv_loc_  = program_.attributeLocation("a_uv");

    // VAO records the vertex layout and VBO binding so paintGL() only needs
    // to bind the VAO (required in OpenGL core profile; #version 150 core).
    vao_.create();
    vao_.bind();

    vbo_.create();
    vbo_.bind();
    vbo_.allocate(kQuad, sizeof(kQuad));

    program_.bind();
    program_.enableAttributeArray(pos_loc_);
    program_.enableAttributeArray(uv_loc_);
    program_.setAttributeBuffer(pos_loc_, GL_FLOAT, 0,                2, 4 * sizeof(float));
    program_.setAttributeBuffer(uv_loc_,  GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    program_.release();

    vbo_.release();
    vao_.release();

    gl_ready_ = true;
}

void VideoRenderWidget::resizeGL(int /*w*/, int /*h*/) {
    // Viewport recalculated per-frame in paintGL() to maintain aspect ratio.
}

void VideoRenderWidget::paintGL() {
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!gl_ready_ || !current_frame_.isValid()) return;

    // Convert frame to QImage — handles NVDEC CUDA→CPU transfer internally.
    // At 480p this is ~400 KB/frame; negligible on the PCIe bus.
    const QImage img = current_frame_.toImage();
    if (img.isNull()) return;

    // Normalise every frame to RGBA8888 so the GL upload is always the same
    // format. convertedTo() is O(w×h) — at 480p < 0.5 ms per frame.
    const QImage rgba = img.convertedTo(QImage::Format_RGBA8888);
    if (rgba.isNull()) return;

    // Create or recreate texture when dimensions change using explicit
    // format+size+allocateStorage.  We must NOT use the QImage constructor
    // or setData(QImage) overload: they call setFormat()/setSize()/
    // setMipLevels() on every invocation, which are all no-ops (with
    // warnings) once storage is allocated — so no pixels ever get uploaded
    // and the screen stays black while audio plays.
    if (!texture_ || texture_->width() != rgba.width() || texture_->height() != rgba.height()) {
        delete texture_;
        texture_ = nullptr;
        texture_ = new QOpenGLTexture(QOpenGLTexture::Target2D);
        texture_->setFormat(QOpenGLTexture::RGBA8_UNorm);
        texture_->setSize(rgba.width(), rgba.height());
        texture_->setMipLevels(1);
        texture_->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        texture_->setMinificationFilter(QOpenGLTexture::Linear);
        texture_->setMagnificationFilter(QOpenGLTexture::Linear);
        texture_->setWrapMode(QOpenGLTexture::ClampToEdge);
    }

    // Raw-pixel overload → glTexSubImage2D only; never touches format/size.
    texture_->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                      static_cast<const void*>(rgba.constBits()));

    // Letter-box: restrict the GL viewport to maintain aspect ratio.
    const float fw = img.width(), fh = img.height();
    const float scale = std::min(width() / fw, height() / fh);
    const int vw = static_cast<int>(fw * scale);
    const int vh = static_cast<int>(fh * scale);
    glViewport((width() - vw) / 2, (height() - vh) / 2, vw, vh);

    // VAO already has the vertex layout recorded from initializeGL().
    program_.bind();
    texture_->bind();
    vao_.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    vao_.release();
    texture_->release();
    program_.release();
}
#endif

// ── Preset channels ─────────────────────────────────────────────────────────

struct PresetChannel {
    const char* name;
    const char* stream_url;
    const char* description;
    const char* accent;
};

// Channel /live URLs redirect to whatever is currently streaming, so they
// never go stale the way specific watch?v= video IDs do.
// All presets verified live and free via yt-dlp --js-runtimes node.
static const PresetChannel kPresets[] = {
    {"CNBC Live",     "https://www.youtube.com/@CNBC/live",         "US markets & business",           "#2563eb"},
    {"Yahoo Finance", "https://www.youtube.com/@YahooFinance/live", "Markets & analysis",              "#16a34a"},
    {"CNA",           "https://www.youtube.com/@CNAInsider/live",   "Asia-Pacific & China markets",    "#f59e0b"},
    {"Euronews",      "https://www.youtube.com/@euronews/live",     "Europe business & finance",       "#9D4EDD"},
    {"DW News",       "https://www.youtube.com/@dwnews/live",       "International business",          "#0891b2"},
};

static constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

static bool is_youtube_url(const QString& url) {
    return url.contains("youtube.com/watch") || url.contains("youtu.be/") || url.contains("youtube.com/live") ||
           url.contains("youtube.com/@");
}

// ─────────────────────────────────────────────────────────────────────────────

VideoPlayerWidget::VideoPlayerWidget(QWidget* parent) : BaseWidget("LIVE TV / STREAMS", parent, ui::colors::AMBER()) {

    stack_ = new QStackedWidget;
    stack_->setStyleSheet("background: transparent;");
    content_layout()->addWidget(stack_, 1);

    build_channel_list();
    build_player_view();

    connect(this, &BaseWidget::refresh_requested, this, &VideoPlayerWidget::refresh_data);

    apply_styles();
    stack_->setCurrentIndex(0);
}

// ── Page 0: Channel list ─────────────────────────────────────────────────────

void VideoPlayerWidget::build_channel_list() {
    list_page_ = new QWidget(this);
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);

    auto* container = new QWidget(this);
    auto* vl = new QVBoxLayout(container);
    vl->setContentsMargins(6, 6, 6, 6);
    vl->setSpacing(3);

    channel_header_ = new QLabel("FINANCIAL TV");
    vl->addWidget(channel_header_);

    for (int i = 0; i < kPresetCount; ++i) {
        const auto& ch = kPresets[i];

        auto* row = new QPushButton;
        row->setCursor(Qt::PointingHandCursor);
        row->setFixedHeight(32);
        channel_rows_.append(row);

        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 0, 8, 0);
        rl->setSpacing(8);

        auto* dot = new QLabel;
        dot->setFixedSize(6, 6);
        dot->setStyleSheet(QString("background: %1; border-radius: 3px;").arg(ch.accent));
        rl->addWidget(dot);

        auto* play = new QLabel(QChar(0x25B6));
        play->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent;").arg(ch.accent));
        rl->addWidget(play);

        auto* name = new QLabel(ch.name);
        channel_name_labels_.append(name);
        rl->addWidget(name);

        auto* desc = new QLabel(ch.description);
        channel_desc_labels_.append(desc);
        rl->addWidget(desc, 1);

        connect(row, &QPushButton::clicked, this, [this, i]() { play_preset(i); });
        vl->addWidget(row);
    }

    // Separator
    channel_sep_ = new QLabel;
    channel_sep_->setFixedHeight(1);
    vl->addSpacing(4);
    vl->addWidget(channel_sep_);
    vl->addSpacing(4);

    // Custom URL
    custom_header_ = new QLabel("CUSTOM STREAM");
    vl->addWidget(custom_header_);

    auto* input_row = new QWidget(this);
    input_row->setStyleSheet("background: transparent;");
    auto* irl = new QHBoxLayout(input_row);
    irl->setContentsMargins(0, 2, 0, 0);
    irl->setSpacing(4);

    url_input_ = new QLineEdit;
    url_input_->setPlaceholderText("YouTube URL, HLS (.m3u8), MP4, or direct stream...");
    connect(url_input_, &QLineEdit::returnPressed, this, &VideoPlayerWidget::play_custom_url);
    irl->addWidget(url_input_, 1);

    play_btn_ = new QPushButton("PLAY");
    play_btn_->setFixedWidth(50);
    play_btn_->setCursor(Qt::PointingHandCursor);
    connect(play_btn_, &QPushButton::clicked, this, &VideoPlayerWidget::play_custom_url);
    irl->addWidget(play_btn_);

    vl->addWidget(input_row);

    helper_label_ = new QLabel("YouTube streams resolved via yt-dlp and played inline.");
    vl->addWidget(helper_label_);

    vl->addStretch();
    scroll_->setWidget(container);

    auto* page_layout = new QVBoxLayout(list_page_);
    page_layout->setContentsMargins(0, 0, 0, 0);
    page_layout->addWidget(scroll_);

    stack_->addWidget(list_page_);
}

// ── Page 1: Player view ──────────────────────────────────────────────────────

void VideoPlayerWidget::build_player_view() {
    player_page_ = new QWidget(this);
    auto* vl = new QVBoxLayout(player_page_);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

#ifdef HAS_QT_MULTIMEDIA
    // VideoRenderWidget is a plain QWidget — no native Wayland surface.
    // Mouse events reach the tile's resize grip without any platform tricks.
    video_widget_ = new VideoRenderWidget(this);
    vl->addWidget(video_widget_, 1);

    player_       = new QMediaPlayer(this);
    audio_output_ = new QAudioOutput(this);
    audio_output_->setVolume(0.5f);
    player_->setAudioOutput(audio_output_);

    // QVideoSink is the frame delivery pipe: it receives decoded frames from
    // the multimedia engine and emits videoFrameChanged. The queued connection
    // crosses from the decode thread to the main thread cleanly.
    video_sink_ = new QVideoSink(this);
    player_->setVideoOutput(video_sink_);
    connect(video_sink_, &QVideoSink::videoFrameChanged,
            video_widget_, &VideoRenderWidget::present,
            Qt::QueuedConnection);

    connect(player_, &QMediaPlayer::errorOccurred, this,
            &VideoPlayerWidget::on_player_error);
    connect(player_, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState s) {
                if (s == QMediaPlayer::PlayingState)
                    play_in_progress_ = false;
            });
#else
    status_label_placeholder_ =
        new QLabel("Qt Multimedia not available.\nBuild with Qt6 Multimedia for inline playback.");
    status_label_placeholder_->setAlignment(Qt::AlignCenter);
    vl->addWidget(status_label_placeholder_, 1);
#endif

    // Status overlay (loading / error)
    status_label_ = new QLabel;
    status_label_->setAlignment(Qt::AlignCenter);
    status_label_->setFixedHeight(20);
    status_label_->hide();
    vl->addWidget(status_label_);

    // Control bar
    controls_ = new QWidget(this);
    controls_->setFixedHeight(28);

    auto* cl = new QHBoxLayout(controls_);
    cl->setContentsMargins(8, 0, 8, 0);
    cl->setSpacing(8);

    now_playing_ = new QLabel("—");
    cl->addWidget(now_playing_, 1);

    stop_btn_ = new QPushButton(QString(QChar(0x25A0)) + " STOP");
    stop_btn_->setCursor(Qt::PointingHandCursor);
    stop_btn_->setFixedHeight(20);
    connect(stop_btn_, &QPushButton::clicked, this, &VideoPlayerWidget::stop_playback);
    cl->addWidget(stop_btn_);

    vl->addWidget(controls_);

    stack_->addWidget(player_page_);
}

// ── Actions ──────────────────────────────────────────────────────────────────

void VideoPlayerWidget::play_preset(int index) {
    if (index < 0 || index >= kPresetCount)
        return;
    const auto& ch = kPresets[index];
    play_url(QString(ch.stream_url), QString(ch.name));
}

void VideoPlayerWidget::play_custom_url() {
    QString url = url_input_->text().trimmed();
    if (url.isEmpty())
        return;
    if (!url.startsWith("http://") && !url.startsWith("https://"))
        url.prepend("https://");
    play_url(url, "Custom Stream");
}

void VideoPlayerWidget::play_url(const QString& url, const QString& title) {
    current_url_ = url;
    current_title_ = title;
    pending_title_ = title;
    play_in_progress_ = true; // held until PlayingState or error/stop
    now_playing_->setText(QString(QChar(0x25B6)) + " " + title);
    set_title("LIVE TV — " + title.toUpper());

#ifdef HAS_QT_MULTIMEDIA
    if (is_youtube_url(url)) {
        resolve_youtube_and_play(url, title);
    } else {
        play_direct(url);
    }
#else
    QDesktopServices::openUrl(QUrl(url));
    // No player to signal PlayingState — clear immediately so refresh_data()
    // doesn't stay blocked after the one-shot openUrl() call.
    play_in_progress_ = false;
#endif
}

void VideoPlayerWidget::refresh_data() {
    if (current_url_.isEmpty())
        return;
    // play_in_progress_: set from first play_url() call until the player
    // reaches PlayingState (or errors). The multimedia engine takes time to
    // open a stream; re-entering play_url() before the pipeline is stable
    // would call setSource() on a mid-transition player.
    if (play_in_progress_)
        return;
#ifdef HAS_QT_MULTIMEDIA
    // Live streams play continuously — nothing to refresh while playing.
    if (player_ && player_->playbackState() == QMediaPlayer::PlayingState)
        return;
#endif
    play_url(current_url_, current_title_.isEmpty() ? "Custom Stream" : current_title_);
}

QString VideoPlayerWidget::resolve_ytdlp_program() const {
#ifdef _WIN32
    const QString exe_name = "yt-dlp.exe";
#else
    const QString exe_name = "yt-dlp";
#endif
    const QString exe_dir = QCoreApplication::applicationDirPath();
    const QStringList local_candidates = {
        QDir(exe_dir).filePath(exe_name),
        QDir(exe_dir).filePath("tools/" + exe_name),
        QDir(exe_dir).filePath("bin/" + exe_name),
        QDir(exe_dir).filePath("../tools/" + exe_name),
    };

    for (const auto& candidate : local_candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    const QString from_path = QStandardPaths::findExecutable("yt-dlp");
    if (!from_path.isEmpty()) {
        return from_path;
    }

    return {};
}

void VideoPlayerWidget::resolve_youtube_and_play(const QString& youtube_url, const QString& title) {
    Q_UNUSED(title)
    set_loading(true);
    stack_->setCurrentIndex(1);

    const QString ytdlp_program = resolve_ytdlp_program();
    if (ytdlp_program.isEmpty()) {
        set_loading(false);
        status_label_->setText("yt-dlp not found. Place the binary next to the finterm executable, or install it on your PATH.");
        status_label_->show();
        LOG_ERROR("VideoPlayer", "yt-dlp not found in app directory or PATH");
        return;
    }

    // Tag the process with the requested URL so the finished callback can detect
    // if the user stopped or switched channels before yt-dlp returned.
    auto* proc = new QProcess(this);
    proc->setProperty("requested_url", youtube_url);
    connect(proc, &QProcess::finished, this, &VideoPlayerWidget::on_ytdlp_finished);
    connect(proc, &QProcess::finished, proc, &QProcess::deleteLater);
    connect(proc, &QProcess::errorOccurred, this, &VideoPlayerWidget::on_ytdlp_error);

    // Force HLS (m3u8_native) protocol — critical for memory safety.
    //
    // The previous DASH format (bestvideo[ext=mp4]+bestaudio[ext=m4a]) returns
    // two separate URLs (video-only + audio-only). We only use the first (video),
    // which GStreamer's dashdemux then buffers into /dev/shm at full network
    // speed with no cap — causing the 23 GB shmem / OOM kill seen in practice.
    //
    // HLS (m3u8_native) is a rolling-window protocol: GStreamer's hlsdemux keeps
    // only 3-10 segments (a few MB) in memory at any time. A single URL contains
    // both video and audio. Financial TV at 480p is perfectly readable.
    //
    // -g: print stream URL only (no download). --no-playlist: single stream.
    // --quiet: suppress all progress/warning output from stdout so only the URL
    //          reaches on_ytdlp_finished (warnings go to stderr separately).
    // HLS live streams (YouTube formats 91-96) are combined video+audio; they
    // are not accessible via bestvideo+bestaudio and have no separate tracks.
    // We select directly with `best[protocol=m3u8_native]` which always returns
    // a single muxed .m3u8 URL — no two-line DASH output to misparse.
    // --js-runtimes node: yt-dlp 2025+ requires a JS runtime for YouTube
    // extraction. Node.js v22 is installed on this system; without this flag
    // newer yt-dlp versions may miss certain HLS formats.
    proc->start(ytdlp_program,
                {"-f",
                 "best[protocol=m3u8_native][height<=480]"
                 "/best[protocol=m3u8_native]"
                 "/best[height<=480]/best",
                 "--no-playlist", "--quiet", "--js-runtimes", "node",
                 "-g", youtube_url});
}

void VideoPlayerWidget::on_ytdlp_finished(int exit_code, QProcess::ExitStatus /*status*/) {
    auto* proc = qobject_cast<QProcess*>(sender());
    if (!proc)
        return;

    // Discard if the user stopped or switched channels before yt-dlp returned.
    const QString requested = proc->property("requested_url").toString();
    if (requested != current_url_) {
        play_in_progress_ = false;
        return;
    }

    if (exit_code != 0) {
        const QString err = proc->readAllStandardError().trimmed();
        play_in_progress_ = false;
        current_url_.clear(); // stop refresh_data() from retrying
        set_loading(false);
        status_label_->setText("yt-dlp error: " + (err.isEmpty() ? QString("Unknown error") : err.left(120)));
        status_label_->show();
        LOG_ERROR("VideoPlayer", "yt-dlp failed: " + err.left(250));
        return;
    }

    // yt-dlp may return two lines when bestvideo+bestaudio are separate tracks
    // Qt Multimedia can only handle one URL, so take the first (video) line
    const QString output = proc->readAllStandardOutput().trimmed();
    const QString stream_url = output.split('\n').first().trimmed();

    if (stream_url.isEmpty()) {
        set_loading(false);
        status_label_->setText("Could not extract stream URL.");
        status_label_->show();
        return;
    }

    play_direct(stream_url);
}

void VideoPlayerWidget::on_ytdlp_error(QProcess::ProcessError /*error*/) {
    auto* proc = qobject_cast<QProcess*>(sender());
    if (!proc)
        return;

    // Discard if the user stopped or switched channels before yt-dlp failed.
    const QString requested = proc->property("requested_url").toString();
    if (requested != current_url_) {
        play_in_progress_ = false;
        proc->deleteLater();
        return;
    }

    const QString err = proc->errorString();
    play_in_progress_ = false;
    current_url_.clear(); // stop refresh_data() from retrying indefinitely
    set_loading(false);
    status_label_->setText("Failed to start yt-dlp: " + (err.isEmpty() ? QString("Unknown error") : err.left(90)));
    status_label_->show();
    LOG_ERROR("VideoPlayer", "yt-dlp process error: " + err);
    proc->deleteLater();
}

void VideoPlayerWidget::play_direct(const QString& stream_url) {
#ifdef HAS_QT_MULTIMEDIA
    // Clean, unconditional: show the player page, set source, play.
    // No stop(), no winId(), no WA_NativeWindow, no ordering hacks.
    // VideoRenderWidget has no native surface so none of those tricks
    // are needed or meaningful — the pipeline is pure data flow.
    set_loading(false);
    status_label_->hide();
    stack_->setCurrentIndex(1);
    player_->setSource(QUrl(stream_url));
    player_->play();
#else
    Q_UNUSED(stream_url)
#endif
}

void VideoPlayerWidget::stop_playback() {
#ifdef HAS_QT_MULTIMEDIA
    player_->stop();
#endif
    stack_->setCurrentIndex(0);

    play_in_progress_ = false;
    current_url_.clear();
    current_title_.clear();
    set_loading(false);
    status_label_->hide();
    set_title("LIVE TV / STREAMS");
}

void VideoPlayerWidget::on_player_error() {
#ifdef HAS_QT_MULTIMEDIA
    const QString err = player_->errorString();
    play_in_progress_ = false;
    current_url_.clear(); // stops refresh_data() from retrying
    set_loading(false);
    status_label_->setText("Playback error: " + (err.isEmpty() ? "Unknown" : err.left(120)));
    status_label_->show();
    LOG_ERROR("VideoPlayer", "QMediaPlayer error: " + err.left(250));
#endif
}

void VideoPlayerWidget::set_loading(bool loading) {
    if (loading) {
        status_label_->setText("Resolving stream via yt-dlp...");
        status_label_->show();
    } else {
        status_label_->hide();
    }
}

void VideoPlayerWidget::apply_styles() {
    // Channel list page
    scroll_->setStyleSheet(QString("QScrollArea { border: none; background: transparent; }"
                                   "QScrollBar:vertical { width: 5px; background: transparent; }"
                                   "QScrollBar::handle:vertical { background: %1; border-radius: 2px; }"
                                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
                               .arg(ui::colors::BORDER_MED()));

    channel_header_->setStyleSheet(QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 0.5px; "
                                           "background: transparent; padding: 2px 0;")
                                       .arg(ui::colors::TEXT_SECONDARY()));

    for (int i = 0; i < channel_rows_.size() && i < kPresetCount; ++i) {
        const auto& ch = kPresets[i];
        channel_rows_[i]->setStyleSheet(
            QString("QPushButton { background: %1; border: 1px solid %2; border-radius: 2px; "
                    "text-align: left; padding: 0 8px; }"
                    "QPushButton:hover { background: %3; border-color: %4; }")
                .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM(), ui::colors::BG_HOVER(), ch.accent));
    }
    for (auto* lbl : channel_name_labels_) {
        lbl->setStyleSheet(QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;")
                               .arg(ui::colors::TEXT_PRIMARY()));
    }
    for (auto* lbl : channel_desc_labels_) {
        lbl->setStyleSheet(
            QString("color: %1; font-size: 9px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
    }

    channel_sep_->setStyleSheet(QString("background: %1;").arg(ui::colors::BORDER_DIM()));

    custom_header_->setStyleSheet(QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 0.5px; "
                                          "background: transparent;")
                                      .arg(ui::colors::TEXT_SECONDARY()));

    url_input_->setStyleSheet(
        QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; padding: 4px 8px; font-size: 10px; }"
                "QLineEdit:focus { border-color: %4; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER()));

    play_btn_->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: none; border-radius: 2px; "
                                     "font-size: 9px; font-weight: bold; padding: 5px; }"
                                     "QPushButton:hover { background: %3; }")
                                 .arg(ui::colors::AMBER(), ui::colors::TEXT_ON_ACCENT(), ui::colors::AMBER_DIM()));

    helper_label_->setStyleSheet(
        QString("color: %1; font-size: 8px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));

    // Player page — VideoRenderWidget fills its background in paintEvent(); no stylesheet needed.
#ifdef HAS_QT_MULTIMEDIA
#else
    if (status_label_placeholder_)
        status_label_placeholder_->setStyleSheet(QString("color: %1; font-size: 11px; background: %2;")
                                                     .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_BASE()));
#endif

    status_label_->setStyleSheet(QString("color: %1; font-size: 9px; background: %2; padding: 0 8px;")
                                     .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_RAISED()));

    controls_->setStyleSheet(
        QString("background: %1; border-top: 1px solid %2;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));

    now_playing_->setStyleSheet(
        QString("color: %1; font-size: 9px; font-weight: bold; background: transparent;").arg(ui::colors::AMBER()));

    stop_btn_->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid %2; color: %3; "
                                     "font-size: 9px; font-weight: bold; padding: 0 10px; border-radius: 2px; }"
                                     "QPushButton:hover { background: %4; color: %5; }")
                                 .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_SECONDARY(),
                                      ui::colors::NEGATIVE(), ui::colors::TEXT_PRIMARY()));
}

void VideoPlayerWidget::on_theme_changed() {
    apply_styles();
}

} // namespace fincept::screens::widgets

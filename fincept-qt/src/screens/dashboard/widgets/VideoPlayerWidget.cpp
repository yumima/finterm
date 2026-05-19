#include "screens/dashboard/widgets/VideoPlayerWidget.h"

#include "core/diagnostics/SlowOpTimer.h"
#include "core/logging/Logger.h"
#include "services/video/LiveHlsProxy.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"

#include <QResizeEvent>

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QRadioButton>
#include <QRect>
#include <QTableWidget>

#include <algorithm>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace fincept::screens::widgets {

// ── VideoRenderWidget ─────────────────────────────────────────────────────────
// Plain QWidget + QPainter. Decoded frames arrive from QVideoSink and are
// stored as QImage; paintEvent() draws the cached image letter-boxed in the
// widget. Transient toImage() failures simply skip the present() call and
// the previous image stays on screen — no black flash, no flicker.

#ifdef HAS_QT_MULTIMEDIA

VideoRenderWidget::VideoRenderWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent); // we paint the entire surface ourselves
    setAutoFillBackground(false);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
}

void VideoRenderWidget::clear_frame() {
    last_image_ = QImage();
    scaled_image_ = QImage();
    paint_pending_ = false;
    update();
}

void VideoRenderWidget::present(const QVideoFrame& frame) {
    if (!frame.isValid())
        return;

    // Backpressure guard: if the previous frame's paintEvent hasn't run yet,
    // the main thread is behind. Drop this frame instead of queueing another
    // paint behind whatever is keeping the event loop busy (typing, clicks,
    // other widget updates). Live tiles trade frame completeness for input
    // responsiveness — a dropped frame is invisible; queued frames stack
    // up as visible UI lag.
    if (paint_pending_)
        return;

    // Surface a warning if a single frame's worth of conversion + rescale
    // ever crosses 25 ms — that's already chewing through a 40 ms 25 fps
    // budget and would explain stutter on the calling thread.
    FT_TIME_SLOT("VideoRenderWidget.present", 25);

    QImage img = frame.toImage();
    if (img.isNull())
        return; // Keep showing the previous frame — no flash.
    last_image_ = std::move(img);
    rescale_for_widget();
    paint_pending_ = true;
    update();   // schedule a repaint only when we have a new valid frame
}

void VideoRenderWidget::rescale_for_widget() {
    if (last_image_.isNull() || size().isEmpty()) {
        scaled_image_ = QImage();
        scaled_origin_ = QPoint();
        return;
    }
    // DPR-aware: scale to the *device* pixel size of the letterbox region,
    // then tag the image with the matching devicePixelRatio. drawImage(QPoint,
    // QImage) will lay it down 1:1 in device pixels at the logical origin —
    // crisp on hi-DPI, no additional scaling at paint time. We do this work
    // once per frame instead of once per paint.
    const qreal dpr = devicePixelRatioF();
    const QSize target_logical = last_image_.size().scaled(size(), Qt::KeepAspectRatio);
    const QSize target_device(qMax(1, static_cast<int>(target_logical.width()  * dpr)),
                              qMax(1, static_cast<int>(target_logical.height() * dpr)));
    scaled_image_ = last_image_.scaled(target_device, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
    scaled_image_.setDevicePixelRatio(dpr);
    scaled_origin_ = QPoint((width()  - target_logical.width())  / 2,
                            (height() - target_logical.height()) / 2);
}

void VideoRenderWidget::resizeEvent(QResizeEvent* event) {
    rescale_for_widget();
    QWidget::resizeEvent(event);
}

void VideoRenderWidget::paintEvent(QPaintEvent* /*event*/) {
    paint_pending_ = false;
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (scaled_image_.isNull())
        return;
    // Direct blit — no scaling in the paint path, just memcpy-ish. This is
    // why the present() call did the SmoothTransformation work upstream.
    p.drawImage(scaled_origin_, scaled_image_);
}
#endif

// ── Channel storage ─────────────────────────────────────────────────────────
//
// Channels live in SettingsRepository as a JSON array of {name, url} objects
// under key "video.channels" (category "video"). On first launch the key is
// absent: we write the small default seed below so the widget is useful out of
// the box, then immediately treat the persisted copy as authoritative. Once
// the user edits the list, the seed never reasserts itself — including when
// the user has deliberately deleted every channel (an empty array is still a
// valid user choice).

namespace {
constexpr const char* kSettingsKey      = "video.channels";
constexpr const char* kEngineKey        = "video.engine";   // "gl" | "web"
constexpr const char* kMaxHeightKey     = "video.max_height"; // "480" | "720" | "1080"
constexpr const char* kSettingsCategory = "video";

// Color palette assigned round-robin to channels. Users pick name/URL only;
// we own the accent so the list stays visually coherent regardless of order.
const QStringList kAccentPalette = {
    "#2563eb", // blue
    "#16a34a", // green
    "#9D4EDD", // purple
    "#f59e0b", // amber
    "#0891b2", // cyan
    "#dc2626", // red
};
} // namespace

QVector<VideoPlayerWidget::ChannelDef> VideoPlayerWidget::default_channels() {
    return {
        {"Bloomberg",     "https://www.youtube.com/live/iEpJwprxDdk",    {}},
        {"CNBC Live",     "https://www.youtube.com/@CNBC/live",          {}},
        {"Yahoo Finance", "https://www.youtube.com/@YahooFinance/live",  {}},
        {"Euronews",      "https://www.youtube.com/@euronews/live",      {}},
    };
}

void VideoPlayerWidget::assign_colors(QVector<ChannelDef>& channels) {
    for (int i = 0; i < channels.size(); ++i)
        channels[i].color = kAccentPalette[i % kAccentPalette.size()];
}

void VideoPlayerWidget::load_channels() {
    auto& repo = fincept::SettingsRepository::instance();
    auto r = repo.get(kSettingsKey);
    QString raw = r.is_ok() ? r.value() : QString();

    if (raw.isEmpty()) {
        // First launch — seed defaults and persist so subsequent boots take
        // the user-owned path even if they never open the editor.
        channels_ = default_channels();
        save_channels(channels_);
        assign_colors(channels_);
        return;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        LOG_ERROR("VideoPlayer",
                  "video.channels JSON unparseable, falling back to defaults: " + err.errorString());
        channels_ = default_channels();
        assign_colors(channels_);
        return;
    }

    channels_.clear();
    for (const auto& v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString().trimmed();
        const QString url  = o.value("url").toString().trimmed();
        if (name.isEmpty() || url.isEmpty())
            continue; // skip malformed entries silently — the editor enforces this on save
        channels_.append({name, url, {}});
    }
    assign_colors(channels_);
}

void VideoPlayerWidget::save_channels(const QVector<ChannelDef>& channels) {
    QJsonArray arr;
    for (const auto& ch : channels) {
        QJsonObject o;
        o.insert("name", ch.name);
        o.insert("url",  ch.url);
        arr.append(o);
    }
    const QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    auto r = fincept::SettingsRepository::instance().set(kSettingsKey, json, kSettingsCategory);
    if (r.is_err())
        LOG_ERROR("VideoPlayer",
                  "failed to persist video.channels: " + QString::fromStdString(r.error()));
}

void VideoPlayerWidget::load_engine() {
    auto r = fincept::SettingsRepository::instance().get(kEngineKey);
    const QString raw = r.is_ok() ? r.value().trimmed().toLower() : QString();
    // Default to GL when unset — the WebEngine path fails for several major
    // news channels that block /embed/VIDEO_ID. GL works for any public stream.
    use_web_engine_ = (raw == QStringLiteral("web"));
}

void VideoPlayerWidget::save_engine(bool web) {
    auto r = fincept::SettingsRepository::instance().set(
        kEngineKey, web ? "web" : "gl", kSettingsCategory);
    if (r.is_err())
        LOG_ERROR("VideoPlayer",
                  "failed to persist video.engine: " + QString::fromStdString(r.error()));
}

void VideoPlayerWidget::load_max_height() {
    auto r = fincept::SettingsRepository::instance().get(kMaxHeightKey);
    bool ok = false;
    const int raw = r.is_ok() ? r.value().toInt(&ok) : 0;
    // Clamp to the allowed set. Anything else (including the unset case
    // where toInt returns 0) falls through to the 1080 default.
    if (ok && (raw == 480 || raw == 720 || raw == 1080))
        max_height_ = raw;
    else
        max_height_ = 1080;
}

void VideoPlayerWidget::save_max_height(int height) {
    auto r = fincept::SettingsRepository::instance().set(
        kMaxHeightKey, QString::number(height), kSettingsCategory);
    if (r.is_err())
        LOG_ERROR("VideoPlayer",
                  "failed to persist video.max_height: " + QString::fromStdString(r.error()));
}

static bool is_youtube_url(const QString& url) {
    return url.contains("youtube.com/watch") || url.contains("youtu.be/") || url.contains("youtube.com/live") ||
           url.contains("youtube.com/@");
}

// YouTube *live* (vs. VOD). Live HLS streams use sliding-window playlists
// that Qt 6.8.3's in-process libavformat HLS demuxer can't keep drained
// smoothly — it stalls intermittently and the whole audio+video pipeline
// freezes (Bloomberg/CNBC/Yahoo all reproduce). VOD HLS plays fine through
// the same demuxer because the playlist is static. We route live through
// the local trimming proxy; this predicate decides which path to take.
//   - /live/{11-char-id}                — direct live video URL
//   - /@channel/live, /c/.../live, etc. — channel's current broadcast
static bool is_youtube_live_url(const QString& url) {
    if (url.contains(QStringLiteral("/live/"))) return true;
    static const QRegularExpression re(
        QStringLiteral(R"(youtube\.com/(?:@[^/]+|c/[^/]+|user/[^/]+)/live(?:[/?]|$))"));
    return re.match(url).hasMatch();
}

// Extract YouTube video ID from any standard YouTube URL format.
static QString extract_youtube_id(const QString& url) {
    QUrl u(url);
    if (u.host().contains("youtu.be"))
        return u.path().mid(1).section('?', 0, 0);
    QString v = QUrlQuery(u).queryItemValue("v");
    if (!v.isEmpty()) return v;
    QRegularExpression re(R"(/(?:embed|live|shorts)/([A-Za-z0-9_-]{11}))");
    auto m = re.match(u.path());
    return m.hasMatch() ? m.captured(1) : QString();
}

// ─────────────────────────────────────────────────────────────────────────────

VideoPlayerWidget::VideoPlayerWidget(QWidget* parent) : BaseWidget("LIVE TV / STREAMS", parent, ui::colors::AMBER()) {

    load_channels(); // populates channels_ from SettingsRepository (or seeds defaults)
    load_engine();   // GL by default; WEB persists across sessions if user picks it
    load_max_height();

    stack_ = new QStackedWidget;
    stack_->setStyleSheet("background: transparent;");
    content_layout()->addWidget(stack_, 1);

    build_channel_list(); // page 0
    build_player_view();  // page 1

    set_configurable(true); // enable the gear icon in the title bar

    // Controls bar lives outside the QStackedWidget so it remains visible
    // regardless of whether page 1 (GL) or page 2 (WebEngine) is shown.
    // Hidden while on the channel list (page 0); shown when any player is active.
    {
        controls_ = new QWidget(this);
        controls_->setFixedHeight(28);
        auto* cl = new QHBoxLayout(controls_);
        cl->setContentsMargins(8, 0, 8, 0);
        cl->setSpacing(8);

        now_playing_ = new QLabel("—");
        cl->addWidget(now_playing_, 1);

        stop_btn_ = new QPushButton(QString(QChar(0x25A0)) + " BACK");
        stop_btn_->setCursor(Qt::PointingHandCursor);
        stop_btn_->setFixedHeight(20);
        connect(stop_btn_, &QPushButton::clicked, this, &VideoPlayerWidget::stop_playback);
        cl->addWidget(stop_btn_);

        content_layout()->addWidget(controls_);
        controls_->hide();
    }

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

    // Sub-layout for just the preset rows so the config dialog can rebuild
    // them without disturbing the custom-URL section / engine toggle below.
    channel_rows_layout_ = new QVBoxLayout();
    channel_rows_layout_->setContentsMargins(0, 0, 0, 0);
    channel_rows_layout_->setSpacing(3);
    vl->addLayout(channel_rows_layout_);

    populate_channel_rows();

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

// Build (or rebuild) one QPushButton row per entry in channels_. Called once
// during construction and again whenever the config dialog accepts a change.
// Clears channel_rows_layout_ first; widget pointers held in
// channel_rows_/channel_name_labels_/channel_desc_labels_ are owned by the
// layout, so deleteLater() runs through the layout teardown.
void VideoPlayerWidget::populate_channel_rows() {
    // Tear down previous row widgets. The buttons own their children (dot/play
    // glyph/name/desc) so deleting the button drops the whole row.
    while (QLayoutItem* it = channel_rows_layout_->takeAt(0)) {
        if (auto* w = it->widget())
            w->deleteLater();
        delete it;
    }
    channel_rows_.clear();
    channel_name_labels_.clear();
    channel_desc_labels_.clear();

    for (int i = 0; i < channels_.size(); ++i) {
        const auto& ch = channels_[i];

        auto* row = new QPushButton;
        row->setCursor(Qt::PointingHandCursor);
        row->setFixedHeight(32);
        channel_rows_.append(row);

        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 0, 8, 0);
        rl->setSpacing(8);

        auto* dot = new QLabel;
        dot->setFixedSize(6, 6);
        dot->setStyleSheet(QString("background: %1; border-radius: 3px;").arg(ch.color));
        rl->addWidget(dot);

        auto* play = new QLabel(QChar(0x25B6));
        play->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent;").arg(ch.color));
        rl->addWidget(play);

        auto* name = new QLabel(ch.name);
        channel_name_labels_.append(name);
        rl->addWidget(name);

        // Description column is now derived from the URL host — keeps the
        // row visually balanced without forcing the user to author copy.
        auto* desc = new QLabel(QUrl(ch.url).host());
        channel_desc_labels_.append(desc);
        rl->addWidget(desc, 1);

        connect(row, &QPushButton::clicked, this, [this, i]() { play_preset(i); });
        channel_rows_layout_->addWidget(row);
    }
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

    stack_->addWidget(player_page_); // page 1
}

// ── Page 2: WebEngine player ─────────────────────────────────────────────────
#ifdef HAS_QT_WEBENGINE
void VideoPlayerWidget::build_web_view() {
    auto* profile = new QWebEngineProfile(QStringLiteral("finterm_tv"), this);
    profile->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    profile->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled,   true);
    profile->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled,          true);

    // Override the default Qt UA ("QtWebEngine/x.y.z") — YouTube detects this
    // string as an automated browser and returns "An error occurred" with a
    // Playback ID instead of playing the stream. A standard Chrome UA bypasses
    // the check while still being honest about the Chromium engine version.
    profile->setHttpUserAgent(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");

    web_view_ = new QWebEngineView(profile, this);
    web_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Auto-grant media permissions (Qt 6.8+ API).
    connect(web_view_->page(), &QWebEnginePage::permissionRequested,
            this, [](QWebEnginePermission permission) {
                using PT = QWebEnginePermission::PermissionType;
                const auto t = permission.permissionType();
                if (t == PT::MediaAudioCapture        ||
                    t == PT::MediaVideoCapture        ||
                    t == PT::MediaAudioVideoCapture   ||
                    t == PT::DesktopVideoCapture      ||
                    t == PT::DesktopAudioVideoCapture)
                    permission.grant();
            });

    stack_->addWidget(web_view_); // page 2
}

// Embed a YouTube video by ID — the path that already works smoothly for
// custom URLs. Channel-level embed restrictions (CNBC error 152) DO NOT apply
// at the individual video level, only at the `live_stream?channel=` endpoint.
void VideoPlayerWidget::play_web_video_id(const QString& video_id, const QString& title,
                                          const QString& source_url) {
    if (!web_view_) build_web_view();

    const QString embed_url = QString("https://www.youtube-nocookie.com/embed/%1"
                                      "?autoplay=1&controls=1&rel=0"
                                      "&origin=https://www.youtube-nocookie.com")
                                  .arg(video_id);

    const QString html = QStringLiteral(
        "<!DOCTYPE html><html><head><style>"
        "*{margin:0;padding:0;box-sizing:border-box;background:#000}"
        "iframe{border:none;width:100vw;height:100vh}"
        "</style></head><body>"
        "<iframe src='%1' allow='autoplay;fullscreen;encrypted-media' allowfullscreen></iframe>"
        "</body></html>").arg(embed_url);

    web_view_->setHtml(html, QUrl("https://www.youtube-nocookie.com"));

    play_in_progress_ = false;
    current_url_   = source_url;
    current_title_ = title;
    now_playing_->setText(QChar(0x25B6) + QString(" ") + title);
    set_title("LIVE TV — " + title.toUpper());
    status_label_->hide();
    controls_->show();
    stack_->setCurrentIndex(2);
}

// Run yt-dlp to extract the *current* live video ID from a channel URL,
// then embed that single video — same code path as custom URLs. Fast because
// we only ask for the ID, not the HLS manifest.
void VideoPlayerWidget::resolve_live_id_and_play_web(const QString& channel_url, const QString& title) {
    const QString ytdlp = resolve_ytdlp_program();
    if (ytdlp.isEmpty()) {
        // No yt-dlp available — show an error directly in the web view.
        if (!web_view_) build_web_view();
        web_view_->setHtml(QStringLiteral(
            "<html><body style='background:#000;color:#999;font:14px sans-serif;"
            "display:flex;align-items:center;justify-content:center;height:100vh'>"
            "<div>yt-dlp not found — cannot resolve live stream ID for WEB mode.<br>"
            "Switch engine to GL.</div></body></html>"));
        controls_->show();
        stack_->setCurrentIndex(2);
        return;
    }

    // Loading placeholder while yt-dlp runs (~2-3 s).
    if (!web_view_) build_web_view();
    web_view_->setHtml(QStringLiteral(
        "<html><body style='background:#000;color:#888;font:14px sans-serif;"
        "display:flex;align-items:center;justify-content:center;height:100vh'>"
        "Resolving live stream…</body></html>"));
    current_url_   = channel_url;
    current_title_ = title;
    now_playing_->setText(QChar(0x25B6) + QString(" ") + title + " (resolving…)");
    set_title("LIVE TV — " + title.toUpper());
    controls_->show();
    stack_->setCurrentIndex(2);

    auto* proc = new QProcess(this);
    proc->setProperty("requested_url", channel_url);
    proc->setProperty("requested_title", title);
    connect(proc, &QProcess::finished, this,
            [this, proc](int code, QProcess::ExitStatus) {
                const QString req_url = proc->property("requested_url").toString();
                const QString req_title = proc->property("requested_title").toString();
                proc->deleteLater();

                // Drop result if the user switched channels in the meantime.
                if (req_url != current_url_)
                    return;

                if (code != 0) {
                    LOG_ERROR("VideoPlayer",
                              "yt-dlp --print id failed: " + proc->readAllStandardError().left(250));
                    return;
                }
                const QString video_id =
                    QString::fromLatin1(proc->readAllStandardOutput()).trimmed().section('\n', 0, 0);
                if (video_id.size() != 11) {
                    LOG_ERROR("VideoPlayer", "yt-dlp returned unexpected ID: " + video_id);
                    return;
                }
                play_web_video_id(video_id, req_title, req_url);
            });

    // --print id alone is fast: yt-dlp resolves the channel's current live video
    // ID without touching the HLS manifest. --js-runtimes node bypasses yt-dlp's
    // PoToken / signature deciphering issues on recent YouTube changes.
    proc->start(ytdlp,
                {"--print", "id", "--no-download", "--no-playlist", "--quiet", "--no-warnings",
                 "--js-runtimes", "node", channel_url});
}

void VideoPlayerWidget::play_web(const QString& channel_url, const QString& title) {
    // Direct video ID (custom URL): embed immediately. No network round-trip.
    const QString vid = extract_youtube_id(channel_url);
    if (!vid.isEmpty()) {
        play_web_video_id(vid, title, channel_url);
        return;
    }
    // Channel /live URL (preset): yt-dlp resolves the current live video ID,
    // then embeds that specific video. Bypasses channel-level embed restriction
    // (error 152) because the restriction applies to live_stream?channel=, not
    // to /embed/VIDEO_ID itself.
    resolve_live_id_and_play_web(channel_url, title);
}

void VideoPlayerWidget::stop_web() {
    if (web_view_) web_view_->setUrl(QUrl("about:blank"));
}
#endif

// ── Actions ──────────────────────────────────────────────────────────────────

void VideoPlayerWidget::play_preset(int index) {
    if (index < 0 || index >= channels_.size())
        return;
    const auto& ch = channels_[index];
#ifdef HAS_QT_WEBENGINE
    if (use_web_engine_) {
        play_web(ch.url, ch.name);
        return;
    }
#endif
    play_url(ch.url, ch.name);
}

void VideoPlayerWidget::play_custom_url() {
    QString url = url_input_->text().trimmed();
    if (url.isEmpty())
        return;
    if (!url.startsWith("http://") && !url.startsWith("https://"))
        url.prepend("https://");

#ifdef HAS_QT_WEBENGINE
    // For YouTube URLs, extract the video ID and embed via WebEngine iframe.
    // This avoids the GL render-loop flashing and works for any public video.
    if (use_web_engine_ && is_youtube_url(url)) {
        const QString vid = extract_youtube_id(url);
        if (!vid.isEmpty()) {
            play_web(url, "Custom: " + vid);
            return;
        }
    }
#endif
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
    // Sticky flag — read by on_ytdlp_finished() to decide whether to route
    // the resolved URL through the local trimming proxy (live, needed to
    // avoid Qt 6.8.3's broken sliding-window HLS demuxer) or play_direct()
    // (VOD, fine through Qt's normal path).
    current_is_live_ = is_youtube_live_url(url);

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
    controls_->show();
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
    // HLS rolling-window buffering keeps only 3-10 segments (a few MB) resident
    // regardless of resolution, so we can target the user's chosen ceiling
    // without the DASH-style OOM that bestvideo+bestaudio formats triggered.
    //
    // Format chain: ceiling HLS → (one explicit step-down HLS if the ceiling
    // is above 720, e.g. 1080→720) → any HLS → ceiling-capped best → best.
    // YouTube live formats: 91(144p) 92(240p) 93(360p) 94(480p) 95(720p) 96(1080p).
    // --js-runtimes node: yt-dlp 2025+ needs a JS runtime for YouTube extraction.
    const int ceiling = max_height_;
    QString fmt = QString("best[protocol=m3u8_native][height<=%1]").arg(ceiling);
    // Only add the explicit step-down term when it would be lower than the
    // ceiling — otherwise it'd be a duplicate of the first term and yt-dlp
    // would burn parse cycles retrying the same filter.
    if (ceiling > 720)
        fmt += QStringLiteral("/best[protocol=m3u8_native][height<=720]");
    fmt += QString("/best[protocol=m3u8_native]/best[height<=%1]/best").arg(ceiling);
    proc->start(ytdlp_program,
                {"-f", fmt,
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

    // Live HLS goes through the local trimming proxy — Qt 6.8.3's in-process
    // HLS demuxer stalls on YouTube's 3.5 MB DVR playlists. The proxy hands
    // it a 7 KB trimmed playlist instead. VOD HLS plays fine via Qt's normal
    // path so we skip the proxy for those.
    if (current_is_live_)
        play_via_proxy(stream_url);
    else
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
    // If a prior live playback left the local relay proxy around (e.g.
    // user switched from a live preset to a VOD URL), tear it down before
    // attaching the player to a new source.
    stop_hls_proxy();
    set_loading(false);
    status_label_->hide();
    controls_->show();
    stack_->setCurrentIndex(1);
    player_->setSource(QUrl(stream_url));
    player_->play();
#else
    Q_UNUSED(stream_url)
#endif
}

// Play a live HLS stream through a local trimming proxy. The proxy
// fetches the YouTube DVR playlist (3.5 MB / 2800 segments), keeps just
// the last 6 segments, and serves the resulting 7 KB playlist over
// loopback HTTP. QMediaPlayer uses its existing libavformat HLS demuxer
// — but now on a tiny playlist that refreshes in milliseconds instead
// of seconds. The actual segment downloads still go direct from
// QMediaPlayer to YouTube's CDN (segment URLs are unchanged in the
// trimmed playlist), so no video bytes flow through us.
//
// This is the root-cause fix: Qt's HLS demuxer was choking on the
// 14-second-per-refresh upstream playlist, not on the segments. Now
// it refreshes a 7 KB playlist from RAM in microseconds and stays
// ahead of the live edge with bandwidth to spare.
void VideoPlayerWidget::play_via_proxy(const QString& hls_url) {
#ifdef HAS_QT_MULTIMEDIA
    // Stop the player and any prior proxy FIRST so QMediaPlayer releases
    // its hold on the old source before we hook up a new one.
    player_->stop();
    stop_hls_proxy();

    hls_proxy_ = new fincept::services::video::LiveHlsProxy(this);

    // Wait for the proxy's first successful fetch before pointing
    // QMediaPlayer at it. Otherwise QMediaPlayer's first GET hits a
    // 503 "not ready yet" and Qt's HLS demuxer typically gives up
    // immediately rather than retrying.
    connect(hls_proxy_, &fincept::services::video::LiveHlsProxy::ready,
            this, [this]() {
        if (!hls_proxy_) return; // user stopped between fetch start and ready
        status_label_->hide();
        controls_->show();
        stack_->setCurrentIndex(1);
        player_->setSource(hls_proxy_->local_url());
        player_->play();
    });
    connect(hls_proxy_, &fincept::services::video::LiveHlsProxy::upstream_error,
            this, [this](const QString& msg) {
        set_loading(false);
        status_label_->setText("Live stream relay failed: " + msg.left(200));
        status_label_->show();
        play_in_progress_ = false;
    });

    if (!hls_proxy_->start(QUrl(hls_url))) {
        const QString err = hls_proxy_->last_error();
        stop_hls_proxy();
        set_loading(false);
        status_label_->setText("Could not start local relay: " + err.left(200));
        status_label_->show();
        play_in_progress_ = false;
        LOG_ERROR("VideoPlayer", "LiveHlsProxy::start failed: " + err);
        return;
    }

    set_loading(false);
    status_label_->setText("Starting live relay…");
    status_label_->show();
    controls_->show();
    stack_->setCurrentIndex(1);
#else
    Q_UNUSED(hls_url)
#endif
}

void VideoPlayerWidget::stop_hls_proxy() {
#ifdef HAS_QT_MULTIMEDIA
    if (!hls_proxy_) return;
    // Disconnect signals before delete so the late-arriving `ready` /
    // `upstream_error` from an in-flight upstream fetch doesn't touch a
    // widget mid-teardown.
    disconnect(hls_proxy_, nullptr, this, nullptr);
    hls_proxy_->stop();
    hls_proxy_->deleteLater();
    hls_proxy_ = nullptr;
#endif
}

void VideoPlayerWidget::stop_playback() {
    // Clear current_url_ FIRST so any in-flight yt-dlp resolver (web ID lookup
    // or HLS extraction) sees the mismatch in its finished callback and bails
    // out before it can re-show the player after the user clicked BACK.
    current_url_.clear();
    current_title_.clear();
    play_in_progress_ = false;

    controls_->hide();
    stack_->setCurrentIndex(0);
#ifdef HAS_QT_WEBENGINE
    stop_web();
#endif
#ifdef HAS_QT_MULTIMEDIA
    player_->stop();
    // Tear down the relay proxy AFTER player_->stop() so QMediaPlayer is
    // no longer pulling from it when we close the listening socket.
    stop_hls_proxy();
    current_is_live_ = false;
    if (video_widget_)
        video_widget_->clear_frame();
#endif
    set_loading(false);
    status_label_->hide();
    set_title("LIVE TV / STREAMS");
}


void VideoPlayerWidget::on_player_error() {
#ifdef HAS_QT_MULTIMEDIA
    const QString err = player_->errorString();
    play_in_progress_ = false;
    current_url_.clear(); // stops refresh_data() from retrying
    // Stop the render loop — without this, frameSwapped keeps firing 60fps
    // rendering the last frozen frame behind the error label indefinitely.
    if (video_widget_)
        video_widget_->clear_frame();
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

    for (int i = 0; i < channel_rows_.size() && i < channels_.size(); ++i) {
        const auto& ch = channels_[i];
        channel_rows_[i]->setStyleSheet(
            QString("QPushButton { background: %1; border: 1px solid %2; border-radius: 2px; "
                    "text-align: left; padding: 0 8px; }"
                    "QPushButton:hover { background: %3; border-color: %4; }")
                .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM(), ui::colors::BG_HOVER(), ch.color));
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

#ifndef HAS_QT_MULTIMEDIA
    if (status_label_placeholder_)
        status_label_placeholder_->setStyleSheet(QString("color: %1; font-size: 12px; background: %2;")
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

// ── Config dialog ────────────────────────────────────────────────────────────
//
// Modal editor for the channel list. Two columns (Name, URL) over a
// QTableWidget; row-level toolbar for Add / Remove / Move Up / Move Down /
// Reset. The dialog returns its changes via the standard QDialog accept/reject
// flow: on OK we persist via SettingsRepository, reload the in-memory list,
// and repopulate the channel rows. Rejection leaves channels_ untouched.

QDialog* VideoPlayerWidget::make_config_dialog(QWidget* parent) {
    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle("Configure — Live TV");
    dlg->resize(560, 420);

    auto* root = new QVBoxLayout(dlg);

    auto* hint = new QLabel(
        "Channels appear on the LIVE TV tile in the order shown. "
        "YouTube channel /live URLs, HLS .m3u8, or any direct stream URL.",
        dlg);
    hint->setWordWrap(true);
    root->addWidget(hint);

    // Engine selector. The choice is global to the widget; it applies to
    // both preset channels and custom URLs and persists across sessions.
#ifdef HAS_QT_WEBENGINE
    auto* engine_group = new QGroupBox("Streaming engine", dlg);
    auto* engine_h = new QHBoxLayout(engine_group);
    auto* gl_radio  = new QRadioButton("GL — yt-dlp + QPainter (HLS)", engine_group);
    auto* web_radio = new QRadioButton("WEB — YouTube iframe (Chromium)", engine_group);
    gl_radio->setToolTip("Works for any public stream. Default — works for CNBC, Yahoo, etc.");
    web_radio->setToolTip("Smoother for plain YouTube videos. Some news channels block embedding.");
    (use_web_engine_ ? web_radio : gl_radio)->setChecked(true);
    engine_h->addWidget(gl_radio);
    engine_h->addWidget(web_radio);
    engine_h->addStretch();
    root->addWidget(engine_group);
#endif

    // Maximum resolution. Only affects the GL pipeline (yt-dlp -f selector);
    // the WEB iframe path picks its own resolution. Higher ceilings deliver
    // a sharper picture at the cost of CPU/GPU decode load — drop to 720p
    // or 480p on weaker hardware. The format chain falls back gracefully if
    // the source doesn't offer the chosen height.
    auto* res_group = new QGroupBox("Maximum resolution (GL pipeline)", dlg);
    auto* res_h     = new QHBoxLayout(res_group);
    auto* res_combo = new QComboBox(res_group);
    res_combo->addItem("1080p — Sharper, more decode load", 1080);
    res_combo->addItem("720p — Balanced",                    720);
    res_combo->addItem("480p — Lowest CPU load",             480);
    {
        const int idx = res_combo->findData(max_height_);
        res_combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    res_h->addWidget(res_combo);
    res_h->addStretch();
    root->addWidget(res_group);

    auto* table = new QTableWidget(0, 2, dlg);
    table->setHorizontalHeaderLabels({"Name", "URL"});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(table, 1);

    auto fill_table = [table](const QVector<ChannelDef>& src) {
        table->setRowCount(src.size());
        for (int i = 0; i < src.size(); ++i) {
            table->setItem(i, 0, new QTableWidgetItem(src[i].name));
            table->setItem(i, 1, new QTableWidgetItem(src[i].url));
        }
    };
    fill_table(channels_);

    // Row controls
    auto* row_bar = new QHBoxLayout();
    auto* add_btn   = new QPushButton("Add", dlg);
    auto* remove_btn= new QPushButton("Remove", dlg);
    auto* up_btn    = new QPushButton(QChar(0x2191), dlg); // ↑
    auto* down_btn  = new QPushButton(QChar(0x2193), dlg); // ↓
    auto* reset_btn = new QPushButton("Reset to Defaults", dlg);
    row_bar->addWidget(add_btn);
    row_bar->addWidget(remove_btn);
    row_bar->addWidget(up_btn);
    row_bar->addWidget(down_btn);
    row_bar->addStretch();
    row_bar->addWidget(reset_btn);
    root->addLayout(row_bar);

    connect(add_btn, &QPushButton::clicked, dlg, [table]() {
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(""));
        table->setItem(r, 1, new QTableWidgetItem(""));
        table->setCurrentCell(r, 0);
        table->editItem(table->item(r, 0));
    });

    connect(remove_btn, &QPushButton::clicked, dlg, [table]() {
        const int r = table->currentRow();
        if (r >= 0)
            table->removeRow(r);
    });

    auto move_row = [table](int delta) {
        const int r = table->currentRow();
        const int target = r + delta;
        if (r < 0 || target < 0 || target >= table->rowCount())
            return;
        // Swap the two rows item-by-item; QTableWidget has no native move.
        for (int c = 0; c < table->columnCount(); ++c) {
            QTableWidgetItem* a = table->takeItem(r, c);
            QTableWidgetItem* b = table->takeItem(target, c);
            table->setItem(r, c, b);
            table->setItem(target, c, a);
        }
        table->setCurrentCell(target, table->currentColumn());
    };
    connect(up_btn,   &QPushButton::clicked, dlg, [move_row]() { move_row(-1); });
    connect(down_btn, &QPushButton::clicked, dlg, [move_row]() { move_row(+1); });

    connect(reset_btn, &QPushButton::clicked, dlg, [dlg, fill_table]() {
        const auto ok = QMessageBox::question(dlg, "Reset Channels",
                                              "Replace the current list with the default CNBC / Yahoo / Euronews seed?");
        if (ok == QMessageBox::Yes)
            fill_table(default_channels());
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, dlg,
            [this, dlg, table, res_combo
#ifdef HAS_QT_WEBENGINE
             , web_radio
#endif
            ]() {
        // Commit any in-progress cell edit before we read the table.
        table->setCurrentCell(-1, -1);

        QVector<ChannelDef> next;
        next.reserve(table->rowCount());
        for (int i = 0; i < table->rowCount(); ++i) {
            const auto* name_item = table->item(i, 0);
            const auto* url_item  = table->item(i, 1);
            const QString name = name_item ? name_item->text().trimmed() : QString();
            const QString url  = url_item  ? url_item->text().trimmed()  : QString();
            if (name.isEmpty() || url.isEmpty())
                continue; // drop blank rows silently
            next.append({name, url, {}});
        }

        save_channels(next);
        channels_ = next;
        assign_colors(channels_);
        populate_channel_rows();

#ifdef HAS_QT_WEBENGINE
        const bool want_web = web_radio->isChecked();
        if (want_web != use_web_engine_) {
            use_web_engine_ = want_web;
            save_engine(use_web_engine_);
        }
#endif

        const int want_height = res_combo->currentData().toInt();
        if (want_height != max_height_ && (want_height == 480 || want_height == 720 || want_height == 1080)) {
            max_height_ = want_height;
            save_max_height(max_height_);
            // The new ceiling applies to the next stream resolution — currently
            // playing stream keeps its existing height. Users who want immediate
            // effect can hit BACK and restart the channel.
        }

        apply_styles();
        dlg->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    return dlg;
}

} // namespace fincept::screens::widgets

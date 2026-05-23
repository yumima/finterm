#include "screens/dashboard/widgets/VideoPlayerWidget.h"

#include "auth/InactivityGuard.h"
#include "core/diagnostics/SlowOpTimer.h"
#include "core/logging/Logger.h"
#include "services/video/LiveHlsProxy.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"

#include <QEvent>
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
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QRadioButton>
#include <QRect>
#include <QTableWidget>

#include <algorithm>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
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
    // Pointer cursor on hover signals "this surface is interactive"
    // — same affordance as YouTube's player.  The owner connects
    // the `clicked` signal to the play/pause toggle.
    setCursor(Qt::PointingHandCursor);
}

void VideoRenderWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void VideoRenderWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void VideoRenderWidget::clear_frame() {
    last_image_ = QImage();
    scaled_image_ = QImage();
    last_frame_ = QVideoFrame();
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

    last_frame_ = frame;

    // GPU fast path: NV12 frames go through the offscreen GL scaler, which
    // does YUV→RGB conversion + scaling in a single shader pass and returns
    // a widget-sized RGB QImage. ~10-15 ms/frame at 1080p vs ~25-40 ms CPU.
    QImage gpu_out = scaler_.process(frame, size(), devicePixelRatioF());
    if (!gpu_out.isNull()) {
        scaled_image_ = std::move(gpu_out);
        const qreal dpr = scaled_image_.devicePixelRatio();
        const QSize logical(int(scaled_image_.width()  / dpr),
                            int(scaled_image_.height() / dpr));
        scaled_origin_ = QPoint((width()  - logical.width())  / 2,
                                (height() - logical.height()) / 2);
        paint_pending_ = true;
        update();
        return;
    }

    // CPU fallback: non-NV12 frames (most software-decoded paths) or any GL
    // failure. Identical to the prior all-CPU implementation — keep showing
    // the previous frame on transient toImage() failure (no flash).
    QImage img = frame.toImage();
    if (img.isNull())
        return;
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
    // Re-process the last frame at the new size. With live playback the next
    // frame arrives within ~33 ms and would correct the scale anyway, but
    // paused playback would leave the image at the old size otherwise.
    if (last_frame_.isValid()) {
        QImage gpu_out = scaler_.process(last_frame_, size(), devicePixelRatioF());
        if (!gpu_out.isNull()) {
            scaled_image_ = std::move(gpu_out);
            const qreal dpr = scaled_image_.devicePixelRatio();
            const QSize logical(int(scaled_image_.width()  / dpr),
                                int(scaled_image_.height() / dpr));
            scaled_origin_ = QPoint((width()  - logical.width())  / 2,
                                    (height() - logical.height()) / 2);
            QWidget::resizeEvent(event);
            return;
        }
    }
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

void VideoRenderWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::ParentChange) {
        // The native window backing this widget was just destroyed
        // (setParent(nullptr) for fullscreen, or insertWidget()
        // back into the layout). Any paint event we previously
        // scheduled via update() from present() was tied to that
        // dead window and will never fire on the new one. Without
        // resetting the flag, present()'s backpressure guard would
        // drop every subsequent frame indefinitely — the user sees
        // "video stuck on the last paused frame after fullscreen
        // and back" even though the decoder is happily producing
        // frames and videoFrameChanged is firing.
        paint_pending_ = false;
        // Force a fresh paint at the new size — scaled_image_ is
        // still the pre-reparent letterbox, which would otherwise
        // sit unchanged until the next live frame arrives. Matters
        // most for paused playback where there IS no next live
        // frame.
        update();
    }
    QWidget::changeEvent(event);
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

// Parse a Spotify public URL into a (type, id) pair if it looks like one of the
// embeddable resource forms (show / episode / playlist / track / album). The id
// is a 22-character base62 string assigned by Spotify; the regex is intentionally
// loose on length so a future format bump doesn't silently drop matches — the
// embed iframe itself is the source of truth for whether the id resolves.
//
// Returns {} if the URL is not a Spotify resource we can embed (including the
// already-/embed/ form, which we leave alone so we don't re-wrap it). The host
// check accepts both "open.spotify.com" and the rare "play.spotify.com" alias.
static std::pair<QString, QString> spotify_resource(const QString& url) {
    const QUrl u(url);
    const QString host = u.host().toLower();
    if (host != QStringLiteral("open.spotify.com") &&
        host != QStringLiteral("play.spotify.com"))
        return {};
    // Match /<type>/<id>, where <type> is one of the embeddable kinds.
    // The path may have a trailing slash or further segments (Spotify
    // appends a locale prefix like /intl-en/ for some routes — strip it).
    QString path = u.path();
    static const QRegularExpression locale_re(QStringLiteral(R"(^/intl-[a-z]{2,3}/)"));
    path.replace(locale_re, QStringLiteral("/"));
    static const QRegularExpression re(
        QStringLiteral(R"(^/(show|episode|playlist|track|album)/([A-Za-z0-9]+))"));
    auto m = re.match(path);
    if (!m.hasMatch())
        return {};
    return {m.captured(1), m.captured(2)};
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

        // Pause/resume — GL pipeline only. WEB engine's iframe is
        // cross-origin so JS pause is unreliable; we hide the button in WEB
        // mode. Visibility is set immediately below this controls_ block
        // (initial) and re-applied in the config dialog's accept handler
        // when the user switches engines.
        play_pause_btn_ = new QPushButton(QString(QChar(0x23F8)) + " PAUSE");
        play_pause_btn_->setCursor(Qt::PointingHandCursor);
        play_pause_btn_->setFixedHeight(20);
        play_pause_btn_->setToolTip("Pause / resume the current stream");
        connect(play_pause_btn_, &QPushButton::clicked, this, [this]() {
#ifdef HAS_QT_MULTIMEDIA
            if (!player_) return;
            // User explicitly pressed the button — handle each state.
            // resume_playback() applies the Qt6 FFmpeg audio-detach
            // workaround on the Paused→Playing edge; on Stopped (e.g.
            // the stream ended) a bare play() restarts from the current
            // source, which is what "PLAY pressed on a stopped stream"
            // intuitively means.
            switch (player_->playbackState()) {
                case QMediaPlayer::PlayingState:
                    player_->pause();
                    break;
                case QMediaPlayer::PausedState:
                    resume_playback();
                    break;
                case QMediaPlayer::StoppedState:
                    player_->play();
                    break;
            }
            // User pressed the button → not an auto-pause, so clear the
            // latch even if a subsequent unlock fires.
            auto_paused_on_lock_ = false;
#endif
        });
        cl->addWidget(play_pause_btn_);

        // Fullscreen toggle. Works for both GL (reparents
        // video_widget_) and WEB (reparents web_view_). Exit via
        // clicking the button again, pressing Esc, or — in WEB mode —
        // using YouTube's own fullscreen control.
        fullscreen_btn_ = new QPushButton(QString(QChar(0x26F6)) + " FULLSCREEN");
        fullscreen_btn_->setCursor(Qt::PointingHandCursor);
        fullscreen_btn_->setFixedHeight(20);
        fullscreen_btn_->setToolTip("Toggle fullscreen (Esc to exit)");
        connect(fullscreen_btn_, &QPushButton::clicked,
                this, &VideoPlayerWidget::toggle_fullscreen);
        cl->addWidget(fullscreen_btn_);

        stop_btn_ = new QPushButton(QString(QChar(0x25A0)) + " BACK");
        stop_btn_->setCursor(Qt::PointingHandCursor);
        stop_btn_->setFixedHeight(20);
        connect(stop_btn_, &QPushButton::clicked, this, &VideoPlayerWidget::stop_playback);
        cl->addWidget(stop_btn_);

        content_layout()->addWidget(controls_);
        controls_->hide();
#ifdef HAS_QT_WEBENGINE
        // Pause/play has no effect on the WEB engine (cross-origin iframe);
        // hide it in that mode so the control bar reflects what actually
        // works. Re-toggled by the config dialog's accept handler when the
        // user switches engines.
        play_pause_btn_->setVisible(!use_web_engine_);
#endif
    }

    connect(this, &BaseWidget::refresh_requested, this, &VideoPlayerWidget::refresh_data);

    // Auto-pause GL playback when the terminal locks; auto-resume on unlock
    // — only if we'd been the ones to pause it, so a user-initiated pause
    // before lock is not undone. WEB engine is unaffected (cross-origin JS
    // can't reliably pause an embedded YouTube iframe).
    connect(&auth::InactivityGuard::instance(),
            &auth::InactivityGuard::terminal_locked_changed, this,
            [this](bool locked) {
#ifdef HAS_QT_WEBENGINE
                // Spotify-embed branch. The embed iframe is cross-origin, so
                // we can't poke at its DOM directly; the contract is
                // postMessage to https://open.spotify.com. The embed
                // controller accepts {command: 'pause'} and {command: 'resume'}
                // as distinct verbs, so we can mirror the GL pipeline's
                // "pause iff playing" contract: only set the latch when we
                // pause, only resume what we paused. Best-effort: if the
                // embed isn't yet hooked up (still loading), the message is
                // dropped silently — the worst case is the embed keeps
                // playing while the lock screen is up, no harder failure.
                if (web_is_spotify_ && web_view_) {
                    auto post = [this](const char* cmd) {
                        // QStringLiteral wants a literal; build the snippet
                        // with a single arg() so the verb is the only thing
                        // that varies between the two call sites.
                        const QString js = QStringLiteral(
                            "(function(){var f=document.querySelector('iframe');"
                            "if(!f||!f.contentWindow)return;"
                            "f.contentWindow.postMessage({command:'%1'},"
                            "'https://open.spotify.com');})();").arg(QLatin1String(cmd));
                        web_view_->page()->runJavaScript(js);
                    };
                    if (locked) {
                        post("pause");
                        spotify_auto_paused_on_lock_ = true;
                    } else if (spotify_auto_paused_on_lock_) {
                        spotify_auto_paused_on_lock_ = false;
                        post("resume");
                    }
                }
#endif
#ifdef HAS_QT_MULTIMEDIA
                if (!player_) return;
                if (locked) {
                    if (player_->playbackState() == QMediaPlayer::PlayingState) {
                        player_->pause();
                        auto_paused_on_lock_ = true;
                    }
                } else if (auto_paused_on_lock_) {
                    auto_paused_on_lock_ = false;
                    resume_playback();
                }
#else
                Q_UNUSED(locked);
#endif
            });

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
    url_input_->setPlaceholderText("YouTube / Spotify / HLS (.m3u8) / MP4 URL...");
    connect(url_input_, &QLineEdit::returnPressed, this, &VideoPlayerWidget::play_custom_url);
    irl->addWidget(url_input_, 1);

    play_btn_ = new QPushButton("PLAY");
    play_btn_->setFixedWidth(50);
    play_btn_->setCursor(Qt::PointingHandCursor);
    connect(play_btn_, &QPushButton::clicked, this, &VideoPlayerWidget::play_custom_url);
    irl->addWidget(play_btn_);

    vl->addWidget(input_row);

    helper_label_ = new QLabel("YouTube via yt-dlp; Spotify shows / episodes / playlists via embed iframe.");
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

    // YouTube-style click-to-toggle: route the surface click through the
    // existing pause button click handler so both paths share one source
    // of truth.  The button is hidden in WebEngine mode (cross-origin
    // iframe), but it still exists — clicking it programmatically is a
    // no-op when player_ is null, which is the only state where this
    // would fire wrong.
    connect(video_widget_, &VideoRenderWidget::clicked, this, [this]() {
        if (play_pause_btn_)
            play_pause_btn_->click();
    });
    // YouTube-style double-click → fullscreen. Qt's event sequence
    // delivers a mousePressEvent before mouseDoubleClickEvent on a
    // dblclick, so the first click already fired `clicked` above and
    // toggled play/pause. Reverse that toggle here so the user's
    // pre-dblclick state survives the fullscreen entry — without
    // this, dblclicking a playing video drops into fullscreen
    // *paused* and the controls bar (where PAUSE/PLAY lives) is
    // hidden behind the fullscreen window, forcing the user to Esc
    // out just to resume.
    //
    // We bypass resume_playback() / play_pause_btn_->click(): the
    // player was running (or just paused) a frame ago, so no audio
    // re-attach or decoder rebuild is needed — a plain play()/pause()
    // pair suffices and is synchronous, so the state matches reality
    // by the time toggle_fullscreen() runs.
    connect(video_widget_, &VideoRenderWidget::doubleClicked, this, [this]() {
#ifdef HAS_QT_MULTIMEDIA
        if (player_) {
            // StoppedState is left alone on purpose — it only occurs
            // after EOF or error, in which case the first click of
            // the dblclick already called player_->play() to restart
            // and there's nothing to reverse.
            if (player_->playbackState() == QMediaPlayer::PausedState)
                player_->play();
            else if (player_->playbackState() == QMediaPlayer::PlayingState)
                player_->pause();
        }
#endif
        toggle_fullscreen();
    });

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
                // Record the pause moment so resume_playback() can pick
                // between the cheap and the heavy recovery — both manual
                // pause and lock-auto-pause flow through this signal.
                if (s == QMediaPlayer::PausedState)
                    paused_at_ = QDateTime::currentDateTime();
                // Reflect state on the pause/play button. StoppedState falls
                // through to "PAUSE" since the controls bar is hidden then
                // anyway (stop_playback returns the user to the channel list).
                if (play_pause_btn_) {
                    if (s == QMediaPlayer::PlayingState) {
                        play_pause_btn_->setText(QString(QChar(0x23F8)) + " PAUSE");
                    } else if (s == QMediaPlayer::PausedState) {
                        play_pause_btn_->setText(QString(QChar(0x25B6)) + " PLAY");
                    }
                }
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

#ifdef HAS_QT_MULTIMEDIA
    // Space-bar pause/resume on the GL player. QShortcut scoped to
    // player_page_ with WidgetWithChildrenShortcut so it only fires
    // when the player page (not the channel list) has focus and
    // doesn't steal Space from any QLineEdit / QTextEdit elsewhere
    // in the app. Routes through play_pause_btn_->click() so the
    // existing state machine (PausedState → resume_playback,
    // PlayingState → pause, StoppedState → play, auto-pause-lock
    // latch clear) stays the single source of truth.
    auto* space_sc = new QShortcut(QKeySequence(Qt::Key_Space), player_page_);
    space_sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(space_sc, &QShortcut::activated, this, [this]() {
        if (play_pause_btn_ && play_pause_btn_->isVisible())
            play_pause_btn_->click();
    });
#endif
}

// Resume from a paused stream by picking the lightest correct
// recovery for the situation:
//
//   • Short pauses (<= kPauseLiveResetThresholdSec) and VOD sources
//     use a setSource(same) workaround — rebuilds the decoder chain
//     (fixes Qt6 FFmpeg's audio-sink detach on pause→play) without
//     losing the buffered position or paying the ~1 s playlist
//     re-fetch.
//
//   • Long pauses on the local trimming-proxy URL trigger a full
//     reset (stop → clear source → re-set source → defensively
//     re-attach outputs → play).  This is the only path that
//     escapes the freeze where Qt tries to resume from a segment
//     that has fallen out of the proxy's ~6-segment window — and
//     it matches live-TV semantics ("rejoin the broadcast now"
//     rather than "play back the gap").
//
// The threshold is conservative: HLS segments are typically 5–10s,
// the proxy keeps ~6, so the cursor can stay valid for ~30s in
// theory.  In practice we go full-reset after 5s of pause on the
// proxy URL — small enough that the user rarely runs into the
// freeze, large enough that genuine "click PAUSE then immediately
// click PLAY" stays on the cheap path.
//
// Contract: this helper *only* handles the Paused→Playing edge.  It
// is a no-op on Stopped or Playing.  Callers that want different
// behavior on Stopped (e.g. PLAY-after-STOP restart) handle that
// themselves — keeping this helper single-purpose lets the unlock
// auto-resume be a one-liner with the right safety semantics
// (auto_paused_on_lock_ contract: "we paused it; resume only if
// it's still paused; otherwise leave alone").
void VideoPlayerWidget::resume_playback() {
#ifdef HAS_QT_MULTIMEDIA
    if (!player_ || player_->playbackState() != QMediaPlayer::PausedState)
        return;
    const QUrl src = player_->source();
    if (!src.isValid()) {
        player_->play();
        return;
    }

    constexpr qint64 kPauseLiveResetThresholdSec = 5;
    // LiveHlsProxy binds 127.0.0.1 but the player URL may be constructed
    // with either the literal or the hostname depending on call site —
    // accept both so we don't silently fall onto the heavy path for a
    // local-proxy URL and risk the long-pause freeze.
    const QString host = src.host();
    const bool is_live_proxy =
        host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost");
    const qint64 elapsed_sec =
        paused_at_.isValid() ? paused_at_.secsTo(QDateTime::currentDateTime()) : 0;
    const bool needs_full_reset =
        is_live_proxy && elapsed_sec > kPauseLiveResetThresholdSec;

    if (needs_full_reset) {
        // Live HLS, paused long enough that the proxy's 6-segment
        // window has rolled past our buffered position. Only path
        // where setSource() is justified: we MUST re-fetch a fresh
        // playlist to land at the live edge.
        //
        // Order matters — without the explicit empty setSource()
        // between stop() and setSource(src), Qt may treat the re-set
        // as a seek and reuse the stale cursor.
        player_->stop();
        player_->setSource(QUrl{});
        player_->setSource(src);
        // Defensive output re-attach AFTER setSource() — the Qt6
        // FFmpeg backend has been buggy enough to drop outputs across
        // source resets, and no-op-when-already-attached calls are
        // cheap insurance.
        if (audio_output_) player_->setAudioOutput(audio_output_);
        if (video_sink_)   player_->setVideoOutput(video_sink_);
        player_->play();
        return;
    }

    // Cheap path — short pauses (VOD always, and live ≤5s). DO NOT
    // call setSource(same) here: it tears down the decoder chain and
    // races with the immediate play() below, leaving the player in a
    // half-loaded state where playbackState transitions to Playing
    // (the button text flips correctly) but the new decoder has not
    // yet emitted frames. On a live stream the playlist re-fetch can
    // take seconds — "paused for long time" — and intermittently
    // fails entirely if the stream is mid-rollover.
    //
    // The original setSource(same) workaround (07737672) was intended
    // to fix a Qt6 FFmpeg audio-sink detach across pause→play.
    // setAudioOutput(audio_output_) achieves the same re-attach
    // without the source reload — it's a documented API call to
    // (re)bind the audio chain, completes synchronously, no race.
    // setVideoOutput is symmetric insurance.
    if (audio_output_) player_->setAudioOutput(audio_output_);
    if (video_sink_)   player_->setVideoOutput(video_sink_);
    player_->play();
#endif
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

    // Honor HTML5 fullscreen requests from the YouTube iframe (the ⛶
    // button inside its own player chrome). Without accept() the
    // request is denied by default and the iframe stays embedded. We
    // mirror the toggle into our own reparent path so the QWebEngineView
    // also fills the screen — accepting alone only sizes the <video>
    // to the view's bounds, not the display.
    connect(web_view_->page(), &QWebEnginePage::fullScreenRequested,
            this, [this](QWebEngineFullScreenRequest request) {
                request.accept();
                if (request.toggleOn() && !fullscreen_target_) {
                    fullscreen_via_web_request_ = true;
                    enter_fullscreen();
                } else if (!request.toggleOn() && fullscreen_target_) {
                    exit_fullscreen();
                }
            });

    stack_->addWidget(web_view_); // page 2
}

// Embed a YouTube video by ID — the path that already works smoothly for
// custom URLs. Channel-level embed restrictions (CNBC error 152) DO NOT apply
// at the individual video level, only at the `live_stream?channel=` endpoint.
void VideoPlayerWidget::play_web_video_id(const QString& video_id, const QString& title,
                                          const QString& source_url) {
    if (!web_view_) build_web_view();
    // Clear Spotify-mode latches in case the user switched from a Spotify
    // embed straight to a YouTube URL without hitting BACK. Without this the
    // lock handler would keep posting Spotify-targeted messages to a YouTube
    // iframe (harmless — wrong origin — but the visibility latch on
    // play_pause_btn_ would also stay stuck off).
    web_is_spotify_              = false;
    spotify_auto_paused_on_lock_ = false;
    if (play_pause_btn_) play_pause_btn_->setVisible(!use_web_engine_);

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
    // Clear Spotify-specific latches so a subsequent unlock or refresh doesn't
    // try to resume something we already tore down.
    web_is_spotify_ = false;
    spotify_auto_paused_on_lock_ = false;
}

// Render a Spotify resource via the official embed iframe.
//
// We load https://open.spotify.com/embed/<type>/<id>?utm_source=generator
// directly into QWebEngineView. No HTML wrapper — Spotify's embed page is
// already a chromeless player, and loading it as the top-level document means
// the page's own origin (open.spotify.com) hosts the iframe-API JS. That
// matters because some of the embed's internal navigation (Premium upsell,
// "Listen in app") only renders when the page origin matches the embed's
// expectations.
//
// Hard-strip any query the user pasted (typically `?si=…` share tokens). The
// embed only needs `utm_source=generator` for analytics; passing through
// arbitrary query state has occasionally surfaced inconsistent embed UI.
void VideoPlayerWidget::play_spotify_embed(const QString& type, const QString& id,
                                            const QString& title, const QString& source_url) {
    if (!web_view_) build_web_view();

    // Tear down any GL pipeline that was running — otherwise the user would
    // hear the previous stream alongside the Spotify embed with no obvious
    // way to silence it (the GL pause button gets hidden below). Symmetric
    // with play_via_proxy() / play_direct(), which stop their own pipelines
    // before swapping pages.
#ifdef HAS_QT_MULTIMEDIA
    if (player_) {
        player_->stop();
        if (video_widget_) video_widget_->clear_frame();
    }
    stop_hls_proxy();
    current_is_live_     = false;
    auto_paused_on_lock_ = false;
#endif

    const QString embed_url = QStringLiteral("https://open.spotify.com/embed/%1/%2?utm_source=generator")
                                  .arg(type, id);
    web_view_->setUrl(QUrl(embed_url));

    web_is_spotify_              = true;
    spotify_auto_paused_on_lock_ = false;
    play_in_progress_            = false;
    current_url_                 = source_url;
    current_title_               = title;
    now_playing_->setText(QChar(0x25B6) + QString(" ") + title);
    set_title("LIVE TV — " + title.toUpper());
    status_label_->hide();
    controls_->show();
    // Spotify's iframe owns the transport UI — its play/pause is reachable
    // inside the embed. Hide our pause button so users don't expect it to
    // drive the embed (cross-origin postMessage is best-effort; the visible
    // controls live inside the iframe).
    if (play_pause_btn_) play_pause_btn_->setVisible(false);
    stack_->setCurrentIndex(2);
}
#endif

VideoPlayerWidget::~VideoPlayerWidget() {
    // If we're being destroyed mid-fullscreen, the reparented surface
    // is no longer in Qt's parent-child chain (setParent(nullptr) ran
    // in enter_fullscreen). Without explicit cleanup it leaks; worse,
    // the qApp event filter we installed still points at this dying
    // QObject and the next key press anywhere in the app would
    // dispatch into freed memory.
    //
    // Order matters: remove the filter first so no event can reach a
    // half-destroyed `this`, then delete the orphan. The player_ /
    // video_sink_ are still children of `this` and will be torn down
    // by Qt's normal child-destruction immediately after this body
    // returns — any in-flight queued frames whose receiver
    // (video_widget_) we just deleted are discarded by Qt's
    // dead-receiver check on QueuedConnection delivery.
    if (fullscreen_target_) {
        QCoreApplication::instance()->removeEventFilter(this);
        delete fullscreen_target_;
        fullscreen_target_ = nullptr;
    }
}

// ── Actions ──────────────────────────────────────────────────────────────────

void VideoPlayerWidget::play_preset(int index) {
    if (index < 0 || index >= channels_.size())
        return;
    const auto& ch = channels_[index];
#ifdef HAS_QT_WEBENGINE
    // Spotify presets ride the iframe path regardless of the GL/WEB engine
    // toggle — there is no GL pipeline for Spotify (no public stream URL,
    // DRM'd audio). The toggle only chooses between yt-dlp+QPainter and the
    // YouTube iframe for *YouTube* URLs.
    if (auto [type, id] = spotify_resource(ch.url); !type.isEmpty()) {
        play_spotify_embed(type, id, ch.name, ch.url);
        return;
    }
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
    // Spotify URLs (show / episode / playlist / track / album) bypass GL
    // entirely — there is no public stream we can hand to QMediaPlayer, only
    // the official embed iframe. Detect first so it wins regardless of the
    // GL/WEB toggle.
    if (auto [type, id] = spotify_resource(url); !type.isEmpty()) {
        // Title format mirrors the YouTube "Custom: <id>" pattern so the
        // status bar stays consistent across custom-URL embed kinds.
        play_spotify_embed(type, id, QStringLiteral("Spotify %1: %2").arg(type, id), url);
        return;
    }
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
#ifdef HAS_QT_WEBENGINE
    // Spotify embeds keep playing in the iframe; refresh is a no-op there.
    // Routing through play_url() would hand a Spotify URL to yt-dlp and
    // surface a confusing "yt-dlp error" status, so short-circuit here.
    if (web_is_spotify_)
        return;
#endif
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

// ── Fullscreen ───────────────────────────────────────────────────────────────
//
// Entry points:
//   • FULLSCREEN button on the controls bar (both engines)
//   • Double-click on the GL video surface
//   • YouTube iframe's own ⛶ button (WEB engine, via
//     QWebEnginePage::fullScreenRequested)
//
// Exit points:
//   • FULLSCREEN button again
//   • Esc (handled by the qApp event filter we install only while
//     fullscreen is active)
//   • YouTube iframe's exit-fullscreen (WEB only, same signal as entry)
//   • stop_playback() — exits fullscreen first so the surface is back
//     inside the tile before we hide the controls bar
//
// We reparent the *surface* (VideoRenderWidget or QWebEngineView) to
// top-level, not the whole VideoPlayerWidget. The media stack
// (QMediaPlayer, QVideoSink, QWebEnginePage) is unchanged, so playback
// continues without a source reset.

void VideoPlayerWidget::toggle_fullscreen() {
    if (fullscreen_target_) exit_fullscreen();
    else                    enter_fullscreen();
}

void VideoPlayerWidget::enter_fullscreen() {
    if (fullscreen_target_) return;
    QWidget* target = nullptr;
#ifdef HAS_QT_WEBENGINE
    if (stack_->currentIndex() == 2 && web_view_) {
        target = web_view_;
    } else
#endif
#ifdef HAS_QT_MULTIMEDIA
    if (stack_->currentIndex() == 1 && video_widget_) {
        target = video_widget_;
    }
#endif
    if (!target) return;

    fullscreen_target_ = target;
    // setParent(nullptr) reparents out of the layout and implicitly
    // makes the widget a top-level window. setWindowTitle gives it a
    // sensible window-list entry in case the compositor shows one.
    target->setParent(nullptr);
    target->setWindowTitle(current_title_.isEmpty() ? QStringLiteral("Fincept — Video")
                                                    : current_title_);
    target->showFullScreen();
    target->setFocus();
    // App-wide filter so Esc fires no matter which descendant of
    // target_ owns focus — QWebEngineView routes keys to its
    // RenderWidgetHostViewQtDelegateWidget child, which doesn't
    // forward to filters installed on the QWebEngineView itself.
    QCoreApplication::instance()->installEventFilter(this);
    // We intentionally don't flip the FULLSCREEN button text to
    // something like "EXIT FULL" — once the surface fills the screen
    // it covers the controls bar entirely, so a state-toggle label
    // would never be visible to the user. Esc is the documented exit.
}

void VideoPlayerWidget::exit_fullscreen() {
    if (!fullscreen_target_) return;
    QCoreApplication::instance()->removeEventFilter(this);
    QWidget* target = fullscreen_target_;
    fullscreen_target_ = nullptr;
    fullscreen_via_web_request_ = false;

#ifdef HAS_QT_WEBENGINE
    if (target == web_view_) {
        // Re-insert at the same stack slot (page 2). insertWidget
        // reparents internally; explicit setParent is redundant.
        stack_->insertWidget(2, web_view_);
        stack_->setCurrentIndex(2);
        web_view_->showNormal();
        return;
    }
#endif
#ifdef HAS_QT_MULTIMEDIA
    if (target == video_widget_) {
        // video_widget_ was the first child of player_page_'s
        // QVBoxLayout (status_label_ is the second). Insert at 0 with
        // stretch 1 to match build_player_view(). player_page_ is
        // unconditionally created in build_player_view() so we don't
        // null-check it here.
        if (auto* vl = qobject_cast<QVBoxLayout*>(player_page_->layout()))
            vl->insertWidget(0, video_widget_, 1);
        stack_->setCurrentIndex(1);
        video_widget_->showNormal();
    }
#endif
}

bool VideoPlayerWidget::eventFilter(QObject* obj, QEvent* event) {
    if (fullscreen_target_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            // If WEB fullscreen was started by the iframe's own
            // requestFullscreen(), let Chromium handle Esc — it'll
            // emit fullScreenRequested(toggleOn=false) which routes
            // back through our signal handler and calls exit_fullscreen
            // cleanly. Intercepting here would leave the page stuck in
            // HTML5 fullscreen state with the view reparented out.
            if (fullscreen_via_web_request_) return false;
            exit_fullscreen();
            return true;
        }
    }
    return BaseWidget::eventFilter(obj, event);
}

void VideoPlayerWidget::stop_playback() {
    // Bring the surface back into the tile before tearing playback
    // down — otherwise the user gets a momentarily-visible fullscreen
    // window with a stopped (black) frame.
    if (fullscreen_target_) exit_fullscreen();

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
    // stop_web() cleared web_is_spotify_; restore the pause button to match
    // the currently selected engine so the next GL playback gets its control
    // back (Spotify-mode hides it because the embed owns its own transport).
    if (play_pause_btn_)
        play_pause_btn_->setVisible(!use_web_engine_);
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

    // Three control-bar buttons (PAUSE/PLAY, FULLSCREEN, BACK) share
    // one base look — same font size, weight, padding, border, radius —
    // so they don't visually disagree on the same toolbar. Hover color
    // is the only knob we vary, and only to encode meaning:
    //   • BACK  → NEGATIVE (red): destructive, exits playback
    //   • PAUSE / FULLSCREEN → TEXT_PRIMARY: neutral, non-destructive
    const QString btn_base = QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; color: %3; "
        "font-size: 9px; font-weight: bold; padding: 0 10px; border-radius: 2px; }"
        "QPushButton:hover { background: %4; color: %5; }");
    if (play_pause_btn_) {
        play_pause_btn_->setStyleSheet(btn_base.arg(
            ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_SECONDARY(),
            ui::colors::BG_HOVER(), ui::colors::TEXT_PRIMARY()));
    }
    if (fullscreen_btn_) {
        fullscreen_btn_->setStyleSheet(btn_base.arg(
            ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_SECONDARY(),
            ui::colors::BG_HOVER(), ui::colors::TEXT_PRIMARY()));
    }
    stop_btn_->setStyleSheet(btn_base.arg(
        ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_SECONDARY(),
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
            // Pause/play targets the GL player only; reflect that in the
            // control bar so the button disappears when WEB takes over.
            if (play_pause_btn_)
                play_pause_btn_->setVisible(!use_web_engine_);
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

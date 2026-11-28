// src/screens/equity_research/EarningsCallAudio.cpp
#include "screens/equity_research/EarningsCallAudio.h"

#include "screens/equity_research/EarningsCallWebPanel.h"

#include "core/logging/Logger.h"
#include "services/video/LiveHlsProxy.h"
#include "services/video/YtDlp.h"
#include "ui/theme/Theme.h"

#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#ifdef HAS_QT_MULTIMEDIA
#    include <QAudioOutput>
#    include <QMediaPlayer>
#endif

namespace finterm::screens {

namespace {
constexpr int kStallTickMs = 5000;
constexpr int kStallTicks = 4;     // 20 s without the position moving = dead
constexpr int kMaxRetries = 3;
constexpr int kResolveTimeoutMs = 30000;
} // namespace

EarningsCallAudio::EarningsCallAudio(QWidget* parent) : QPushButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    connect(this, &QPushButton::clicked, this, &EarningsCallAudio::toggle);
    stall_timer_ = new QTimer(this);
    stall_timer_->setInterval(kStallTickMs);
    connect(stall_timer_, &QTimer::timeout, this, &EarningsCallAudio::on_stall_tick);
#ifdef HAS_QT_MULTIMEDIA
    player_ = new QMediaPlayer(this);
    output_ = new QAudioOutput(this);
    player_->setAudioOutput(output_);
    // Audio only: no video sink is attached, so nothing is decoded for display
    // and no surface is created (see project_video_wayland_subsurface).
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& msg) {
        if (active_)
            fail(msg.isEmpty() ? QStringLiteral("playback error") : msg);
    });
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState) { paint_state(); });
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64) { paint_state(); });
#endif
    paint_state();
}

EarningsCallAudio::~EarningsCallAudio() {
    if (proc_)
        proc_->kill();
}

void EarningsCallAudio::set_source(const QString& url, const QString& kind, const QString& unavailable) {
    unavailable_ = unavailable;
    if (url == url_ && kind == kind_) {
        paint_state();
        return;
    }
    stop();
    url_ = url;
    kind_ = kind;
    error_.clear();
    paint_state();
}

void EarningsCallAudio::set_page(const QString& page_url, const QString& host) {
    page_url_ = page_url;
    page_host_ = host;
    paint_state();
}

void EarningsCallAudio::set_page_open(bool open) {
    page_open_ = open;
    paint_state();
}

void EarningsCallAudio::toggle() {
    if (active_) {
        stop();
        return;
    }
    // An open host-page player means "close it" — even if a public stream
    // has turned up since: never two copies of the call at once.
    if (page_open_ || url_.isEmpty()) {
        if (!page_url_.isEmpty())
            emit page_requested(page_url_);
        return;
    }
    active_ = true;
    retries_ = 0;
    error_.clear();
    if (kind_ == QLatin1String("media"))
        play(url_, false); // an IR-hosted file (or recorded HLS) plays as-is
    else
        resolve(false);
    paint_state();
}

void EarningsCallAudio::stop() {
    active_ = false;
    stall_timer_->stop();
    if (proc_) {
        // Its `finished` handler (which would delete it) is disconnected, so
        // delete it here — a QPointer left non-null would make every later
        // resolve() return early and wedge the button on "loading…".
        QProcess* p = proc_;
        p->disconnect(this);
        p->kill();
        p->deleteLater();
        proc_ = nullptr;
    }
#ifdef HAS_QT_MULTIMEDIA
    player_->stop();
    player_->setSource(QUrl());
#endif
    if (proxy_) {
        proxy_->stop();
        proxy_->deleteLater();
    }
    paint_state();
}

void EarningsCallAudio::resolve(bool refresh_session) {
    const QString program = services::video::ytdlp_program();
    if (program.isEmpty()) {
        fail(QStringLiteral("yt-dlp is missing: Settings › Python Env › Install Missing adds it"));
        return;
    }
    if (proc_)
        return; // one resolve at a time
    auto* proc = new QProcess(this);
    proc_ = proc;
    connect(proc, &QProcess::finished, this, [this, proc, refresh_session](int code, QProcess::ExitStatus) {
        proc->deleteLater();
        if (!active_)
            return;
        const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed().split('\n').value(0).trimmed();
        if (code != 0 || out.isEmpty()) {
            if (refresh_session)
                return; // not fatal: the stall watch is the backstop
            QString err = services::video::ytdlp_error(proc->readAllStandardError());
            // "ERROR: [youtube] UiUWuWd9-Hk: This live event will begin in 6 days."
            err.remove(QRegularExpression(QStringLiteral("^ERROR:\\s*(\\[[^\\]]*\\]\\s*)?([\\w-]+:\\s*)?")));
            fail(kind_ == QLatin1String("youtube_live") ? QStringLiteral("the channel isn't live right now")
                                                        : err.trimmed().left(160));
            return;
        }
        // An HLS answer from YouTube is a LIVE stream (a recording resolves
        // to a plain audio file): it goes through the relay.
        const bool live = out.contains(QLatin1String(".m3u8")) || out.contains(QLatin1String("/manifest/"));
        if (refresh_session && proxy_) {
            proxy_->set_upstream(QUrl(out));
            return;
        }
        play(out, live);
    });
    QTimer::singleShot(kResolveTimeoutMs, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    // Audio first; a live stream has no audio-only rendition, so the smallest
    // muxed HLS rendition (144p) — its video is never decoded here.
    const QString fmt = QStringLiteral("bestaudio/worst[protocol=m3u8_native]/worst");
    proc->start(program, services::video::ytdlp_resolve_args(fmt, url_));
    paint_state();
}

void EarningsCallAudio::play(const QString& stream_url, bool live) {
#ifdef HAS_QT_MULTIMEDIA
    last_position_ = -1;
    stall_ticks_ = 0;
    if (live) {
        if (proxy_) {
            proxy_->stop();
            proxy_->deleteLater();
        }
        auto* proxy = new services::video::LiveHlsProxy(this);
        proxy_ = proxy;
        connect(proxy, &services::video::LiveHlsProxy::ready, this, [this, proxy]() {
            if (!active_ || proxy_ != proxy)
                return;
            player_->setSource(proxy->local_url());
            player_->play();
            stall_timer_->start();
        });
        connect(proxy, &services::video::LiveHlsProxy::refresh_upstream_requested, this,
                [this]() { resolve(/*refresh_session=*/true); });
        connect(proxy, &services::video::LiveHlsProxy::upstream_error, this,
                [this](const QString& msg) { fail(QStringLiteral("live relay: ") + msg.left(120)); });
        if (!proxy->start(QUrl(stream_url)))
            fail(QStringLiteral("could not start the local relay: ") + proxy->last_error().left(120));
        return;
    }
    player_->setSource(QUrl(stream_url));
    player_->play();
    stall_timer_->start();
#else
    Q_UNUSED(stream_url)
    Q_UNUSED(live)
    fail(QStringLiteral("this build has no audio playback"));
#endif
}

void EarningsCallAudio::on_stall_tick() {
#ifdef HAS_QT_MULTIMEDIA
    if (!active_)
        return;
    const qint64 pos = player_->position();
    if (player_->playbackState() == QMediaPlayer::PlayingState && pos != last_position_) {
        last_position_ = pos;
        stall_ticks_ = 0;
        return;
    }
    if (player_->mediaStatus() == QMediaPlayer::EndOfMedia) {
        stop();
        return;
    }
    // Qt's FFmpeg backend reports nothing when an HLS stream dies; a position
    // that stops moving is the only sign. Re-resolve from scratch — reopening
    // the dead session cannot bring it back.
    if (++stall_ticks_ < kStallTicks)
        return;
    stall_ticks_ = 0;
    if (kind_ == QLatin1String("media") || ++retries_ > kMaxRetries) {
        fail(QStringLiteral("the stream stopped"));
        return;
    }
    LOG_WARN("EarningsCallAudio", "stream stalled — re-resolving");
    player_->stop();
    resolve(false);
#endif
}

void EarningsCallAudio::fail(const QString& why) {
    LOG_WARN("EarningsCallAudio", "playback failed: " + why);
    stop();
    error_ = why;
    paint_state();
}

void EarningsCallAudio::paint_state() {
    const bool via_page = (url_.isEmpty() || page_open_) && !page_url_.isEmpty();
    const bool available = !url_.isEmpty() || via_page;
    setEnabled(available);
    QString text = QStringLiteral("▶ LISTEN");
    QString tip;
    if (!available) {
        tip = unavailable_;
    } else if (via_page) {
        if (page_open_)
            text = QStringLiteral("■ CLOSE CALL");
        if (page_open_)
            tip = QStringLiteral("Close the call player (stops the audio).");
        else if (EarningsCallWebPanel::kEmbedded)
            tip = QStringLiteral("Listen in finterm, in the call host's own player (%1). If the host asks for a "
                                 "name and email first (or a sign-in — one Q4 account covers every Q4-hosted "
                                 "call), that's asked once and remembered.\n%2")
                      .arg(page_host_, page_url_);
        else
            tip = QStringLiteral("Opens the call in your browser, in its host's player (%1).\n%2")
                      .arg(page_host_, page_url_);
    } else if (active_) {
        bool playing = false;
        qint64 pos = 0;
#ifdef HAS_QT_MULTIMEDIA
        playing = player_->playbackState() == QMediaPlayer::PlayingState && player_->position() > 0;
        pos = player_->position() / 1000;
#endif
        if (playing)
            text = QStringLiteral("■ STOP · %1:%2").arg(pos / 60).arg(pos % 60, 2, 10, QLatin1Char('0'));
        else
            text = QStringLiteral("■ STOP · loading…");
        tip = QStringLiteral("Playing the call's audio in finterm — click to stop.\n%1").arg(url_);
    } else {
        tip = QStringLiteral("Listen to the call here, audio only%1.\n%2")
                  .arg(kind_ == QLatin1String("youtube_live") ? QStringLiteral(" (the company's live stream)")
                                                              : QString(),
                       url_);
        if (!error_.isEmpty())
            tip = QStringLiteral("Couldn't play: %1\n\n%2").arg(error_, tip);
    }
    setText(text);
    setToolTip(tip);
    const QString color = !available                 ? ui::colors::TEXT_TERTIARY()
                          : !error_.isEmpty()        ? ui::colors::NEGATIVE()
                          : (active_ || page_open_)  ? ui::colors::AMBER()
                                                     : ui::colors::CYAN();
    // Restyle only when the colour changes: this runs on every position
    // tick, and setStyleSheet re-polishes the widget each time.
    if (color != painted_color_) {
        painted_color_ = color;
        setStyleSheet(QString("QPushButton { color:%1; font-size:12px; font-weight:700; letter-spacing:1px; "
                              "background:transparent; border:0; padding:0 2px; }"
                              "QPushButton:hover { text-decoration:underline; }")
                          .arg(color));
    }
}

} // namespace finterm::screens

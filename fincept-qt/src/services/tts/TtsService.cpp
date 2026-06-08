#include "services/tts/TtsService.h"

#include "core/logging/Logger.h"
#include "storage/repositories/SettingsRepository.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fincept::services {

namespace {

constexpr const char* kTtsTag = "TtsService";

QString get_setting(const QString& key) {
    auto r = SettingsRepository::instance().get(key);
    return r.is_err() ? QString() : r.value().trimmed();
}

/// Where does piper_tts.py live?  Try (in order):
///   1. <app dir>/scripts/voice/piper_tts.py   (installed)
///   2. <app dir>/../fincept-qt/scripts/voice/piper_tts.py (dev build)
/// Falls back to empty string when not found — caller surfaces.
QString resolve_script_path() {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        app + "/scripts/voice/piper_tts.py",
        app + "/../fincept-qt/scripts/voice/piper_tts.py",
    };
    for (const auto& p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

/// The Piper voice .onnx to use. Prefers the explicit `tts.model_path` setting;
/// otherwise auto-discovers a voice provisioned by `hearth voice` / `hearth
/// setup` so the user doesn't have to configure a path by hand.
QString resolve_model_path() {
    const QString configured = get_setting("tts.model_path");
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;
    const QString home = QDir::homePath();
    for (const QString& d : {home + "/.local/share/finterm/piper", home + "/.hearth/voices"}) {
        const QStringList found = QDir(d).entryList(QStringList{"*.onnx"}, QDir::Files, QDir::Name);
        if (!found.isEmpty())
            return QDir(d).filePath(found.first());
    }
    return {};
}

/// piper is on PATH (incl. ~/.local/bin, where `hearth voice` symlinks it).
bool piper_on_path() {
    const QString lbin = QDir::homePath() + "/.local/bin";
    return !QStandardPaths::findExecutable("piper").isEmpty()
           || !QStandardPaths::findExecutable("piper", {lbin}).isEmpty()
           || !QStandardPaths::findExecutable("piper-tts").isEmpty()
           || !QStandardPaths::findExecutable("piper-tts", {lbin}).isEmpty();
}

QString new_temp_wav_path() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QString("%1/finterm-tts-%2.wav").arg(dir, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

/// A raw-PCM audio sink for low-latency streaming playback. When present, speak()
/// streams piper straight to the speakers (audio in ~1-2s) instead of
/// synthesising the whole clip to a WAV first (~20s for a long article).
bool stream_sink_available() {
    const QString lbin = QDir::homePath() + "/.local/bin";
    for (const char* p : {"paplay", "pw-play", "aplay"}) {
        if (!QStandardPaths::findExecutable(p).isEmpty() ||
            !QStandardPaths::findExecutable(p, {lbin}).isEmpty())
            return true;
    }
    return false;
}

/// Tear down `p` AND all its descendants. The TTS subprocess is python3 spawning
/// bash → piper → player; signalling only python3 would orphan the audio
/// pipeline. speak() makes python3 a process-group leader (setsid), so here we
/// signal the whole group. Falls back to plain terminate/kill off-Unix.
void kill_process_group(QProcess* p) {
#if defined(Q_OS_UNIX)
    const qint64 pid = p->processId();
    if (pid > 0) {
        ::kill(static_cast<pid_t>(-pid), SIGTERM);  // negative pid = the process group
        // SIGTERM reaps piper/player promptly; keep the GUI-thread block close to
        // the old ~500ms (stop() is called synchronously from the button + aboutToQuit).
        if (!p->waitForFinished(400)) {
            ::kill(static_cast<pid_t>(-pid), SIGKILL);
            p->waitForFinished(150);
        }
        return;
    }
#endif
    p->terminate();
    if (!p->waitForFinished(400))
        p->kill();
}

} // anonymous namespace

TtsService& TtsService::instance() {
    static TtsService s;
    return s;
}

TtsService::TtsService() {
    // Hook shutdown into aboutToQuit so the QProcess + QMediaPlayer
    // tear down while the event loop is still alive.  The destructor
    // of this function-local static runs after main() returns and
    // after QApplication is gone — calling Qt at that point is
    // unsafe.
    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &TtsService::stop);
    }
}

TtsService::~TtsService() {
    // Best-effort: aboutToQuit should have already run, so proc_ /
    // player_ are nullptr.  If somehow not, this is a no-op-friendly
    // path since stop() handles null members.
    cleanup_temp_file();
}

bool TtsService::is_available() const {
    // Called once per chat-message bubble render (AiChatScreen). The probe walks
    // PATH and scans voice dirs, so cache the result for a short TTL — the inputs
    // (installed binary, provisioned voice, settings) don't change second-to-
    // second, and a 2s TTL still reflects a Settings → Voice change promptly.
    if (avail_cache_age_.isValid() && avail_cache_age_.elapsed() < kAvailCacheTtlMs)
        return avail_cache_;
    avail_cache_ = compute_available();
    avail_cache_age_.restart();
    return avail_cache_;
}

bool TtsService::compute_available() const {
    const QString provider = get_setting("tts.provider");
    if (provider == QStringLiteral("none"))
        return false;  // explicitly disabled
    // Unset or "piper" → auto-detect a provisioned voice + the piper runtime.
    if (!provider.isEmpty() && provider != QStringLiteral("piper"))
        return false;
    if (resolve_model_path().isEmpty())
        return false;
    if (resolve_script_path().isEmpty())
        return false;
    if (!piper_on_path())
        return false;
    return true;
}

bool TtsService::speak(const QString& text) {
    if (text.trimmed().isEmpty())
        return false;

    // Cancel any in-flight utterance + cleanup before starting new
    // one.  The user clicking 🔊 twice should mean "just say the new
    // one", not "play both overlapping".
    stop();

    if (!is_available()) {
        emit error(QStringLiteral("TTS not available — configure Piper under Settings → Voice"));
        return false;
    }

    // Resolve the voice once and reuse it for the env var + the log line. Guards
    // the TOCTOU where the .onnx vanished between is_available() and here — an
    // empty FINCEPT_TTS_MODEL would otherwise launch piper with no model.
    const QString model = resolve_model_path();
    if (model.isEmpty()) {
        emit error(QStringLiteral("TTS voice not found — run `hearth voice` to provision one"));
        return false;
    }

    const QString script = resolve_script_path();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Ensure ~/.local/bin (where `hearth voice` symlinks piper) is on PATH so
    // piper_tts.py's shutil.which("piper") finds it even under a desktop launch.
    const QString lbin = QDir::homePath() + "/.local/bin";
    const QString cur_path = env.value("PATH");
    if (cur_path.isEmpty())
        env.insert("PATH", lbin);
    else if (!cur_path.split(QLatin1Char(':')).contains(lbin))
        env.insert("PATH", lbin + QLatin1Char(':') + cur_path);
    env.insert("FINCEPT_TTS_MODEL", model);
    const QString length = get_setting("tts.length");
    if (!length.isEmpty())
        env.insert("FINCEPT_TTS_LENGTH", length);
    const QString noise = get_setting("tts.noise");
    if (!noise.isEmpty())
        env.insert("FINCEPT_TTS_NOISE", noise);

    proc_ = new QProcess(this);
    proc_->setProcessEnvironment(env);
    proc_->setProgram("python3");
#if defined(Q_OS_UNIX)
    // Run python3 as its own session/group leader so stop() can group-kill the
    // whole piper→player pipeline (its grandchildren) atomically — signalling
    // only python3 would orphan the audio.
    proc_->setChildProcessModifier([] { ::setsid(); });
#endif

    // Prefer streaming: pipe piper straight to the speakers so audio starts in
    // ~1-2s. Fall back to synthesise-to-WAV + QMediaPlayer when no raw sink is
    // available (e.g. a headless/no-PulseAudio host).
    stream_mode_ = stream_sink_available();
    stream_started_ = false;
    if (stream_mode_) {
        proc_->setArguments({script, "--stream"});
        stream_out_.clear();
        // Flip to "speaking" the moment piper reports the pipeline launched (the
        // "streaming" marker). We keep draining + accumulating stdout afterwards so
        // a later fatal line isn't lost — on a fast failure the marker and the fatal
        // line can arrive in one readyRead burst; on_proc_finished needs both.
        connect(proc_, &QProcess::readyReadStandardOutput, this, [this]() {
            if (!proc_)
                return;
            stream_out_ += proc_->readAllStandardOutput();
            if (!stream_started_ && stream_out_.contains("\"streaming\"")) {
                stream_started_ = true;
                emit state_changed(true);
            }
        });
    } else {
        pending_wav_path_ = new_temp_wav_path();
        proc_->setArguments({script, "--output", pending_wav_path_});
    }

    connect(proc_, &QProcess::finished, this,
            [this](int exit_code, QProcess::ExitStatus) { on_proc_finished(exit_code); });

    LOG_INFO(kTtsTag, QString("speaking via Piper (model=%1, %2 chars, mode=%3)")
                          .arg(model, QString::number(text.size()), stream_mode_ ? "stream" : "buffered"));
    proc_->start();
    if (!proc_->waitForStarted(2000)) {
        emit error(QStringLiteral("Failed to start piper_tts.py: %1").arg(proc_->errorString()));
        proc_->deleteLater();
        proc_ = nullptr;
        stream_mode_ = false;
        return false;
    }
    proc_->write(text.toUtf8());
    proc_->closeWriteChannel();
    // NB: state_changed(true) is intentionally NOT emitted here. In BUFFERED mode
    // synthesis takes ~20s; signalling "speaking" now would flip the UI to "STOP"
    // while still silent. It's emitted from on_proc_finished() at PlayingState
    // (buffered) or from the readyRead handler above on the "streaming" marker
    // (streaming, ~1-2s). Callers show a "preparing" state in between. We return
    // true so the caller can show that without racing the internal stop() above.
    return true;
}

void TtsService::stop() {
    if (proc_) {
        // Disconnect first so on_proc_finished doesn't fire and try to play the
        // (possibly half-written) WAV. Group-kill so the streaming pipeline's
        // grandchildren (piper/player) die too — not just python3.
        disconnect(proc_, nullptr, this, nullptr);
        kill_process_group(proc_);
        proc_->deleteLater();
        proc_ = nullptr;
    }
    if (player_) {
        // Disconnect first (like proc_ above): on a synchronous multimedia
        // backend, player_->stop() can re-enter the playbackStateChanged
        // handler → teardown_playback() nulls player_ before the deleteLater()
        // here runs — a use-after-free + a duplicate state_changed(false).
        disconnect(player_, nullptr, this, nullptr);
        player_->stop();
        player_->deleteLater();
        player_ = nullptr;
    }
    if (audio_) {
        audio_->deleteLater();
        audio_ = nullptr;
    }
    stream_mode_ = false;
    stream_started_ = false;
    stream_out_.clear();
    cleanup_temp_file();
    emit state_changed(false);
}

void TtsService::on_proc_finished(int exit_code) {
    if (!proc_)
        return;
    const QByteArray out = proc_->readAllStandardOutput();
    proc_->deleteLater();
    proc_ = nullptr;

    // Streaming mode: the process WAS the playback (piper → speaker pipeline),
    // there is no WAV or QMediaPlayer. Its exit is the end of playback.
    if (stream_mode_) {
        stream_mode_ = false;
        stream_started_ = false;
        if (exit_code != 0) {
            // Diagnostics: the readyRead handler already drained most stdout into
            // stream_out_; `out` catches anything written after the last burst.
            const QByteArray diag = stream_out_ + out;
            emit error(QStringLiteral("TTS streaming exited %1: %2")
                           .arg(exit_code)
                           .arg(QString::fromUtf8(diag).trimmed().left(300)));
        }
        stream_out_.clear();
        emit state_changed(false);
        return;
    }

    if (exit_code != 0) {
        emit error(QStringLiteral("piper_tts.py exited %1: %2").arg(exit_code).arg(QString::fromUtf8(out).left(300)));
        cleanup_temp_file();
        emit state_changed(false);
        return;
    }
    if (!QFileInfo::exists(pending_wav_path_)) {
        emit error(QStringLiteral("piper_tts.py succeeded but WAV not found"));
        emit state_changed(false);
        return;
    }

    // Hand the WAV to QMediaPlayer.  Lives on this QObject so the
    // parent's destruction sweeps it.
    player_ = new QMediaPlayer(this);
    audio_ = new QAudioOutput(this);
    player_->setAudioOutput(audio_);
    connect(player_, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState s) {
                if (s == QMediaPlayer::PlayingState)
                    emit state_changed(true);  // audio actually started (synthesis done)
                else if (s == QMediaPlayer::StoppedState)
                    teardown_playback();
            });
    // A decode/output failure (corrupt WAV, no audio device, busy sink) makes
    // QMediaPlayer emit errorOccurred WITHOUT a PlayingState→StoppedState pair,
    // so without this the UI would hang on "preparing" forever. Surface it and
    // reset state.
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error err, const QString& msg) {
                if (err == QMediaPlayer::NoError)
                    return;
                emit error(QStringLiteral("Audio playback failed: %1").arg(msg));
                teardown_playback();
            });
    player_->setSource(QUrl::fromLocalFile(pending_wav_path_));
    player_->play();
}

void TtsService::teardown_playback() {
    // Idempotent: the StoppedState and errorOccurred handlers can both fire;
    // whichever runs first does the teardown + signal, the second is a no-op.
    if (!player_ && !audio_ && pending_wav_path_.isEmpty())
        return;
    cleanup_temp_file();
    // Drop the player + audio output too — leaving stale pointers around
    // invites use-after-free on the next speak() call's stop() codepath.
    if (player_) {
        player_->deleteLater();
        player_ = nullptr;
    }
    if (audio_) {
        audio_->deleteLater();
        audio_ = nullptr;
    }
    emit state_changed(false);
}

void TtsService::cleanup_temp_file() {
    if (pending_wav_path_.isEmpty())
        return;
    QFile::remove(pending_wav_path_);
    pending_wav_path_.clear();
}

} // namespace fincept::services

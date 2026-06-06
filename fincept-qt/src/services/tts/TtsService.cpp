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

void TtsService::speak(const QString& text) {
    if (text.trimmed().isEmpty())
        return;

    // Cancel any in-flight utterance + cleanup before starting new
    // one.  The user clicking 🔊 twice should mean "just say the new
    // one", not "play both overlapping".
    stop();

    if (!is_available()) {
        emit error(QStringLiteral("TTS not available — configure Piper under Settings → Voice"));
        return;
    }

    const QString script = resolve_script_path();
    const QString wav_path = new_temp_wav_path();
    pending_wav_path_ = wav_path;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Ensure ~/.local/bin (where `hearth voice` symlinks piper) is on PATH so
    // piper_tts.py's shutil.which("piper") finds it even under a desktop launch.
    const QString lbin = QDir::homePath() + "/.local/bin";
    const QString cur_path = env.value("PATH");
    if (cur_path.isEmpty())
        env.insert("PATH", lbin);
    else if (!cur_path.split(QLatin1Char(':')).contains(lbin))
        env.insert("PATH", lbin + QLatin1Char(':') + cur_path);
    env.insert("FINCEPT_TTS_MODEL", resolve_model_path());
    const QString length = get_setting("tts.length");
    if (!length.isEmpty())
        env.insert("FINCEPT_TTS_LENGTH", length);
    const QString noise = get_setting("tts.noise");
    if (!noise.isEmpty())
        env.insert("FINCEPT_TTS_NOISE", noise);

    proc_ = new QProcess(this);
    proc_->setProcessEnvironment(env);
    proc_->setProgram("python3");
    proc_->setArguments({script, "--output", wav_path});

    connect(proc_, &QProcess::finished, this,
            [this](int exit_code, QProcess::ExitStatus) { on_proc_finished(exit_code); });

    LOG_INFO(kTtsTag, QString("speaking via Piper (model=%1, %2 chars)")
                          .arg(resolve_model_path(), QString::number(text.size())));
    proc_->start();
    if (!proc_->waitForStarted(2000)) {
        emit error(QStringLiteral("Failed to start piper_tts.py: %1").arg(proc_->errorString()));
        proc_->deleteLater();
        proc_ = nullptr;
        return;
    }
    proc_->write(text.toUtf8());
    proc_->closeWriteChannel();
    emit state_changed(true);
}

void TtsService::stop() {
    if (proc_) {
        // Disconnect first so on_proc_finished doesn't fire and try
        // to play the (possibly half-written) WAV.
        disconnect(proc_, nullptr, this, nullptr);
        proc_->terminate();
        if (!proc_->waitForFinished(500))
            proc_->kill();
        proc_->deleteLater();
        proc_ = nullptr;
    }
    if (player_) {
        player_->stop();
        player_->deleteLater();
        player_ = nullptr;
    }
    if (audio_) {
        audio_->deleteLater();
        audio_ = nullptr;
    }
    cleanup_temp_file();
    emit state_changed(false);
}

void TtsService::on_proc_finished(int exit_code) {
    if (!proc_)
        return;
    const QByteArray out = proc_->readAllStandardOutput();
    proc_->deleteLater();
    proc_ = nullptr;

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
                if (s == QMediaPlayer::StoppedState) {
                    cleanup_temp_file();
                    // Drop the player + audio output too — leaving
                    // stale pointers around invites use-after-free
                    // on the next speak() call's stop() codepath.
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
            });
    player_->setSource(QUrl::fromLocalFile(pending_wav_path_));
    player_->play();
}

void TtsService::cleanup_temp_file() {
    if (pending_wav_path_.isEmpty())
        return;
    QFile::remove(pending_wav_path_);
    pending_wav_path_.clear();
}

} // namespace fincept::services

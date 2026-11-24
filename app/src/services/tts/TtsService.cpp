#include "services/tts/TtsService.h"

#include "services/tts/SpeechText.h"

#include "ai_chat/HearthService.h"
#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "storage/repositories/SettingsRepository.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <limits>

#if defined(Q_OS_UNIX)
#    include <csignal>
#    include <sys/types.h>
#    include <unistd.h>
#endif

namespace finterm::services {

namespace {

constexpr const char* kTtsTag = "TtsService";

QString get_setting(const QString& key) {
    auto r = SettingsRepository::instance().get(key);
    return r.is_err() ? QString() : r.value().trimmed();
}

/// Where does piper_tts.py live?  Try (in order):
///   1. <app dir>/scripts/voice/piper_tts.py   (installed)
///   2. <app dir>/../app/scripts/voice/piper_tts.py (dev build)
/// Falls back to empty string when not found — caller surfaces.
QString resolve_script_path() {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        app + "/scripts/voice/piper_tts.py",
        app + "/../app/scripts/voice/piper_tts.py",
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
    // hearth's own folder, then where earlier versions of it put them.
    for (const QString& d :
         {HearthService::install_dir() + "/voices/piper", home + "/.local/share/finterm/piper", home + "/.hearth/voices"}) {
        const QStringList found = QDir(d).entryList(QStringList{"*.onnx"}, QDir::Files, QDir::Name);
        if (!found.isEmpty())
            return QDir(d).filePath(found.first());
    }
    return {};
}

/// piper is on PATH (incl. ~/.local/bin, where `hearth voice` symlinks it).
bool piper_on_path() {
    const QString lbin = QDir::homePath() + "/.local/bin";
    return !QStandardPaths::findExecutable("piper").isEmpty() ||
           !QStandardPaths::findExecutable("piper", {lbin}).isEmpty() ||
           !QStandardPaths::findExecutable("piper-tts").isEmpty() ||
           !QStandardPaths::findExecutable("piper-tts", {lbin}).isEmpty();
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
        if (!QStandardPaths::findExecutable(p).isEmpty() || !QStandardPaths::findExecutable(p, {lbin}).isEmpty())
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
        ::kill(static_cast<pid_t>(-pid), SIGTERM); // negative pid = the process group
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
        connect(app, &QCoreApplication::aboutToQuit, this, &TtsService::discard_warm);
    }
}

TtsService::~TtsService() {
    // Best-effort: aboutToQuit should have already run, so proc_ /
    // player_ are nullptr.  If somehow not, this is a no-op-friendly
    // path since stop() handles null members.
    cleanup_temp_file();
}

bool TtsService::is_available() const {
    // Called once per chat-message bubble render. The probe walks
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
        return false; // explicitly disabled
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

bool TtsService::engine_or_local_available() const {
    if (get_setting("tts.provider") == QStringLiteral("none"))
        return false; // the user switched read-aloud off — engine included
    const auto ep = ai_chat::LlmService::instance().speech_endpoint();
    return (ep && engine_usable(ep->url)) || is_available();
}

namespace {
/// Chunked synthesis at the engine's own ramp (SpeechText.h has the rules).
QStringList split_for_speech(const QString& text, const ai_chat::LlmService::SpeechEndpoint& ep) {
    return speech::split_for_speech(text, ep.first_chunk, ep.second_chunk, ep.rest_chunk, ep.max_input);
}

/// What a synthesised clip depends on: where it was made, with which
/// model/voice, saying what. A warm clip is only reused on an exact match, so
/// a settings change between prewarm and click cannot splice voices.
QString warm_key(const QString& url, const QJsonObject& body, const QString& chunk) {
    return url + QChar(0x1f) + QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)) + QChar(0x1f) +
           chunk;
}

QNetworkReply* post_speech(QNetworkAccessManager* nam, const QString& url, const QMap<QString, QString>& headers,
                           QJsonObject body, const QString& input) {
    QNetworkRequest req{QUrl(url)};
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    // Chunks are short (≤ ~900 chars ≈ 5s of CPU synthesis), but the first
    // request to a freshly started hearth also loads the Piper voice. 60s is
    // hearth's own synth ceiling; past it the request is dead anyway.
    req.setTransferTimeout(60000);
    body["input"] = input;
    return nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

/// A finished /audio/speech reply → a temp WAV path, or empty with `why` set.
QString wav_from_reply(QNetworkReply* r, QString* why, int* status_out) {
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    *status_out = status;
    const QByteArray data = r->readAll();
    // RIFF = a WAV. Anything else (an error JSON with a 200, an mp3 from a
    // provider ignoring response_format) is not something we asked for.
    if (r->error() != QNetworkReply::NoError || status != 200 || !data.startsWith("RIFF")) {
        // status is 0 when nothing answered (refused, timed out) — say that.
        *why = status ? QStringLiteral("HTTP %1").arg(status) : r->errorString();
        LOG_WARN(kTtsTag, QString("engine speech failed (%1): %2").arg(*why, QString::fromUtf8(data.left(200))));
        return {};
    }
    const QString path = new_temp_wav_path();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
        f.close();
        QFile::remove(path);
        *why = QStringLiteral("could not write the audio to a temp file");
        return {};
    }
    return path;
}
} // namespace

void TtsService::prewarm_engine(const QString& text) {
    if (text.trimmed().isEmpty())
        return;
    if (get_setting("tts.provider") == QStringLiteral("none"))
        return;
    const auto ep = ai_chat::LlmService::instance().speech_endpoint();
    // Local engines only: on a paid API this would bill every brief nobody
    // listens to.
    if (!ep || !ep->local || !engine_usable(ep->url))
        return;
    const QStringList chunks = split_for_speech(text, *ep);
    // Keyed on the FIRST CHUNK, not the whole text: a prewarm from the brief's
    // top half (breakdown still generating) must match the finished brief.
    if (chunks.isEmpty())
        return;
    const QString key = warm_key(ep->url, ep->body, chunks.first());
    if (key == warm_text_)
        return;
    discard_warm();
    if (!nam_)
        nam_ = new QNetworkAccessManager(this);
    warm_text_ = key;
    LOG_INFO(kTtsTag, QString("prewarming first chunk (%1 chars)").arg(chunks.first().size()));
    warm_reply_ = post_speech(nam_, ep->url, ep->headers, ep->body, chunks.first());
    connect(warm_reply_, &QNetworkReply::finished, this, [this, url = ep->url]() {
        QNetworkReply* r = warm_reply_;
        if (!r)
            return;
        warm_reply_ = nullptr;
        r->deleteLater();
        QString why;
        int status = 0;
        const QString path = wav_from_reply(r, &why, &status);
        // The prewarm doubles as the probe: a dead engine is found here, before
        // the brief renders, so its speaker never appears.
        if (path.isEmpty())
            mark_engine_failed(url, status);
        if (!path.isEmpty())
            warm_wav_ = path;
        else
            warm_text_.clear(); // no clip — a later speak fetches normally
        if (!eng_waiting_warm_)
            return;
        // A reading of this text was started while the clip synthesised and
        // is waiting on it as its chunk 0.
        eng_waiting_warm_ = false;
        if (!eng_session_)
            return;
        if (!path.isEmpty() && adopt_warm_clip())
            return;
        engine_fetch_next(); // fetch chunk 0 the normal way (its failure falls back to Piper)
    });
}

bool TtsService::adopt_warm_clip() {
    // Copy, so stopping and replaying the same brief is instant again.
    const QString copy = new_temp_wav_path();
    if (warm_wav_.isEmpty() || !QFile::copy(warm_wav_, copy))
        return false;
    eng_ready_ << copy;
    eng_next_ = 1;
    if (!player_)
        engine_play_next();
    engine_fetch_next();
    return true;
}

void TtsService::discard_warm() {
    if (warm_reply_) {
        QNetworkReply* r = warm_reply_;
        warm_reply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    if (!warm_wav_.isEmpty())
        QFile::remove(warm_wav_);
    warm_wav_.clear();
    warm_text_.clear();
    if (eng_waiting_warm_) {
        // A reading was waiting on the clip just thrown away: fetch its chunk
        // 0 the normal way rather than leave it on PREP… forever.
        eng_waiting_warm_ = false;
        engine_fetch_next();
    }
}

void TtsService::set_speaking(bool speaking) {
    if (speaking) {
        if (announced_)
            return; // later engine chunks: the reading is already "speaking"
        announced_ = true;
    } else {
        announced_ = false;
        if (!restarting_)
            set_reading(false); // every reading's end passes through here
    }
    emit state_changed(speaking);
    emit session_state(session_, speaking);
}

void TtsService::set_reading(bool reading) {
    if (reading_ == reading)
        return;
    reading_ = reading;
    emit reading_changed(reading);
}

void TtsService::begin_reading() {
    restarting_ = true;
    stop();
    restarting_ = false;
    ++session_;
    // reading_ is NOT raised here: a launch that fails synchronously must not
    // flash every TV mute. Each successful launch calls set_reading(true).
}

void TtsService::fail(const QString& message) {
    emit error(message);
    emit session_error(session_, message);
}

bool TtsService::engine_usable(const QString& url) const {
    const auto it = engine_unusable_until_.constFind(url);
    return it == engine_unusable_until_.cend() || QDateTime::currentMSecsSinceEpoch() >= *it;
}

void TtsService::mark_engine_failed(const QString& url, int http_status) {
    if (http_status == 404) {
        // No speech route on this server (bare Ollama, LM Studio, a proxy): it
        // will not grow one mid-run.
        engine_unusable_until_[url] = std::numeric_limits<qint64>::max();
        LOG_WARN(kTtsTag, QString("%1 has no speech route — using the local voice from now on").arg(url));
    } else if (http_status == 0) {
        // Nothing answered (refused / timed out): hearth may be down or still
        // starting. Retry after a minute rather than on every click.
        engine_unusable_until_[url] = QDateTime::currentMSecsSinceEpoch() + 60000;
    }
    // A 5xx is a failed synthesis, not a missing engine — keep trying.
    if (http_status == 404 || http_status == 0)
        emit availability_changed();
}

bool TtsService::speak_via_engine(const QString& text) {
    if (text.trimmed().isEmpty())
        return false;
    begin_reading();

    // A synchronous `return false` emits no state_changed(false), so it ends
    // the previous reading's hold itself — BEFORE fail(): an error slot that
    // starts a new reading must not have it cleared afterwards.
    if (get_setting("tts.provider") == QStringLiteral("none")) {
        set_reading(false);
        fail(QStringLiteral("Read-aloud is switched off (Settings → Voice)"));
        return false;
    }
    const auto ep = ai_chat::LlmService::instance().speech_endpoint();
    if (!ep || !engine_usable(ep->url))
        return start_piper(text); // no (working) speech route: Anthropic, Gemini, a dead hearth, …

    eng_chunks_ = split_for_speech(text, *ep);
    if (eng_chunks_.isEmpty()) {
        set_reading(false);
        return false;
    }
    set_reading(true); // launched: every exit below returns true
    eng_url_ = ep->url;
    eng_headers_ = ep->headers;
    eng_body_ = ep->body;
    eng_text_ = text;
    eng_next_ = 0;
    eng_session_ = true;
    const bool warm = !warm_text_.isEmpty() && warm_key(eng_url_, eng_body_, eng_chunks_.first()) == warm_text_;
    LOG_INFO(kTtsTag, QString("speaking via engine %1 (%2 chars, %3 chunks, first chunk %4)")
                          .arg(ep->url)
                          .arg(text.size())
                          .arg(eng_chunks_.size())
                          .arg(!warm         ? "cold"
                               : warm_reply_ ? "synthesising"
                                             : "ready"));
    if (warm && warm_reply_) {
        eng_waiting_warm_ = true; // its finished handler starts the reading
        return true;
    }
    if (warm && adopt_warm_clip())
        return true;
    engine_fetch_next();
    return true;
}

void TtsService::engine_fetch_next() {
    // One request at a time, at most two clips buffered ahead of the speaker.
    if (!eng_session_ || eng_waiting_warm_ || reply_ || eng_next_ >= eng_chunks_.size() || eng_ready_.size() >= 2)
        return;
    if (!nam_)
        nam_ = new QNetworkAccessManager(this);
    reply_ = post_speech(nam_, eng_url_, eng_headers_, eng_body_, eng_chunks_.at(eng_next_));
    eng_fetch_timer_.start();
    connect(reply_, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* r = reply_;
        if (!r)
            return;
        reply_ = nullptr;
        r->deleteLater();
        QString why;
        int status = 0;
        const QString path = wav_from_reply(r, &why, &status);
        if (path.isEmpty()) {
            mark_engine_failed(eng_url_, status);
            if (eng_next_ > 0) {
                // error() FIRST: stop()'s state_changed(false) resets the button,
                // after which it would no longer listen for the reason.
                fail(QStringLiteral("Engine speech stopped part-way (%1)").arg(why));
                stop(); // ends the session + the clip playing
                return;
            }
            // Nothing has played yet: fall back to the on-device voice. Quietly —
            // no stop(): its state_changed(false) would reset the caller's
            // "preparing" button while Piper is about to speak.
            const QString text = eng_text_;
            end_engine_session();
            if (!is_available()) {
                fail(QStringLiteral("Engine speech failed (%1) and no local voice is set up").arg(why));
                set_speaking(false);
                return;
            }
            if (!start_piper(text))
                set_speaking(false);
            return;
        }
        eng_ready_ << path;
        ++eng_next_;
        // A reading only flows while the engine speaks faster than the voice
        // plays. When it does not (a busy machine), the speaker runs out of
        // audio at the join between two clips and the listener hears a long
        // silence there. Nothing here can make the engine faster; the log
        // says it happened and how long the wait was.
        if (!player_ && eng_starved_.isValid()) {
            LOG_WARN(kTtsTag, QStringLiteral("ran out of audio for %1 ms before clip %2 of %3 (it took %4 ms to "
                                             "synthesise) — the speech engine is slower than real time")
                                  .arg(eng_starved_.elapsed())
                                  .arg(eng_next_)
                                  .arg(eng_chunks_.size())
                                  .arg(eng_fetch_timer_.elapsed()));
            eng_starved_.invalidate();
        }
        if (!player_)
            engine_play_next();
        engine_fetch_next();
    });
}

void TtsService::engine_play_next() {
    if (!eng_session_)
        return;
    if (eng_ready_.isEmpty()) {
        // Starved (next clip still synthesising) → wait for its reply. All
        // clips fetched and played → the reading is over.
        if (!reply_ && eng_next_ >= eng_chunks_.size()) {
            end_engine_session();
            set_speaking(false);
        } else if (eng_next_ > 0 && !eng_starved_.isValid()) {
            eng_starved_.start(); // mid-reading with nothing to play: the wait is timed
        }
        return;
    }
    pending_wav_path_ = eng_ready_.takeFirst();
    start_playback();
    engine_fetch_next(); // a buffer slot just freed up
}

void TtsService::end_engine_session() {
    if (reply_) {
        // Disconnect first so abort()'s synchronous finished() doesn't run the
        // chunk handler (fallback / playback) for a reading that has ended.
        QNetworkReply* r = reply_;
        reply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    eng_session_ = false;
    eng_starved_.invalidate();
    eng_waiting_warm_ = false; // a warm request still finishing just stores its clip
    eng_chunks_.clear();
    eng_text_.clear();
    eng_next_ = 0;
    for (const QString& p : std::as_const(eng_ready_))
        QFile::remove(p);
    eng_ready_.clear();
}

bool TtsService::speak(const QString& text) {
    if (text.trimmed().isEmpty())
        return false;

    // Cancel any in-flight utterance + cleanup before starting new
    // one.  The user clicking 🔊 twice should mean "just say the new
    // one", not "play both overlapping".
    begin_reading();
    return start_piper(text);
}

bool TtsService::start_piper(const QString& text) {
    if (!is_available()) {
        set_reading(false); // before fail(): see speak_via_engine
        fail(QStringLiteral("TTS not available — configure Piper under Settings → Voice"));
        return false;
    }

    // Resolve the voice once and reuse it for the env var + the log line. Guards
    // the TOCTOU where the .onnx vanished between is_available() and here — an
    // empty FINTERM_TTS_MODEL would otherwise launch piper with no model.
    const QString model = resolve_model_path();
    if (model.isEmpty()) {
        set_reading(false); // before fail(): see speak_via_engine
        fail(QStringLiteral("TTS voice not found — run `hearth voice` to provision one"));
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
    env.insert("FINTERM_TTS_MODEL", model);
    const QString length = get_setting("tts.length");
    if (!length.isEmpty())
        env.insert("FINTERM_TTS_LENGTH", length);
    const QString noise = get_setting("tts.noise");
    if (!noise.isEmpty())
        env.insert("FINTERM_TTS_NOISE", noise);

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
                set_speaking(true);
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
        set_reading(false); // before fail(): see speak_via_engine
        fail(QStringLiteral("Failed to start piper_tts.py: %1").arg(proc_->errorString()));
        proc_->deleteLater();
        proc_ = nullptr;
        stream_mode_ = false;
        return false;
    }
    // One line: Piper's file mode may treat each stdin line as its own
    // utterance (and output). The pauses survive as the stops SpeechText adds.
    proc_->write(text.simplified().toUtf8());
    proc_->closeWriteChannel();
    // The engine fell back here after the user paused while it prepared:
    // the new streaming pipeline must start held too.
    if (paused_)
        signal_stream(true);
    // NB: state_changed(true) is intentionally NOT emitted here. In BUFFERED mode
    // synthesis takes ~20s; signalling "speaking" now would flip the UI to "STOP"
    // while still silent. It's emitted from on_proc_finished() at PlayingState
    // (buffered) or from the readyRead handler above on the "streaming" marker
    // (streaming, ~1-2s). Callers show a "preparing" state in between. We return
    // true so the caller can show that without racing the internal stop() above.
    set_reading(true); // launched — "preparing" counts (no-op on the engine fallback)
    return true;
}

bool TtsService::pause() {
    if (!reading_ || paused_)
        return false;
    paused_ = true;
    if (player_)
        player_->pause(); // keeps the position; a clip not yet started stays loaded
    // Streaming Piper: what the sound server already buffered (≲1 s) still
    // plays out — the fallback path's limit; the engine path stops dead.
    signal_stream(true);
    LOG_INFO(kTtsTag, QStringLiteral("reading paused"));
    emit reading_changed(false); // is_reading() just turned false: the TV may speak
    emit session_paused(session_, true);
    return true;
}

bool TtsService::resume() {
    if (!paused_)
        return false;
    paused_ = false;
    if (player_)
        player_->play();
    signal_stream(false);
    // Engine reading paused while starved (no clip loaded): nothing to do —
    // the next clip's arrival starts playback, now that paused_ is down.
    LOG_INFO(kTtsTag, QStringLiteral("reading resumed"));
    emit reading_changed(true);
    emit session_paused(session_, false);
    return true;
}

void TtsService::signal_stream(bool hold) {
#if defined(Q_OS_UNIX)
    // The streaming pipeline (python → bash → piper → player) IS the playback:
    // freezing its process group holds synthesis and the sink together, and
    // SIGCONT carries on from the same sample. The buffered path has no
    // process to hold once playing — QMediaPlayer covers it.
    if (proc_ && stream_mode_ && proc_->processId() > 0)
        ::kill(static_cast<pid_t>(-proc_->processId()), hold ? SIGSTOP : SIGCONT);
#else
    Q_UNUSED(hold);
#endif
}

void TtsService::stop() {
    // A frozen pipeline can't act on SIGTERM: thaw it so it dies promptly
    // instead of costing kill_process_group its full wait + SIGKILL.
    const bool was_paused = paused_;
    if (paused_)
        signal_stream(false);
    paused_ = false;
    // A paused reading had given the TV its sound back; one being replaced
    // (begin_reading) keeps reading_ up without set_reading() firing, so
    // re-mute here. An ending one gets set_reading(false) below.
    if (was_paused && restarting_ && reading_)
        emit reading_changed(true);
    end_engine_session(); // also aborts an in-flight chunk request
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
    set_speaking(false);
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
            fail(QStringLiteral("TTS streaming exited %1: %2")
                     .arg(exit_code)
                     .arg(QString::fromUtf8(diag).trimmed().left(300)));
        }
        stream_out_.clear();
        set_speaking(false);
        return;
    }

    if (exit_code != 0) {
        fail(QStringLiteral("piper_tts.py exited %1: %2").arg(exit_code).arg(QString::fromUtf8(out).left(300)));
        cleanup_temp_file();
        set_speaking(false);
        return;
    }
    if (!QFileInfo::exists(pending_wav_path_)) {
        fail(QStringLiteral("piper_tts.py succeeded but WAV not found"));
        set_speaking(false);
        return;
    }

    start_playback();
}

void TtsService::start_playback() {
    // Hand the WAV to QMediaPlayer.  Lives on this QObject so the
    // parent's destruction sweeps it.
    player_ = new QMediaPlayer(this);
    audio_ = new QAudioOutput(this);
    player_->setAudioOutput(audio_);
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        if (s == QMediaPlayer::PlayingState)
            set_speaking(true); // audio actually started (synthesis done)
        else if (s == QMediaPlayer::StoppedState)
            teardown_playback();
    });
    // A decode/output failure (corrupt WAV, no audio device, busy sink) makes
    // QMediaPlayer emit errorOccurred WITHOUT a PlayingState→StoppedState pair,
    // so without this the UI would hang on "preparing" forever. Surface it and
    // reset state.
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error err, const QString& msg) {
        if (err == QMediaPlayer::NoError)
            return;
        // No device / undecodable clip: every later chunk would fail the
        // same way, so the whole reading ends here.
        end_engine_session();
        fail(QStringLiteral("Audio playback failed: %1").arg(msg));
        teardown_playback();
    });
    player_->setSource(QUrl::fromLocalFile(pending_wav_path_));
    // Paused before the audio arrived: load it and hold, so resume() starts it.
    if (paused_)
        player_->pause();
    else
        player_->play();
}

void TtsService::teardown_playback() {
    // Idempotent: the StoppedState and errorOccurred handlers can both fire;
    // whichever runs first does the teardown + signal, the second is a no-op.
    if (!player_ && !audio_ && pending_wav_path_.isEmpty())
        return;
    cleanup_temp_file();
    // Engine reading with the next clip already synthesised: keep the SAME
    // player and audio output and just swap the source — re-opening the audio
    // device per chunk is an audible seam.
    if (eng_session_ && player_ && !eng_ready_.isEmpty()) {
        pending_wav_path_ = eng_ready_.takeFirst();
        player_->setSource(QUrl::fromLocalFile(pending_wav_path_));
        if (paused_)
            player_->pause();
        else
            player_->play();
        engine_fetch_next(); // a buffer slot just freed up
        return;
    }
    // Drop the player + audio output too — leaving stale pointers around
    // invites use-after-free on the next speak() call's stop() codepath.
    if (player_) {
        // Disconnect: a late signal from this dying player must not tear down
        // the NEXT chunk's player, which engine_play_next() creates below.
        disconnect(player_, nullptr, this, nullptr);
        player_->deleteLater();
        player_ = nullptr;
    }
    if (audio_) {
        audio_->deleteLater();
        audio_ = nullptr;
    }
    if (eng_session_) {
        engine_play_next(); // starved: waits for the next clip; or ends the reading
        return;
    }
    set_speaking(false);
}

void TtsService::cleanup_temp_file() {
    if (pending_wav_path_.isEmpty())
        return;
    QFile::remove(pending_wav_path_);
    pending_wav_path_.clear();
}

// ── Voice catalogue ─────────────────────────────────────────────────────────

QString TtsService::engine_voice_key() const {
    const auto ep = ai_chat::LlmService::instance().speech_endpoint();
    if (!ep)
        return {};
    const QString voice = ep->body.value("voice").toString();
    // OpenAI voices all belong to one model; the key's model part is the family.
    return ep->kind == QLatin1String("openai") ? QStringLiteral("openai:") + voice
                                               : ep->body.value("model").toString() + QLatin1Char(':') + voice;
}

void TtsService::set_engine_voice(const QString& key) {
    if (key.isEmpty() || key == engine_voice_key())
        return;
    auto r = SettingsRepository::instance().set(QStringLiteral("tts.engine_voice"), key, QStringLiteral("voice"));
    if (r.is_err()) {
        LOG_WARN(kTtsTag, QString("could not save the read-aloud voice: %1").arg(QString::fromStdString(r.error())));
        return;
    }
    // No discard_warm(): warm_key() includes the request body, so a clip in the
    // old voice is never reused — and a reading may be waiting on that request.
    LOG_INFO(kTtsTag, QString("read-aloud voice → %1").arg(key));
    emit engine_voices_changed();
}

void TtsService::drop_voices_request() {
    if (QNetworkReply* r = voices_reply_) {
        voices_reply_ = nullptr; // first: the finished handler ignores a reply that isn't current
        r->abort();
    }
}

void TtsService::refresh_engine_voices() {
    const auto ep = ai_chat::LlmService::instance().speech_endpoint();
    if (!ep) {
        drop_voices_request();
        if (!voices_.isEmpty() || !voices_url_.isEmpty()) {
            voices_.clear();
            voices_url_.clear();
            voices_unlisted_ = false;
            emit engine_voices_changed();
        }
        return;
    }
    if (ep->kind == QLatin1String("openai")) {
        drop_voices_request();
        if (voices_url_ == ep->url)
            return; // fixed set, already shown
        // OpenAI's set is fixed; softer voices first.
        static const QVector<EngineVoice> kOpenAi = {
            {"openai", "shimmer", "Shimmer — soft, warm"},
            {"openai", "nova", "Nova — bright, friendly"},
            {"openai", "coral", "Coral — warm, conversational"},
            {"openai", "sage", "Sage — calm"},
            {"openai", "alloy", "Alloy — neutral"},
            {"openai", "ash", "Ash — male"},
        };
        voices_ = kOpenAi;
        voices_url_ = ep->url;
        voices_unlisted_ = false;
        emit engine_voices_changed();
        return;
    }
    // hearth: GET /v1/audio/voices next to /v1/audio/speech. A list for the
    // same engine younger than a minute is reused — the menu calls this on
    // every open. A request still in flight for ANOTHER engine (the provider
    // changed) is dropped, so its late answer can't overwrite this one's.
    QString url = ep->url;
    url.replace(QStringLiteral("/audio/speech"), QStringLiteral("/audio/voices"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (voices_reply_ && voices_reply_url_ == url)
        return;
    if (url == voices_url_ && now - voices_at_ < 60'000)
        return;
    drop_voices_request();
    if (!nam_)
        nam_ = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(url)};
    for (auto it = ep->headers.cbegin(); it != ep->headers.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    req.setTransferTimeout(5000);
    QNetworkReply* reply = nam_->get(req);
    voices_reply_ = reply;
    voices_reply_url_ = url;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        if (reply != voices_reply_)
            return; // dropped: superseded by another engine's request
        voices_reply_ = nullptr;
        QVector<EngineVoice> list;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200;
        if (ok) {
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).object().value("voices").toArray();
            for (const auto& v : arr) {
                const QJsonObject o = v.toObject();
                const QString id = o.value("id").toString();
                const QString model = o.value("model").toString();
                // A voice without a model can't be asked for by name.
                if (!id.isEmpty() && !model.isEmpty())
                    list.append({model, id, o.value("label").toString(id)});
            }
        } else {
            LOG_WARN(
                kTtsTag,
                QString("voice list unavailable (%1)").arg(status ? QString::number(status) : reply->errorString()));
        }
        // No list (a hearth from before /audio/voices answers 404) → nothing
        // is offered rather than a guess at what the engine really speaks.
        voices_ = list;
        voices_url_ = url;
        voices_at_ = ok ? QDateTime::currentMSecsSinceEpoch() : 0; // a failure retries on the next open
        voices_unlisted_ = !ok;
        emit engine_voices_changed();
    });
}

} // namespace finterm::services

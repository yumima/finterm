#include "services/tts/TtsService.h"

#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "storage/repositories/SettingsRepository.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMediaPlayer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QDateTime>

#include <limits>

#include <algorithm>

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

bool TtsService::engine_or_local_available() const {
    if (get_setting("tts.provider") == QStringLiteral("none"))
        return false;  // the user switched read-aloud off — engine included
    const auto ep = ai_chat::LlmService::instance().speech_endpoint();
    return (ep && engine_usable(ep->url)) || is_available();
}

namespace {
/// Split text for chunked synthesis on sentence ends. The FIRST chunk is kept
/// short so audio starts within a couple of seconds; later chunks are larger
/// (fewer seams) and are synthesised while the previous one plays. No chunk
/// exceeds `cap`, the engine's input limit — so a long brief is read in full
/// rather than truncated (OpenAI rejects input past 4096 chars).
QStringList split_for_speech(const QString& text, int cap) {
    // Sizes ramp up: a short first chunk starts the audio fast, a medium second
    // one is synthesised while the first plays (so no gap after it), and the
    // rest are long to keep the seams few.
    constexpr int kFirst = 240, kSecond = 500, kRest = 900;
    // Chunk 0 closes at the first paragraph end only once it holds this much —
    // a one-line opener ("Markets rallied.") alone would play in a second and
    // leave silence while chunk 1 synthesises.
    constexpr int kMinFirst = 80;
    cap = cap > 0 ? cap : 4096;
    static const QRegularExpression kSentenceEnd(QStringLiteral("(?<=[.!?:;])\\s+"));
    // Pieces = sentences, each tagged with whether it ends a paragraph.
    struct Piece { QString text; bool para_end; };
    QList<Piece> pieces;
    for (const QString& para : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList sents = para.split(kSentenceEnd, Qt::SkipEmptyParts);
        for (int i = 0; i < sents.size(); ++i) {
            const QString t = sents[i].simplified();
            if (!t.isEmpty())
                pieces.append({t, false});
        }
        if (!pieces.isEmpty())
            pieces.last().para_end = true;
    }
    QStringList chunks;
    QString cur;
    for (int i = 0; i < pieces.size(); ++i) {
        const int target =
            std::min(chunks.isEmpty() ? kFirst : chunks.size() == 1 ? kSecond : kRest, cap);
        Piece pc = pieces[i];
        // A run-on piece longer than this chunk's target is cut on whitespace
        // and the rest goes back in line — so even a punctuation-free opening
        // yields a short chunk 0 and audio in ~2s.
        if (cur.isEmpty() && pc.text.size() > target) {
            int cut = pc.text.lastIndexOf(QLatin1Char(' '), target);
            if (cut <= 0)
                cut = target;
            pieces.insert(i + 1, {pc.text.mid(cut).trimmed(), pc.para_end});
            pc = {pc.text.left(cut), false};
        }
        if (!cur.isEmpty() && cur.size() + 1 + pc.text.size() > target) {
            chunks << cur;
            cur.clear();
            --i;  // re-place this piece against the next chunk's target
            continue;
        }
        cur = cur.isEmpty() ? pc.text : cur + QLatin1Char(' ') + pc.text;
        // Chunk 0 ends at a paragraph end (or at kFirst), so it depends ONLY on
        // the brief's opening paragraphs: a prewarm from the top half (breakdown
        // still being written) speaks the same chunk 0 as the finished brief.
        if (chunks.isEmpty() && pc.para_end && cur.size() >= kMinFirst) {
            chunks << cur;
            cur.clear();
        }
    }
    if (!cur.isEmpty())
        chunks << cur;
    return chunks;
}

/// What a synthesised clip depends on: where it was made, with which
/// model/voice, saying what. A warm clip is only reused on an exact match, so
/// a settings change between prewarm and click cannot splice voices.
QString warm_key(const QString& url, const QJsonObject& body, const QString& chunk) {
    return url + QChar(0x1f) + QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact))
           + QChar(0x1f) + chunk;
}

QNetworkReply* post_speech(QNetworkAccessManager* nam, const QString& url,
                           const QMap<QString, QString>& headers, QJsonObject body, const QString& input) {
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
    const QStringList chunks = split_for_speech(text, ep->max_input);
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
            warm_text_.clear();  // no clip — a later speak fetches normally
        if (!eng_waiting_warm_)
            return;
        // A reading of this text was started while the clip synthesised and
        // is waiting on it as its chunk 0.
        eng_waiting_warm_ = false;
        if (!eng_session_)
            return;
        if (!path.isEmpty() && adopt_warm_clip())
            return;
        engine_fetch_next();  // fetch chunk 0 the normal way (its failure falls back to Piper)
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
            return;  // later engine chunks: the reading is already "speaking"
        announced_ = true;
    } else {
        announced_ = false;
    }
    emit state_changed(speaking);
    emit session_state(session_, speaking);
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
    stop();
    ++session_;

    if (get_setting("tts.provider") == QStringLiteral("none")) {
        fail(QStringLiteral("Read-aloud is switched off (Settings → Voice)"));
        return false;
    }
    const auto ep = ai_chat::LlmService::instance().speech_endpoint();
    if (!ep || !engine_usable(ep->url))
        return start_piper(text);  // no (working) speech route: Anthropic, Gemini, a dead hearth, …

    eng_chunks_ = split_for_speech(text, ep->max_input);
    if (eng_chunks_.isEmpty())
        return false;
    eng_url_ = ep->url;
    eng_headers_ = ep->headers;
    eng_body_ = ep->body;
    eng_text_ = text;
    eng_next_ = 0;
    eng_session_ = true;
    const bool warm = !warm_text_.isEmpty() && warm_key(eng_url_, eng_body_, eng_chunks_.first()) == warm_text_;
    LOG_INFO(kTtsTag, QString("speaking via engine %1 (%2 chars, %3 chunks, first chunk %4)")
                          .arg(ep->url).arg(text.size()).arg(eng_chunks_.size())
                          .arg(!warm ? "cold" : warm_reply_ ? "synthesising" : "ready"));
    if (warm && warm_reply_) {
        eng_waiting_warm_ = true;  // its finished handler starts the reading
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
                stop();  // ends the session + the clip playing
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
        }
        return;
    }
    pending_wav_path_ = eng_ready_.takeFirst();
    start_playback();
    engine_fetch_next();  // a buffer slot just freed up
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
    eng_waiting_warm_ = false;  // a warm request still finishing just stores its clip
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
    stop();
    ++session_;
    return start_piper(text);
}

bool TtsService::start_piper(const QString& text) {
    if (!is_available()) {
        fail(QStringLiteral("TTS not available — configure Piper under Settings → Voice"));
        return false;
    }

    // Resolve the voice once and reuse it for the env var + the log line. Guards
    // the TOCTOU where the .onnx vanished between is_available() and here — an
    // empty FINCEPT_TTS_MODEL would otherwise launch piper with no model.
    const QString model = resolve_model_path();
    if (model.isEmpty()) {
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
        fail(QStringLiteral("Failed to start piper_tts.py: %1").arg(proc_->errorString()));
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
    end_engine_session();  // also aborts an in-flight chunk request
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
    connect(player_, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState s) {
                if (s == QMediaPlayer::PlayingState)
                    set_speaking(true);  // audio actually started (synthesis done)
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
                // No device / undecodable clip: every later chunk would fail the
                // same way, so the whole reading ends here.
                end_engine_session();
                fail(QStringLiteral("Audio playback failed: %1").arg(msg));
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
    // Engine reading with the next clip already synthesised: keep the SAME
    // player and audio output and just swap the source — re-opening the audio
    // device per chunk is an audible seam.
    if (eng_session_ && player_ && !eng_ready_.isEmpty()) {
        pending_wav_path_ = eng_ready_.takeFirst();
        player_->setSource(QUrl::fromLocalFile(pending_wav_path_));
        player_->play();
        engine_fetch_next();  // a buffer slot just freed up
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
        engine_play_next();  // starved: waits for the next clip; or ends the reading
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

} // namespace fincept::services

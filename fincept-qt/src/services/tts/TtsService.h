#pragma once
// TtsService.h — local text-to-speech via Piper (Track 6A).
//
// Mirrors the SpeechService (STT) shape on the output side.  One
// public entry point: `speak(text)` synthesises the audio + plays
// it via QMediaPlayer.  Concurrent calls cancel the in-flight one
// — chat is the primary consumer and the user expects "read this,
// not what I clicked five seconds ago".
//
// Configuration via SettingsRepository:
//   `tts.provider`   = "piper" | "none" | unset
//                      "none" disables; unset or "piper" auto-detects a voice
//                      provisioned by `hearth voice` / `hearth setup`.
//   `tts.model_path` = absolute path to a Piper .onnx voice (optional;
//                      auto-discovered under ~/.local/share/finterm/piper or
//                      ~/.hearth/voices when unset)
//   `tts.length`     = float string, default "1.0"
//   `tts.noise`      = float string, default "0.667"
//
// Piper voices are user-supplied (~30 MB each).  finterm doesn't
// bundle them — the Settings → Voice surface tells the user where
// to download and points at the FINCEPT_TTS_MODEL slot.

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QProcess;
class QMediaPlayer;
class QNetworkAccessManager;
class QNetworkReply;
class QAudioOutput;

namespace fincept::services {

class TtsService : public QObject {
    Q_OBJECT
  public:
    static TtsService& instance();

    /// True when `tts.provider` is set to a non-"none" value AND the
    /// configured backend appears usable (binary on PATH, model file
    /// present).  The chat "🔊" buttons hide when this returns false.
    bool is_available() const;

    /// Synthesise + play.  Returns immediately; the actual subprocess
    /// + playback happen async.  Calling again before the previous
    /// utterance finishes cancels it.
    /// @return true if synthesis was launched (the caller may show a
    ///         "preparing" state); false if it failed synchronously
    ///         (unavailable, no voice, or the process didn't start) —
    ///         in which case error() has already been emitted.
    bool speak(const QString& text);

    /// True when the configured LLM engine serves speech (hearth / OpenAI —
    /// see LlmService::speech_endpoint) OR the on-device Piper voice works.
    bool engine_or_local_available() const;

    /// Synthesise on the CONFIGURED LLM ENGINE (POST /v1/audio/speech) and play
    /// the returned WAV. Falls back to the on-device Piper path when the engine
    /// has no speech route or the request fails. Same contract as speak():
    /// true = launched (state_changed(true) follows when audio starts).
    bool speak_via_engine(const QString& text);

    /// Synthesise the FIRST chunk of `text` in the background, so a later
    /// speak_via_engine(text) starts playing at once instead of waiting on the
    /// engine. Local engines only (hearth) — on a paid API this would bill
    /// every text nobody listens to. Replaces any previous warm clip.
    void prewarm_engine(const QString& text);

    /// Stop any in-flight playback.  Idempotent.
    void stop();

    /// Id of the current reading. speak()/speak_via_engine() start a new one;
    /// read it right after a successful launch to own that reading, then follow
    /// session_state()/session_error() for THAT id only — no guessing from the
    /// global signals which reading an edge belongs to.
    int session() const { return session_; }

  signals:
    /// Any reading (legacy, session-blind): for consumers that mirror ALL
    /// playback, like chat's hands-free voice mode.
    void state_changed(bool is_speaking);
    void error(const QString& message);
    /// The same edges, tagged with the reading they belong to. speaking=true
    /// fires once per reading (not per engine chunk).
    void session_state(int session, bool is_speaking);
    void session_error(int session, const QString& message);
    /// An engine was found unusable (404 / unreachable): speaker buttons
    /// gated on engine_or_local_available() should re-check.
    void availability_changed();

  private:
    TtsService();
    ~TtsService() override;

    QProcess* proc_ = nullptr;       ///< current piper_tts.py invocation
    QMediaPlayer* player_ = nullptr; ///< plays the produced WAV (buffered path only)
    QAudioOutput* audio_ = nullptr;
    QString pending_wav_path_;    ///< temp WAV cleaned up after playback
    bool stream_mode_ = false;    ///< proc_ is the piper→player streaming pipeline (no WAV)
    bool stream_started_ = false; ///< the streaming pipeline reported it launched
    QByteArray stream_out_;       ///< accumulated streaming-mode stdout (status/fatal) for diagnostics

    int session_ = 0;
    bool announced_ = false; ///< speaking=true already emitted for session_
    /// Emit state_changed + session_state; true at most once per reading.
    void set_speaking(bool speaking);
    /// Emit error + session_error.
    void fail(const QString& message);

    /// Engine speech URLs known not to work: 404 (no route — for the rest of
    /// the run) or unreachable (refused / timed out — for a minute). Checked
    /// before showing a speaker or calling the engine, so a dead hearth or an
    /// OpenAI-compatible server without speech is not hit on every click.
    QHash<QString, qint64> engine_unusable_until_; ///< url → epoch ms
    bool engine_usable(const QString& url) const;
    void mark_engine_failed(const QString& url, int http_status);

    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* reply_ = nullptr; ///< in-flight engine /audio/speech request

    // Engine reading, synthesised in sentence chunks: the first is short so
    // audio starts in ~2s, and each later chunk is fetched while the previous
    // one plays. Also keeps every request under the engine's input cap.
    bool eng_session_ = false;
    QStringList eng_chunks_;
    int eng_next_ = 0;      ///< index of the next chunk to fetch
    QStringList eng_ready_; ///< synthesised WAVs waiting to play (≤ 2)
    QString eng_text_;      ///< the whole text, for the Piper fallback
    QString eng_url_;
    QMap<QString, QString> eng_headers_;
    QJsonObject eng_body_;
    void engine_fetch_next();
    void engine_play_next();
    void end_engine_session(); ///< abort the chunk request, drop queue + buffered WAVs; no signals

    // prewarm_engine() state: one clip for one text.
    QString warm_text_; ///< warm_key(url, body, first chunk) of the warm clip — the match key
    QString warm_wav_;
    QNetworkReply* warm_reply_ = nullptr;
    bool eng_waiting_warm_ = false; ///< the session's chunk 0 IS the in-flight warm request
    /// Queue a copy of the warm clip as chunk 0 and start; false if unusable.
    bool adopt_warm_clip();
    void discard_warm();

    /// speak() without the leading stop() — the engine path's fallback must not
    /// emit a spurious state_changed(false) between "preparing" and "playing".
    bool start_piper(const QString& text);
    /// Play pending_wav_path_ through QMediaPlayer (both buffered paths).
    void start_playback();
    void on_proc_finished(int exit_code);
    void cleanup_temp_file();
    /// Tear down the QMediaPlayer/QAudioOutput, remove the temp WAV, and
    /// emit state_changed(false). Idempotent — safe to call from both the
    /// StoppedState and errorOccurred handlers (whichever fires first does
    /// the work; the second is a no-op).
    void teardown_playback();

    /// The actual (filesystem-heavy) availability probe. is_available() caches
    /// its result briefly because it's called once per chat-message bubble.
    bool compute_available() const;
    mutable bool avail_cache_ = false;      ///< last compute_available() result
    mutable QElapsedTimer avail_cache_age_; ///< invalid until first probe; TTL'd below
    static constexpr int kAvailCacheTtlMs = 2000;
};

} // namespace fincept::services

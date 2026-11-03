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
#include <QObject>
#include <QString>

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

    /// Stop any in-flight playback.  Idempotent.
    void stop();

  signals:
    void state_changed(bool is_speaking);
    void error(const QString& message);

  private:
    TtsService();
    ~TtsService() override;

    QProcess* proc_ = nullptr;        ///< current piper_tts.py invocation
    QMediaPlayer* player_ = nullptr;  ///< plays the produced WAV (buffered path only)
    QAudioOutput* audio_ = nullptr;
    QString pending_wav_path_;        ///< temp WAV cleaned up after playback
    bool stream_mode_ = false;        ///< proc_ is the piper→player streaming pipeline (no WAV)
    bool stream_started_ = false;     ///< the streaming pipeline reported it launched
    QByteArray stream_out_;           ///< accumulated streaming-mode stdout (status/fatal) for diagnostics

    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* reply_ = nullptr;  ///< in-flight engine /audio/speech request

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
    mutable bool avail_cache_ = false;     ///< last compute_available() result
    mutable QElapsedTimer avail_cache_age_; ///< invalid until first probe; TTL'd below
    static constexpr int kAvailCacheTtlMs = 2000;
};

} // namespace fincept::services

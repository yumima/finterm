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

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

class QProcess;
class QMediaPlayer;
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
    void speak(const QString& text);

    /// Stop any in-flight playback.  Idempotent.
    void stop();

  signals:
    void state_changed(bool is_speaking);
    void error(const QString& message);

  private:
    TtsService();
    ~TtsService() override;

    QProcess* proc_ = nullptr;        ///< current piper_tts.py invocation
    QMediaPlayer* player_ = nullptr;  ///< plays the produced WAV
    QAudioOutput* audio_ = nullptr;
    QString pending_wav_path_;        ///< temp WAV cleaned up after playback

    void on_proc_finished(int exit_code);
    void cleanup_temp_file();

    /// The actual (filesystem-heavy) availability probe. is_available() caches
    /// its result briefly because it's called once per chat-message bubble.
    bool compute_available() const;
    mutable bool avail_cache_ = false;     ///< last compute_available() result
    mutable QElapsedTimer avail_cache_age_; ///< invalid until first probe; TTL'd below
    static constexpr int kAvailCacheTtlMs = 2000;
};

} // namespace fincept::services

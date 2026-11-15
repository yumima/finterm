#pragma once
#include "ai_chat/LlmService.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#ifdef HAS_QT_TTS
#    include <QTextToSpeech>
#endif
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

namespace finterm {

namespace ai_chat {
class VoiceOrb;
}

// ── AiChatBubble ─────────────────────────────────────────────────────────────
// AI assistant launcher + chat panel.
//
// The launcher button sits in the main window's status bar (reserved space),
// so it never covers screen content. The chat panel is an on-demand overlay
// anchored to the bottom-right of `panel_host` (the dock area), resizable by
// its top-left grip, or EXPANDED to fill the host (⤢ in the header). Both
// persist.
//
// Reading (Claude / Gemini style): the assistant's reply is rendered Markdown
// across the full width of the conversation column — no bubble — with Copy
// and a read-aloud speaker under it; the user's message is a compact bubble on
// the right. On a wide panel the column is centred at a reading width.
//
// Voice:
//   🎤  dictation — one utterance into the composer
//   ◉ LIVE  a spoken conversation (Gemini Live style): the orb listens, you
//       speak, it thinks, it answers aloud in the engine's voice (the same
//       Kokoro/Piper voice as News' read-aloud), then listens again. Click
//       the orb to interrupt it; Mute pauses the mic; End returns to text.
//       The conversation is the same thread: every turn is in the transcript.
//   Listening       → SpeechService::start_listening()
//   transcription   → on_transcription() → sent
//   reply done      → speak_text() → TtsService (QTextToSpeech fallback)
//   speech finished → listen again

class AiChatBubble : public QWidget {
    Q_OBJECT
  public:
    /// `panel_host` is the widget the chat panel overlays (the dock area);
    /// the launcher itself is placed by the caller in a layout.
    explicit AiChatBubble(QWidget* panel_host, QWidget* parent = nullptr);

    /// Re-anchor the open panel to the host's bottom-right corner.
    void reposition();

  protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private slots:
    void on_toggle_open();
    void on_send();
    void on_new_session();
    void on_stream_chunk(const QString& chunk, bool done);
    void on_streaming_done(ai_chat::LlmResponse response);
    void on_toggle_mic();
    void on_transcription(const QString& text);
    void on_stt_listening_changed(bool active);
    void on_stt_error(const QString& message);

  private:
    // ── UI panels ────────────────────────────────────────────────────────────
    QWidget* bubble_btn_ = nullptr;
    QWidget* chat_panel_ = nullptr;
    QWidget* panel_host_ = nullptr;
    QWidget* resize_grip_ = nullptr; ///< top-left corner of the panel
    QLabel* unread_badge_ = nullptr;

    // Panel internals
    QStackedWidget* stack_ = nullptr; ///< 0 = conversation, 1 = live voice
    QScrollArea* scroll_area_ = nullptr;
    QWidget* msg_container_ = nullptr;
    QWidget* column_ = nullptr; ///< the centred conversation column
    QVBoxLayout* msg_layout_ = nullptr;
    QPlainTextEdit* input_box_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    QPushButton* mic_btn_ = nullptr;
    QPushButton* live_btn_ = nullptr;
    QPushButton* expand_btn_ = nullptr;
    QPushButton* new_btn_ = nullptr;
    QPushButton* close_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr; ///< thinking / listening / errors, under the conversation

    // Live voice page
    ai_chat::VoiceOrb* orb_ = nullptr;
    QLabel* live_state_lbl_ = nullptr;
    QLabel* live_you_lbl_ = nullptr;
    QLabel* live_reply_lbl_ = nullptr;
    QPushButton* live_mute_btn_ = nullptr;
    QPushButton* live_end_btn_ = nullptr;

    // ── State ────────────────────────────────────────────────────────────────
    bool is_open_ = false;
    bool expanded_ = false;
    bool live_ = false;
    bool live_muted_ = false;
    bool is_listening_ = false;
    bool is_speaking_ = false;
    bool streaming_ = false;
    int unread_count_ = 0;
    int tts_session_ = -1; ///< TtsService session of the reply being spoken
    QString stream_text_;  ///< the reply so far (live captions)
    QString voice_error_;  ///< last voice/STT failure, shown until the next listen or send

    QString active_session_id_;
    std::vector<ai_chat::ConversationMessage> history_;
    QPointer<QTextEdit> streaming_bubble_;

    // Panel size. Invalid until the user resizes: then reposition() uses the
    // default for the current host size. Persisted to SettingsRepository under
    // ui.chat_panel.w / .h on release of the grip; expanded under .expanded.
    QSize user_panel_size_;
    bool resizing_ = false;
    QPoint resize_press_pos_; // global press position
    QSize resize_start_size_;

    // User bubbles are as wide as their text, up to a share of the column.
    std::vector<QPointer<QWidget>> user_bubbles_;
    void rewrap_messages();

    // ── TTS ──────────────────────────────────────────────────────────────────
#ifdef HAS_QT_TTS
    QTextToSpeech* tts_engine_ = nullptr;
#endif

    // ── Build ────────────────────────────────────────────────────────────────
    void build_bubble_button();
    void build_chat_panel();
    QWidget* build_panel_header();
    QWidget* build_input_row();
    QWidget* build_live_page();

    // ── Helpers ──────────────────────────────────────────────────────────────
    void refresh_theme();
    void open_panel();
    void close_panel();
    void set_expanded(bool on);
    void update_expand_button();
    void resume_live_listening();
    void ensure_session();
    void add_user_message(const QString& text);
    QWidget* add_assistant_message(const QString& markdown, bool streaming);
    void finish_assistant_message(QWidget* row, const QString& markdown);
    void scroll_to_bottom();
    void set_ui_enabled(bool enabled);
    void update_unread(int delta);
    void fit_input();
    void start_listening();
    void stop_listening();
    void speak_text(const QString& text);
    void stop_tts();
    void speech_finished();
    void enter_live();
    void exit_live();
    void update_status();
};

} // namespace finterm

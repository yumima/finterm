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
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

namespace finterm {

// ── AiChatBubble ─────────────────────────────────────────────────────────────
// AI assistant launcher + chat panel.
//
// The launcher button sits in the main window's status bar (reserved space),
// so it never covers screen content — it used to float over the bottom-right
// corner of every screen, on top of the News TL;DR and the Equity Research
// FINANCIAL HEALTH values. The chat panel is an on-demand overlay anchored to
// the bottom-right of `panel_host` (the dock area), shown only while open, and
// resizable by its top-left grip; the chosen size persists.
// Features: streaming chat, voice input (Python speech_recognition + Google STT),
// TTS output via QMediaPlayer, session persistence via ChatRepository.
//
// Voice flow:
//   mic_btn / voice_mode_btn
//       → SpeechService::start_listening()
//       → SpeechService::transcription_ready(text)   [signal on UI thread]
//       → on_transcription(text)
//       → input_box_ populated → on_send()

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
    void on_toggle_voice_mode();
    void on_toggle_mic();
    void on_stop_speech();
    void on_transcription(const QString& text);
    void on_stt_listening_changed(bool active);
    void on_stt_error(const QString& message);
    void on_tts_media_status(QMediaPlayer::MediaStatus status);

  private:
    // ── UI panels ────────────────────────────────────────────────────────────
    QWidget* bubble_btn_ = nullptr;
    QWidget* chat_panel_ = nullptr;
    QWidget* panel_host_ = nullptr;
    QWidget* resize_grip_ = nullptr; ///< top-left corner of the panel
    QLabel* unread_badge_ = nullptr;

    // Panel internals
    QScrollArea* scroll_area_ = nullptr;
    QWidget* msg_container_ = nullptr;
    QVBoxLayout* msg_layout_ = nullptr;
    QPlainTextEdit* input_box_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    QPushButton* mic_btn_ = nullptr;
    QPushButton* voice_mode_btn_ = nullptr;
    QPushButton* stop_speech_btn_ = nullptr;
    QPushButton* new_btn_ = nullptr;
    QPushButton* close_btn_ = nullptr;
    QWidget* voice_status_bar_ = nullptr;
    QLabel* voice_status_lbl_ = nullptr;

    // ── State ────────────────────────────────────────────────────────────────
    bool is_open_ = false;
    bool voice_mode_ = false;
    bool is_listening_ = false;
    bool is_speaking_ = false;
    bool streaming_ = false;
    int unread_count_ = 0;

    QString active_session_id_;
    std::vector<ai_chat::ConversationMessage> history_;
    QPointer<QTextEdit> streaming_bubble_;

    // Panel size. Invalid until the user resizes: then reposition() uses the
    // default for the current host size. Persisted to SettingsRepository under
    // ui.chat_panel.w / .h on release of the grip.
    QSize user_panel_size_;
    bool resizing_ = false;
    QPoint resize_press_pos_; // global press position
    QSize resize_start_size_;

    // Message columns, re-wrapped when the panel width changes.
    struct MsgCol {
        QPointer<QWidget> col;
        QPointer<QTextEdit> body;
        int min_h = 18;
    };
    std::vector<MsgCol> msg_cols_;
    int msg_col_width() const;
    void rewrap_messages();

    // ── TTS ──────────────────────────────────────────────────────────────────
    QMediaPlayer* tts_player_ = nullptr;
    QTimer* pulse_timer_ = nullptr;
    int pulse_step_ = 0;
#ifdef HAS_QT_TTS
    QTextToSpeech* tts_engine_ = nullptr;
#endif

    // ── Build ────────────────────────────────────────────────────────────────
    void build_bubble_button();
    void build_chat_panel();
    QWidget* build_panel_header();
    QWidget* build_input_row();
    QWidget* build_voice_status_bar();

    // ── Helpers ──────────────────────────────────────────────────────────────
    void refresh_theme();
    void open_panel();
    void close_panel();
    void ensure_session();
    void add_bubble(const QString& role, const QString& text);
    QTextEdit* add_streaming_bubble();
    void scroll_to_bottom();
    void set_ui_enabled(bool enabled);
    void update_unread(int delta);
    void start_listening();
    void stop_listening();
    void speak_text(const QString& text);
    void stop_tts();
    void update_voice_status();
    void animate_pulse();
};

} // namespace finterm

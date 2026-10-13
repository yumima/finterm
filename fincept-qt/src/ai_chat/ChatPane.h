// src/ai_chat/ChatPane.h
#pragma once
// ChatPane — one reusable, scoped chat widget.
//
// The terminal used to carry four chat implementations (the AI Chat screen,
// the floating bubble, the Equity Research stock chat, chat mode), each with
// its own send path, rendering and quirks — and only one of them had tools.
// ChatPane is the convergence point for the EMBEDDED surfaces: a host screen
// drops it in, names a persona (tool curation), a role (model binding) and a
// context (what the user is looking at), and gets a persistent, tool-capable
// thread scoped to that thing.
//
// Scope is explicit, not ambient: the host supplies a context block that is
// injected as a leading system message on every request, so the model is
// grounded in the pane's own subject even when AppContextService knows
// nothing about it.
//
// Persistence: a pane with a context tag stores its thread as an ordinary
// ChatRepository session tagged "symbol:NVDA" / "portfolio:<id>", and finds
// it again on revisit. An untagged pane is ephemeral.

#include "ai_chat/LlmService.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace fincept::ai_chat {

struct ChatPaneConfig {
    /// ChatPersonas.h id — system prompt + curated tool allow-list.
    QString persona_id = QStringLiteral("general");
    /// AiRoles.h id — which model binding this pane's calls resolve through.
    QString role = QStringLiteral("ai_chat");
    /// Composer placeholder text.
    QString placeholder;
    /// Attach the persona's tools to every request. Off = pure conversation
    /// over the context block only.
    bool use_tools = true;
    /// Request local chain-of-thought suppression (PersonaScope::think).
    bool think = false;
    /// Mount 🎤 hands-free voice + 🔊 read-aloud (SpeechService / TtsService).
    bool voice = false;
    /// Extra grounding injected as the FIRST system message of every request —
    /// the host's live view of its subject (stock data block, portfolio
    /// summary). Re-evaluated per send so the figures are current.
    std::function<QString()> context_block;
};

class ChatPane : public QWidget {
    Q_OBJECT
  public:
    explicit ChatPane(ChatPaneConfig cfg, QWidget* parent = nullptr);

    /// Re-scope the pane. `tag` keys the persisted thread (ChatRepository
    /// context_tag; empty = ephemeral), `title` names a newly created session,
    /// `welcome_html` fills the empty thread. Aborts any in-flight request,
    /// then restores the tag's most recent thread when one exists.
    void set_context(const QString& tag, const QString& title, const QString& welcome_html);

    bool busy() const { return streaming_; }

  protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

  private slots:
    void on_send();

  private:
    void build_ui();
    void add_bubble(const QString& role, const QString& content, bool markdown = true);
    QLabel* add_streaming_bubble();
    void clear_thread_ui();
    void show_welcome(bool show);
    void scroll_to_bottom();
    void set_input_enabled(bool enabled);
    void load_thread();
    bool ensure_session();

    // Voice (cfg_.voice only).
    void wire_audio();
    void toggle_voice_mode();
    bool speak_reply(const QString& text);

    ChatPaneConfig cfg_;
    QString context_tag_;
    QString session_title_;
    QString session_id_;      // lazily created on first send
    QString welcome_html_;

    std::vector<ConversationMessage> history_;
    bool streaming_ = false;
    quint64 epoch_ = 0;       // strands stale async deliveries on re-scope

    // UI
    QScrollArea* scroll_area_ = nullptr;
    QWidget* messages_container_ = nullptr;
    QVBoxLayout* messages_layout_ = nullptr;
    QLabel* welcome_lbl_ = nullptr;
    QPlainTextEdit* input_box_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    QPointer<QLabel> streaming_bubble_;
    QTimer* spinner_timer_ = nullptr;
    int spinner_frame_ = 0;

    // Voice state (mirrors the battle-tested EquityAiTab wiring).
    QPushButton* mic_btn_ = nullptr;
    QPushButton* read_btn_ = nullptr;
    bool voice_mode_ = false;
    bool tts_speaking_ = false;
    bool tts_pending_ = false;
    QString last_reply_;
};

} // namespace fincept::ai_chat

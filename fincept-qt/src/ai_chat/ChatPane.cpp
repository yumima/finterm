// src/ai_chat/ChatPane.cpp
#include "ai_chat/ChatPane.h"

#include "ai_chat/ChatPersonas.h"
#include "ai_chat/ThinkTrace.h"
#include "core/logging/Logger.h"
#include "services/stt/SpeechService.h"
#include "services/tts/TtsService.h"
#include "storage/repositories/ChatRepository.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::ai_chat {

static constexpr const char* kChatPaneTag = "ChatPane";
namespace col = fincept::ui::colors;
namespace fnt = fincept::ui::fonts;

// A tagged thread persists and grows for as long as the user keeps a symbol or
// portfolio, so both ends must be bounded: the turns kept in memory / shipped
// per request (the model's context window is the real limit — a months-old
// NVDA thread must not push the DATA block out of it), and the bubbles
// materialised on restore (widget trees, not rows).
static constexpr int kMaxRequestTurns = 40;    // most recent turns sent per request
static constexpr int kMaxRestoredMessages = 200;  // bubbles built when a thread reloads

// Same sizing rules as the old chat screen's bubbles: QLabel + word wrap +
// a max width; heightForWidth does the rest.
static constexpr int kUserColMaxWidth = 460;
static constexpr int kAiColMaxWidth = 560;
static constexpr int kBubblePadX = 12;

static QString pane_bubble_style(const QString& role) {
    if (role == QLatin1String("user"))
        return QStringLiteral("background:rgba(120,53,15,0.45);border:1px solid rgba(217,119,6,0.28);"
                              "border-radius:0px;padding:8px 12px;");
    if (role == QLatin1String("system"))
        return QStringLiteral("background:rgba(50,12,12,0.85);border:1px solid rgba(220,38,38,0.22);"
                              "border-radius:0px;padding:8px 12px;");
    return QStringLiteral("background:%1;border:1px solid %2;border-radius:0px;padding:8px 12px;")
        .arg(col::BG_SURFACE(), col::BORDER_DIM());
}

static QString pane_body_color(const QString& role) {
    if (role == QLatin1String("user"))
        return QStringLiteral("#fff7ed");
    if (role == QLatin1String("system"))
        return QStringLiteral("#fee2e2");
    return col::TEXT_PRIMARY();
}

ChatPane::ChatPane(ChatPaneConfig cfg, QWidget* parent) : QWidget(parent), cfg_(std::move(cfg)) {
    build_ui();
    if (cfg_.voice)
        wire_audio();
}

void ChatPane::build_ui() {
    setStyleSheet(QStringLiteral("background:%1;").arg(col::BG_BASE()));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setStyleSheet(
        QStringLiteral("QScrollArea{background:%1;border:1px solid %2;}"
                       "QScrollBar:vertical{background:transparent;width:5px;margin:2px;}"
                       "QScrollBar::handle:vertical{background:%3;border-radius:0px;min-height:30px;}"
                       "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}")
            .arg(col::BG_BASE(), col::BORDER_DIM(), col::BORDER_MED()));

    messages_container_ = new QWidget;
    messages_container_->setStyleSheet(QStringLiteral("background:%1;").arg(col::BG_BASE()));
    messages_layout_ = new QVBoxLayout(messages_container_);
    messages_layout_->setContentsMargins(12, 10, 12, 8);
    messages_layout_->setSpacing(12);

    welcome_lbl_ = new QLabel;
    welcome_lbl_->setWordWrap(true);
    welcome_lbl_->setTextFormat(Qt::RichText);
    welcome_lbl_->setStyleSheet(
        QStringLiteral("color:%1;font-size:%2px;background:transparent;").arg(col::TEXT_TERTIARY()).arg(fnt::SMALL));
    messages_layout_->addWidget(welcome_lbl_);
    messages_layout_->addStretch();

    scroll_area_->setWidget(messages_container_);
    root->addWidget(scroll_area_, 1);

    auto* row = new QHBoxLayout;
    row->setSpacing(6);

    const auto icon_btn_ss = [](bool on) {
        return QStringLiteral("QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:4px;"
                              "padding:7px 10px;font-size:14px;}QPushButton:hover{border-color:%4;}")
            .arg(on ? QString(col::AMBER()) : QString(col::BG_SURFACE()),
                 on ? QString(col::BG_BASE()) : QString(col::TEXT_PRIMARY()), col::BORDER_MED(), col::AMBER());
    };

    if (cfg_.voice) {
        mic_btn_ = new QPushButton(QStringLiteral("🎤"));
        mic_btn_->setCursor(Qt::PointingHandCursor);
        mic_btn_->setToolTip(QStringLiteral("Hands-free voice: speak your question, hear the answer"));
        mic_btn_->setStyleSheet(icon_btn_ss(false));
        mic_btn_->setProperty("ss_on", icon_btn_ss(true));
        mic_btn_->setProperty("ss_off", icon_btn_ss(false));
        connect(mic_btn_, &QPushButton::clicked, this, &ChatPane::toggle_voice_mode);
        row->addWidget(mic_btn_);
    }

    input_box_ = new QPlainTextEdit;
    input_box_->setPlaceholderText(cfg_.placeholder);
    input_box_->setFixedHeight(38);
    input_box_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    input_box_->setStyleSheet(QStringLiteral("QPlainTextEdit{background:%1;color:%2;border:1px solid %3;"
                                             "border-radius:4px;padding:6px 10px;font-size:13px;}"
                                             "QPlainTextEdit:focus{border-color:%4;}")
                                  .arg(col::BG_SURFACE(), col::TEXT_PRIMARY(), col::BORDER_MED(), col::AMBER()));
    input_box_->installEventFilter(this);
    connect(input_box_, &QPlainTextEdit::textChanged, this, [this]() {
        const int lines = input_box_->document()->blockCount();
        input_box_->setFixedHeight(qMin(qMax(lines, 1), 5) * 22 + 16);
    });
    row->addWidget(input_box_, 1);

    send_btn_ = new QPushButton(QStringLiteral("Send"));
    send_btn_->setCursor(Qt::PointingHandCursor);
    send_btn_->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:4px;"
                                            "padding:7px 14px;font-weight:700;}"
                                            "QPushButton:hover:enabled{background:%4;border-color:%2;}"
                                            "QPushButton:disabled{color:%5;}")
                                 .arg(col::BG_SURFACE(), col::AMBER(), col::BORDER_MED(), col::BG_RAISED(),
                                      col::TEXT_DIM()));
    connect(send_btn_, &QPushButton::clicked, this, &ChatPane::on_send);
    row->addWidget(send_btn_);

    if (cfg_.voice) {
        read_btn_ = new QPushButton(QStringLiteral("🔊"));
        read_btn_->setCursor(Qt::PointingHandCursor);
        read_btn_->setToolTip(QStringLiteral("Read the last answer aloud"));
        read_btn_->setStyleSheet(icon_btn_ss(false));
        connect(read_btn_, &QPushButton::clicked, this, [this]() {
            if (tts_speaking_)
                services::TtsService::instance().stop();
            else
                speak_reply(last_reply_);
        });
        row->addWidget(read_btn_);
    }

    root->addLayout(row);
}

bool ChatPane::eventFilter(QObject* obj, QEvent* event) {
    if (obj == input_box_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (ke->modifiers() & Qt::ShiftModifier)
                return false;
            ke->accept();
            on_send();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ── Scope ────────────────────────────────────────────────────────────────────

void ChatPane::set_context(const QString& tag, const QString& title, const QString& welcome_html) {
    ++epoch_; // strand any in-flight stream for the old scope
    context_tag_ = tag;
    session_title_ = title;
    session_id_.clear();
    welcome_html_ = welcome_html;
    history_.clear();
    last_reply_.clear();
    streaming_ = false;
    if (spinner_timer_)
        spinner_timer_->stop();
    streaming_bubble_.clear();
    set_input_enabled(true);

    // Leave hands-free mode on a scope switch — the new subject should not
    // inherit a hot mic. Clear the flag BEFORE stopping TTS: stop() can emit
    // state_changed(false) synchronously, and that handler resumes listening
    // while voice mode is on.
    if (cfg_.voice) {
        const bool was_voice = voice_mode_;
        voice_mode_ = false;
        services::TtsService::instance().stop();
        if (was_voice) {
            services::SpeechService::instance().stop_listening();
            if (mic_btn_) {
                mic_btn_->setText(QStringLiteral("🎤"));
                mic_btn_->setStyleSheet(mic_btn_->property("ss_off").toString());
            }
        }
    }

    clear_thread_ui();
    welcome_lbl_->setText(welcome_html_);
    if (!context_tag_.isEmpty())
        load_thread();
    show_welcome(history_.empty());
}

void ChatPane::load_thread() {
    auto latest = ChatRepository::instance().latest_session_for_context(context_tag_);
    if (latest.is_err() || !latest.value().has_value())
        return;
    session_id_ = latest.value()->id;
    auto msgs = ChatRepository::instance().get_messages(session_id_);
    if (msgs.is_err())
        return;
    // Only the most recent messages become widgets and model context; the DB
    // keeps the full thread.
    const auto& all = msgs.value();
    const qsizetype start = std::max<qsizetype>(0, all.size() - kMaxRestoredMessages);
    for (qsizetype i = start; i < all.size(); ++i) {
        const auto& m = all[i];
        // Same formats as live rendering: only assistant text is Markdown. A
        // restored user message re-parsed as Markdown would turn "#1 pick" into
        // a heading the user never typed.
        add_bubble(m.role, m.content, /*markdown=*/m.role == QLatin1String("assistant"));
        if (m.role != QLatin1String("system"))
            history_.push_back({m.role, m.content});
    }
    if (!history_.empty() && history_.back().role == QLatin1String("assistant"))
        last_reply_ = strip_think_traces(history_.back().content);
    scroll_to_bottom();
}

bool ChatPane::ensure_session() {
    if (context_tag_.isEmpty())
        return true; // ephemeral pane — nothing to create
    if (!session_id_.isEmpty())
        return true;
    auto& llm = LlmService::instance();
    auto r = ChatRepository::instance().create_session(session_title_.isEmpty() ? context_tag_ : session_title_,
                                                       llm.active_provider(), llm.active_model(), cfg_.persona_id,
                                                       context_tag_);
    if (r.is_err()) {
        LOG_ERROR(kChatPaneTag, "create_session failed: " + QString::fromStdString(r.error()));
        return false;
    }
    session_id_ = r.value().id;
    return true;
}

// ── Send ─────────────────────────────────────────────────────────────────────

void ChatPane::on_send() {
    if (streaming_)
        return;
    const QString text = input_box_->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    if (!LlmService::instance().is_configured()) {
        add_bubble(QStringLiteral("system"),
                   QStringLiteral("No model configured — Settings → AI Config."), /*markdown=*/false);
        return;
    }
    if (!ensure_session()) {
        add_bubble(QStringLiteral("system"), QStringLiteral("Could not create a chat session — try again."),
                   /*markdown=*/false);
        return;
    }
    // Pause the mic only once the send is definitely going ahead: an early
    // return above with the mic already stopped would strand hands-free mode
    // (red button, nothing listening, nothing to restart it).
    if (cfg_.voice && voice_mode_)
        services::SpeechService::instance().stop_listening(); // resume on TTS end

    input_box_->clear();
    input_box_->setFixedHeight(38);
    show_welcome(false);
    streaming_ = true;
    set_input_enabled(false);
    add_bubble(QStringLiteral("user"), text, /*markdown=*/false);
    history_.push_back({QStringLiteral("user"), text});
    if (!session_id_.isEmpty())
        ChatRepository::instance().add_message(session_id_, QStringLiteral("user"), text,
                                               LlmService::instance().active_provider(),
                                               LlmService::instance().active_model());

    // The request: context block as the LEADING system message (reaches the
    // model with tools on or off), then the stored turns. Persona + target are
    // carried per request — nothing global.
    std::vector<ConversationMessage> request_history;
    if (cfg_.context_block) {
        const QString ctx = cfg_.context_block();
        if (!ctx.isEmpty())
            request_history.push_back({QStringLiteral("system"), ctx});
    }
    // The most recent turns up to (not including) the message being sent — a
    // trailing window, so a long-lived thread cannot crowd the context block
    // and the question itself out of the model's context window.
    const auto turns_end = history_.end() - 1;
    const auto turns_begin =
        (turns_end - history_.begin()) > kMaxRequestTurns ? turns_end - kMaxRequestTurns : history_.begin();
    request_history.insert(request_history.end(), turns_begin, turns_end);

    PersonaScope persona;
    if (const ChatPersona* p = find_persona(cfg_.persona_id))
        persona = {p->system_prompt, p->tool_globs, true};
    persona.think = cfg_.think;
    {
        const auto target = LlmService::instance().scope_for_role(cfg_.role);
        persona.provider = target.provider;
        persona.model = target.model;
        persona.api_key = target.api_key;
        persona.base_url = target.base_url;
    }

    streaming_bubble_ = add_streaming_bubble();
    scroll_to_bottom();

    // Spinner until the first token: the local model can be silent a while,
    // and a hung-looking pane invites a second click.
    if (!spinner_timer_) {
        spinner_timer_ = new QTimer(this);
        connect(spinner_timer_, &QTimer::timeout, this, [this]() {
            static const char* kFrames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            QLabel* b = streaming_bubble_;
            if (!b || !b->property("acc").toString().isEmpty()) {
                spinner_timer_->stop();
                return;
            }
            spinner_frame_ = (spinner_frame_ + 1) % 10;
            b->setText(QString::fromUtf8(kFrames[spinner_frame_]) + QStringLiteral(" thinking…"));
        });
    }
    spinner_frame_ = 0;
    spinner_timer_->start(110);

    const quint64 epoch = ++epoch_;
    QPointer<ChatPane> self = this;
    LlmService::instance().chat_streaming(
        text, request_history,
        [self, epoch](const QString& chunk, bool done) {
            if (!self)
                return;
            QMetaObject::invokeMethod(
                self.data(),
                [self, epoch, chunk, done]() {
                    if (!self || epoch != self->epoch_)
                        return;
                    QLabel* b = self->streaming_bubble_;
                    if (!b || chunk.isEmpty())
                        return;
                    if (self->spinner_timer_)
                        self->spinner_timer_->stop();
                    const QString acc = b->property("acc").toString() + chunk;
                    b->setProperty("acc", acc);
                    b->setText(strip_think_traces(acc));
                    self->scroll_to_bottom();
                    Q_UNUSED(done)
                },
                Qt::QueuedConnection);
        },
        cfg_.use_tools, persona,
        // Finalisation carries the LlmResponse: errors and quota-fallback
        // notes reach the user instead of dying in the chunk stream.
        [self, epoch](LlmResponse resp) {
            if (!self || epoch != self->epoch_)
                return;
            self->streaming_ = false;
            if (self->spinner_timer_)
                self->spinner_timer_->stop();
            self->set_input_enabled(true);

            QLabel* b = self->streaming_bubble_;
            self->streaming_bubble_.clear();

            const QString used_note =
                resp.fell_back && !resp.model_used.isEmpty()
                    ? QStringLiteral("\n\n*Answered by %1 (%2) — fell back from %3 after a quota limit.*")
                          .arg(resp.model_used, resp.provider_used,
                               resp.requested_model.isEmpty() ? QStringLiteral("the configured model")
                                                              : resp.requested_model)
                    : QString();

            if (!resp.success) {
                const QString err =
                    QStringLiteral("Error: %1").arg(resp.error.isEmpty()
                                                        ? QStringLiteral("the model returned no answer.")
                                                        : resp.error) + used_note;
                if (b) {
                    b->setTextFormat(Qt::MarkdownText);
                    b->setText(err);
                }
                LOG_WARN(kChatPaneTag, "chat failed: " + resp.error);
                if (self->cfg_.voice && self->voice_mode_)
                    services::SpeechService::instance().start_listening();
                return;
            }

            const QString content = strip_think_traces(resp.content).trimmed();
            if (b) {
                if (!content.isEmpty()) {
                    b->setTextFormat(Qt::MarkdownText);
                    b->setText(content + used_note);
                    b->setProperty("acc", content);
                    if (auto* copy_btn = qobject_cast<QPushButton*>(
                            b->property("copy_btn").value<QObject*>()))
                        copy_btn->show();
                } else {
                    b->setText(QStringLiteral("(No answer came back — try rephrasing the question.)"));
                }
            }
            if (!content.isEmpty()) {
                self->history_.push_back({QStringLiteral("assistant"), content});
                // Bound the in-memory thread the same way a restore is bounded;
                // the DB keeps everything.
                if (self->history_.size() > std::size_t(kMaxRestoredMessages) * 2)
                    self->history_.erase(self->history_.begin(),
                                         self->history_.end() - kMaxRestoredMessages);
                self->last_reply_ = content;
                if (!self->session_id_.isEmpty())
                    ChatRepository::instance().add_message(
                        self->session_id_, QStringLiteral("assistant"), content,
                        resp.provider_used.isEmpty() ? LlmService::instance().active_provider() : resp.provider_used,
                        resp.model_used.isEmpty() ? LlmService::instance().active_model() : resp.model_used,
                        resp.total_tokens);
            }
            self->scroll_to_bottom();
            // Hear it; the mic resumes when TTS finishes. If TTS can't start,
            // resume listening now so voice mode doesn't stall.
            if (self->cfg_.voice && self->voice_mode_) {
                if (content.isEmpty() || !self->speak_reply(content))
                    services::SpeechService::instance().start_listening();
            }
        });
}

// ── Voice ────────────────────────────────────────────────────────────────────

void ChatPane::wire_audio() {
    auto& stt = services::SpeechService::instance();
    connect(&stt, &services::SpeechService::transcription_ready, this, [this](const QString& text) {
        if (!voice_mode_ || text.trimmed().isEmpty())
            return;
        input_box_->setPlainText(text.trimmed());
        on_send();
    });
    connect(&stt, &services::SpeechService::error_occurred, this, [this](const QString&) {
        if (voice_mode_)
            toggle_voice_mode(); // bail out of voice mode on STT failure
    });

    auto& tts = services::TtsService::instance();
    connect(&tts, &services::TtsService::state_changed, this, [this](bool speaking) {
        tts_speaking_ = speaking;
        if (read_btn_)
            read_btn_->setText(speaking ? QStringLiteral("⏹") : QStringLiteral("🔊"));
        if (speaking) {
            tts_pending_ = false; // real playback started
            return;
        }
        // Ignore the synchronous teardown false that speak() fires BEFORE
        // audio begins — otherwise the mic resumes during the answer and
        // transcribes the assistant's own voice (feedback loop).
        if (tts_pending_)
            return;
        if (voice_mode_ && !streaming_)
            services::SpeechService::instance().start_listening();
    });
}

bool ChatPane::speak_reply(const QString& text) {
    if (text.trimmed().isEmpty())
        return false;
    tts_pending_ = true;
    const bool ok = services::TtsService::instance().speak(text);
    if (!ok)
        tts_pending_ = false;
    return ok;
}

void ChatPane::toggle_voice_mode() {
    voice_mode_ = !voice_mode_;
    if (mic_btn_) {
        mic_btn_->setStyleSheet(mic_btn_->property(voice_mode_ ? "ss_on" : "ss_off").toString());
        mic_btn_->setText(voice_mode_ ? QStringLiteral("🔴") : QStringLiteral("🎤"));
    }
    if (voice_mode_) {
        services::TtsService::instance().stop();
        services::SpeechService::instance().start_listening();
    } else {
        services::SpeechService::instance().stop_listening();
        services::TtsService::instance().stop();
    }
}

// ── Rendering ────────────────────────────────────────────────────────────────

void ChatPane::add_bubble(const QString& role, const QString& content, bool markdown) {
    const bool is_user = (role == QLatin1String("user"));

    auto* row = new QWidget;
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    if (is_user)
        rl->addStretch();

    auto* col_widget = new QWidget;
    col_widget->setStyleSheet(QStringLiteral("background:transparent;"));
    col_widget->setMaximumWidth(is_user ? kUserColMaxWidth : kAiColMaxWidth);
    auto* cvl = new QVBoxLayout(col_widget);
    cvl->setContentsMargins(0, 0, 0, 0);
    cvl->setSpacing(3);

    auto* bubble = new QFrame;
    bubble->setStyleSheet(pane_bubble_style(role));
    auto* bvl = new QVBoxLayout(bubble);
    bvl->setContentsMargins(0, 0, 0, 0);

    auto* body = new QLabel;
    body->setTextFormat(markdown ? Qt::MarkdownText : Qt::PlainText);
    body->setText(role == QLatin1String("assistant") ? strip_think_traces(content) : content);
    body->setWordWrap(true);
    body->setMaximumWidth((is_user ? kUserColMaxWidth : kAiColMaxWidth) - 2 * kBubblePadX);
    body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    body->setStyleSheet(QStringLiteral("QLabel{background:transparent;color:%1;font-size:%2px;}")
                            .arg(pane_body_color(role))
                            .arg(fnt::SMALL));
    bvl->addWidget(body);
    cvl->addWidget(bubble);

    rl->addWidget(col_widget);
    if (!is_user)
        rl->addStretch();

    messages_layout_->insertWidget(messages_layout_->count() - 1, row);
}

QLabel* ChatPane::add_streaming_bubble() {
    auto* row = new QWidget;
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);

    auto* col_widget = new QWidget;
    col_widget->setStyleSheet(QStringLiteral("background:transparent;"));
    col_widget->setMaximumWidth(kAiColMaxWidth);
    auto* cvl = new QVBoxLayout(col_widget);
    cvl->setContentsMargins(0, 0, 0, 0);
    cvl->setSpacing(3);

    auto* bubble = new QFrame;
    bubble->setStyleSheet(pane_bubble_style(QStringLiteral("assistant")));
    auto* bvl = new QVBoxLayout(bubble);
    bvl->setContentsMargins(0, 0, 0, 0);

    auto* body = new QLabel(QStringLiteral("⠋ thinking…"));
    body->setTextFormat(Qt::PlainText);
    body->setProperty("acc", QString{});
    body->setWordWrap(true);
    body->setMaximumWidth(kAiColMaxWidth - 2 * kBubblePadX);
    body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    body->setStyleSheet(QStringLiteral("QLabel{background:transparent;color:%1;font-size:%2px;}")
                            .arg(col::TEXT_PRIMARY())
                            .arg(fnt::SMALL));
    bvl->addWidget(body);
    cvl->addWidget(bubble);

    // Copy button — hidden while streaming, shown on completion.
    auto* copy_btn = new QPushButton(QStringLiteral("Copy"));
    copy_btn->setFixedHeight(18);
    copy_btn->setCursor(Qt::PointingHandCursor);
    copy_btn->setStyleSheet(QStringLiteral("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                                           "border-radius:0px;padding:0 8px;font-size:%3px;}"
                                           "QPushButton:hover{background:%2;color:%4;}")
                                .arg(col::TEXT_DIM(), col::BORDER_MED())
                                .arg(fnt::TINY)
                                .arg(col::TEXT_PRIMARY()));
    copy_btn->hide();
    connect(copy_btn, &QPushButton::clicked, this, [body, copy_btn]() {
        QApplication::clipboard()->setText(body->property("acc").toString());
        copy_btn->setText(QStringLiteral("Copied!"));
        QTimer::singleShot(1500, copy_btn, [copy_btn]() { copy_btn->setText(QStringLiteral("Copy")); });
    });
    auto* footer = new QWidget;
    footer->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* fhl = new QHBoxLayout(footer);
    fhl->setContentsMargins(0, 0, 0, 0);
    fhl->addStretch();
    fhl->addWidget(copy_btn);
    cvl->addWidget(footer);
    body->setProperty("copy_btn", QVariant::fromValue(static_cast<QObject*>(copy_btn)));

    rl->addWidget(col_widget);
    rl->addStretch();
    messages_layout_->insertWidget(messages_layout_->count() - 1, row);
    return body;
}

void ChatPane::clear_thread_ui() {
    streaming_bubble_.clear();
    // Keep the trailing stretch and the welcome label (index just before it).
    while (messages_layout_->count() > 2) {
        QLayoutItem* item = messages_layout_->takeAt(1);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void ChatPane::show_welcome(bool show) {
    if (welcome_lbl_)
        welcome_lbl_->setVisible(show);
}

void ChatPane::scroll_to_bottom() {
    QTimer::singleShot(50, this, [this]() {
        if (scroll_area_ && scroll_area_->verticalScrollBar())
            scroll_area_->verticalScrollBar()->setValue(scroll_area_->verticalScrollBar()->maximum());
    });
}

void ChatPane::set_input_enabled(bool enabled) {
    if (input_box_)
        input_box_->setEnabled(enabled);
    if (send_btn_) {
        send_btn_->setEnabled(enabled);
        send_btn_->setText(enabled ? QStringLiteral("Send") : QStringLiteral("···"));
    }
}

} // namespace fincept::ai_chat

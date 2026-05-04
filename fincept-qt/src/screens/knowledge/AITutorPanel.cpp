#include "screens/knowledge/AITutorPanel.h"

#include "ai_chat/LlmService.h"
#include "screens/knowledge/ContentLoader.h"
#include "screens/knowledge/KnowledgeTypes.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {

constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";

QString chip_ss() {
    return QString("QPushButton { color: %1; background: %2; border: 1px solid %3;"
                   " padding: 4px 9px; font-size: 10px; font-weight: bold; letter-spacing: 0.5px; %4 }"
                   "QPushButton:hover { color: %5; border-color: %5; background: %6; }"
                   "QPushButton:disabled { color: %7; border-color: %3; }")
        .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_BASE(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::AMBER(), ui::colors::ACCENT_BG(), ui::colors::TEXT_DIM());
}

QString thread_view_ss() {
    return QString("QTextBrowser { background: %1; color: %2; border: 1px solid %3;"
                   " padding: 8px 10px; font-size: 11px; %4 }"
                   "QScrollBar:vertical { width: 6px; background: transparent; }"
                   "QScrollBar::handle:vertical { background: %5; }"
                   "QScrollBar::handle:vertical:hover { background: %6; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::BORDER_MED(), ui::colors::BORDER_BRIGHT());
}

QString input_ss() {
    return QString("QLineEdit { background: %1; color: %2; border: 1px solid %3;"
                   " padding: 7px 10px; font-size: 12px; %4 }"
                   "QLineEdit:focus { border-color: %5; }"
                   "QLineEdit:disabled { color: %6; }")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::AMBER(), ui::colors::TEXT_DIM());
}

} // namespace

AITutorPanel::AITutorPanel(const KnowledgeEntry& entry, QWidget* parent) : QWidget(parent) {
    entry_id_ = entry.id;
    entry_title_ = entry.title;
    entry_subtitle_ = entry.subtitle;
    abbreviation_ = entry.abbreviation;
    entry_body_ = ContentLoader::instance().load_body(entry.id);

    setStyleSheet(QString("background: %1;").arg(ui::colors::BG_SURFACE()));

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    // Status header with model + entry context. Always visible at top.
    status_ = new QLabel("", this);
    status_->setStyleSheet(QString("color: %1; background: transparent; font-size: 10px; %2")
                               .arg(ui::colors::TEXT_DIM(), MONO));
    status_->setWordWrap(true);
    vl->addWidget(status_);

    // ── Input row (TOP — always visible even when rail is cropped) ────────────
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    input_ = new QLineEdit(this);
    input_->setPlaceholderText(QString("Ask about %1…").arg(entry_title_));
    input_->setStyleSheet(input_ss());
    input_->setMinimumHeight(32);
    row->addWidget(input_, 1);

    send_btn_ = new QPushButton(QString::fromUtf8("Ask  →"), this);
    send_btn_->setCursor(Qt::PointingHandCursor);
    send_btn_->setMinimumHeight(32);
    send_btn_->setStyleSheet(QString("QPushButton { color: %1; background: %2; border: 1px solid %3;"
                                     " padding: 6px 14px; font-size: 11px; font-weight: bold; %4 }"
                                     "QPushButton:hover { color: %5; border-color: %5; background: %6; }"
                                     "QPushButton:disabled { color: %7; }")
                                 .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(),
                                      ui::colors::BORDER_DIM(), MONO, ui::colors::AMBER(),
                                      ui::colors::ACCENT_BG(), ui::colors::TEXT_DIM()));
    row->addWidget(send_btn_);
    vl->addLayout(row);

    // ── Quick-action chips (under input — also always visible) ────────────────
    auto* chips = new QHBoxLayout;
    chips->setSpacing(4);
    chips->setContentsMargins(0, 2, 0, 2);

    example_btn_ = new QPushButton("Example", this);
    quiz_btn_ = new QPushButton("Quiz me", this);
    apply_btn_ = new QPushButton("Apply to portfolio", this);
    for (auto* b : {example_btn_, quiz_btn_, apply_btn_}) {
        b->setStyleSheet(chip_ss());
        b->setCursor(Qt::PointingHandCursor);
        chips->addWidget(b);
    }
    chips->addStretch();
    vl->addLayout(chips);

    connect(example_btn_, &QPushButton::clicked, this, [this]() { quick_action("example"); });
    connect(quiz_btn_, &QPushButton::clicked, this, [this]() { quick_action("quiz"); });
    connect(apply_btn_, &QPushButton::clicked, this, [this]() { quick_action("apply"); });

    // ── Thread view (BELOW — scrolls within, won't push input off-screen) ────
    thread_view_ = new QTextBrowser(this);
    thread_view_->setOpenExternalLinks(true);
    thread_view_->setStyleSheet(thread_view_ss());
    thread_view_->setMinimumHeight(140);
    thread_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vl->addWidget(thread_view_, 1);

    connect(send_btn_, &QPushButton::clicked, this, [this]() {
        const auto t = input_->text().trimmed();
        if (!t.isEmpty())
            send_user_message(t);
    });
    connect(input_, &QLineEdit::returnPressed, this, [this]() {
        const auto t = input_->text().trimmed();
        if (!t.isEmpty())
            send_user_message(t);
    });

    // Initial status — flag clearly when LLM isn't configured so the user
    // knows the input is real but won't currently get a response.
    auto& svc = ai_chat::LlmService::instance();
    if (svc.is_configured()) {
        status_->setText(QString("AI · context: %1   (model: %2)")
                             .arg(entry_title_)
                             .arg(svc.active_model().isEmpty() ? svc.active_provider() : svc.active_model()));
    } else {
        status_->setText("AI · LLM not configured — open Settings → AI to add an API key.");
    }
    redraw_thread();
}

QString AITutorPanel::build_system_prompt() const {
    QString sys =
        QString("You are a finance tutor embedded in finterm, a Bloomberg-style desktop terminal. "
                "Your audience is amateur retail investors who want plain-English explanations grounded in "
                "concrete examples. Avoid unexplained jargon. Be concise — usually 3-6 short paragraphs or "
                "a tight bulleted list. Never invent numbers; if asked for a current price or ratio you don't "
                "know, say so and suggest opening the relevant finterm screen.\n\n"
                "The user is currently reading the '%1' entry%2. Use this body as your authoritative source:\n\n"
                "----- BEGIN ENTRY BODY -----\n%3\n----- END ENTRY BODY -----")
            .arg(entry_title_,
                 abbreviation_.isEmpty() ? QString{} : QString(" (%1)").arg(abbreviation_),
                 entry_body_);
    return sys;
}

void AITutorPanel::quick_action(const QString& label) {
    QString prompt;
    if (label == "example") {
        prompt = QString("Walk me through one concrete worked example of %1 using a well-known publicly traded "
                         "company. Show the formula, plug in plausible numbers, and explain what the result "
                         "would mean to an investor.")
                     .arg(entry_title_);
    } else if (label == "quiz") {
        prompt = QString("Quiz me on %1. Ask 3 multiple-choice questions ranging easy → hard, then wait for my "
                         "answers. Reveal the correct answers and explanations only after I respond.")
                     .arg(entry_title_);
    } else if (label == "apply") {
        prompt = QString("Suppose I hold a typical retirement portfolio (60%% US equities, 30%% bonds, 10%% "
                         "international). How does %1 apply to my situation, what specific things should I "
                         "look for in finterm to evaluate it, and what action — if any — would it suggest?")
                     .arg(entry_title_);
    }
    if (!prompt.isEmpty())
        send_user_message(prompt);
}

void AITutorPanel::send_user_message(const QString& text) {
    if (busy_)
        return;

    auto& svc = ai_chat::LlmService::instance();
    if (!svc.is_configured()) {
        append_message("assistant",
                       "*LLM is not configured. Open Settings → AI to add an API key for one of the "
                       "supported providers.*");
        return;
    }

    append_message("user", text);
    input_->clear();
    streaming_buffer_.clear();
    set_busy(true);

    // Build conversation history. System prompt first, then prior thread
    // (excluding the user message we just appended — service takes that
    // separately as user_message).
    std::vector<ai_chat::ConversationMessage> hist;
    hist.push_back({QStringLiteral("system"), build_system_prompt()});
    for (int i = 0; i < thread_.size() - 1; ++i)
        hist.push_back({thread_[i].role, thread_[i].content});

    // Streaming callback fires on a background thread. Capture a QPointer
    // so a destroyed panel (e.g. user closes the tab mid-response) doesn't
    // dereference dead memory.
    QPointer<AITutorPanel> guard(this);
    svc.chat_streaming(
        text, hist,
        [guard](const QString& chunk, bool done) {
            if (!guard)
                return;
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, chunk, done]() {
                    if (!guard)
                        return;
                    guard->streaming_buffer_ += chunk;
                    if (guard->thread_.isEmpty() || guard->thread_.last().role != "assistant") {
                        guard->thread_.push_back({"assistant", guard->streaming_buffer_});
                    } else {
                        guard->thread_.last().content = guard->streaming_buffer_;
                    }
                    guard->redraw_thread();
                    if (done)
                        guard->set_busy(false);
                },
                Qt::QueuedConnection);
        },
        /*use_tools=*/false);
}

void AITutorPanel::append_message(const QString& role, const QString& content) {
    thread_.push_back({role, content});
    redraw_thread();
}

void AITutorPanel::redraw_thread() {
    QString html;
    html.reserve(thread_.size() * 200);
    const QString user_color = ui::colors::CYAN();
    const QString asst_color = ui::colors::TEXT_PRIMARY();
    const QString role_color = ui::colors::TEXT_TERTIARY();

    for (const auto& m : thread_) {
        const bool is_user = (m.role == "user");
        const QString role_label = is_user ? "YOU" : "AI";
        const QString body_color = is_user ? user_color : asst_color;
        QString safe = m.content.toHtmlEscaped().replace('\n', "<br>");
        html += QString("<div style='margin-bottom:10px;'>"
                        "<span style='color:%1; font-size:9px; font-weight:bold; letter-spacing:1px;'>%2</span><br>"
                        "<span style='color:%3;'>%4</span>"
                        "</div>")
                    .arg(role_color, role_label, body_color, safe);
    }
    if (thread_.isEmpty()) {
        html = QString("<div style='color:%1; font-size:11px;'>"
                       "Type a question above, or click <b>Example</b> / <b>Quiz me</b> / "
                       "<b>Apply to portfolio</b> to start.</div>")
                   .arg(ui::colors::TEXT_DIM());
    }
    thread_view_->setHtml(html);
    auto* sb = thread_view_->verticalScrollBar();
    if (sb)
        sb->setValue(sb->maximum());
}

void AITutorPanel::set_busy(bool busy) {
    busy_ = busy;
    input_->setEnabled(!busy);
    send_btn_->setEnabled(!busy);
    example_btn_->setEnabled(!busy);
    quiz_btn_->setEnabled(!busy);
    apply_btn_->setEnabled(!busy);
    if (busy) {
        status_->setText(QString::fromUtf8("AI · …thinking"));
    } else {
        auto& svc = ai_chat::LlmService::instance();
        if (svc.is_configured()) {
            status_->setText(QString("AI · context: %1   (model: %2)")
                                 .arg(entry_title_)
                                 .arg(svc.active_model().isEmpty() ? svc.active_provider() : svc.active_model()));
        } else {
            status_->setText("AI · LLM not configured — open Settings → AI to add an API key.");
        }
    }
}

} // namespace fincept::knowledge

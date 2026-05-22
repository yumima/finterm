#include "services/agents/InlineCompletionController.h"

#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "storage/repositories/SettingsRepository.h"

#include <QFutureWatcher>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace fincept::services {

namespace {

constexpr int kIdleDebounceMs = 450;
constexpr int kMinPrefixChars = 8;     // don't pester the LLM for two-char prefixes
constexpr int kPromptTailChars = 600;  // how much preceding context we send
constexpr const char* kIcTag = "InlineCompletion";

/// Cap on how much the model is asked to continue.  Inline completion
/// should feel like a hint, not a paragraph dump.
constexpr int kMaxCompletionChars = 200;

QColor ghost_color() { return QColor(140, 140, 140); }

} // anonymous namespace

bool InlineCompletionController::enabled() {
    auto r = SettingsRepository::instance().get(QStringLiteral("inline_completion.enabled"));
    if (r.is_err())
        return false;
    const QString v = r.value().trimmed().toLower();
    return v == QStringLiteral("true") || v == QStringLiteral("1") || v == QStringLiteral("yes");
}

InlineCompletionController::InlineCompletionController(QPlainTextEdit* editor, QObject* parent)
    : QObject(parent), editor_(editor) {
    if (!editor_)
        return;
    idle_timer_ = new QTimer(this);
    idle_timer_->setSingleShot(true);
    idle_timer_->setInterval(kIdleDebounceMs);
    connect(idle_timer_, &QTimer::timeout, this, &InlineCompletionController::on_idle_timeout);

    connect(editor_, &QPlainTextEdit::textChanged,
            this, &InlineCompletionController::on_text_changed);

    // Tab / Esc / any non-Tab key are caught via event filter (so we
    // can swallow Tab on accept; the editor's default Tab handler
    // would insert a tab character we don't want).
    editor_->installEventFilter(this);
}

InlineCompletionController::~InlineCompletionController() = default;

bool InlineCompletionController::eventFilter(QObject* watched, QEvent* event) {
    if (watched != editor_ || event->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);
    auto* key = static_cast<QKeyEvent*>(event);

    if (pending_len_ > 0) {
        if (key->key() == Qt::Key_Tab) {
            accept_pending();
            return true;  // swallow — don't let Qt insert a tab
        }
        // Any other key cancels the suggestion.  We do not swallow
        // here — the editor still processes the keystroke normally,
        // but we delete our ghost text first.  on_text_changed will
        // fire from the user's keystroke and start a fresh debounce.
        cancel_pending();
    }
    return QObject::eventFilter(watched, event);
}

void InlineCompletionController::on_text_changed() {
    if (!enabled())
        return;
    if (ignore_next_change_) {
        ignore_next_change_ = false;
        return;
    }
    // A real user edit invalidates any pending suggestion.  cancel_pending
    // sets ignore_next_change_ before the delete so we don't recurse.
    if (pending_len_ > 0)
        cancel_pending();
    schedule_idle();
}

void InlineCompletionController::schedule_idle() {
    idle_timer_->start();   // restart from now — debounce
}

void InlineCompletionController::on_idle_timeout() {
    if (!editor_ || !enabled())
        return;
    if (!ai_chat::LlmService::instance().is_configured())
        return;
    if (pending_len_ > 0)
        return;  // a suggestion is already visible

    const QString prefix = current_prefix_for_completion();
    if (prefix.size() < kMinPrefixChars)
        return;

    // Cache hit — render immediately without round-tripping.
    if (const QString hit = cache_get(prefix); !hit.isEmpty()) {
        pending_prefix_ = prefix;
        show_ghost(hit);
        return;
    }

    // Already a watcher in flight?  Don't stack — the previous run
    // will resolve and either show ghost text (which schedule_idle's
    // debounce coalesced under) or no-op.
    if (watcher_)
        return;

    pending_prefix_ = prefix;
    const QString context_tail = prefix.right(kPromptTailChars);
    LOG_DEBUG(kIcTag, QString("requesting completion for %1 chars").arg(context_tail.size()));

    watcher_ = new QFutureWatcher<ai_chat::LlmResponse>(this);
    connect(watcher_, &QFutureWatcher<ai_chat::LlmResponse>::finished, this,
            &InlineCompletionController::on_completion_arrived);
    auto future = QtConcurrent::run([context_tail]() {
        const QString prompt = QStringLiteral(
            "Continue the following text naturally.  Output ONLY the "
            "continuation — no preface, no quotation marks, no labels.  "
            "Stop at the end of a sentence or %1 characters.\n\n%2")
                                   .arg(kMaxCompletionChars)
                                   .arg(context_tail);
        return ai_chat::LlmService::instance().chat(
            prompt, std::vector<ai_chat::ConversationMessage>{}, /*use_tools=*/false);
    });
    watcher_->setFuture(future);
}

void InlineCompletionController::on_completion_arrived() {
    auto* w = watcher_;
    watcher_ = nullptr;
    if (!w)
        return;
    const auto resp = w->result();
    w->deleteLater();
    if (!editor_ || !enabled())
        return;
    if (!resp.success || resp.content.isEmpty()) {
        // Don't surface failures — the user didn't ask, we shouldn't
        // distract them.  Logged for the dev surface.
        if (!resp.error.isEmpty())
            LOG_DEBUG(kIcTag, QString("completion error: %1").arg(resp.error));
        return;
    }
    QString completion = resp.content.trimmed();
    if (completion.size() > kMaxCompletionChars)
        completion = completion.left(kMaxCompletionChars);
    cache_put(pending_prefix_, completion);

    // The user may have typed more text while we were waiting.  If
    // the editor's current prefix no longer matches what we asked
    // for, the suggestion is stale — drop it.
    if (current_prefix_for_completion() != pending_prefix_)
        return;

    // If the user is mid-selection (drag / shift-arrow), don't
    // interrupt — Cursor / Copilot suppress in this state too.
    // setTextCursor with a collapsed cursor would clobber the
    // selection and there's no undo for cursor moves.
    if (editor_->textCursor().hasSelection())
        return;

    show_ghost(completion);
}

void InlineCompletionController::show_ghost(const QString& completion) {
    if (!editor_ || completion.isEmpty())
        return;
    auto* doc = editor_->document();
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    const int anchor = cursor.position();

    QTextCharFormat fmt;
    fmt.setForeground(ghost_color());
    fmt.setFontItalic(true);

    ignore_next_change_ = true;  // suppress the textChanged that the insert fires
    cursor.beginEditBlock();
    cursor.insertText(completion, fmt);
    cursor.endEditBlock();

    pending_anchor_ = anchor;
    pending_len_ = completion.size();

    // Move the visible cursor back to where the user was typing so
    // the suggestion looks like a passive hint, not new input.
    QTextCursor user_cursor = editor_->textCursor();
    user_cursor.setPosition(anchor);
    editor_->setTextCursor(user_cursor);
}

void InlineCompletionController::accept_pending() {
    if (!editor_ || pending_len_ <= 0)
        return;
    // Strip the grayed format from the pending range — text stays,
    // visual treatment becomes normal.
    auto* doc = editor_->document();
    QTextCursor cursor(doc);
    cursor.setPosition(pending_anchor_);
    cursor.setPosition(pending_anchor_ + pending_len_, QTextCursor::KeepAnchor);
    QTextCharFormat fmt;
    fmt.setForeground(QBrush());  // default
    fmt.setFontItalic(false);
    ignore_next_change_ = true;
    cursor.mergeCharFormat(fmt);

    // Place the editing cursor at the end of the accepted text.
    QTextCursor end_cursor = editor_->textCursor();
    end_cursor.setPosition(pending_anchor_ + pending_len_);
    editor_->setTextCursor(end_cursor);

    pending_anchor_ = -1;
    pending_len_ = 0;
    pending_prefix_.clear();
}

void InlineCompletionController::cancel_pending() {
    if (!editor_ || pending_len_ <= 0)
        return;
    auto* doc = editor_->document();
    QTextCursor cursor(doc);
    cursor.setPosition(pending_anchor_);
    cursor.setPosition(pending_anchor_ + pending_len_, QTextCursor::KeepAnchor);
    ignore_next_change_ = true;
    cursor.removeSelectedText();

    pending_anchor_ = -1;
    pending_len_ = 0;
    pending_prefix_.clear();
}

QString InlineCompletionController::current_prefix_for_completion() const {
    if (!editor_)
        return {};
    // We always complete at the end of the document — composer UX
    // doesn't have a meaningful mid-document cursor.  If we ever
    // attach this to an editor with mid-document edits, swap this
    // for textCursor().position() and only show suggestions when
    // the cursor is at end-of-block.
    return editor_->toPlainText();
}

void InlineCompletionController::cache_put(const QString& prefix, const QString& completion) {
    // True LRU on update — demote the existing entry to the back
    // before re-appending.  Without this, a re-cache for the same
    // prefix updates the value but leaves the order stale, so
    // eviction can drop the most-recently-touched entry.
    if (cache_.contains(prefix))
        cache_order_.removeOne(prefix);
    cache_order_.append(prefix);
    cache_[prefix] = completion;
    while (cache_order_.size() > kInlineCacheCap) {
        const QString evicted = cache_order_.takeFirst();
        cache_.remove(evicted);
    }
}

QString InlineCompletionController::cache_get(const QString& prefix) const {
    return cache_.value(prefix);
}

} // namespace fincept::services

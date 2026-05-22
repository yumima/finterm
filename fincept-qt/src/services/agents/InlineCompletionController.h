#pragma once
// InlineCompletionController.h — IDE-style ghost-text suggestions in
// a QPlainTextEdit (Track 8 scaffold).
//
// Install on a target editor; the controller debounces text changes,
// asks LlmService for a continuation, and renders the result inline
// with a grayed-out QTextCharFormat.  User accepts with Tab (the
// format is removed, the text stays).  Any other input dismisses
// (the ghost text is deleted).
//
// Gated by SettingsRepository key `inline_completion.enabled` — off
// by default.  Latency is API-bound until a fast local model lands
// (Engine M1); the README acknowledges this honestly.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTextCursor>

class QPlainTextEdit;
class QTimer;
template <typename T> class QFutureWatcher;
namespace fincept::ai_chat { struct LlmResponse; }

namespace fincept::services {

class InlineCompletionController : public QObject {
    Q_OBJECT
  public:
    explicit InlineCompletionController(QPlainTextEdit* editor, QObject* parent = nullptr);
    ~InlineCompletionController() override;

    /// Read SettingsRepository.inline_completion.enabled.  Default false.
    static bool enabled();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:
    void on_text_changed();
    void on_idle_timeout();
    void on_completion_arrived();

  private:
    QPointer<QPlainTextEdit> editor_;
    QTimer* idle_timer_ = nullptr;
    QFutureWatcher<fincept::ai_chat::LlmResponse>* watcher_ = nullptr;

    // Ghost-text marker — when a completion is displayed inline, the
    // text in [pending_anchor_, pending_anchor_ + pending_len_) carries
    // the grayed format.  Empty span means "no pending completion".
    int pending_anchor_ = -1;
    int pending_len_ = 0;
    QString pending_prefix_;     // text we requested a completion for
    bool ignore_next_change_ = false;  // guard against our own insert re-triggering

    // LRU on the prefix → completion mapping.  Avoids re-asking the
    // model for the same context after a user dismisses + retypes.
    QHash<QString, QString> cache_;
    QStringList cache_order_;
    static constexpr int kInlineCacheCap = 50;

    void schedule_idle();
    void cancel_pending();
    void accept_pending();
    void show_ghost(const QString& completion);
    void cache_put(const QString& prefix, const QString& completion);
    QString cache_get(const QString& prefix) const;
    QString current_prefix_for_completion() const;
};

} // namespace fincept::services

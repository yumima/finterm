#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QTextBrowser;
class QLabel;

namespace fincept::knowledge {

struct KnowledgeEntry;

/// Embedded compact AI tutor — talks directly to LlmService. Pre-seeds
/// the system prompt with the current knowledge entry's context (title,
/// abbreviation, body). Maintains an ephemeral chat thread per entry that
/// is cleared when the panel is destroyed.
class AITutorPanel : public QWidget {
    Q_OBJECT
  public:
    explicit AITutorPanel(const KnowledgeEntry& entry, QWidget* parent = nullptr);

  private:
    struct Msg {
        QString role;     // "user" | "assistant"
        QString content;
    };

    void send_user_message(const QString& text);
    void append_message(const QString& role, const QString& content);
    void redraw_thread();
    void set_busy(bool busy);
    QString build_system_prompt() const;
    void quick_action(const QString& label);

    QString entry_id_;
    QString entry_title_;
    QString entry_subtitle_;
    QString entry_body_;       ///< loaded once at construction
    QString abbreviation_;
    QVector<Msg> thread_;
    QString streaming_buffer_;
    bool busy_ = false;

    QLabel* status_ = nullptr;
    QTextBrowser* thread_view_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    QPushButton* example_btn_ = nullptr;
    QPushButton* quiz_btn_ = nullptr;
    QPushButton* apply_btn_ = nullptr;
};

} // namespace fincept::knowledge

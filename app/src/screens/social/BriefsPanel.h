#pragma once
// BriefsPanel.h — SOCIAL › BRIEFS: generate, review, correct, schedule and
// publish briefs (app/scripts/brief_drafts.py via social_app.py).
//
//   the list      every draft: this week's brief, theme briefs; state,
//                 schedule, published
//   the reader    the draft rendered (READ) — the week in brief, last week,
//                 next week — or its Markdown (EDIT). Private sections (the
//                 Nasdaq IPO calendar) show with a banner and are stripped
//                 from anything published.
//   posting       the X text (edited here; Bluesky / Mastodon use the
//                 issue's own text for their limits), the platforms ticked
//                 for it, and — once published — Post for any not yet
//                 posted. Every disabled control says why.
//   publishing    Schedule (the tick timer, or this screen while open) or
//                 Publish now — both confirm, neither runs on unsaved edits
//
// "Generate weekly brief" builds in the background (minutes); "New theme
// brief" gathers EDGAR facts for the tickers given, asks the AI for the
// overview (optional) and lays it out in the fixed structure.

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QWidget>

class QCheckBox;
class QDateTimeEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTextBrowser;
class QTimer;

namespace finterm::screens {

class BuildProgressView;

class BriefsPanel : public QWidget {
    Q_OBJECT
  public:
    explicit BriefsPanel(QWidget* parent = nullptr);
    void refresh();

  signals:
    /// A brief went public (or was posted): the PUBLISHED view and the
    /// history should reload.
    void published();

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

  private:
    void load_list();
    void load_draft(const QString& id);
    void render_list();
    void render_draft();
    void render_controls();
    void set_status(const QString& text, bool error = false);
    bool dirty() const;
    QString x_text() const;

    void generate_weekly(bool force);
    void cancel_build();
    /// A running build's page so far, in the reading view.
    void show_build();
    void show_page(const QString& body);
    void new_theme();
    void save();
    void revert();
    void discard();
    void schedule(bool accept_incomplete = false);
    void unschedule();
    void confirm_publish();
    void publish(bool accept_incomplete);
    void set_post_to();
    void post_now();
    QStringList ticked_platforms() const;
    static QStringList awaiting_posts(const QJsonObject& draft);

    QJsonObject list_;  ///< the "drafts" answer
    QJsonObject draft_; ///< the selected draft (the "draft" answer)
    QString current_;   ///< selected draft id
    QString loaded_body_, loaded_x_;
    QString live_body_;   ///< the running build's page on show
    QString shown_build_; ///< the build that was brought up by itself
    int list_seq_ = 0, draft_seq_ = 0;
    bool busy_ = false;
    bool list_inflight_ = false;

    QTableWidget* table_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* chips_ = nullptr;
    QPushButton* gen_btn_ = nullptr;
    QPushButton* read_btn_ = nullptr;
    QPushButton* edit_btn_ = nullptr;
    QPushButton* save_btn_ = nullptr;
    QPushButton* revert_btn_ = nullptr;
    QPushButton* rebuild_btn_ = nullptr;
    QPushButton* delete_btn_ = nullptr;
    QStackedWidget* view_ = nullptr;
    QTextBrowser* preview_ = nullptr;
    QPlainTextEdit* source_ = nullptr;
    QPlainTextEdit* x_edit_ = nullptr;
    QLabel* x_count_ = nullptr;
    QMap<QString, QCheckBox*> post_to_; ///< x / bluesky / mastodon
    QPushButton* post_now_btn_ = nullptr;
    QLabel* post_why_ = nullptr;
    QDateTimeEdit* when_ = nullptr;
    QPushButton* schedule_btn_ = nullptr;
    QPushButton* unschedule_btn_ = nullptr;
    QPushButton* publish_btn_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* log_ = nullptr;
    BuildProgressView* build_ = nullptr;
    QSplitter* split_ = nullptr;
    QWidget* post_box_ = nullptr;
    QTimer* poll_ = nullptr;
    bool setting_ = false;
};

} // namespace finterm::screens

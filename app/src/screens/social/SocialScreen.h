#pragma once
// SocialScreen.h — SOCIAL: publishing, where NEWS is for reading.
//
// Three panes over the existing Python backend (app/scripts/social_app.py):
//   CHANNELS  the networks, which are connected and as whom; Connect (X's
//             PIN, Mastodon's code, Bluesky's app password) and Disconnect.
//             Networks the backend has not enabled are "coming soon".
//   BRIEFS    (BriefsPanel) generate this week's brief or a theme brief,
//             read it, correct it, schedule it or publish it now
//   ISSUE     the PUBLISHED weekly brief (its rows, whether its page is live,
//             what has been posted) and the posting HISTORY
//   COMPOSER  per-network draft prefilled with the generated text, a live
//             counter, the PRE-FLIGHT checklist and Publish — which always
//             asks first and is the only way this screen posts. For Reddit
//             and Hacker News: the launch drafts, copied by hand and recorded
//             once posted — nothing is submitted by the app.
// The composer posts only on Publish; a brief scheduled or published from
// BRIEFS with "post to X" ticked is posted once its page is live. Pure logic:
// SocialModel.h.

#include "screens/social/SocialModel.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace finterm::screens {
class BriefsPanel;
}

namespace finterm::screens {

class SocialScreen : public QWidget {
    Q_OBJECT
  public:
    explicit SocialScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    QWidget* build_channels_pane();
    QWidget* build_issue_pane();
    QWidget* build_composer_pane();
    QWidget* build_manual_page();
    void apply_styles();

    void refresh_all();
    void load_channels();
    void load_brief();
    void load_history();
    void load_launch();

    void render_channels();
    void render_brief();
    void render_history();
    void render_composer();
    void render_busy();
    void render_launch();
    void render_launch_counts();

    void adopt_generated_texts();
    void select_network(const QString& network);
    void open_x_connect();
    void open_bsky_connect();
    void open_mastodon_connect();
    void confirm_disconnect(const QString& network);
    void adopt_launch_templates();
    const social::LaunchTarget* current_launch_target() const;
    void show_launch_target(const QString& id);
    void copy_to_clipboard(const QString& text, const QString& what);
    void open_submit_page();
    void open_mark_posted();
    void confirm_publish();
    void publish(const QString& network, const QString& issue, const QString& text);
    void set_status(const QString& html, bool error);

    // Last good answers (kept through a failed refresh, which is shown).
    QVector<social::Channel> channels_;
    bool channels_loaded_ = false;
    QString channels_error_;
    social::Brief brief_;
    QString brief_error_;
    QVector<social::HistoryRow> history_;
    bool history_loaded_ = false;
    QString history_error_;
    social::LaunchSet launch_;
    QString launch_error_;

    // Composer: one draft per network, reset when a new issue arrives.
    QString network_ = QStringLiteral("x");
    QMap<QString, QString> drafts_;
    QMap<QString, QString> generated_;
    QString drafts_issue_;
    bool setting_editor_ = false;

    // Launch drafts (Reddit / HN): one per target, kept while switching.
    struct LaunchDraft {
        QString title, body, url, comment;
        bool operator==(const LaunchDraft&) const = default;
    };
    QMap<QString, LaunchDraft> launch_drafts_;
    QMap<QString, LaunchDraft> launch_templates_; ///< what each draft started from
    QMap<QString, QString> launch_target_;        ///< network -> selected target id

    // A newer request supersedes an older one still in flight.
    int channels_seq_ = 0, brief_seq_ = 0, history_seq_ = 0, launch_seq_ = 0;
    bool channels_busy_ = false, brief_busy_ = false, history_busy_ = false, publish_busy_ = false;
    bool launch_busy_ = false, mark_busy_ = false;
    QDateTime last_refresh_;

    BriefsPanel* briefs_ = nullptr;
    // CHANNELS
    QVBoxLayout* channels_box_ = nullptr;
    // ISSUE
    QLabel* issue_title_ = nullptr;
    QLabel* chip_live_ = nullptr;
    QLabel* chip_week_ = nullptr;
    QLabel* chip_posted_ = nullptr;
    QPushButton* open_page_btn_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QLabel* busy_label_ = nullptr;
    QLabel* issue_error_ = nullptr;
    QTableWidget* rows_table_ = nullptr;
    QLabel* rows_note_ = nullptr;
    QTableWidget* history_table_ = nullptr;
    QLabel* history_note_ = nullptr;
    // COMPOSER
    QButtonGroup* network_group_ = nullptr;
    QMap<QString, QPushButton*> network_btns_;
    QStackedWidget* composer_stack_ = nullptr;
    QLabel* posting_as_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    QLabel* counter_ = nullptr;
    QPushButton* reset_btn_ = nullptr;
    QVector<QLabel*> check_labels_;
    QPushButton* publish_btn_ = nullptr;
    QLabel* status_label_ = nullptr;
    // COMPOSER, manual networks
    QComboBox* launch_combo_ = nullptr;
    QLabel* launch_info_ = nullptr;
    QLineEdit* launch_title_ = nullptr;
    QLabel* launch_title_count_ = nullptr;
    QLabel* launch_url_label_ = nullptr;
    QLineEdit* launch_url_ = nullptr;
    QLabel* launch_body_label_ = nullptr;
    QPlainTextEdit* launch_body_ = nullptr;
    QLabel* launch_comment_label_ = nullptr;
    QPlainTextEdit* launch_comment_ = nullptr;
    QPushButton* copy_title_btn_ = nullptr;
    QPushButton* copy_body_btn_ = nullptr;
    QPushButton* copy_comment_btn_ = nullptr;
    QPushButton* open_submit_btn_ = nullptr;
    QPushButton* mark_posted_btn_ = nullptr;
    QPushButton* launch_reset_btn_ = nullptr;
    QLabel* launch_rules_ = nullptr;
};

} // namespace finterm::screens

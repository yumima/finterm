#pragma once
// SocialScreen.h — SOCIAL: publishing, where NEWS is for reading.
//
// Three panes over the existing Python backend (app/scripts/social_app.py):
//   CHANNELS  the networks, which are connected and as whom; X's PIN sign-in
//   ISSUE     the PUBLISHED weekly brief (its rows, whether its page is live,
//             what has been posted) and the posting HISTORY
//   COMPOSER  per-network draft prefilled with the generated text, a live
//             counter, the PRE-FLIGHT checklist and Publish — which always
//             asks first and is the only way this screen posts.
// The weekly timer posts on its own; this screen never does. Pure logic:
// SocialModel.h.

#include "screens/social/SocialModel.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

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
    void apply_styles();

    void refresh_all();
    void load_channels();
    void load_brief();
    void load_history();

    void render_channels();
    void render_brief();
    void render_history();
    void render_composer();
    void render_busy();

    void adopt_generated_texts();
    void select_network(const QString& network);
    void open_x_connect();
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

    // Composer: one draft per network, reset when a new issue arrives.
    QString network_ = QStringLiteral("x");
    QMap<QString, QString> drafts_;
    QMap<QString, QString> generated_;
    QString drafts_issue_;
    bool setting_editor_ = false;

    // A newer request supersedes an older one still in flight.
    int channels_seq_ = 0, brief_seq_ = 0, history_seq_ = 0;
    bool channels_busy_ = false, brief_busy_ = false, history_busy_ = false, publish_busy_ = false;
    QDateTime last_refresh_;

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
    QLabel* posting_as_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    QLabel* counter_ = nullptr;
    QPushButton* reset_btn_ = nullptr;
    QVector<QLabel*> check_labels_;
    QPushButton* publish_btn_ = nullptr;
    QLabel* status_label_ = nullptr;
};

} // namespace finterm::screens

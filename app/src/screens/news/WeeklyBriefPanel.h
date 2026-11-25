#pragma once
// WeeklyBriefPanel — NEWS › What's On › WEEKLY BRIEF: the weekly brief's
// control panel, in the drawer beside WHAT'S ON.
//
// The brief is a snapshot of the week just ended and what to watch in the
// next, to read and to share. This panel:
//   THIS WEEK    the current issue — built / being built / published — and
//                where it has been posted, with its link
//   ACTIONS      Generate (a fresh snapshot, also of a published brief) ·
//                Review (the workspace beside the drawer: read, correct,
//                schedule, publish) · Post (a draft is published first; to
//                the platforms ticked below, again only when asked). Off
//                while something runs; a click that cannot act says what
//                it needs. A build shows its steps here and fills the
//                page in beside the drawer; Cancel stops it.
//   TOPICS       which sections it carries (at a glance, markets and economy, IPOs
//                and lock-ups, insider buying, fund moves, screen leaders)
//   DETAIL       how many rows each shows (full / standard / short)
//   PLATFORMS    where it is posted once its page is live — each platform
//                once, unless Post is asked for again; a platform not connected says so (connect it in
//                the workspace's CHANNELS)
//   AUTOMATION   Sunday's run builds a draft for your review, or publishes
//                and posts on its own
// Settings: app/scripts/brief_drafts.py settings (the build and the Sunday
// timer read the same file).

#include <QJsonObject>
#include <QMap>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

namespace finterm::screens {

class BuildProgressView;

class WeeklyBriefPanel : public QWidget {
    Q_OBJECT
  public:
    explicit WeeklyBriefPanel(QWidget* parent = nullptr);

  signals:
    /// Open the brief workspace beside the drawer ("social", {"tab": …}).
    void open_beside(const QString& screen_id, const QVariantMap& args);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void refresh();
    void load_settings();
    void save_settings();
    void render();
    void generate(bool force);
    void cancel_build();
    void render_actions();
    void follow_build();
    /// One draft's answer, into the list: shown before the next poll.
    void adopt(const QJsonObject& draft);
    void post();
    /// Begin a call to the backend, or say that one is still running.
    bool start(const QString& what);
    void say(const QString& text, const QString& colour = {});
    bool ask(const QString& title, const QString& question);
    QJsonObject current_weekly() const;

    QJsonObject settings_;
    QJsonObject drafts_;
    QJsonObject channels_;
    bool busy_ = false;
    QString built_before_; ///< the draft on disk when the build was first seen
    bool building_ = false; ///< a build was seen running: its end is said
    bool loading_ = false; ///< settings being applied to the controls: no save

    QLabel* issue_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* generate_btn_ = nullptr;
    QPushButton* review_btn_ = nullptr;
    QPushButton* post_btn_ = nullptr;
    BuildProgressView* build_ = nullptr;
    QMap<QString, QCheckBox*> topics_;
    QComboBox* detail_ = nullptr;
    QMap<QString, QCheckBox*> platforms_;
    QMap<QString, QLabel*> platform_notes_;
    QButtonGroup* auto_ = nullptr;
    bool refresh_inflight_ = false;
    bool settings_loaded_ = false;
    QTimer* poll_ = nullptr;
};

} // namespace finterm::screens

#pragma once
// WeeklyBriefPanel — NEWS › What's On › WEEKLY BRIEF: the weekly brief's
// control panel, in the drawer beside WHAT'S ON.
//
// The brief is a snapshot of the week just ended and what to watch in the
// next, to read and to share. This panel:
//   THIS WEEK    the current issue — built / being built / published — and
//                where it has been posted, with its link
//   ACTIONS      Generate now (a fresh snapshot, on demand) · Review &
//                publish (the full workspace opens beside the drawer: read,
//                correct, schedule, publish) · Post now (a published brief,
//                to the platforms ticked below that it has not gone to yet)
//   TOPICS       which sections it carries (at a glance, markets and economy, insider
//                buying, fund moves, screen leaders, your private IPO calendar)
//   DETAIL       how many rows each shows (full / standard / short)
//   PLATFORMS    where it is posted once its page is live — each platform
//                once, ever; a platform not connected says so (connect it in
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
    void generate();
    void post_now();
    QJsonObject current_weekly() const;

    QJsonObject settings_;
    QJsonObject drafts_;
    QJsonObject channels_;
    bool busy_ = false;
    bool loading_ = false; ///< settings being applied to the controls: no save

    QLabel* issue_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* generate_btn_ = nullptr;
    QPushButton* review_btn_ = nullptr;
    QPushButton* post_btn_ = nullptr;
    QMap<QString, QCheckBox*> topics_;
    QComboBox* detail_ = nullptr;
    QMap<QString, QCheckBox*> platforms_;
    QMap<QString, QLabel*> platform_notes_;
    QButtonGroup* auto_ = nullptr;
    QString generate_tip_;
    bool refresh_inflight_ = false;
    bool settings_loaded_ = false;
    QTimer* poll_ = nullptr;
};

} // namespace finterm::screens

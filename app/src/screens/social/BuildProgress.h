#pragma once
// A weekly brief being built, as the app shows it. The generator keeps a file
// (tools/weekly_brief.py Progress): its steps, and the page as far as it is
// known. BuildWatch follows that file — one per app, so a build started in one
// panel is seen in the other and its end is told wherever the user is.
// BuildProgressView draws it: a bar, the steps with counts and times, Cancel.

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace finterm::screens {

struct BuildProgress {
    struct Step {
        QString label, state, detail; ///< state: wait | run | done | warn
        double part = 0, t0 = 0, t1 = 0;
    };
    bool valid = false, done = false;
    double started = 0; ///< seconds since the epoch
    QVector<Step> steps;
    QString body; ///< the page so far, Markdown

    static BuildProgress parse(const QJsonObject& o);
    /// 0..1: finished steps whole, a running one by its own count.
    double fraction() const;
};

class BuildWatch : public QObject {
    Q_OBJECT
  public:
    static BuildWatch& instance();
    /// Follow @p path (a draft's progress_path); "" when nothing is being built —
    /// from a fresh answer only: an old list would stop what another panel follows.
    void watch(const QString& path);
    const BuildProgress& progress() const { return progress_; }
    bool active() const { return !path_.isEmpty(); }
    int spin() const { return spin_; }

  signals:
    void updated();  ///< twice a second while a build runs
    void finished(); ///< the page is written

  private:
    BuildWatch();
    void tick();
    QString path_;
    qint64 stamp_ = -1;
    double told_ = 0; ///< the build whose end was told
    int spin_ = 0;
    BuildProgress progress_;
    QTimer* timer_ = nullptr;
};

class BuildProgressView : public QWidget {
    Q_OBJECT
  public:
    explicit BuildProgressView(QWidget* parent = nullptr);

  signals:
    void cancel();

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void render();
    QLabel* head_ = nullptr;
    QLabel* steps_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

} // namespace finterm::screens

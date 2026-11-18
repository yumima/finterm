#pragma once
// ScreenerService.h — the owner of the stock screener's runs
// (scripts/screener_data.py): the strategies' metadata, the scans with their
// progress lines, and the last scan of each universe on disk.
//
// A scan takes minutes on a cold price store, so it streams
// "PROGRESS <done> <total> <text>" lines on stderr; they are forwarded as they
// arrive. Every scan gets a generation, increasing across the app: a scan
// that finishes late never overwrites a newer saved result for its universe,
// and a caller compares generations to drop a superseded answer.

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace finterm::services {

class ScreenerService : public QObject {
    Q_OBJECT
  public:
    static ScreenerService& instance();

    struct Reply {
        bool ok = false;
        QJsonObject root; ///< screener_data.py's answer (parse with screener::parse_scan)
        QString error;    ///< why, when !ok (the script's words, or the runner's)
    };

    /// The strategies and their records, no lists (`meta`, a local read).
    /// Answered on the GUI thread after this returns; dropped with `ctx`.
    void fetch_meta(QObject* ctx, std::function<void(const Reply&)> done);

    /// Run every screen over the universe of stocks of at least `min_mcap`.
    /// Returns the scan's generation. `progress` gets each progress line;
    /// `done` the answer — both on the GUI thread after this returns, dropped
    /// with `ctx`. A good answer is saved as the universe's latest
    /// (latest_scan) unless a newer scan of it already was.
    using Progress = std::function<void(int done, int total, const QString& text)>;
    int run_scan(double min_mcap, QObject* ctx, Progress progress, std::function<void(const Reply&)> done);

    /// The universe's last saved scan (not an object when none), and the
    /// generation that saved it (-1: an earlier session's file).
    QJsonDocument latest_scan(double min_mcap) const;
    int saved_generation(double min_mcap) const { return saved_gen_.value(scan_file(min_mcap), -1); }

    /// Retire scans of an earlier file format.
    void prune_old_scans();

  private:
    ScreenerService() = default;
    Q_DISABLE_COPY(ScreenerService)

    static QString scan_file(double min_mcap);

    int gen_ = 0;
    QHash<QString, int> saved_gen_; ///< scan file → newest generation saved to it
};

} // namespace finterm::services

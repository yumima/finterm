#pragma once

#include "services/updater/UpdateInstaller.h"
#include "services/updater/UpdatePolicy.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <memory>
#include <optional>

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QNetworkReply;
class QWidget;

namespace finterm::services {

/// Cross-platform update service.
///
/// Fetches this project's manifest (updates.json, written by release.yml on
/// every tag), compares the running version with the manifest's entry for
/// this platform, and — when the user chooses to install — downloads the
/// installer, verifies its sha256 and launches it.
///
/// Two ways a check runs:
///  - On request (Help › Check for Updates, About): always runs, and reports
///    the result in a dialog — up to date, the error, or the offer.
///  - Automatically, once a day, ONLY after the user turned it on (the
///    "Check for updates automatically" setting; finterm makes no network
///    request the user didn't agree to). It never opens a dialog: a newer
///    version becomes an offer, which the status bar shows as a badge the
///    user can open when they choose.
///
/// The setting and the time of the last check are install-wide (QSettings),
/// not per profile: two profiles running at once share one daily check.
class UpdateService : public QObject {
    Q_OBJECT

  public:
    enum class AutoCheck {
        Unasked, ///< never decided — the status bar offers the choice once
        On,
        Off,
    };

    static UpdateService& instance();

    /// Run a check. interactive=true reports the result either way;
    /// interactive=false is the automatic check (no dialog, offer only).
    void check_for_updates(bool interactive);

    /// Start the automatic schedule: a first look shortly after the main
    /// window is up, then hourly looks that check only when a day has passed
    /// since the last successful check. Idempotent — the sign-in and unlock
    /// paths both call it. Does nothing until the user turned checks on.
    void start_automatic();

    /// Whether this build can be updated at all: a release version on a
    /// platform the manifest covers. Dev builds never check.
    bool updates_supported() const;

    AutoCheck auto_check() const;
    void set_auto_check(bool on);

    /// The newer version found by the last check, if any.
    bool has_offer() const { return !offer_.version.isEmpty(); }
    QString offered_version() const { return offer_.version; }

    /// Where the offer stands: its download running; verified and staged to
    /// replace this build in place (applied when the app quits, or now on
    /// Restart Now); or — where the build can't replace itself — its
    /// installer handed to the user (launched or revealed) this session.
    enum class Stage { Offered, Downloading, Ready, Handed };
    Stage stage() const;

    /// Whether this build replaces itself (an AppImage, a writable .app, an
    /// IFW install) — else an update is the installer the user runs.
    bool can_update_in_place() const { return in_place_.has_value(); }

    /// Apply the staged update now: the helper waits for this process, puts
    /// the new version in place and starts it; every window closes (saving
    /// its workspace) and the app quits.
    void restart_now();

    /// Quit without applying the staged update: another finterm was just
    /// started from this install (a profile switch). It stays staged.
    void hold_apply() { hold_apply_ = true; }

    /// What the last update did, reported once by the launch after it
    /// ("Updated to 4.0.11" / why it failed); empty when there is nothing.
    QString last_result() const { return last_result_; }
    bool last_result_ok() const { return last_result_ok_; }
    void clear_last_result();

    /// Open the install prompt for the current offer (the status-bar badge).
    void show_offer();

    /// Parent widget for dialogs (defaults to QApplication::activeWindow()).
    void set_dialog_parent(QWidget* parent) { dialog_parent_ = parent; }

  signals:
    /// Emitted after every check, regardless of result.
    void check_finished(bool update_found);

    /// The offer appeared, changed or went away.
    void offer_changed();

    /// The automatic-check setting changed.
    void auto_check_changed();

    /// stage() changed (a download started, failed, or was handed over).
    void stage_changed();

    /// Emitted after a successful verified download, before launch.
    void download_finished(const QString& filepath);

  private slots:
    void on_manifest_reply_finished();
    void on_download_reply_finished();
    void on_download_ready_read();
    void on_download_progress(qint64 received, qint64 total);

  private:
    explicit UpdateService(QObject* parent = nullptr);

    /// Return the lookup key for the current runtime (e.g. "windows-x64").
    /// Returns empty string if the platform/arch combination isn't supported.
    static QString current_platform_key();

    /// The automatic schedule's tick: checks only when on, supported, idle
    /// and due.
    void maybe_check_automatically();
    /// Show the offer an earlier run / another profile stored (release builds).
    void restore_saved_offer();
    void set_offer(const update_policy::Offer& offer);
    void prompt_install();
    void prompt_restart();
    /// `quiet`: started by the automatic schedule — a failure is logged and
    /// the badge goes back to the offer, no dialog.
    void start_download(const update_policy::Offer& offer, bool quiet);
    void download_failed(const QString& why);
    /// The automatic schedule may start (or retry) the offer's download.
    bool may_stage_quietly() const;
    /// Another instance already applied an update to this install.
    bool replaced_by_another_instance() const;
    /// The verified download is in place: Ready.
    void staged();
    /// The staged file vanished: back to the offer. True when it did.
    bool staged_file_gone();
    /// Another finterm runs from the install and the swap would pull files
    /// from under it (macOS bundle, Windows install — not an AppImage).
    bool blocked_by_other_instance() const;
    /// At quit: a staged update goes in (no relaunch).
    void apply_on_quit();
    void report_last_update();
    void launch_installer(const QString& path); // not in place: the user runs it
    void reveal_in_file_manager(const QString& path);
    void finish_check(bool update_found);
    /// `handed`: the verified installer was launched or revealed.
    void finish_download(bool handed = false);
    void show_error(const QString& text);
    QWidget* dialog_parent();

    static constexpr const char* DEFAULT_MANIFEST_URL =
        "https://raw.githubusercontent.com/yumima/finterm/main/updates.json";

    QNetworkAccessManager net_;
    QString manifest_url_ = DEFAULT_MANIFEST_URL;
    update_policy::Offer offer_;
    QString pending_local_path_; ///< where the verified download goes (staged / temp)
    QString download_version_;   ///< the version being downloaded
    QString handed_version_;     ///< the version whose installer was handed over this session
    QString pending_expected_sha256_;
    QString ready_version_; ///< staged, verified, waiting to be applied in place
    QString ready_sha256_;  ///< of the staged bytes
    QString failed_sha256_; ///< a background download that failed, and its back-off
    QDateTime retry_at_;
    int failed_tries_ = 0;
    bool download_quiet_ = false;
    bool hold_apply_ = false;      ///< a profile switch: this quit must not start the helper
    QDateTime session_hold_until_; ///< the Windows session may be ending
    bool applied_ = false; ///< the helper has been started (no second one at quit)
    std::optional<update_install::Target> in_place_;
    std::unique_ptr<QFile> part_; ///< the download being written (<path>.part)
    std::unique_ptr<QCryptographicHash> hasher_;
    QString last_result_;
    bool last_result_ok_ = false;
    QPointer<QWidget> dialog_parent_;
    QTimer schedule_;
    bool interactive_ = false;
    bool checking_ = false;    ///< a manifest request is in flight
    bool downloading_ = false; ///< an installer download is in flight
    bool prompt_open_ = false;
    bool prompt_pending_ = false; ///< prompt_install() asked while a dialog was open
    qint64 last_logged_ = 0;      ///< download progress logged up to
    bool automatic_started_ = false;
};

} // namespace finterm::services

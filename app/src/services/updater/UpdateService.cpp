#include "services/updater/UpdateService.h"

#include "core/logging/Logger.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <utility>

namespace finterm::services {

// ── Settings ────────────────────────────────────────────────────────────────

namespace {
// Install-wide (QSettings), not per profile — see the class comment.
constexpr const char* kAutoCheckKey = "updates/auto_check"; // "on" | "off"; absent = never asked
constexpr const char* kLastCheckKey = "updates/last_check"; // ISO-8601 UTC of the last successful check
// The newer version that check found (update_policy::offer_to_json): kept so a
// restart — or a second running profile, whose check isn't due — still shows
// the badge. Absent when the last check found nothing newer.
constexpr const char* kOfferKey = "updates/offer";
// A version that failed to start and was rolled back: never staged by the
// automatic schedule again (a check on request still offers it).
constexpr const char* kBadVersionKey = "updates/rolled_back_version";

// A version whose apply failed (not a rollback), and how many times: past two
// the automatic schedule stops staging it — it would fail at every quit.
constexpr const char* kFailedVersionKey = "updates/failed_version";
constexpr const char* kFailedCountKey = "updates/failed_count";

/// Not staged automatically again (a check on request still offers it).
bool auto_blocked(const QString& version) {
    if (version.isEmpty())
        return false;
    QSettings s;
    if (s.value(kBadVersionKey).toString() == version)
        return true;
    return s.value(kFailedVersionKey).toString() == version && s.value(kFailedCountKey).toInt() >= 2;
}
constexpr int kFirstLookDelayMs = 10 * 1000;
constexpr int kScheduleTickMs = 60 * 60 * 1000;
} // namespace

UpdateService& UpdateService::instance() {
    static UpdateService inst;
    return inst;
}

UpdateService::UpdateService(QObject* parent) : QObject(parent) {
    net_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // GitHub release asset downloads redirect across origins — allow the jump.
    net_.setStrictTransportSecurityEnabled(true);
    // Only a release build replaces itself (a dev tree is updated from git).
    if (updates_supported())
        target_ = update_install::in_place_target();
    in_place_ = target_;
    if (in_place_) {
        const auto saved = update_policy::offer_from_json(QSettings().value(kOfferKey).toByteArray());
        const bool wanted = update_policy::is_newer(QApplication::applicationVersion(), saved.version);
        update_install::sweep_stale(*in_place_, wanted ? update_install::staged_path(*in_place_, saved.version,
                                                                                    localized(saved).download_url)
                                                       : QString());
    }
    // A staged update goes in when the app quits.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &UpdateService::apply_on_quit);
}

QWidget* UpdateService::dialog_parent() {
    if (dialog_parent_)
        return dialog_parent_.data();
    return QApplication::activeWindow();
}

// ── Platform / arch detection ───────────────────────────────────────────────

QString UpdateService::current_platform_key() {
    const QString arch = QSysInfo::currentCpuArchitecture();
    // QSysInfo returns "x86_64", "i386", "arm64", "arm" etc.
    const bool is_arm64 = arch.contains(QLatin1String("arm64")) || arch.contains(QLatin1String("aarch64"));
    const bool is_x64 = arch.contains(QLatin1String("x86_64")) || arch.contains(QLatin1String("x64"));

#if defined(Q_OS_WIN)
    if (is_arm64)
        return QStringLiteral("windows-arm64");
    if (is_x64)
        return QStringLiteral("windows-x64");
    return {};
#elif defined(Q_OS_MACOS)
    if (is_arm64)
        return QStringLiteral("macos-arm64");
    if (is_x64)
        return QStringLiteral("macos-x64");
    return {};
#elif defined(Q_OS_LINUX)
    if (is_arm64)
        return QStringLiteral("linux-arm64");
    if (is_x64)
        return QStringLiteral("linux-x64");
    return {};
#else
    return {};
#endif
}

bool UpdateService::updates_supported() const {
    int maj = 0, min = 0, pat = 0;
    return update_policy::parse_version(QApplication::applicationVersion(), maj, min, pat) &&
           !current_platform_key().isEmpty();
}

UpdateService::AutoCheck UpdateService::auto_check() const {
    const QString v = QSettings().value(kAutoCheckKey).toString();
    if (v == QLatin1String("on"))
        return AutoCheck::On;
    if (v == QLatin1String("off"))
        return AutoCheck::Off;
    return AutoCheck::Unasked;
}

void UpdateService::set_auto_check(bool on) {
    const AutoCheck want = on ? AutoCheck::On : AutoCheck::Off;
    if (auto_check() == want)
        return;
    QSettings().setValue(kAutoCheckKey, on ? QStringLiteral("on") : QStringLiteral("off"));
    LOG_INFO("UpdateService", QString("Automatic update checks turned %1").arg(on ? "on" : "off"));
    emit auto_check_changed();
    // Turned on: look now rather than up to an hour from now (still gated on
    // a day having passed since the last check).
    if (on && automatic_started_)
        QTimer::singleShot(2000, this, &UpdateService::maybe_check_automatically);
}

// ── Automatic schedule ──────────────────────────────────────────────────────

void UpdateService::restore_saved_offer() {
    // Shown whatever the automatic setting: it is local (a check on request
    // stores it too), so the badge doesn't depend on how it was found. A dev
    // build can't compare versions — it must not drop the shared note.
    if (!updates_supported() || checking_ || downloading_ || prompt_open_)
        return;
    if (stage() == Stage::Handed)
        return; // this process's installer is out: keep what the user is finishing
    const auto saved = update_policy::offer_from_json(QSettings().value(kOfferKey).toByteArray());
    if (saved.version.isEmpty() ||
        !update_policy::is_newer(QApplication::applicationVersion(), saved.version)) {
        // Installed since, pulled from the manifest by another profile's
        // check, or unusable: nothing to show (set_offer drops the note too).
        set_offer({});
        return;
    }
    // Always: a re-published release keeps its version but not its URL/sha256
    // (set_offer signals only on a real change).
    set_offer(saved);
    // Found by an earlier run / another profile and not staged here yet.
    if (auto_check() == AutoCheck::On && may_stage_quietly())
        start_download(offer_, /*quiet=*/true);
}

void UpdateService::start_automatic() {
    report_last_update();
    restore_saved_offer();
    if (!automatic_started_) {
        automatic_started_ = true;
        schedule_.setInterval(kScheduleTickMs);
        connect(&schedule_, &QTimer::timeout, this, &UpdateService::maybe_check_automatically);
        schedule_.start();
    }
    // Each sign-in / unlock takes one look once the UI has settled; the look
    // only fetches when a day has passed, so this is cheap to repeat.
    QTimer::singleShot(kFirstLookDelayMs, this, &UpdateService::maybe_check_automatically);
}

void UpdateService::maybe_check_automatically() {
    if (auto_check() != AutoCheck::On || !updates_supported())
        return;
    if (checking_ || downloading_ || prompt_open_)
        return;
    // Another profile's check may have found a version since the last look.
    restore_saved_offer();
    const QDateTime last = QDateTime::fromString(QSettings().value(kLastCheckKey).toString(), Qt::ISODate);
    if (!update_policy::check_due(last, QDateTime::currentDateTimeUtc()))
        return;
    check_for_updates(/*interactive=*/false);
}

// ── Public entry point ──────────────────────────────────────────────────────

void UpdateService::check_for_updates(bool interactive) {
    if (downloading_) {
        if (interactive)
            QMessageBox::information(dialog_parent(), QStringLiteral("finterm"),
                                     QStringLiteral("finterm %1 is already downloading.").arg(download_version_));
        emit check_finished(true);
        return;
    }
    if (checking_) {
        // A check is already in flight (the automatic one): let it report
        // to the user who just asked, instead of starting a second request.
        if (interactive)
            interactive_ = true;
        LOG_INFO("UpdateService", "Check already in progress — joining it");
        return;
    }
    // Only the automatic schedule calls this non-interactively, and only once
    // the user turned automatic checks on (maybe_check_automatically).
    interactive_ = interactive;

    // Dev builds' version strings don't match the manifest format.
    const QString local_version = QApplication::applicationVersion();
    int maj = 0, min = 0, pat = 0;
    if (!update_policy::parse_version(local_version, maj, min, pat)) {
        LOG_INFO("UpdateService",
                 QString("Skipping update check — running version '%1' is not a release build").arg(local_version));
        if (interactive_)
            show_error(QStringLiteral("This is a development build (%1). Auto-update is disabled.").arg(local_version));
        interactive_ = false;
        emit check_finished(false);
        return;
    }

    const QString platform_key = current_platform_key();
    if (platform_key.isEmpty()) {
        LOG_WARN("UpdateService", QString("Unsupported platform/arch: %1 / %2")
                                      .arg(QSysInfo::kernelType(), QSysInfo::currentCpuArchitecture()));
        if (interactive_)
            show_error(QStringLiteral("Auto-update is not supported on this platform."));
        interactive_ = false;
        emit check_finished(false);
        return;
    }

    checking_ = true;
    LOG_INFO("UpdateService", QString("Checking for updates (%1) — platform=%2, local=%3, manifest=%4")
                                  .arg(interactive_ ? "on request" : "automatic", platform_key, local_version,
                                       manifest_url_));

    QNetworkRequest req{QUrl(manifest_url_)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // Only the version and platform — nothing about the user.
    req.setHeader(QNetworkRequest::UserAgentHeader, QString("finterm/%1 (%2)").arg(local_version, platform_key));
    req.setTransferTimeout(30 * 1000);
    QNetworkReply* reply = net_.get(req);
    connect(reply, &QNetworkReply::finished, this, &UpdateService::on_manifest_reply_finished);
}

// ── Manifest response ───────────────────────────────────────────────────────

void UpdateService::on_manifest_reply_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    // An automatic check that fails says nothing: it is retried on the next
    // hourly tick (the last-check time only moves on success).
    if (reply->error() != QNetworkReply::NoError) {
        LOG_WARN("UpdateService",
                 QString("Manifest fetch failed: %1 (%2)").arg(reply->errorString()).arg(reply->error()));
        if (interactive_)
            show_error(QStringLiteral("Could not reach the update server.\n\n%1").arg(reply->errorString()));
        finish_check(false);
        return;
    }

    // Turned off while this automatic check was in flight: drop its answer.
    if (!interactive_ && auto_check() != AutoCheck::On) {
        LOG_INFO("UpdateService", "Automatic checks were turned off — ignoring the in-flight check");
        finish_check(false);
        return;
    }

    update_policy::Offer entry;
    const auto result = update_policy::parse_manifest(reply->readAll(), current_platform_key(), entry);
    if (result == update_policy::ManifestResult::Malformed) {
        LOG_WARN("UpdateService", "Manifest is not a JSON object");
        if (interactive_)
            show_error(QStringLiteral("The update manifest is malformed."));
        finish_check(false);
        return;
    }

    if (result == update_policy::ManifestResult::Incomplete) {
        // A broken release manifest: retried hourly like an unreachable one,
        // so a fix published minutes later is picked up.
        LOG_WARN("UpdateService", QString("Manifest entry for '%1' is missing latest-version or download-url")
                                      .arg(current_platform_key()));
        if (interactive_)
            show_error(QStringLiteral("The update manifest is malformed."));
        finish_check(false);
        return;
    }

    // A usable answer, whatever it says: the daily check is done.
    QSettings().setValue(kLastCheckKey, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (result == update_policy::ManifestResult::NoEntry) {
        LOG_INFO("UpdateService", QString("Manifest has no entry for '%1' — no update available")
                                      .arg(current_platform_key()));
        set_offer({});
        if (interactive_)
            QMessageBox::information(dialog_parent(), QStringLiteral("finterm"),
                                     QStringLiteral("No updates are published for this platform."));
        finish_check(false);
        return;
    }
    const QString local_version = QApplication::applicationVersion();
    if (!update_policy::is_newer(local_version, entry.version)) {
        LOG_INFO("UpdateService",
                 QString("Already up to date — local=%1, remote=%2").arg(local_version, entry.version));
        set_offer({});
        if (interactive_) {
            QMessageBox::information(dialog_parent(), QStringLiteral("finterm"),
                                     QStringLiteral("You're running the latest version (%1).").arg(local_version));
        }
        finish_check(false);
        return;
    }

    LOG_INFO("UpdateService", QString("Update available: %1 → %2").arg(local_version, entry.version));
    set_offer(entry);
    const bool ask_now = interactive_;
    finish_check(true);
    // On request: ask right away. Automatic: where this build replaces itself
    // the download starts in the background (the badge then says READY);
    // otherwise the badge carries the offer until the user opens it.
    if (ask_now)
        prompt_install();
    else if (may_stage_quietly())
        start_download(offer_, /*quiet=*/true);
}

// ── Offer ───────────────────────────────────────────────────────────────────

update_policy::Offer UpdateService::localized(update_policy::Offer offer) const {
    if (target_ && target_->kind == QLatin1String("ifw") && !offer.inplace_url.isEmpty()) {
        offer.download_url = offer.inplace_url;
        offer.sha256 = offer.inplace_sha256;
    }
    return offer;
}

void UpdateService::set_offer(const update_policy::Offer& offer) {
    const update_policy::Offer here = localized(offer);
    const bool changed = here.version != offer_.version || here.download_url != offer_.download_url ||
                         here.sha256 != offer_.sha256;
    offer_ = here;
    // A release without the package is the installer the user runs.
    in_place_ = target_ && target_->kind == QLatin1String("ifw") && offer.inplace_url.isEmpty() && !offer.version.isEmpty()
                    ? std::nullopt
                    : target_;
    QSettings settings;
    if (offer.version.isEmpty())
        settings.remove(kOfferKey);
    else
        settings.setValue(kOfferKey, update_policy::offer_to_json(offer));
    if (changed)
        emit offer_changed();
}

UpdateService::Stage UpdateService::stage() const {
    if (downloading_)
        return Stage::Downloading;
    // The bytes too: a re-published release keeps its version, and the build
    // it replaced must not be the one that goes in.
    if (has_offer() && ready_version_ == offer_.version && ready_sha256_ == offer_.sha256)
        return Stage::Ready;
    if (has_offer() && handed_version_ == offer_.version)
        return Stage::Handed;
    return Stage::Offered;
}

bool UpdateService::may_stage_quietly() const {
    if (!in_place_ || stage() != Stage::Offered || auto_blocked(offer_.version))
        return false;
    // What another finterm put in is here already: this session can't apply.
    if (replaced_by_another_instance())
        return false;
    // A background download that failed waits (an hour, doubling to a day):
    // each try is the whole installer again.
    return failed_sha256_ != offer_.sha256 || QDateTime::currentDateTimeUtc() >= retry_at_;
}

void UpdateService::show_offer() {
    if (stage() == Stage::Ready)
        prompt_restart();
    else
        prompt_install();
}

void UpdateService::prompt_install() {
    if (!has_offer())
        return;
    if (prompt_open_) {
        prompt_pending_ = true; // shown when the open updater dialog closes
        return;
    }
    // Already under way: say so rather than doing nothing, and never fetch
    // and run the same installer twice.
    if (downloading_) {
        QMessageBox::information(dialog_parent(), QStringLiteral("finterm"),
                                 (in_place_ ? QStringLiteral("finterm %1 is downloading. It will be ready to install "
                                                             "when you restart or quit finterm.")
                                            : QStringLiteral("finterm %1 is downloading. Its installer opens when "
                                                             "the download is complete."))
                                     .arg(download_version_));
        return;
    }
    if (stage() == Stage::Ready) {
        prompt_restart();
        return;
    }
    if (stage() == Stage::Handed) {
        // The installer may have been cancelled or the file deleted: offer to
        // fetch it again rather than point at a file that may be gone.
        QMessageBox box(dialog_parent());
        box.setWindowTitle(QStringLiteral("finterm"));
        box.setIcon(QMessageBox::Information);
        box.setText(QStringLiteral("The finterm %1 installer was started:\n%2\n\n"
                                   "Finish it, then restart finterm.")
                        .arg(offer_.version, QDir::toNativeSeparators(pending_local_path_)));
        QPushButton* again = box.addButton(QStringLiteral("Download Again"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Ok);
        prompt_open_ = true;
        box.exec();
        prompt_open_ = false;
        QTimer::singleShot(kFirstLookDelayMs, this, &UpdateService::maybe_check_automatically);
        if (box.clickedButton() != again || downloading_ || !has_offer()) {
            if (std::exchange(prompt_pending_, false))
                QTimer::singleShot(0, this, &UpdateService::prompt_install);
            return;
        }
        prompt_pending_ = false; // the download it starts answers it
        handed_version_.clear();
        start_download(offer_, /*quiet=*/false);
        return;
    }
    if (in_place_ && replaced_by_another_instance()) {
        QMessageBox::information(dialog_parent(), QStringLiteral("finterm"),
                                 QStringLiteral("An update has been installed here since finterm started. "
                                                "Quit finterm and start it again to use it."));
        return;
    }
    // The dialog runs a nested event loop in which the schedule can replace
    // offer_ — act on the offer the user is looking at.
    const update_policy::Offer offer = offer_;
    const QString local_version = QApplication::applicationVersion();

    // Include the changelog if present, and always offer the release notes
    // (open-url) so the user can read more before installing.
    QString prompt = QStringLiteral("A new version of finterm is available.\n\n"
                                    "Current version: %1\nLatest version:  %2\n\n")
                         .arg(local_version, offer.version);
    if (!offer.changelog.isEmpty()) {
        QString snippet = offer.changelog;
        if (snippet.size() > 500)
            snippet = snippet.left(500) + QStringLiteral("\n…");
        prompt += QStringLiteral("What's new:\n%1\n\n").arg(snippet);
    }
    prompt += in_place_ ? QStringLiteral("Download it now? It installs when you restart finterm — "
                                         "your workspace and data stay as they are.")
                        : QStringLiteral("Download the installer now?");

    QMessageBox box(dialog_parent());
    box.setWindowTitle(QStringLiteral("Update Available"));
    box.setIcon(QMessageBox::Information);
    box.setText(prompt);
    QPushButton* install_btn = box.addButton(in_place_ ? QStringLiteral("Download") : QStringLiteral("Download && Install"),
                                             QMessageBox::AcceptRole);
    QPushButton* release_btn = offer.open_url.isEmpty()
                                   ? nullptr
                                   : box.addButton(QStringLiteral("View Release Notes"), QMessageBox::HelpRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(install_btn);
    prompt_open_ = true;
    box.exec();
    prompt_open_ = false;
    // A check the user asked for while this dialog was open: answer it now.
    if (std::exchange(prompt_pending_, false))
        QTimer::singleShot(0, this, &UpdateService::prompt_install);
    // A scheduled look that fell while the dialog was open was skipped.
    QTimer::singleShot(kFirstLookDelayMs, this, &UpdateService::maybe_check_automatically);

    QAbstractButton* clicked = box.clickedButton();
    if (clicked == install_btn) {
        start_download(offer, /*quiet=*/false);
    } else if (release_btn && clicked == release_btn) {
        QDesktopServices::openUrl(QUrl(offer.open_url));
    } else {
        LOG_INFO("UpdateService", "User declined the update for now");
    }
}

// ── Restart ─────────────────────────────────────────────────────────────────

void UpdateService::prompt_restart() {
    if (prompt_open_) {
        prompt_pending_ = true;
        return;
    }
    QMessageBox box(dialog_parent());
    box.setWindowTitle(QStringLiteral("Update Ready"));
    box.setIcon(QMessageBox::Information);
    box.setText(QStringLiteral("finterm %1 is downloaded and verified.\n\n"
                               "Restart now to switch to it, or keep working — it installs when you quit "
                               "finterm. Your workspace and data stay as they are.")
                    .arg(ready_version_));
    QPushButton* now = box.addButton(QStringLiteral("Restart Now"), QMessageBox::AcceptRole);
    QPushButton* notes = offer_.open_url.isEmpty()
                             ? nullptr
                             : box.addButton(QStringLiteral("View Release Notes"), QMessageBox::HelpRole);
    box.addButton(QStringLiteral("When I Quit"), QMessageBox::RejectRole);
    box.setDefaultButton(now);
    prompt_open_ = true;
    box.exec();
    prompt_open_ = false;
    if (std::exchange(prompt_pending_, false))
        QTimer::singleShot(0, this, &UpdateService::prompt_install);
    if (box.clickedButton() == now)
        restart_now();
    else if (notes && box.clickedButton() == notes)
        QDesktopServices::openUrl(QUrl(offer_.open_url));
}

bool UpdateService::blocked_by_other_instance() const {
    // An AppImage replaced by a rename keeps running for every instance that
    // has it open (the old inode) — only a bundle swap / an IFW reinstall
    // would pull files from under another running finterm.
    return in_place_ && in_place_->kind != QLatin1String("appimage") && update_install::other_instance_running();
}

bool UpdateService::replaced_by_another_instance() const {
    const QString mine = update_install::startup_stamp();
    return mine.isEmpty() || update_install::target_stamp(*in_place_) != mine;
}

bool UpdateService::staged_file_gone() {
    if (QFileInfo::exists(pending_local_path_))
        return false;
    LOG_WARN("UpdateService", "The staged update is gone: " + pending_local_path_);
    ready_version_.clear();
    ready_sha256_.clear();
    emit stage_changed(); // offered again
    return true;
}

void UpdateService::restart_now() {
    if (!in_place_ || stage() != Stage::Ready || applied_)
        return;
    if (replaced_by_another_instance()) {
        show_error(QStringLiteral("Another finterm window has already installed an update here. "
                                  "Quit finterm and start it again to use it."));
        return;
    }
    if (staged_file_gone()) {
        start_download(offer_, /*quiet=*/false);
        return;
    }
    if (update_install::helper_in_flight()) {
        show_error(QStringLiteral("Another finterm is putting an update in. Quit finterm and start it again to use it."));
        return;
    }
    if (blocked_by_other_instance()) {
        show_error(QStringLiteral("Another finterm (another profile) is running from this installation. "
                                  "Close it first — the update would replace files under it."));
        return;
    }
    if (!update_install::launch_helper(*in_place_, pending_local_path_, QApplication::applicationVersion(),
                                       ready_version_, /*relaunch=*/true)) {
        show_error(QStringLiteral("The update could not be started. It will be tried again when you quit finterm."));
        return;
    }
    applied_ = true;
    LOG_INFO("UpdateService", "Restarting into " + ready_version_);
    // Every window saves its workspace as it closes; the app quits after the
    // last one and the helper takes over.
    QTimer::singleShot(0, qApp, []() {
        QApplication::closeAllWindows();
        QCoreApplication::quit();
    });
}

void UpdateService::apply_on_quit() {
    if (!in_place_ || stage() != Stage::Ready || applied_ || hold_apply_ || staged_file_gone())
        return;
    // Another profile that quit first applied it: again would keep the new
    // version as its own "previous".
    if (replaced_by_another_instance()) {
        LOG_INFO("UpdateService", "Update already applied by another finterm");
        return;
    }
    // Another profile still runs from this install: the last one to quit
    // applies it (it stages the same version).
    if (blocked_by_other_instance()) {
        LOG_INFO("UpdateService", "Update not applied at quit — another finterm is still running");
        return;
    }
    applied_ = update_install::launch_helper(*in_place_, pending_local_path_, QApplication::applicationVersion(),
                                             ready_version_, /*relaunch=*/false);
}

void UpdateService::report_last_update() {
    if (!updates_supported())
        return; // a dev build leaves the note to the release it belongs to
    const auto status = update_install::take_status();
    if (!status)
        return;
    const bool ok = status->value(QStringLiteral("ok")).toBool();
    const QString to = status->value(QStringLiteral("to")).toString();
    const QString kind = status->value(QStringLiteral("kind")).toString();
    const QString msg = status->value(QStringLiteral("message")).toString();
    if (kind.endsWith(QLatin1String("-rollback")) && ok) // don't stage the failed version again
        QSettings().setValue(kBadVersionKey, status->value(QStringLiteral("from")).toString());
    if (!kind.endsWith(QLatin1String("-rollback"))) {
        QSettings s;
        if (ok) {
            s.remove(kFailedVersionKey);
            s.remove(kFailedCountKey);
        } else {
            const int n = s.value(kFailedVersionKey).toString() == to ? s.value(kFailedCountKey).toInt() + 1 : 1;
            s.setValue(kFailedVersionKey, to);
            s.setValue(kFailedCountKey, n);
        }
    }
    if (kind.endsWith(QLatin1String("-rollback")))
        last_result_ = ok ? QStringLiteral("finterm %1 failed to start twice — the previous version was restored.")
                                .arg(status->value(QStringLiteral("from")).toString())
                          : QStringLiteral("Restoring the previous version failed: %1").arg(msg);
    else
        last_result_ = ok ? QStringLiteral("Updated to finterm %1.").arg(to)
                          : QStringLiteral("The update to %1 failed: %2").arg(to, msg);
    last_result_ok_ = ok && !kind.endsWith(QLatin1String("-rollback"));
    LOG_INFO("UpdateService", "Last update: " + last_result_);
    emit stage_changed();
}

void UpdateService::clear_last_result() {
    if (last_result_.isEmpty())
        return;
    last_result_.clear();
    emit stage_changed();
}

// ── Download + verify ───────────────────────────────────────────────────────
// Streamed to <path>.part and hashed as it arrives (an installer is 100+ MB);
// verified, it is renamed into place — a partial or tampered file never
// carries the final name.

void UpdateService::start_download(const update_policy::Offer& offer, bool quiet) {
    if (downloading_)
        return;
    // Staged already (while the dialog asking for it was open).
    if (in_place_ && !ready_version_.isEmpty() && ready_version_ == offer.version && ready_sha256_ == offer.sha256 &&
        stage() == Stage::Ready && QFileInfo::exists(pending_local_path_)) {
        if (!quiet)
            prompt_restart();
        return;
    }
    // What was staged goes: a newer release came out, or this one was
    // re-published.
    if (in_place_ && !ready_version_.isEmpty()) {
        QFile::remove(pending_local_path_);
        ready_version_.clear();
        ready_sha256_.clear();
    }
    // What this download is for: the offer may change while it runs.
    download_in_place_ = in_place_.has_value();
    download_version_ = offer.version;
    download_quiet_ = quiet;
    pending_expected_sha256_ = offer.sha256;
    if (in_place_) {
        pending_local_path_ = update_install::staged_path(*in_place_, offer.version, offer.download_url);
    } else {
        const QString file_name = QFileInfo(QUrl(offer.download_url).path()).fileName();
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        pending_local_path_ =
            QDir(dir).filePath(file_name.isEmpty() ? QStringLiteral("finterm-update.tmp") : file_name);
    }
    // An in-place update must be verified: without a sha256 it isn't staged.
    if (in_place_ && offer.sha256.isEmpty()) {
        download_failed(QStringLiteral("the release has no checksum to verify it against"));
        return;
    }

    // Staged by a session that ended without quitting (a shutdown): checked
    // and used rather than fetched again.
    if (in_place_ && QFileInfo::exists(pending_local_path_)) {
        downloading_ = true;
        emit stage_changed();
        auto* watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, offer]() {
            watcher->deleteLater();
            downloading_ = false;
            if (watcher->result()) {
                staged();
                return;
            }
            // Held open or not ours to delete: backed off like any failure,
            // not hashed for ever.
            if (!QFile::remove(pending_local_path_)) {
                download_failed(QStringLiteral("cannot replace %1").arg(pending_local_path_));
                return;
            }
            start_download(offer, download_quiet_);
        });
        watcher->setFuture(QtConcurrent::run([path = pending_local_path_, want = offer.sha256]() {
            QFile f(path);
            QCryptographicHash h(QCryptographicHash::Sha256);
            return f.open(QIODevice::ReadOnly) && h.addData(&f) &&
                   QString::fromLatin1(h.result().toHex()).toLower() == want;
        }));
        return;
    }

    // This process's own part file: two profiles may download the same
    // version at once into the same staging folder.
    part_ = std::make_unique<QFile>(pending_local_path_ +
                                    QStringLiteral(".%1.part").arg(QCoreApplication::applicationPid()));
    if (!part_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString why = part_->errorString();
        part_.reset();
        download_failed(QStringLiteral("cannot write %1: %2").arg(pending_local_path_, why));
        return;
    }
    hasher_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    downloading_ = true;
    last_logged_ = 0;
    emit stage_changed();

    LOG_INFO("UpdateService", QString("Downloading %1 → %2 (expected sha256=%3)%4")
                                  .arg(offer.download_url, pending_local_path_,
                                       offer.sha256.isEmpty() ? QStringLiteral("<none>") : offer.sha256,
                                       quiet ? QStringLiteral(", in the background") : QString()));

    QNetworkRequest req{QUrl(offer.download_url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QString("finterm/%1").arg(QApplication::applicationVersion()));
    // Qt's transfer timeout counts time WITHOUT data, so a slow download is
    // fine and a stalled one ends in an error instead of blocking the updater
    // for the rest of the session.
    req.setTransferTimeout(60 * 1000);
    QNetworkReply* reply = net_.get(req);
    connect(reply, &QNetworkReply::readyRead, this, &UpdateService::on_download_ready_read);
    connect(reply, &QNetworkReply::finished, this, &UpdateService::on_download_reply_finished);
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateService::on_download_progress);
}

void UpdateService::on_download_ready_read() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || !part_)
        return;
    const QByteArray chunk = reply->readAll();
    if (part_->write(chunk) != chunk.size()) {
        // Say why (a full disk), not the abort's "Operation canceled".
        // The reply goes first: the failure's dialog runs an event loop in
        // which it could finish and be deleted.
        const QString why = part_->errorString();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
        download_failed(QStringLiteral("cannot save the download: %1").arg(why));
        return;
    }
    hasher_->addData(chunk);
}

void UpdateService::on_download_progress(qint64 received, qint64 total) {
    // Log every ~5 MiB; the badge says the download is running.
    if (received - last_logged_ >= 5 * 1024 * 1024 || received == total) {
        last_logged_ = received;
        LOG_DEBUG("UpdateService", QString("Download progress: %1 / %2 bytes").arg(received).arg(total));
    }
}

void UpdateService::download_failed(const QString& why) {
    LOG_ERROR("UpdateService", "Update download failed: " + why);
    if (part_) {
        part_->close();
        part_->remove();
        part_.reset();
    }
    hasher_.reset();
    // Whoever asked for it: the background one waits before it tries.
    failed_tries_ = failed_sha256_ == pending_expected_sha256_ ? failed_tries_ + 1 : 1;
    failed_sha256_ = pending_expected_sha256_;
    const int hours = std::min(24, 1 << std::min(failed_tries_ - 1, 5));
    retry_at_ = QDateTime::currentDateTimeUtc().addSecs(hours * 60 * 60);
    if (!download_quiet_)
        show_error(QStringLiteral("The update could not be downloaded.\n\n%1").arg(why));
    finish_download();
}

void UpdateService::on_download_reply_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    reply->deleteLater();
    if (!part_) // already failed (download_failed ran)
        return;
    if (reply->error() != QNetworkReply::NoError) {
        download_failed(reply->errorString());
        return;
    }
    const QByteArray rest = reply->readAll();
    if (!rest.isEmpty()) {
        part_->write(rest);
        hasher_->addData(rest);
    }
    const bool flushed = part_->flush();
    part_->close();
    if (!flushed || part_->error() != QFileDevice::NoError) {
        download_failed(QStringLiteral("cannot save the download: %1").arg(part_->errorString()));
        return;
    }

    if (!pending_expected_sha256_.isEmpty()) {
        // The bytes on disk are what gets installed: hash the file itself
        // (the stream's hash is checked too — they must agree).
        QString on_disk;
        {
            QFile f(part_->fileName());
            QCryptographicHash h(QCryptographicHash::Sha256);
            if (f.open(QIODevice::ReadOnly) && h.addData(&f))
                on_disk = QString::fromLatin1(h.result().toHex()).toLower();
        }
        const QString streamed = QString::fromLatin1(hasher_->result().toHex()).toLower();
        const QString actual = on_disk == streamed ? on_disk : QString();
        if (actual != pending_expected_sha256_) {
            LOG_ERROR("UpdateService",
                      QString("Sha256 mismatch — expected=%1, actual=%2").arg(pending_expected_sha256_, actual));
            download_failed(QStringLiteral("it failed integrity verification (it may be corrupt or tampered with)"));
            return;
        }
        LOG_INFO("UpdateService", QString("Sha256 verified: %1").arg(actual));
    } else {
        LOG_WARN("UpdateService", "No sha256 in manifest — skipping integrity verification");
    }

    // Verified: it takes its final name.
    QFile::remove(pending_local_path_);
    if (!part_->rename(pending_local_path_)) {
        download_failed(QStringLiteral("cannot rename the download: %1").arg(part_->errorString()));
        return;
    }
    part_.reset();
    hasher_.reset();

    if (download_in_place_) {
        staged();
        return;
    }

    // Not in place: on platforms where we can launch the installer (and the
    // user will be prompted by the OS anyway), do so. Otherwise reveal the file.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    launch_installer(pending_local_path_);
#else
    reveal_in_file_manager(pending_local_path_);
#endif
    finish_download(/*handed=*/true);
}

void UpdateService::staged() {
    // It goes in when the app quits, or now on Restart Now.
    ready_version_ = download_version_;
    ready_sha256_ = pending_expected_sha256_;
    finish_download();
    LOG_INFO("UpdateService", QString("finterm %1 staged at %2").arg(ready_version_, pending_local_path_));
    if (!download_quiet_)
        prompt_restart();
}

// ── Launch / reveal ─────────────────────────────────────────────────────────

void UpdateService::launch_installer(const QString& path) {
    LOG_INFO("UpdateService", QString("Launching installer: %1").arg(path));

#ifdef Q_OS_LINUX
    // Linux installers (.run) need the executable bit set before they can be
    // launched. QDesktopServices::openUrl on a non-executable .run just hands
    // it to the file manager, which is not what the user expects after
    // clicking "Install".
    QFile f(path);
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup |
                     QFileDevice::ExeOther);
#endif

    // startDetached is the right call here — we want the installer to outlive
    // this process so the user can restart and finish installation.
    const bool started = QProcess::startDetached(path, {});
    if (!started) {
        LOG_WARN("UpdateService", "startDetached failed — falling back to reveal-in-file-manager");
        reveal_in_file_manager(path);
        QMessageBox::information(dialog_parent(), QStringLiteral("Update Downloaded"),
                                 QStringLiteral("The installer has been downloaded to:\n%1\n\n"
                                                "Please run it manually to complete the update.")
                                     .arg(QDir::toNativeSeparators(path)));
    }
}

void UpdateService::reveal_in_file_manager(const QString& path) {
    const QUrl url = QUrl::fromLocalFile(QFileInfo(path).absolutePath());
    QDesktopServices::openUrl(url);
}

// ── Helpers ─────────────────────────────────────────────────────────────────

void UpdateService::finish_check(bool update_found) {
    checking_ = false;
    interactive_ = false;
    emit check_finished(update_found);
}

void UpdateService::finish_download(bool handed) {
    downloading_ = false;
    if (handed)
        handed_version_ = download_version_;
    emit stage_changed();
    // A scheduled look that fell during the download was skipped.
    QTimer::singleShot(kFirstLookDelayMs, this, &UpdateService::maybe_check_automatically);
}

void UpdateService::show_error(const QString& text) {
    QMessageBox::warning(dialog_parent(), QStringLiteral("finterm"), text);
}

} // namespace finterm::services

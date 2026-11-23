#include "services/updater/UpdateService.h"

#include "core/logging/Logger.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

namespace finterm::services {

UpdateService& UpdateService::instance() {
    static UpdateService inst;
    return inst;
}

UpdateService::UpdateService(QObject* parent) : QObject(parent) {
    net_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // GitHub release asset downloads redirect across origins — allow the jump.
    net_.setStrictTransportSecurityEnabled(true);
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

// ── Settings ────────────────────────────────────────────────────────────────

namespace {
// Install-wide (QSettings), not per profile — see the class comment.
constexpr const char* kAutoCheckKey = "updates/auto_check"; // "on" | "off"; absent = never asked
constexpr const char* kLastCheckKey = "updates/last_check"; // ISO-8601 UTC of the last successful check
constexpr int kFirstLookDelayMs = 10 * 1000;
constexpr int kScheduleTickMs = 60 * 60 * 1000;
} // namespace

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

void UpdateService::start_automatic() {
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
    // On request: ask right away. Automatic: the status-bar badge carries the
    // offer until the user opens it.
    if (ask_now)
        prompt_install();
}

// ── Offer ───────────────────────────────────────────────────────────────────

void UpdateService::set_offer(const update_policy::Offer& offer) {
    const bool changed = offer.version != offer_.version || offer.download_url != offer_.download_url ||
                         offer.sha256 != offer_.sha256;
    offer_ = offer;
    if (changed)
        emit offer_changed();
}

UpdateService::Stage UpdateService::stage() const {
    if (downloading_)
        return Stage::Downloading;
    if (has_offer() && handed_version_ == offer_.version)
        return Stage::Handed;
    return Stage::Offered;
}

void UpdateService::show_offer() {
    prompt_install();
}

void UpdateService::prompt_install() {
    if (!has_offer() || prompt_open_)
        return;
    // Already under way: say so rather than doing nothing, and never fetch
    // and run the same installer twice.
    if (downloading_) {
        QMessageBox::information(dialog_parent(), QStringLiteral("finterm"),
                                 QStringLiteral("finterm %1 is downloading. Its installer opens when the "
                                                "download is complete.")
                                     .arg(download_version_));
        return;
    }
    if (stage() == Stage::Handed) {
        QMessageBox::information(dialog_parent(), QStringLiteral("finterm"),
                                 QStringLiteral("The finterm %1 installer was started:\n%2\n\n"
                                                "Finish it, then restart finterm.")
                                     .arg(offer_.version, QDir::toNativeSeparators(pending_local_path_)));
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
    prompt += QStringLiteral("Download the installer now?");

    QMessageBox box(dialog_parent());
    box.setWindowTitle(QStringLiteral("Update Available"));
    box.setIcon(QMessageBox::Information);
    box.setText(prompt);
    QPushButton* install_btn = box.addButton(QStringLiteral("Download && Install"), QMessageBox::AcceptRole);
    QPushButton* release_btn = offer.open_url.isEmpty()
                                   ? nullptr
                                   : box.addButton(QStringLiteral("View Release Notes"), QMessageBox::HelpRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(install_btn);
    prompt_open_ = true;
    box.exec();
    prompt_open_ = false;
    // A scheduled look that fell while the dialog was open was skipped.
    QTimer::singleShot(kFirstLookDelayMs, this, &UpdateService::maybe_check_automatically);

    QAbstractButton* clicked = box.clickedButton();
    if (clicked == install_btn) {
        download_version_ = offer.version;
        start_download(offer.download_url, offer.sha256);
    } else if (release_btn && clicked == release_btn) {
        QDesktopServices::openUrl(QUrl(offer.open_url));
    } else {
        LOG_INFO("UpdateService", "User declined the update for now");
    }
}

// ── Download + verify ───────────────────────────────────────────────────────

void UpdateService::start_download(const QString& url, const QString& expected_sha256) {
    downloading_ = true;
    pending_expected_sha256_ = expected_sha256;
    emit stage_changed();

    // Build a stable local path in the system temp dir, preserving the
    // installer's basename so the user recognises it.
    const QString file_name = QFileInfo(QUrl(url).path()).fileName();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    pending_local_path_ = QDir(dir).filePath(file_name.isEmpty() ? QStringLiteral("finterm-update.tmp") : file_name);

    // Drop any leftover from a previous run — otherwise a partial file could
    // survive and poison sha256 verification.
    QFile::remove(pending_local_path_);

    LOG_INFO("UpdateService", QString("Downloading installer: %1 → %2 (expected sha256=%3)")
                                  .arg(url, pending_local_path_,
                                       expected_sha256.isEmpty() ? QStringLiteral("<none>") : expected_sha256));

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QString("finterm/%1").arg(QApplication::applicationVersion()));
    // Qt's transfer timeout counts time WITHOUT data, so a slow download is
    // fine and a stalled one ends in an error instead of blocking the updater
    // for the rest of the session.
    req.setTransferTimeout(60 * 1000);
    QNetworkReply* reply = net_.get(req);
    connect(reply, &QNetworkReply::finished, this, &UpdateService::on_download_reply_finished);
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateService::on_download_progress);
}

void UpdateService::on_download_progress(qint64 received, qint64 total) {
    // Log every ~5 MiB to avoid spamming. No GUI progress for v1 — keep the
    // surface area small. A proper progress dialog can be layered on later.
    static qint64 last_logged = 0;
    if (received < last_logged) // a new download
        last_logged = 0;
    if (received - last_logged >= 5 * 1024 * 1024 || received == total) {
        last_logged = received;
        LOG_DEBUG("UpdateService", QString("Download progress: %1 / %2 bytes").arg(received).arg(total));
    }
}

void UpdateService::on_download_reply_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        LOG_ERROR("UpdateService", QString("Installer download failed: %1").arg(reply->errorString()));
        show_error(QStringLiteral("The installer could not be downloaded.\n\n%1").arg(reply->errorString()));
        QFile::remove(pending_local_path_);
        finish_download();
        return;
    }

    // Write body to disk.
    QFile f(pending_local_path_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_ERROR("UpdateService", QString("Cannot open %1 for writing: %2").arg(pending_local_path_, f.errorString()));
        show_error(QStringLiteral("Cannot save the installer to disk:\n%1").arg(f.errorString()));
        finish_download();
        return;
    }
    const QByteArray body = reply->readAll();
    f.write(body);
    f.close();

    // Verify sha256 if the manifest provided one. Missing sha256 logs a
    // warning but does not abort — the server may be a pre-sha256 deployment.
    if (!pending_expected_sha256_.isEmpty()) {
        QFile vf(pending_local_path_);
        if (!vf.open(QIODevice::ReadOnly)) {
            LOG_ERROR("UpdateService", QString("Cannot reopen %1 for hashing").arg(pending_local_path_));
            show_error(QStringLiteral("Could not verify the downloaded installer."));
            QFile::remove(pending_local_path_);
            finish_download();
            return;
        }
        QCryptographicHash hasher(QCryptographicHash::Sha256);
        if (!hasher.addData(&vf)) {
            vf.close();
            LOG_ERROR("UpdateService", "Sha256 hashing failed");
            show_error(QStringLiteral("Could not verify the downloaded installer."));
            QFile::remove(pending_local_path_);
            finish_download();
            return;
        }
        vf.close();
        const QString actual = QString::fromLatin1(hasher.result().toHex()).toLower();
        if (actual != pending_expected_sha256_) {
            LOG_ERROR("UpdateService",
                      QString("Sha256 mismatch — expected=%1, actual=%2").arg(pending_expected_sha256_, actual));
            show_error(QStringLiteral("The downloaded installer failed integrity verification. "
                                      "It may be corrupt or tampered with."));
            QFile::remove(pending_local_path_);
            finish_download();
            return;
        }
        LOG_INFO("UpdateService", QString("Sha256 verified: %1").arg(actual));
    } else {
        LOG_WARN("UpdateService", "No sha256 in manifest — skipping integrity verification");
    }

    emit download_finished(pending_local_path_);

    // Hand off: on platforms where we can launch the installer (and the user
    // will be prompted by the OS anyway), do so. Otherwise reveal the file.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    launch_installer(pending_local_path_);
#else
    reveal_in_file_manager(pending_local_path_);
#endif

    finish_download(/*handed=*/true);
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

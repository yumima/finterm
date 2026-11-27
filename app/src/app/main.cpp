#include "ai_chat/HearthService.h"
#include "ai_chat/LlmService.h"
#include "app/MainWindow.h"
#include "auth/AuthManager.h"
#include "auth/AuthService.h"
#include "auth/InactivityGuard.h"
#include "auth/PinManager.h"
#include "core/components/ComponentCatalog.h"
#include "core/config/AppConfig.h"
#include "core/config/AppIdentity.h"
#include "core/config/AppPaths.h"
#include "core/config/LayoutMigrations.h"
#include "core/config/StartFresh.h"
#include "core/network/ProxySettings.h"
#include "services/data_health/DataHealth.h"
#include "core/config/ProfileManager.h"
#include "core/crash/CrashHandler.h"
#include "core/keys/KeyConfigManager.h"
#include "core/logging/Logger.h"
#include "core/session/ScreenStateManager.h"
#include "core/session/SessionManager.h"
#include "core/symbol/SymbolGroup.h"
#include "core/symbol/SymbolRef.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "datahub/TopicPolicy.h"
#include "mcp/McpInit.h"
#include "network/http/HttpClient.h"
#include "app/SelfTest.h"
#include "services/updater/UpdateInstaller.h"
#include "python/PythonSetupManager.h"
#include "python/PythonWorker.h"
#include "services/ownership/OwnershipService.h"
#include "core/util/AppLifecycle.h"
#include "screens/setup/SetupScreen.h"
#include "services/agents/AgentScheduler.h"
#include "services/agents/AgentService.h"
#include "services/dbnomics/DBnomicsService.h"
#include "services/economics/EconomicsService.h"
#include "services/geopolitics/GeopoliticsService.h"
#include "services/gov_data/GovDataService.h"
#include "services/ma_analytics/MAAnalyticsService.h"
#include "services/maritime/MaritimeService.h"
#include "services/markets/MarketDataService.h"
#include "services/news/NewsService.h"
#include "services/polymarket/PolymarketWebSocket.h"
#include "services/prediction/PredictionCredentialStore.h"
#include "services/prediction/PredictionExchangeRegistry.h"
#include "services/prediction/kalshi/KalshiAdapter.h"
#include "services/prediction/polymarket/PolymarketAdapter.h"
#include "services/relationship_map/RelationshipMapService.h"
#include "services/report_builder/ReportBuilderService.h"
#include "storage/cache/CacheManager.h"
#include "storage/repositories/NewsArticleRepository.h"
#include "storage/repositories/SettingsRepository.h"
#include "storage/secure/SecureStorage.h"
#include "storage/sqlite/CacheDatabase.h"
#include "storage/sweep/RetiredStorage.h"
#include "storage/sqlite/Database.h"
#include "storage/sqlite/migrations/MigrationRunner.h"
#include "trading/DataStreamManager.h"
#include "trading/ExchangeService.h"
#include "trading/ExchangeSessionManager.h"
#include "ui/SelectableText.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tooltips.h"

#include <cstdio>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <memory>
#include <singleapplication.h>

#ifdef Q_OS_WIN
#    include <Windows.h>
#endif

#ifndef FINTERM_VERSION_STRING
#    define FINTERM_VERSION_STRING "0.0.0-dev"
#endif

int main(int argc, char* argv[]) {
    // ── --version: answer and leave, before anything is touched ─────────────
    // Unhandled, the flag was ignored and a whole second copy of the app came
    // up on the user's profile. One left behind that way (2026-11-15, from a
    // build script checking the version) sat for a day holding the profile's
    // single-instance slot, and every later launch handed its window to it
    // and exited without a word.
    // Only the FIRST argument, so an option's value can never be read as it.
    if (argc > 1 && (qstrcmp(argv[1], "--version") == 0 || qstrcmp(argv[1], "-V") == 0 ||
                     qstrcmp(argv[1], "--help") == 0 || qstrcmp(argv[1], "-h") == 0)) {
#ifdef Q_OS_WIN
        // A GUI-subsystem program has no stdout; borrow the console it was run from.
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* console = nullptr;
            freopen_s(&console, "CONOUT$", "w", stdout);
        }
#endif
        if (qstrcmp(argv[1], "--version") == 0 || qstrcmp(argv[1], "-V") == 0)
            std::printf("finterm %s\n", FINTERM_VERSION_STRING);
        else
            std::printf("usage: finterm [--profile <name>] [--version]\n");
        std::fflush(stdout);
        return 0;
    }

    // ── Started again by "Start fresh": let the old process go first ───────
    // so this one becomes the primary instance (and its data is free).
    finterm::start_fresh::wait_for_parent(argc, argv);

    // ── Move an older build's data folder to the current name ───────────────
    // Before anything below creates the new root: once it exists, the old
    // folder can no longer be moved into place with a single rename.
    const bool data_root_ready = finterm::AppPaths::migrate_root();

    // ── Parse --profile <name> from argv before Qt initialises ───────────────
    // This must happen first so that:
    //   1. AppPaths returns the correct per-profile directories
    //   2. SingleApplication uses a profile-scoped IPC key so two different
    //      profiles can run simultaneously as independent primary instances
    {
        for (int i = 1; i < argc - 1; ++i) {
            if (qstrcmp(argv[i], "--profile") == 0) {
                finterm::ProfileManager::instance().set_active(QString::fromUtf8(argv[i + 1]));
                break;
            }
        }
        // AppPaths::root() must exist before ensure_all() so ProfileManager can
        // write the manifest. Create root now (single mkdir, idempotent).
        QDir().mkpath(finterm::AppPaths::root());
    }

    // Install the unhandled-exception filter BEFORE any Qt object is
    // constructed. On Windows this writes a minidump to AppPaths::crashdumps()
    // when the process dies from an access violation, stack overflow, or GS
    // cookie check failure (STATUS_STACK_BUFFER_OVERRUN — see issue #215).
    // Do it early so a crash in Qt's own startup still produces a dump.
    finterm::crash::install();

    // finterm is local-only: no library it spawns may phone home. Set in our
    // own environment so EVERY child inherits it — Python scripts, the yfinance
    // daemon, MCP servers, speech/TTS helpers. Agno (60 agent scripts) posts
    // usage to os-api.agno.com unless AGNO_TELEMETRY is set, and the env var
    // overrides even an explicit telemetry=True in an agent config.
    // A value the user exported deliberately (e.g. LANGCHAIN_TRACING_V2=true
    // for their own LangSmith runs) is respected; only the unset default is
    // turned off.
    for (const char* var : {"AGNO_TELEMETRY", "LITELLM_TELEMETRY", "ANONYMIZED_TELEMETRY", "GRADIO_ANALYTICS_ENABLED",
                            "LANGCHAIN_TRACING_V2"})
        if (!qEnvironmentVariableIsSet(var))
            qputenv(var, "false");
    for (const char* var : {"DO_NOT_TRACK", "DISABLE_TELEMETRY", "HF_HUB_DISABLE_TELEMETRY", "POSTHOG_DISABLED"})
        if (!qEnvironmentVariableIsSet(var))
            qputenv(var, "1");

    // Required before QApplication when any dock panel contains an OpenGL widget
    // (Qt Charts, QOpenGLWidget) — prevents black rendering in floating windows.
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // Composite QtWebEngine pages in software (the earnings-call player, the
    // dashboard's web video). Qt 6.10 hands Chromium's GPU-composited frames
    // to Qt via GBM; where GBM isn't usable ("GBM is not supported with the
    // current configuration. Fallback to Vulkan rendering in Chromium" — this
    // Intel Arc + RTX 5070 Ti hybrid on Wayland, and Xvfb) the view shows
    // nothing usable: measured 2026-11-17, a page's text and layers never
    // painted (blank, or only the background colour) while the page ran fine
    // underneath. With GPU compositing off the same Q4 webcast page renders
    // completely; rasterisation and video decode still use the GPU, only the
    // final layer composite is done on the CPU — cheap for a call page.
    // Appended to any flags the user set, never replacing them.
    {
        // Whole flags, not substrings: --disable-gpu-sandbox (common on
        // Wayland/containers) must not suppress this.
        const QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
        const QList<QByteArray> given = flags.split(' ');
        const bool decided = given.contains("--disable-gpu") || given.contains("--disable-gpu-compositing") ||
                             given.contains("--enable-gpu-compositing");
        if (!decided)
            qputenv("QTWEBENGINE_CHROMIUM_FLAGS", (flags.isEmpty() ? QByteArray() : flags + ' ') +
                                                       QByteArrayLiteral("--disable-gpu-compositing"));
    }

    // Pin Qt Multimedia's audio-device backend to PulseAudio. Qt 6.10 defaults
    // to a native PipeWire device backend whose SPA-Pod format parser chokes on
    // PipeWire 1.6.2 — it logs "spaVisitChoice: parse error" while parsing a
    // Bluetooth sink's format and drops the device from its async monitor. The
    // result: a headset that connects (or A2DP-renegotiates on a reconnect)
    // after launch never appears in QMediaDevices::audioOutputs(), and the
    // dashboard TV player can neither list it nor route audio to it — it stays
    // stuck on the built-in speakers. The PulseAudio backend goes through
    // libpulse (PipeWire-pulse exposes every sink, bluez included) and never
    // touches that parser, so device hotplug is reliable. Must be set before
    // QApplication so the first QMediaDevices query picks it up. Honour an
    // explicit override if the user set one in the environment.
    if (qEnvironmentVariableIsEmpty("QT_AUDIO_BACKEND"))
        qputenv("QT_AUDIO_BACKEND", "PulseAudio");

    // SingleApplication enforces one process per profile.
    // The instance key is scoped to the active profile name, so
    // "finterm --profile work" and "finterm --profile personal"
    // are treated as two separate primary instances and run simultaneously.
    // allowSecondary=true: secondary instances send "--new-window" and exit.
    const QString profile_key =
        QString("%1-%2").arg(finterm::AppIdentity::kApp).arg(finterm::ProfileManager::instance().active());
    SingleApplication app(argc, argv, /*allowSecondary=*/true, SingleApplication::Mode::User, 100,
                          profile_key.toUtf8());

    // Every QLabel finterm renders becomes selectable/copyable. Qt's default is
    // unselectable, so model output and article text could be read but not
    // copied — see ui/SelectableText.h for what is deliberately skipped.
    finterm::ui::install_selectable_text(app);
    app.setApplicationName(finterm::AppIdentity::kApp);
    app.setOrganizationName(finterm::AppIdentity::kOrg);
    // Visible brand — deliberately decoupled from the frozen storage identity
    // above, so the app can be rebranded without moving any on-disk data or
    // settings (see core/config/AppIdentity.h). The brand is put into the
    // window title by MainWindow (app_title_base); it is NOT set as Qt's
    // application display name, which Linux appends to every window title —
    // "finterm@host ver: … — finterm".
    // Desktop-entry / WM_CLASS (X11) / app_id (Wayland) association — matches the
    // installed finterm.desktop so the taskbar groups windows under the brand.
    app.setDesktopFileName(finterm::AppIdentity::kDisplayName);
    app.setApplicationVersion(QStringLiteral(FINTERM_VERSION_STRING));

    // Tooltips: instant, at the pointer, wrapped into a box. Installed here
    // rather than with the theme because it swaps the application style, and
    // doing that after a stylesheet is set would force a second full re-polish
    // of every widget already built.
    finterm::ui::tooltips::install(app);

    // ── Secondary instance: signal primary to open a new window, then exit ───
    // The primary receives receivedMessage() and calls open_new_window().
    if (app.isSecondary()) {
#ifdef Q_OS_WIN
        // Grant the primary process permission to bring its new window to the
        // foreground — Windows blocks focus-steal without this.
        AllowSetForegroundWindow(static_cast<DWORD>(app.primaryPid()));
#endif
        app.sendMessage(QByteArray("--new-window"));
        return 0;
    }

    // ── Primary instance from here on ────────────────────────────────────────

    // Quitting stops the data daemon in order rather than leaving it to find
    // its socket closed — but only one that exists: asking for the worker
    // here would build it during teardown. Helpers are also bound to the app
    // at start, so even a crash takes them down — see core/util/AppLifecycle.h.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        if (finterm::python::PythonWorker::exists())
            finterm::python::PythonWorker::instance().stop();
    });

    // An older build's data folder that could not be moved (that build is still
    // running, or the rename failed): stop here. Running on it would split the
    // user's state — settings and keyring entries move below, the data would not.
    if (!data_root_ready) {
        QMessageBox::warning(
            nullptr, QStringLiteral("finterm"),
            QStringLiteral("This version of finterm moves its data folder to\n%1\n\n"
                           "It could not finish moving %2 — an older finterm (any profile) or one of "
                           "its helper processes is probably still running, or a file in that folder "
                           "is open.\n\n"
                           "Close every finterm window and start it again: the move picks up where it "
                           "stopped. Nothing is deleted.")
                .arg(finterm::AppPaths::current_root_path(), finterm::AppPaths::legacy_root_path()));
        return 1;
    }

    // ── A just-applied update that can't start: roll it back ────────────────
    // Each start of a freshly updated version counts until its window has been
    // up a few seconds (confirm_boot below); the third unconfirmed start hands
    // over to the helper, which restores the previous version and runs it.
    if (finterm::services::update_install::boot_check())
        return 0;
    // A clean quit is a good run too (closed within the confirm delay).
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() { finterm::services::update_install::confirm_boot(); });

    // ── Versioned layout migrations (primary only) ───────────────────────────
    // Self-healing for on-disk layout moves between releases (settings-store
    // rebrand, orphaned legacy caches, …). Fast no-op once up to date — a single
    // QSettings read — and narrates to stdout only when it actually migrates, so
    // a terminal launch shows what happened before the GUI. Secondary instances
    // returned above, so this runs once per primary launch. See
    // core/config/LayoutMigrations.h.
    finterm::LayoutMigrations::run();
    // Secrets an older build filed under the previous keyring identity move
    // before anything below reads one (Polymarket credentials, a few lines on).
    finterm::SecureStorage::instance().migrate_identity();

    // ── How to reach the internet (Settings › Data Sources › Network) ───────
    // Before any request, and before the data service starts: it inherits the
    // proxy through the environment.
    finterm::proxy::apply(finterm::proxy::load());

    // ── "Start fresh", asked for in the previous run ────────────────────────
    // Before anything opens a database (ensure_all, the auth service, the
    // per-user DB below).
    if (const auto fresh = finterm::start_fresh::run_if_scheduled(); fresh.ran) {
        QString text = fresh.ok ? QStringLiteral("finterm has been reset. Set it up as on the first start.")
                                : QStringLiteral("finterm could not be reset completely.");
        if (!fresh.ok && !fresh.backup_dir.isEmpty())
            text += QStringLiteral("\n\nA copy of your data was left in:\n%1\nDelete it once finterm shows your data "
                                   "as before.")
                        .arg(QDir::toNativeSeparators(fresh.backup_dir));
        else if (!fresh.backup_dir.isEmpty())
            text += QStringLiteral("\n\nYour previous data was moved, not deleted, to:\n%1\n\nTo get it back, quit "
                                   "finterm and copy the user folder from there into %2. Delete the backup when you "
                                   "no longer need it.")
                        .arg(QDir::toNativeSeparators(fresh.backup_dir),
                             QDir::toNativeSeparators(finterm::ProfileManager::instance().profile_root()));
        if (!fresh.problems.isEmpty())
            text += QStringLiteral("\n\n") + fresh.problems.join(QStringLiteral("\n"));
        if (fresh.ok)
            QMessageBox::information(nullptr, QStringLiteral("finterm"), text);
        else
            QMessageBox::warning(nullptr, QStringLiteral("finterm"), text);
    }

    // Register DataHub payload meta-types (QuoteData, HistoryPoint, InfoData,
    // NewsArticle, EconomicsResult) so they can flow through QVariant-keyed
    // topics and cross-thread queued signals. Phase 0 — see
    // app/DATAHUB_ARCHITECTURE.md.
    // Phase 2: register MarketDataService as the `market:quote:*` producer.
    finterm::datahub::register_metatypes();
    // What data is failing and why — the status bar's READY / DATA slot.
    finterm::services::DataHealth::instance().start();
    // SymbolContext payload types — signals cross threads when a producer
    // service (not just UI) publishes a group change.
    qRegisterMetaType<finterm::SymbolRef>("finterm::SymbolRef");
    qRegisterMetaType<finterm::SymbolGroup>("finterm::SymbolGroup");
    // Phase 6: load the Component Browser catalogue. Try the build-side copy
    // first (present after cmake configure copies resources) and fall back to
    // the source-tree path for local dev runs without install step.
    finterm::ComponentCatalog::instance().load_with_fallbacks({
        QCoreApplication::applicationDirPath() + "/resources/component_catalog.json",
        QCoreApplication::applicationDirPath() + "/component_catalog.json",
        "resources/component_catalog.json",
    });
    finterm::services::MarketDataService::instance().ensure_registered_with_hub();
    // Phase 2 (multi-broker refactor): ExchangeSessionManager is the hub
    // producer for `ws:kraken:*` / `ws:hyperliquid:*`. Individual sessions
    // (created lazily by the manager) publish through its SessionPublisher
    // seam. ExchangeService keeps a back-compat shim but no longer registers
    // itself with the hub.
    finterm::trading::ExchangeSessionManager::instance().ensure_registered_with_hub();
    // Prediction Markets — multi-exchange tab (Polymarket, Kalshi, …).
    // PolymarketWebSocket is the hub producer for `prediction:polymarket:*`;
    // the adapter layer translates exchange-local types into the unified
    // prediction::* data model for the screen.
    finterm::services::polymarket::PolymarketWebSocket::instance().ensure_registered_with_hub();
    {
        auto& reg = finterm::services::prediction::PredictionExchangeRegistry::instance();
        reg.register_adapter(std::make_unique<finterm::services::prediction::polymarket_ns::PolymarketAdapter>());
        reg.register_adapter(std::make_unique<finterm::services::prediction::kalshi_ns::KalshiAdapter>());

        // Hydrate credentials from SecureStorage if previously saved. This
        // has to happen after registration so the adapters exist.
        if (auto* pm = dynamic_cast<finterm::services::prediction::polymarket_ns::PolymarketAdapter*>(
                reg.adapter(QStringLiteral("polymarket")))) {
            pm->reload_credentials();
        }
        if (auto* ks = dynamic_cast<finterm::services::prediction::kalshi_ns::KalshiAdapter*>(
                reg.adapter(QStringLiteral("kalshi")))) {
            if (auto creds = finterm::services::prediction::PredictionCredentialStore::load_kalshi()) {
                ks->set_credentials(*creds);
            }
        }
    }
    // Phase 5: NewsService as the news:general / news:symbol:* /
    // news:category:* / news:cluster:* producer.
    finterm::services::NewsService::instance().ensure_registered_with_hub();
    // Phase 6: Economics + DBnomics + GovData producers.
    finterm::services::EconomicsService::instance().ensure_registered_with_hub();
    finterm::services::DBnomicsService::instance().ensure_registered_with_hub();
    finterm::services::GovDataService::instance().ensure_registered_with_hub();
    // Phase 7: DataStreamManager as the broker:* producer (positions,
    // orders, balance, holdings, quote, ticks). Per-account topic shape
    // `broker:<id>:<account_id>:<channel>`.
    finterm::trading::DataStreamManager::instance().ensure_registered_with_hub();
    // Phase 8: Geopolitics / Maritime / RelationshipMap / MAAnalytics.
    finterm::services::geo::GeopoliticsService::instance().ensure_registered_with_hub();
    finterm::services::maritime::MaritimeService::instance().ensure_registered_with_hub();
    finterm::services::RelationshipMapService::instance().ensure_registered_with_hub();
    finterm::services::ma::MAAnalyticsService::instance().ensure_registered_with_hub();
    // Phase 9: AgentService as agent:* push-only producer (output/stream/status/routing/error).
    finterm::services::AgentService::instance().ensure_registered_with_hub();
    // Track 10: cron-shaped scheduler for agent runs.  Reads agent_schedule
    // on each 60s tick; no-op until the user adds entries.
    finterm::services::AgentScheduler::instance().start();
    // Create all application directories under the data root (AppPaths::root)
    finterm::AppPaths::ensure_all();

    // ── WAL/SHM cleanup — gated behind QLockFile ────────────────────────────
    // Deleting WAL/SHM files while another process has the DB open corrupts it.
    // We only clean up orphaned files from a previous crash when we are the sole
    // running instance. The lock file is held for the entire process lifetime.
    // An older build names its lock fincept.db.lock: probe both names (before
    // taking ours) so a still-running older instance counts as another one.
    const bool other_instance = finterm::AppPaths::data_dir_in_use(finterm::AppPaths::data());
    QLockFile db_lock(finterm::AppPaths::data() + "/finterm.db.lock");
    db_lock.setStaleLockTime(0); // never auto-expire; we control the lifecycle
    const bool sole_instance = db_lock.tryLock(0) && !other_instance;
    if (sole_instance) {
        // Only the -shm index (SQLite rebuilds it). A -wal left by a crash
        // holds COMMITTED transactions that SQLite replays on the next open;
        // deleting it threw those away.
        QFile::remove(finterm::AppPaths::data() + "/fincept.db-shm");
        QFile::remove(finterm::AppPaths::data() + "/cache.db-shm");
    }
    // db_lock remains held (stack-allocated) until main() returns.
    finterm::Logger::instance().set_file(finterm::AppPaths::logs() + "/finterm.log");

    // P3.18 — route Qt's own qDebug/qWarning/qCritical messages into our log
    // file so framework/3rd-party warnings are visible in Release builds.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
        const char* category = (ctx.category && *ctx.category) ? ctx.category : "Qt";
        switch (type) {
            case QtDebugMsg:
                finterm::Logger::instance().debug(category, msg);
                break;
            case QtInfoMsg:
                finterm::Logger::instance().info(category, msg);
                break;
            case QtWarningMsg:
                finterm::Logger::instance().warn(category, msg);
                break;
            case QtCriticalMsg:
                finterm::Logger::instance().error(category, msg);
                break;
            case QtFatalMsg:
                finterm::Logger::instance().error(category, msg);
                finterm::Logger::instance().flush_and_close();
                break;
        }
    });
    {
        auto& log = finterm::Logger::instance();
        auto& cfg = finterm::AppConfig::instance();

        // Global level
        const QString gl = cfg.get("log/global_level", "Info").toString();
        const QHash<QString, finterm::LogLevel> lvl_map = {
            {"Trace", finterm::LogLevel::Trace}, {"Debug", finterm::LogLevel::Debug},
            {"Info", finterm::LogLevel::Info},   {"Warn", finterm::LogLevel::Warn},
            {"Error", finterm::LogLevel::Error}, {"Fatal", finterm::LogLevel::Fatal}};
        log.set_level(lvl_map.value(gl, finterm::LogLevel::Info));

        // JSON output mode (persisted in Settings → Logging)
        log.set_json_mode(cfg.get("log/json_mode", false).toBool());

        // Per-tag overrides
        const int count = cfg.get("log/tag_count", 0).toInt();
        for (int i = 0; i < count; ++i) {
            const QString tag = cfg.get(QString("log/tag_%1_name").arg(i)).toString();
            const QString level = cfg.get(QString("log/tag_%1_level").arg(i)).toString();
            if (!tag.isEmpty() && lvl_map.contains(level))
                log.set_tag_level(tag, lvl_map.value(level));
        }
    }
    LOG_INFO("App", QStringLiteral("finterm v%1 starting...").arg(QCoreApplication::applicationVersion()));

    // Initialize config (HttpClient base URL kept for non-auth third-party requests)
    auto& config = finterm::AppConfig::instance();
    finterm::HttpClient::instance().set_base_url(config.api_base_url());

    // Register migrations explicitly (avoids MSVC /OPT:REF stripping static-init TUs).
    // Actual migration execution happens inside Database::open() which is called by
    // AuthService::open_user_db() — no manual Database::open() call here.
    finterm::register_migration_v001();
    finterm::register_migration_v002();
    finterm::register_migration_v003();
    finterm::register_migration_v004();
    finterm::register_migration_v005();
    finterm::register_migration_v006();
    finterm::register_migration_v007();
    finterm::register_migration_v008();
    finterm::register_migration_v009();
    finterm::register_migration_v010();
    finterm::register_migration_v011();
    finterm::register_migration_v012();
    finterm::register_migration_v013();
    finterm::register_migration_v014();
    finterm::register_migration_v015();
    finterm::register_migration_v016();
    finterm::register_migration_v017();
    finterm::register_migration_v018();
    finterm::register_migration_v019();
    finterm::register_migration_v020();
    finterm::register_migration_v021();
    finterm::register_migration_v022();
    finterm::register_migration_v023();
    finterm::register_migration_v024();
    finterm::register_migration_v025();
    finterm::register_migration_v026();
    finterm::register_migration_v027();
    finterm::register_migration_v028();
    finterm::register_migration_v029();
    finterm::register_migration_v030();
    finterm::register_migration_v031();
    finterm::register_migration_v032();
    finterm::register_migration_v033();
    finterm::register_migration_v034();
    finterm::register_migration_v035();
    finterm::register_migration_v036();
    finterm::register_migration_v037();
    finterm::register_migration_v038();
    finterm::register_migration_v039();
    finterm::register_migration_v040();
    finterm::register_migration_v041();
    finterm::register_migration_v042();
    finterm::register_migration_v043();
    finterm::register_migration_v044();
    finterm::register_migration_v045();
    finterm::register_migration_v046();
    finterm::register_migration_v047();
    finterm::register_migration_v048();
    finterm::register_migration_v049();
    finterm::register_migration_v050();
    finterm::register_migration_v051();
    finterm::register_migration_v052();
    finterm::register_migration_v053();
    finterm::register_migration_v054();
    finterm::register_migration_v055();
    finterm::register_migration_v056();
    finterm::register_migration_v057();
    finterm::register_migration_v058();
    finterm::register_migration_v059();

    // Open cache database (non-fatal if fails)
    QString cache_path = finterm::AppPaths::data() + "/cache.db";
    auto cache_result = finterm::CacheDatabase::instance().open(cache_path);
    if (cache_result.is_err()) {
        LOG_WARN("App", "Cache DB failed (non-fatal): " + QString::fromStdString(cache_result.error()));
    }

    // Storage the app no longer reads (retired caches, superseded files, dead
    // settings groups) — off the startup path, a few seconds in.
    QTimer::singleShot(3000, qApp, [] { finterm::storage::sweep_retired_storage(); });

    // Assign a unique session ID so ScreenStateManager can tag each state write.
    // This lets us distinguish cross-session restores from same-session saves.
    {
        const QString sid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        finterm::ScreenStateManager::instance().set_session_id(sid);
        LOG_INFO("App", "Session ID: " + sid);
    }

    LOG_INFO("App", "Starting session manager...");
    // Start session
    finterm::SessionManager::instance().start_session();

    // Initialize in-process auth service (replaces external stub server).
    // On the very first launch (no auth.db present), wipe any legacy single-user
    // fincept.db so the multi-user setup starts completely clean.
    {
        const QString auth_db = finterm::AppPaths::data() + "/auth.db";
        const QString main_db = finterm::AppPaths::data() + "/fincept.db";
        const QString stub_db = QDir::homePath() + "/.fincept-localhost/users.db";
        if (!QFile::exists(auth_db)) {
            if (QFile::exists(main_db)) {
                QFile::rename(main_db, main_db + ".pre-multiuser.bak");
                LOG_INFO("App", "Archived legacy fincept.db → fincept.db.pre-multiuser.bak");
            }
            if (QFile::exists(stub_db)) {
                QFile::rename(stub_db, stub_db + ".bak");
                LOG_INFO("App", "Archived legacy stub users.db");
            }
        }
    }
    // Credentials some services once kept under private ids move to the
    // names Settings › Credentials edits — once, before anything reads them.
    finterm::SecureStorage::instance().migrate_legacy_ids();

#ifndef Q_OS_WIN
    // finterm's own folders are private to this account. Older builds left
    // them 0755 with auth.db and users/<name>.db 0644, readable by any local
    // account. A 0700 directory closes everything inside it — files written
    // later included — without touching the umask, so files the user saves
    // elsewhere (exports, reports) keep their normal permissions.
    {
        const auto owner_only = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
        const QString config_dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                                   QLatin1Char('/') + QLatin1String(finterm::AppIdentity::kOrg);
        for (const QString& dir_path :
             {finterm::AppPaths::root(), finterm::AppPaths::user(), finterm::AppPaths::data(),
              finterm::AppPaths::data() + "/users", finterm::AppPaths::features(), finterm::AppPaths::agents(),
              finterm::AppPaths::market_data(), finterm::AppPaths::logs(), finterm::AppPaths::files(),
              finterm::AppPaths::cache(), finterm::AppPaths::workspaces(), finterm::AppPaths::crashdumps(),
              config_dir}) {
            if (QFileInfo(dir_path).isDir())
                QFile::setPermissions(dir_path, owner_only);
        }
    }
#endif

    finterm::auth::AuthService::instance().initialize();
    // hearth, for a user who connected it and wants it with finterm; and the
    // look that decides whether to ask one who has not (EngineIndicator). Both
    // are the user's settings, so they wait for the user to be in.
    // The chat's configuration is the user's, too: read again for whoever is in.
    QObject::connect(&finterm::auth::AuthManager::instance(), &finterm::auth::AuthManager::logged_out, &app,
                     []() { finterm::ai_chat::LlmService::instance().reload_config(); });
    QObject::connect(&finterm::auth::AuthManager::instance(), &finterm::auth::AuthManager::terminal_unlocked, &app, []() {
        finterm::ai_chat::LlmService::instance().reload_config();
        QTimer::singleShot(3000, qApp, []() {
            auto& hearth = finterm::HearthService::instance();
            // Looked for whether connected (it may have been updated, or not) or
            // never asked about; not for one who said no.
            if (hearth.link() == finterm::engine::Link::Connected)
                hearth.start_if_wanted();
            if (hearth.link() != finterm::engine::Link::Declined)
                hearth.detect();
        });
    });

    // Initialize auth: load_session() synchronously opens the per-user DB if a
    // saved session exists, so the DB is available for the theme load below.
    finterm::auth::AuthManager::instance().initialize();

    // Apply the theme. Font is fixed to the app default (customization removed)
    // and the theme is always Obsidian, so nothing is read from the user DB here:
    // ThemeManager's built-in default font (Consolas/14) is applied for everyone,
    // ignoring any legacy appearance.font_* keys a previous version may have saved.
    {
        auto& tm = finterm::ui::ThemeManager::instance();
        tm.apply_theme("Obsidian");
        LOG_INFO("App", "Theme: Obsidian, default font");

        // Prune stale news articles; deferred so startup critical path is not blocked.
        if (finterm::Database::instance().is_open()) {
            int64_t news_cutoff = QDateTime::currentSecsSinceEpoch() - (30LL * 86400);
            QTimer::singleShot(0, [news_cutoff]() {
                finterm::NewsArticleRepository::instance().prune_older_than(news_cutoff);
                LOG_INFO("App", "News articles pruned (keeping 30 days)");
            });
        }
    }

    // Initialize PinManager — loads PIN state from SecureStorage
    (void)finterm::auth::PinManager::instance();

    // Inactivity guard — auto-lock after idle timeout.
    // Load timeout setting from DB (default 10 minutes).
    {
        auto& guard = finterm::auth::InactivityGuard::instance();
        auto timeout_r = finterm::SettingsRepository::instance().get("security.lock_timeout_minutes");
        if (timeout_r.is_ok() && !timeout_r.value().isEmpty()) {
            int minutes = timeout_r.value().toInt();
            if (minutes > 0)
                guard.set_timeout_minutes(minutes);
        }
        // Guard is installed on qApp and enabled by MainWindow::on_terminal_unlocked()
        // after the user successfully enters their PIN.
    }

    // Force the ReportBuilderService singleton onto the main thread before
    // MCP tools register — tools route into it via QMetaObject::invokeMethod
    // with BlockingQueuedConnection from worker threads, so the service must
    // already exist with main-thread affinity.
    (void)finterm::services::ReportBuilderService::instance();

    // Initialize MCP tool system — registers all internal tools and starts
    // external MCP servers in the background (non-blocking). Deferred to the
    // next event-loop tick: the ~367 internal tool constructions aren't needed
    // at first paint, so keeping them off the show() path speeds up startup.
    QTimer::singleShot(0, &app, []() { finterm::mcp::initialize_all_tools(); });

    // ── App-focus refresh: invalidate stale data when the user returns ──────
    //
    // The DataHub scheduler keeps polling per TopicPolicy while the app is
    // inactive (so e.g. the dashboard keeps running). But yfinance + Nasdaq
    // also rate-limit, the daemon can drift after many hours, and the
    // CacheManager TTLs (30s for quotes) hide stale data behind cache hits
    // for direct fetch_quotes consumers. When the user comes back to the
    // app after > kWakeThresholdMs idle, force-refresh every subscribed
    // topic AND drop the quote-bucket cache so the next fetch path is also
    // forced to refetch. This is the single "wake from overnight idle"
    // signal that brings every screen back to fresh data on first
    // interaction in the morning — no per-screen polling tuning needed.
    {
        constexpr qint64 kWakeThresholdMs = 5LL * 60LL * 1000LL;
        static qint64 s_last_inactive_ms = 0;
        QObject::connect(&app, &QApplication::applicationStateChanged, &app, [](Qt::ApplicationState state) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (state == Qt::ApplicationActive) {
                if (s_last_inactive_ms == 0)
                    return; // never went inactive
                const qint64 idle_ms = now - s_last_inactive_ms;
                s_last_inactive_ms = 0;
                if (idle_ms < kWakeThresholdMs)
                    return; // brief alt-tab, ignore
                LOG_INFO("App",
                         QString("Resumed after %1s idle — forcing refresh on subscribed topics").arg(idle_ms / 1000));
                // Drop the per-symbol quote cache so PortfolioService /
                // MarketDataService::fetch_quotes paths also re-hit the daemon
                // instead of serving freshly-expired-but-still-30s-fresh data.
                finterm::CacheManager::instance().remove_prefix("market:");
                // Force-refresh every topic that still has subscribers; the
                // DataHub min_interval gate is bypassed by force=true but
                // per-producer max_requests_per_sec stays in effect so
                // upstream yfinance isn't hammered.
                QStringList topics;
                for (const auto& s : finterm::datahub::DataHub::instance().stats())
                    if (s.subscriber_count > 0)
                        topics.append(s.topic);
                if (!topics.isEmpty())
                    finterm::datahub::DataHub::instance().request(topics, /*force=*/true);
            } else if (state == Qt::ApplicationInactive || state == Qt::ApplicationSuspended) {
                if (s_last_inactive_ms == 0)
                    s_last_inactive_ms = now;
            }
        });
    }

    // ── `--selftest [report.json]`: does this install get data? ─────────────
    // Headless: Python setup, the data daemon, one real quote. Before any
    // window, account or PIN. See SelfTest.h.
    {
        const QStringList args = app.arguments();
        const qsizetype at = args.indexOf(QStringLiteral("--selftest"));
        if (at >= 0) {
            const QString report = at + 1 < args.size() && !args.at(at + 1).startsWith(u'-') ? args.at(at + 1) : QString();
            return finterm::selftest::run(app, report);
        }
    }

    // ── Python environment check ─────────────────────────────────────────────
    // check_status() fast path (sentinel + markers present) is synchronous and
    // cheap. The slow path (first run) can spawn processes — but at this point
    // no window is visible yet so the brief block is acceptable. The SetupScreen
    // itself offloads prefill_completed_steps() to a background thread (P1).
    auto setup_status = finterm::python::PythonSetupManager::instance().check_status();

    if (setup_status.needs_setup) {
        LOG_INFO("App", "Python environment not ready — showing setup screen");

        // Use QPointer so the setup_complete lambda is safe against double-fire
        // (e.g. user somehow triggers it twice before the window is hidden).
        auto* setup_screen = new finterm::screens::SetupScreen;
        QPointer<finterm::screens::SetupScreen> screen_guard(setup_screen);
        setup_screen->setWindowTitle("finterm — First-Time Setup");
        setup_screen->resize(800, 600);
        setup_screen->show();

        // When setup completes, hide setup screen and launch main window.
        // The connection uses Qt::SingleShotConnection (Qt 6.0+) so the lambda
        // fires exactly once even if setup_complete is somehow emitted twice.
        QObject::connect(setup_screen, &finterm::screens::SetupScreen::setup_complete, [&app, screen_guard]() {
            if (!screen_guard)
                return; // already cleaned up — ignore
            screen_guard->hide();
            screen_guard->deleteLater();

            finterm::KeyConfigManager::instance();     // init before MainWindow registers shortcuts
            auto* window = new finterm::MainWindow(0); // primary window
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
            // Up: a just-applied update is good (see boot_check above).
            QTimer::singleShot(5000, []() { finterm::services::update_install::confirm_boot(); });

            // Restore any secondary windows that were open at last shutdown so
            // multi-monitor layouts survive across relaunches. Each window
            // restores its own geometry + dock layout from SessionManager.
            {
                const QList<int> saved_ids = finterm::SessionManager::instance().load_window_ids();
                for (int id : saved_ids) {
                    if (id <= 0)
                        continue; // 0 = primary, already created
                    auto* w = new finterm::MainWindow(id);
                    w->setAttribute(Qt::WA_DeleteOnClose);
                    w->show();
                }
                if (!saved_ids.isEmpty())
                    LOG_INFO("App", QString("Restored %1 secondary window(s) from last session")
                                        .arg(saved_ids.size() > 0 ? saved_ids.size() - 1 : 0));
            }

            // Wire new-window handler now that the primary window exists
            QObject::connect(&app, &SingleApplication::receivedMessage,
                             [](quint32 /*instanceId*/, QByteArray /*message*/) {
                                 auto* w = new finterm::MainWindow(finterm::MainWindow::next_window_id());
                                 w->setAttribute(Qt::WA_DeleteOnClose);
                                 w->show();
                                 w->raise();
                                 w->activateWindow();
                                 LOG_INFO("App", "New window opened via secondary instance request");
                             });
            QObject::connect(&app, &QApplication::lastWindowClosed, &app, &QApplication::quit);

            if (!finterm::ai_chat::LlmService::instance().is_configured())
                LOG_WARN("App",
                         "LLM provider not configured — AI chat will prompt user to configure Settings → AI Config");

            // Warm agent discovery cache (same reason as the main path).
            QTimer::singleShot(0, &app, []() { finterm::services::AgentService::instance().discover_agents(); });

            // Pre-spawn the persistent yfinance daemon so its 1–3 s
            // import cost overlaps with the GUI coming up instead of
            // stalling the first dashboard refresh (the global indices
            // and commodities widgets were observed taking long on
            // initial load — that delay is the daemon import, not the
            // upstream API).
            QTimer::singleShot(0, &app, []() { finterm::python::PythonWorker::instance().warm_up(); });
            // The 13F index keeps itself current from launch, not from the first
            // visit to Ownership (see OwnershipService::start_background_sync).
            QTimer::singleShot(0, &app,
                               []() { finterm::services::OwnershipService::instance().start_background_sync(); });

            LOG_INFO("App", "Application ready (after setup)");
        });

        // A terminate signal (finterm.sh stop, logout, kill) becomes a normal
        // quit — installed only now, at the main loop: during startup's own
        // dialogs a quit would end just that dialog and startup would carry on.
        finterm::lifecycle::quit_on_terminate_signals();
        return app.exec();
    }

    // Ensure KeyConfigManager is initialized before MainWindow registers shortcuts
    finterm::KeyConfigManager::instance();

    // Python already set up — launch main window directly
    finterm::MainWindow window;
    window.show();
    // Up: a just-applied update is good (see boot_check above).
    QTimer::singleShot(5000, []() { finterm::services::update_install::confirm_boot(); });

    // Restore any secondary windows that were open at last shutdown. The
    // primary window on the stack owns its own lifetime; restored secondaries
    // use WA_DeleteOnClose and self-remove from QApplication::topLevelWidgets.
    {
        const QList<int> saved_ids = finterm::SessionManager::instance().load_window_ids();
        for (int id : saved_ids) {
            if (id <= 0)
                continue; // 0 = primary, already created
            auto* w = new finterm::MainWindow(id);
            w->setAttribute(Qt::WA_DeleteOnClose);
            w->show();
        }
        if (saved_ids.size() > 1)
            LOG_INFO("App", QString("Restored %1 secondary window(s) from last session").arg(saved_ids.size() - 1));
    }

    // ── New-window handler: fires when the user re-launches the exe ──────────
    // The secondary instance sends "--new-window" and exits. We construct a new
    // independent MainWindow in this process. WA_DeleteOnClose ensures cleanup.
    // QApplication::lastWindowClosed() → quit() handles the final exit.
    QObject::connect(&app, &SingleApplication::receivedMessage, [](quint32 /*instanceId*/, QByteArray /*message*/) {
        auto* w = new finterm::MainWindow(finterm::MainWindow::next_window_id());
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        w->raise();
        w->activateWindow();
        LOG_INFO("App", "New window opened via secondary instance request");
    });

    // Quit when the last window is closed (standard Qt SDI behaviour).
    QObject::connect(&app, &QApplication::lastWindowClosed, &app, &QApplication::quit);

    // If requirements files changed (app update), sync packages in background
    // without blocking the user. Connect setup_complete so failures are logged
    // (no SetupScreen in this path, so we only log — don't show UI).
    if (setup_status.needs_package_sync) {
        LOG_INFO("App", "Requirements changed — syncing packages in background");
        auto& mgr = finterm::python::PythonSetupManager::instance();
        QObject::connect(
            &mgr, &finterm::python::PythonSetupManager::setup_complete, &mgr,
            [](bool success, const QString& error) {
                const QStringList failed = finterm::python::PythonSetupManager::instance().failed_packages();
                if (success && failed.isEmpty())
                    LOG_INFO("App", "Background package sync completed successfully");
                else if (success)
                    LOG_WARN("App", "Background package sync done without " + failed.join(", "));
                else
                    LOG_WARN("App", "Background package sync failed (non-fatal): " + error);
            },
            Qt::SingleShotConnection);
        mgr.run_setup();
    }

    if (!finterm::ai_chat::LlmService::instance().is_configured())
        LOG_WARN("App", "LLM provider not configured — AI chat will prompt user to configure Settings → AI Config");

    // Warm the agent discovery cache on startup. This populates
    // AgentService::cached_agents() so any screen that lists agents
    // (Agent Config, Portfolio → Agent Runner, Node Editor) shows the
    // full finagent_core set immediately instead of falling back to the
    // much smaller DB-only list. Run deferred so Python is fully ready.
    QTimer::singleShot(0, &app, []() { finterm::services::AgentService::instance().discover_agents(); });

    // Pre-spawn the persistent yfinance daemon so its 1–3 s import cost
    // overlaps with the GUI coming up instead of stalling the first
    // dashboard refresh.
    QTimer::singleShot(0, &app, []() { finterm::python::PythonWorker::instance().warm_up(); });
    // The 13F index keeps itself current from launch, not from the first
    // visit to Ownership (see OwnershipService::start_background_sync).
    QTimer::singleShot(0, &app, []() { finterm::services::OwnershipService::instance().start_background_sync(); });

    LOG_INFO("App", "Application ready");
    // Same as the first-run path above: installed only at the main loop.
    finterm::lifecycle::quit_on_terminate_signals();
    return app.exec();
}

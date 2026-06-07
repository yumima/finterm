#include "ai_chat/LlmService.h"
#include "app/MainWindow.h"
#include "auth/AuthManager.h"
#include "auth/AuthService.h"
#include "auth/InactivityGuard.h"
#include "auth/PinManager.h"
#include "auth/SessionGuard.h"
#include "core/config/AppConfig.h"
#include "core/config/AppIdentity.h"
#include "core/config/AppPaths.h"
#include "core/config/LayoutMigrations.h"
#include "core/config/ProfileManager.h"
#include "core/components/ComponentCatalog.h"
#include "core/crash/CrashHandler.h"
#include "core/keys/KeyConfigManager.h"
#include "core/logging/Logger.h"
#include "core/session/ScreenStateManager.h"
#include "core/session/SessionManager.h"
#include "core/symbol/SymbolGroup.h"
#include "core/symbol/SymbolRef.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "storage/cache/CacheManager.h"
#include "mcp/McpInit.h"
#include "network/http/HttpClient.h"
#include "python/PythonSetupManager.h"
#include "python/PythonWorker.h"
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
#include "services/options/OISnapshotter.h"
#include "services/options/OptionChainService.h"
#include "services/polymarket/PolymarketWebSocket.h"
#include "services/prediction/PredictionCredentialStore.h"
#include "services/prediction/PredictionExchangeRegistry.h"
#include "services/prediction/kalshi/KalshiAdapter.h"
#include "services/prediction/polymarket/PolymarketAdapter.h"
#include "services/relationship_map/RelationshipMapService.h"
#include "services/report_builder/ReportBuilderService.h"
#include "datahub/DataHub.h"
#include "datahub/TopicPolicy.h"
#include "services/billing/FeeDiscountService.h"
#include "services/wallet/TokenMetadataService.h"
#include "services/wallet/WalletService.h"
#include "trading/DataStreamManager.h"
#include "trading/ExchangeService.h"
#include "trading/ExchangeSessionManager.h"
#include "storage/repositories/NewsArticleRepository.h"
#include "ai_chat/HearthSupervisor.h"
#include "storage/repositories/SettingsRepository.h"
#include "storage/sqlite/CacheDatabase.h"
#include "storage/sqlite/Database.h"
#include "storage/sqlite/migrations/MigrationRunner.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <memory>

#include <singleapplication.h>

#ifdef Q_OS_WIN
#    include <Windows.h>
#endif

int main(int argc, char* argv[]) {
    // ── Parse --profile <name> from argv before Qt initialises ───────────────
    // This must happen first so that:
    //   1. AppPaths returns the correct per-profile directories
    //   2. SingleApplication uses a profile-scoped IPC key so two different
    //      profiles can run simultaneously as independent primary instances
    {
        for (int i = 1; i < argc - 1; ++i) {
            if (qstrcmp(argv[i], "--profile") == 0) {
                fincept::ProfileManager::instance().set_active(QString::fromUtf8(argv[i + 1]));
                break;
            }
        }
        // AppPaths::root() must exist before ensure_all() so ProfileManager can
        // write the manifest. Create root now (single mkdir, idempotent).
        QDir().mkpath(fincept::AppPaths::root());
    }

    // Install the unhandled-exception filter BEFORE any Qt object is
    // constructed. On Windows this writes a minidump to AppPaths::crashdumps()
    // when the process dies from an access violation, stack overflow, or GS
    // cookie check failure (STATUS_STACK_BUFFER_OVERRUN — see issue #215).
    // Do it early so a crash in Qt's own startup still produces a dump.
    fincept::crash::install();

    // Required before QApplication when any dock panel contains an OpenGL widget
    // (Qt Charts, QOpenGLWidget) — prevents black rendering in floating windows.
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // SingleApplication enforces one process per profile.
    // The instance key is scoped to the active profile name, so
    // "FinceptTerminal --profile work" and "FinceptTerminal --profile personal"
    // are treated as two separate primary instances and run simultaneously.
    // allowSecondary=true: secondary instances send "--new-window" and exit.
    const QString profile_key = QString("%1-%2").arg(fincept::AppIdentity::kApp).arg(fincept::ProfileManager::instance().active());
    SingleApplication app(argc, argv, /*allowSecondary=*/true, SingleApplication::Mode::User, 100,
                          profile_key.toUtf8());
    app.setApplicationName(fincept::AppIdentity::kApp);
    app.setOrganizationName(fincept::AppIdentity::kOrg);
    // Visible brand — deliberately decoupled from the frozen storage identity
    // above, so the app can be rebranded without moving any on-disk data or
    // settings (see core/config/AppIdentity.h).
    app.setApplicationDisplayName(fincept::AppIdentity::kDisplayName);
    // Desktop-entry / WM_CLASS (X11) / app_id (Wayland) association — matches the
    // installed finterm.desktop so the taskbar groups windows under the brand.
    app.setDesktopFileName(fincept::AppIdentity::kDisplayName);
#ifndef FINCEPT_VERSION_STRING
#    define FINCEPT_VERSION_STRING "0.0.0-dev"
#endif
    app.setApplicationVersion(QStringLiteral(FINCEPT_VERSION_STRING));

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

    // ── Versioned layout migrations (primary only) ───────────────────────────
    // Self-healing for on-disk layout moves between releases (settings-store
    // rebrand, orphaned legacy caches, …). Fast no-op once up to date — a single
    // QSettings read — and narrates to stdout only when it actually migrates, so
    // a terminal launch shows what happened before the GUI. Secondary instances
    // returned above, so this runs once per primary launch. See
    // core/config/LayoutMigrations.h.
    fincept::LayoutMigrations::run();

    // Register DataHub payload meta-types (QuoteData, HistoryPoint, InfoData,
    // NewsArticle, EconomicsResult) so they can flow through QVariant-keyed
    // topics and cross-thread queued signals. Phase 0 — see
    // fincept-qt/DATAHUB_ARCHITECTURE.md.
    // Phase 2: register MarketDataService as the `market:quote:*` producer.
    fincept::datahub::register_metatypes();
    // SymbolContext payload types — signals cross threads when a producer
    // service (not just UI) publishes a group change.
    qRegisterMetaType<fincept::SymbolRef>("fincept::SymbolRef");
    qRegisterMetaType<fincept::SymbolGroup>("fincept::SymbolGroup");
    // Phase 6: load the Component Browser catalogue. Try the build-side copy
    // first (present after cmake configure copies resources) and fall back to
    // the source-tree path for local dev runs without install step.
    fincept::ComponentCatalog::instance().load_with_fallbacks({
        QCoreApplication::applicationDirPath() + "/resources/component_catalog.json",
        QCoreApplication::applicationDirPath() + "/component_catalog.json",
        "resources/component_catalog.json",
    });
    fincept::services::MarketDataService::instance().ensure_registered_with_hub();
    // Phase 2 (multi-broker refactor): ExchangeSessionManager is the hub
    // producer for `ws:kraken:*` / `ws:hyperliquid:*`. Individual sessions
    // (created lazily by the manager) publish through its SessionPublisher
    // seam. ExchangeService keeps a back-compat shim but no longer registers
    // itself with the hub.
    fincept::trading::ExchangeSessionManager::instance().ensure_registered_with_hub();
    // Prediction Markets — multi-exchange tab (Polymarket, Kalshi, …).
    // PolymarketWebSocket is the hub producer for `prediction:polymarket:*`;
    // the adapter layer translates exchange-local types into the unified
    // prediction::* data model for the screen.
    fincept::services::polymarket::PolymarketWebSocket::instance().ensure_registered_with_hub();
    {
        auto& reg = fincept::services::prediction::PredictionExchangeRegistry::instance();
        reg.register_adapter(
            std::make_unique<fincept::services::prediction::polymarket_ns::PolymarketAdapter>());
        reg.register_adapter(
            std::make_unique<fincept::services::prediction::kalshi_ns::KalshiAdapter>());

        // Hydrate credentials from SecureStorage if previously saved. This
        // has to happen after registration so the adapters exist.
        if (auto* pm = dynamic_cast<fincept::services::prediction::polymarket_ns::PolymarketAdapter*>(
                reg.adapter(QStringLiteral("polymarket")))) {
            pm->reload_credentials();
        }
        if (auto* ks = dynamic_cast<fincept::services::prediction::kalshi_ns::KalshiAdapter*>(
                reg.adapter(QStringLiteral("kalshi")))) {
            if (auto creds = fincept::services::prediction::PredictionCredentialStore::load_kalshi()) {
                ks->set_credentials(*creds);
            }
        }
    }
    // Phase 5: NewsService as the news:general / news:symbol:* /
    // news:category:* / news:cluster:* producer.
    fincept::services::NewsService::instance().ensure_registered_with_hub();
    // Phase 6: Economics + DBnomics + GovData producers.
    fincept::services::EconomicsService::instance().ensure_registered_with_hub();
    fincept::services::DBnomicsService::instance().ensure_registered_with_hub();
    fincept::services::GovDataService::instance().ensure_registered_with_hub();
    // Phase 7: DataStreamManager as the broker:* producer (positions,
    // orders, balance, holdings, quote, ticks). Per-account topic shape
    // `broker:<id>:<account_id>:<channel>`.
    fincept::trading::DataStreamManager::instance().ensure_registered_with_hub();
    // Phase 8: Geopolitics / Maritime / RelationshipMap / MAAnalytics.
    fincept::services::geo::GeopoliticsService::instance().ensure_registered_with_hub();
    fincept::services::maritime::MaritimeService::instance().ensure_registered_with_hub();
    fincept::services::RelationshipMapService::instance().ensure_registered_with_hub();
    fincept::services::ma::MAAnalyticsService::instance().ensure_registered_with_hub();
    // Phase 9: AgentService as agent:* push-only producer (output/stream/status/routing/error).
    fincept::services::AgentService::instance().ensure_registered_with_hub();
    // Track 10: cron-shaped scheduler for agent runs.  Reads agent_schedule
    // on each 60s tick; no-op until the user adds entries.
    fincept::services::AgentScheduler::instance().start();
    // F&O: option:chain:* / option:tick:* fed by the active broker. OISnapshotter
    // subscribes to option:chain:* and persists per-minute OI snapshots for the
    // OI Buildup tab. FiiDiiService (upstream's India-specific FII/DII feed) is
    // intentionally not registered — that tab was dropped from the screen.
    fincept::services::options::OptionChainService::instance().ensure_registered_with_hub();
    fincept::services::options::OISnapshotter::instance().ensure_registered_with_hub();
    // Crypto: WalletService owns wallet:balance:* and market:price:token:*.
    // TokenMetadataService loads its symbol/name cache from SecureStorage
    // first so the very first balance publish has labels for known tokens;
    // a daily background refresh fires in the background after.
    fincept::wallet::TokenMetadataService::instance().load_from_storage();
    fincept::wallet::TokenMetadataService::instance().refresh_from_jupiter_async();
    // Restore_from_storage runs after the hub is up so a soft-connected
    // wallet's balance topic resolves to a registered producer immediately.
    fincept::wallet::WalletService::instance().ensure_registered_with_hub();
    fincept::wallet::WalletService::instance().restore_from_storage();

    // Phase 2 §2C: fee-discount eligibility producer. Lives in billing/
    // because it's consumed by other paid screens later; for Phase 2 it
    // only feeds HoldingsBar's chip and TradeTab's FeeDiscountPanel.
    {
        static fincept::billing::FeeDiscountService discount_service;
        auto& hub = fincept::datahub::DataHub::instance();
        hub.register_producer(&discount_service);
        fincept::datahub::TopicPolicy p;
        // Eligibility is derived from wallet:balance — refresh cadence here
        // is just a fallback; the service also republishes whenever the
        // balance topic emits.
        p.ttl_ms = 60 * 1000;
        p.min_interval_ms = 15 * 1000;
        hub.set_policy_pattern(QStringLiteral("billing:fncpt_discount:*"), p);
    }

    // Create all application directories under %LOCALAPPDATA%/com.fincept.terminal
    fincept::AppPaths::ensure_all();

    // ── One-time migration from legacy %APPDATA% location ─────────────────
    // Current locations (under %LOCALAPPDATA%\com.fincept.terminal\):
    //   Log: <root>/logs/fincept.log    DB: <root>/data/fincept.db
    {
        const QString old_base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto migrate_file = [](const QString& old_path, const QString& new_path) {
            if (QFile::exists(old_path) && !QFile::exists(new_path))
                QFile::rename(old_path, new_path);
        };
        migrate_file(old_base + "/fincept.db", fincept::AppPaths::data() + "/fincept.db");
        migrate_file(old_base + "/cache.db", fincept::AppPaths::data() + "/cache.db");
        migrate_file(old_base + "/fincept.log", fincept::AppPaths::logs() + "/fincept.log");
        migrate_file(old_base + "/fincept-files", fincept::AppPaths::files());
        // Remove stale WAL/SHM from old location too
        QFile::remove(old_base + "/fincept.db-wal");
        QFile::remove(old_base + "/fincept.db-shm");
        QFile::remove(old_base + "/cache.db-wal");
        QFile::remove(old_base + "/cache.db-shm");
    }

    // ── WAL/SHM cleanup — gated behind QLockFile ────────────────────────────
    // Deleting WAL/SHM files while another process has the DB open corrupts it.
    // We only clean up orphaned files from a previous crash when we are the sole
    // running instance. The lock file is held for the entire process lifetime.
    QLockFile db_lock(fincept::AppPaths::data() + "/fincept.db.lock");
    db_lock.setStaleLockTime(0); // never auto-expire; we control the lifecycle
    const bool sole_instance = db_lock.tryLock(0);
    if (sole_instance) {
        QFile::remove(fincept::AppPaths::data() + "/fincept.db-wal");
        QFile::remove(fincept::AppPaths::data() + "/fincept.db-shm");
        QFile::remove(fincept::AppPaths::data() + "/cache.db-wal");
        QFile::remove(fincept::AppPaths::data() + "/cache.db-shm");
    }
    // db_lock remains held (stack-allocated) until main() returns.
    // Also clean legacy v3 DB location
    {
        const QString local_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        const QString legacy1 = local_dir.section('/', 0, -3) + "/FinceptTerminal/fincept_settings.db";
        const QString legacy2 =
            QString(local_dir).replace("Fincept/FinceptTerminal", "FinceptTerminal") + "/fincept_settings.db";
        QFile::remove(legacy1 + "-wal");
        QFile::remove(legacy1 + "-shm");
        QFile::remove(legacy2 + "-wal");
        QFile::remove(legacy2 + "-shm");
    }

    fincept::Logger::instance().set_file(fincept::AppPaths::logs() + "/fincept.log");

    // P3.18 — route Qt's own qDebug/qWarning/qCritical messages into our log
    // file so framework/3rd-party warnings are visible in Release builds.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
        const char* category = (ctx.category && *ctx.category) ? ctx.category : "Qt";
        switch (type) {
        case QtDebugMsg:    fincept::Logger::instance().debug(category, msg); break;
        case QtInfoMsg:     fincept::Logger::instance().info(category, msg); break;
        case QtWarningMsg:  fincept::Logger::instance().warn(category, msg); break;
        case QtCriticalMsg: fincept::Logger::instance().error(category, msg); break;
        case QtFatalMsg:
            fincept::Logger::instance().error(category, msg);
            fincept::Logger::instance().flush_and_close();
            break;
        }
    });
    {
        auto& log = fincept::Logger::instance();
        auto& cfg = fincept::AppConfig::instance();

        // Global level
        const QString gl = cfg.get("log/global_level", "Info").toString();
        const QHash<QString, fincept::LogLevel> lvl_map = {{"Trace", fincept::LogLevel::Trace},
                                                           {"Debug", fincept::LogLevel::Debug},
                                                           {"Info", fincept::LogLevel::Info},
                                                           {"Warn", fincept::LogLevel::Warn},
                                                           {"Error", fincept::LogLevel::Error},
                                                           {"Fatal", fincept::LogLevel::Fatal}};
        log.set_level(lvl_map.value(gl, fincept::LogLevel::Info));

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
    LOG_INFO("App", "finterm v4.0.2 starting...");

    // Initialize config (HttpClient base URL kept for non-auth third-party requests)
    auto& config = fincept::AppConfig::instance();
    fincept::HttpClient::instance().set_base_url(config.api_base_url());

    // Register migrations explicitly (avoids MSVC /OPT:REF stripping static-init TUs).
    // Actual migration execution happens inside Database::open() which is called by
    // AuthService::open_user_db() — no manual Database::open() call here.
    fincept::register_migration_v001();
    fincept::register_migration_v002();
    fincept::register_migration_v003();
    fincept::register_migration_v004();
    fincept::register_migration_v005();
    fincept::register_migration_v006();
    fincept::register_migration_v007();
    fincept::register_migration_v008();
    fincept::register_migration_v009();
    fincept::register_migration_v010();
    fincept::register_migration_v011();
    fincept::register_migration_v012();
    fincept::register_migration_v013();
    fincept::register_migration_v014();
    fincept::register_migration_v015();
    fincept::register_migration_v016();
    fincept::register_migration_v017();
    fincept::register_migration_v018();
    fincept::register_migration_v019();
    fincept::register_migration_v020();
    fincept::register_migration_v021();
    fincept::register_migration_v022();
    fincept::register_migration_v023();
    fincept::register_migration_v024();
    fincept::register_migration_v025();
    fincept::register_migration_v026();
    fincept::register_migration_v027();
    fincept::register_migration_v028();
    fincept::register_migration_v029();
    fincept::register_migration_v030();
    fincept::register_migration_v031();
    fincept::register_migration_v032();
    fincept::register_migration_v033();
    fincept::register_migration_v034();
    fincept::register_migration_v035();
    fincept::register_migration_v036();
    fincept::register_migration_v037();
    fincept::register_migration_v038();
    fincept::register_migration_v039();
    fincept::register_migration_v040();
    fincept::register_migration_v041();
    fincept::register_migration_v042();
    fincept::register_migration_v043();

    // Open cache database (non-fatal if fails)
    QString cache_path = fincept::AppPaths::data() + "/cache.db";
    auto cache_result = fincept::CacheDatabase::instance().open(cache_path);
    if (cache_result.is_err()) {
        LOG_WARN("App", "Cache DB failed (non-fatal): " + QString::fromStdString(cache_result.error()));
    }

    // Assign a unique session ID so ScreenStateManager can tag each state write.
    // This lets us distinguish cross-session restores from same-session saves.
    {
        const QString sid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        fincept::ScreenStateManager::instance().set_session_id(sid);
        LOG_INFO("App", "Session ID: " + sid);
    }

    LOG_INFO("App", "Checking settings for legacy migration...");
    // One-time migration: copy settings from old DB (Local\FinceptTerminal\fincept_settings.db)
    // to new DB (Roaming\Fincept\FinceptTerminal\fincept.db) if the new DB has no settings yet.
    {
        LOG_INFO("App", "Querying settings...");
        auto existing = fincept::SettingsRepository::instance().get("fincept_session");
        LOG_INFO("App", "Settings query done");
        bool new_db_empty = existing.is_err() || existing.value().isEmpty();
        if (new_db_empty) {
            QString local_base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            // AppLocalDataLocation = .../Local/Fincept/FinceptTerminal — strip to .../Local/FinceptTerminal
            QString old_db_path = local_base.section('/', 0, -3) + "/FinceptTerminal/fincept_settings.db";
            if (!QFile::exists(old_db_path)) {
                // Try without the org subfolder
                old_db_path = local_base.replace("Fincept/FinceptTerminal", "FinceptTerminal") + "/fincept_settings.db";
            }
            if (QFile::exists(old_db_path)) {
                QSqlDatabase old_db = QSqlDatabase::addDatabase("QSQLITE", "legacy_migration");
                old_db.setDatabaseName(old_db_path);
                if (old_db.open()) {
                    QSqlQuery src(old_db);
                    if (src.exec("SELECT key, value FROM settings")) {
                        int count = 0;
                        while (src.next()) {
                            fincept::SettingsRepository::instance().set(src.value(0).toString(),
                                                                        src.value(1).toString(), "migrated");
                            ++count;
                        }
                        LOG_INFO("App", QString("Migrated %1 settings from legacy DB").arg(count));
                    }
                    old_db.close();
                }
                QSqlDatabase::removeDatabase("legacy_migration");
            }
        }
    }

    LOG_INFO("App", "Starting session manager...");
    // Start session
    fincept::SessionManager::instance().start_session();

    // Honor the "Manage local engine" opt-in at launch: if enabled, start the
    // hearth supervisor now. Otherwise it's only wired when Settings → LLM
    // Config is opened, so "start on launch" wouldn't actually happen. Safe
    // no-op when the setting is unset/false or the hearth binary is missing.
    {
        auto m = fincept::SettingsRepository::instance().get("hearth.manage", "false");
        if (m.is_ok() && m.value() == "true")
            fincept::HearthSupervisor::instance().set_enabled(true);
    }

    // Initialize in-process auth service (replaces external stub server).
    // On the very first launch (no auth.db present), wipe any legacy single-user
    // fincept.db so the multi-user setup starts completely clean.
    {
        const QString auth_db  = fincept::AppPaths::data() + "/auth.db";
        const QString main_db  = fincept::AppPaths::data() + "/fincept.db";
        const QString stub_db  = QDir::homePath() + "/.fincept-localhost/users.db";
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
    fincept::auth::AuthService::instance().initialize();

    // Initialize auth: load_session() synchronously opens the per-user DB if a
    // saved session exists, so the DB is available for the theme load below.
    fincept::auth::AuthManager::instance().initialize();

    // Apply the theme. Font is fixed to the app default (customization removed)
    // and the theme is always Obsidian, so nothing is read from the user DB here:
    // ThemeManager's built-in default font (Consolas/14) is applied for everyone,
    // ignoring any legacy appearance.font_* keys a previous version may have saved.
    {
        auto& tm = fincept::ui::ThemeManager::instance();
        tm.apply_theme("Obsidian");
        LOG_INFO("App", "Theme: Obsidian, default font");

        // Prune stale news articles; deferred so startup critical path is not blocked.
        if (fincept::Database::instance().is_open()) {
            int64_t news_cutoff = QDateTime::currentSecsSinceEpoch() - (30LL * 86400);
            QTimer::singleShot(0, [news_cutoff]() {
                fincept::NewsArticleRepository::instance().prune_older_than(news_cutoff);
                LOG_INFO("App", "News articles pruned (keeping 30 days)");
            });
        }
    }

    // Session guard — auto-logout on 401
    fincept::auth::SessionGuard session_guard;

    // Initialize PinManager — loads PIN state from SecureStorage
    (void)fincept::auth::PinManager::instance();

    // Inactivity guard — auto-lock after idle timeout.
    // Load timeout setting from DB (default 10 minutes).
    {
        auto& guard = fincept::auth::InactivityGuard::instance();
        auto timeout_r = fincept::SettingsRepository::instance().get("security.lock_timeout_minutes");
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
    (void)fincept::services::ReportBuilderService::instance();

    // Initialize MCP tool system — registers all internal tools and starts
    // external MCP servers in the background (non-blocking).
    fincept::mcp::initialize_all_tools();

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
        QObject::connect(&app, &QApplication::applicationStateChanged, &app,
                         [](Qt::ApplicationState state) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (state == Qt::ApplicationActive) {
                if (s_last_inactive_ms == 0) return;     // never went inactive
                const qint64 idle_ms = now - s_last_inactive_ms;
                s_last_inactive_ms = 0;
                if (idle_ms < kWakeThresholdMs) return;  // brief alt-tab, ignore
                LOG_INFO("App", QString("Resumed after %1s idle — forcing refresh on subscribed topics")
                                    .arg(idle_ms / 1000));
                // Drop the per-symbol quote cache so PortfolioService /
                // MarketDataService::fetch_quotes paths also re-hit the daemon
                // instead of serving freshly-expired-but-still-30s-fresh data.
                fincept::CacheManager::instance().remove_prefix("market:");
                // Force-refresh every topic that still has subscribers; the
                // DataHub min_interval gate is bypassed by force=true but
                // per-producer max_requests_per_sec stays in effect so
                // upstream yfinance isn't hammered.
                QStringList topics;
                for (const auto& s : fincept::datahub::DataHub::instance().stats())
                    if (s.subscriber_count > 0) topics.append(s.topic);
                if (!topics.isEmpty())
                    fincept::datahub::DataHub::instance().request(topics, /*force=*/true);
            } else if (state == Qt::ApplicationInactive || state == Qt::ApplicationSuspended) {
                if (s_last_inactive_ms == 0)
                    s_last_inactive_ms = now;
            }
        });
    }

    // ── Python environment check ─────────────────────────────────────────────
    // check_status() fast path (sentinel + markers present) is synchronous and
    // cheap. The slow path (first run) can spawn processes — but at this point
    // no window is visible yet so the brief block is acceptable. The SetupScreen
    // itself offloads prefill_completed_steps() to a background thread (P1).
    auto setup_status = fincept::python::PythonSetupManager::instance().check_status();

    if (setup_status.needs_setup) {
        LOG_INFO("App", "Python environment not ready — showing setup screen");

        // Use QPointer so the setup_complete lambda is safe against double-fire
        // (e.g. user somehow triggers it twice before the window is hidden).
        auto* setup_screen = new fincept::screens::SetupScreen;
        QPointer<fincept::screens::SetupScreen> screen_guard(setup_screen);
        setup_screen->setWindowTitle("finterm — First-Time Setup");
        setup_screen->resize(800, 600);
        setup_screen->show();

        // When setup completes, hide setup screen and launch main window.
        // The connection uses Qt::SingleShotConnection (Qt 6.0+) so the lambda
        // fires exactly once even if setup_complete is somehow emitted twice.
        QObject::connect(setup_screen, &fincept::screens::SetupScreen::setup_complete,
                         [&app, screen_guard]() {
            if (!screen_guard)
                return; // already cleaned up — ignore
            screen_guard->hide();
            screen_guard->deleteLater();

            fincept::KeyConfigManager::instance(); // init before MainWindow registers shortcuts
            auto* window = new fincept::MainWindow(0); // primary window
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();

            // Restore any secondary windows that were open at last shutdown so
            // multi-monitor layouts survive across relaunches. Each window
            // restores its own geometry + dock layout from SessionManager.
            {
                const QList<int> saved_ids =
                    fincept::SessionManager::instance().load_window_ids();
                for (int id : saved_ids) {
                    if (id <= 0) continue; // 0 = primary, already created
                    auto* w = new fincept::MainWindow(id);
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
                                 auto* w = new fincept::MainWindow(fincept::MainWindow::next_window_id());
                                 w->setAttribute(Qt::WA_DeleteOnClose);
                                 w->show();
                                 w->raise();
                                 w->activateWindow();
                                 LOG_INFO("App", "New window opened via secondary instance request");
                             });
            QObject::connect(&app, &QApplication::lastWindowClosed, &app, &QApplication::quit);

            if (!fincept::ai_chat::LlmService::instance().is_configured())
                LOG_WARN("App",
                         "LLM provider not configured — AI chat will prompt user to configure Settings → LLM Config");

            // Warm agent discovery cache (same reason as the main path).
            QTimer::singleShot(0, &app, []() {
                fincept::services::AgentService::instance().discover_agents();
            });

            // Pre-spawn the persistent yfinance daemon so its 1–3 s
            // import cost overlaps with the GUI coming up instead of
            // stalling the first dashboard refresh (the global indices
            // and commodities widgets were observed taking long on
            // initial load — that delay is the daemon import, not the
            // upstream API).
            QTimer::singleShot(0, &app, []() {
                fincept::python::PythonWorker::instance().warm_up();
            });

            LOG_INFO("App", "Application ready (after setup)");
        });

        return app.exec();
    }

    // Ensure KeyConfigManager is initialized before MainWindow registers shortcuts
    fincept::KeyConfigManager::instance();

    // Python already set up — launch main window directly
    fincept::MainWindow window;
    window.show();

    // Restore any secondary windows that were open at last shutdown. The
    // primary window on the stack owns its own lifetime; restored secondaries
    // use WA_DeleteOnClose and self-remove from QApplication::topLevelWidgets.
    {
        const QList<int> saved_ids =
            fincept::SessionManager::instance().load_window_ids();
        for (int id : saved_ids) {
            if (id <= 0) continue; // 0 = primary, already created
            auto* w = new fincept::MainWindow(id);
            w->setAttribute(Qt::WA_DeleteOnClose);
            w->show();
        }
        if (saved_ids.size() > 1)
            LOG_INFO("App", QString("Restored %1 secondary window(s) from last session")
                                .arg(saved_ids.size() - 1));
    }

    // ── New-window handler: fires when the user re-launches the exe ──────────
    // The secondary instance sends "--new-window" and exits. We construct a new
    // independent MainWindow in this process. WA_DeleteOnClose ensures cleanup.
    // QApplication::lastWindowClosed() → quit() handles the final exit.
    QObject::connect(&app, &SingleApplication::receivedMessage, [](quint32 /*instanceId*/, QByteArray /*message*/) {
        auto* w = new fincept::MainWindow(fincept::MainWindow::next_window_id());
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
        auto& mgr = fincept::python::PythonSetupManager::instance();
        QObject::connect(&mgr, &fincept::python::PythonSetupManager::setup_complete,
                         &mgr, [](bool success, const QString& error) {
            if (success)
                LOG_INFO("App", "Background package sync completed successfully");
            else
                LOG_WARN("App", "Background package sync failed (non-fatal): " + error);
        }, Qt::SingleShotConnection);
        mgr.run_setup();
    }

    if (!fincept::ai_chat::LlmService::instance().is_configured())
        LOG_WARN("App", "LLM provider not configured — AI chat will prompt user to configure Settings → LLM Config");

    // Warm the agent discovery cache on startup. This populates
    // AgentService::cached_agents() so any screen that lists agents
    // (Agent Config, Portfolio → Agent Runner, Node Editor) shows the
    // full finagent_core set immediately instead of falling back to the
    // much smaller DB-only list. Run deferred so Python is fully ready.
    QTimer::singleShot(0, &app, []() {
        fincept::services::AgentService::instance().discover_agents();
    });

    // Pre-spawn the persistent yfinance daemon so its 1–3 s import cost
    // overlaps with the GUI coming up instead of stalling the first
    // dashboard refresh.
    QTimer::singleShot(0, &app, []() {
        fincept::python::PythonWorker::instance().warm_up();
    });

    LOG_INFO("App", "Application ready");
    return app.exec();
}

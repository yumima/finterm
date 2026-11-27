#pragma once
#include "core/result/Result.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVector>

#include <functional>

namespace finterm {

/// A single versioned migration.
struct Migration {
    int version;
    QString name;
    std::function<Result<void>(QSqlDatabase&)> apply;
    /// Reaches outside the database (the keyring, cache.db): run only for
    /// the user who is logging in, never for every user at startup.
    bool external = false;
};

/// Runs versioned migrations tracked in a schema_version table.
/// Migration files auto-register via static initialization.
class MigrationRunner {
  public:
    explicit MigrationRunner(QSqlDatabase& db);

    /// Apply all pending migrations in order. An error, and nothing changed,
    /// when the database was written by a newer finterm (newer_than_build()).
    Result<void> run();

    /// The database's schema is above every migration this build has.
    bool newer_than_build();

    /// Register a migration (called at static-init time by each v00N file).
    static void register_migration(Migration m);

    /// All registered migrations, sorted by version.
    static const QVector<Migration>& all_migrations();

    /// The highest registered migration (0 when none is registered).
    static int newest_version();

    /// Why a user database may not be migrated where it is now ("" = it
    /// may): set at startup when the copy that must come first could not be
    /// made (DataGate). Database::open then refuses a database still behind,
    /// and the login says why, rather than changing it with no way back.
    static void set_hold(const QString& reason);
    static QString hold();

    /// A registered migration above `version` is `external`.
    static bool external_after(int version);

  private:
    Result<void> ensure_schema_version_table();
    Result<void> apply_migration(const Migration& m);
    Result<void> record_version(int version, const QString& name);
    int read_current_version();

    QSqlDatabase& db_;
};

/// Register every migration this build has (once; later calls do nothing).
/// Before anything opens or upgrades a user database.
void register_all_migrations();

// Explicit registration functions — call these before Database::open()
// to ensure MSVC linker doesn't strip the migration translation units.
void register_migration_v001();
void register_migration_v002();
void register_migration_v003();
void register_migration_v004();
void register_migration_v005();
void register_migration_v006();
void register_migration_v007();
void register_migration_v008();
void register_migration_v009();
void register_migration_v010();
void register_migration_v011();
void register_migration_v012();
void register_migration_v013();
void register_migration_v014();
void register_migration_v015();
void register_migration_v016();
void register_migration_v017();
void register_migration_v018();
void register_migration_v019();
void register_migration_v020();
void register_migration_v021();
void register_migration_v022();
void register_migration_v023();
void register_migration_v024();
void register_migration_v025();
void register_migration_v026();
void register_migration_v027();
void register_migration_v028();
void register_migration_v029();
void register_migration_v030();
void register_migration_v031();
void register_migration_v032();
void register_migration_v033();
void register_migration_v034();
void register_migration_v035();
void register_migration_v036();
void register_migration_v037();
void register_migration_v038();
void register_migration_v039();
void register_migration_v040();
void register_migration_v041();
void register_migration_v042();
void register_migration_v043();
void register_migration_v044();
void register_migration_v045();
void register_migration_v046();
void register_migration_v047();
void register_migration_v048();
void register_migration_v049();
void register_migration_v050();
void register_migration_v051();
void register_migration_v052();
void register_migration_v053();
void register_migration_v054();
void register_migration_v055();
void register_migration_v056();
void register_migration_v057();
void register_migration_v058();
void register_migration_v059();

} // namespace finterm

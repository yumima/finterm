// tests/storage/test_portfolio_snapshots.cpp
//
// Pins the snapshot-provenance invariant: a 'live' NAV snapshot is a real
// end-of-day valuation and must survive every backfill. Before v048 the
// backfill path used the same INSERT OR REPLACE as the live path, so the
// perf chart's period buttons (which request a backfill for longer windows)
// overwrote the entire accumulated real history with a back-projection.
// These tests run against real temp SQLite files with the actual migrations
// applied — the guard lives in SQL, so SQL is what gets tested.
//
// Each slot seeds its own rows on its own dates: QTest supports invoking a
// single slot from the command line, and a slot that only works after its
// siblings ran would silently test the wrong branch there.

#include "storage/repositories/PortfolioRepository.h"
#include "storage/sqlite/Database.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <optional>

using fincept::Database;
using fincept::PortfolioRepository;

class TestPortfolioSnapshots : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void live_survives_backfill();
    void backfill_fills_missing_dates();
    void backfill_corrects_its_own_rows();
    void backfill_reports_suppressed_writes_as_zero();
    void live_supersedes_backfill();
    void check_constraint_rejects_unknown_source();
    void migration_reapplies_cleanly();
    void migration_reclassifies_legacy_backfill_rows();

  private:
    QTemporaryDir dir_;
    QString portfolio_id_;

    QString date(int days_ago) const {
        return QDate::currentDate().addDays(-days_ago).toString(Qt::ISODate);
    }
    std::optional<fincept::portfolio::PortfolioSnapshot> row_for(const QString& d,
                                                                 const QString& portfolio_id = {}) {
        const QString pid = portfolio_id.isEmpty() ? portfolio_id_ : portfolio_id;
        auto snaps = PortfolioRepository::instance().get_snapshots(pid, 36500);
        if (snaps.is_err())
            return std::nullopt;
        for (const auto& s : snaps.value())
            if (s.snapshot_date == d)
                return s;
        return std::nullopt;
    }
};

void TestPortfolioSnapshots::initTestCase() {
    QVERIFY(dir_.isValid());
    fincept::register_migration_v006();
    fincept::register_migration_v048();
    auto opened = Database::instance().open(dir_.filePath(QStringLiteral("test.db")));
    QVERIFY2(opened.is_ok(), opened.is_err() ? opened.error().c_str() : "");

    auto created = PortfolioRepository::instance().create_portfolio(
        QStringLiteral("Test"), QStringLiteral("tester"), QStringLiteral("USD"));
    QVERIFY(created.is_ok());
    portfolio_id_ = created.value();
}

void TestPortfolioSnapshots::live_survives_backfill() {
    auto& repo = PortfolioRepository::instance();
    const QString d = date(10);

    QVERIFY(repo.save_snapshot(portfolio_id_, 100000.0, 80000.0, 20000.0, 25.0, d).is_ok());
    // The backfill's estimate for the same date must be rejected.
    auto bf = repo.save_backfill_snapshot(portfolio_id_, 55000.0, 80000.0, -25000.0, -31.25, d);
    QVERIFY(bf.is_ok());
    QCOMPARE(bf.value(), 0); // suppressed, and reported as such

    const auto row = row_for(d);
    QVERIFY(row.has_value());
    QCOMPARE(row->total_value, 100000.0);
    QCOMPARE(row->source, QStringLiteral("live"));
}

void TestPortfolioSnapshots::backfill_fills_missing_dates() {
    auto& repo = PortfolioRepository::instance();
    const QString d = date(20);

    auto bf = repo.save_backfill_snapshot(portfolio_id_, 98000.0, 80000.0, 18000.0, 22.5, d);
    QVERIFY(bf.is_ok());
    QCOMPARE(bf.value(), 1);

    const auto row = row_for(d);
    QVERIFY(row.has_value());
    QCOMPARE(row->total_value, 98000.0);
    QCOMPARE(row->source, QStringLiteral("backfill"));
}

void TestPortfolioSnapshots::backfill_corrects_its_own_rows() {
    auto& repo = PortfolioRepository::instance();
    const QString d = date(30);

    // Seed this slot's own backfill row, then revise it — the DO UPDATE
    // branch of the upsert, not the plain INSERT.
    QVERIFY(repo.save_backfill_snapshot(portfolio_id_, 90000.0, 80000.0, 10000.0, 12.5, d).is_ok());
    auto revised = repo.save_backfill_snapshot(portfolio_id_, 99500.0, 81000.0, 18500.0, 22.8, d);
    QVERIFY(revised.is_ok());
    QCOMPARE(revised.value(), 1);

    const auto row = row_for(d);
    QVERIFY(row.has_value());
    QCOMPARE(row->total_value, 99500.0);
    QCOMPARE(row->total_cost_basis, 81000.0);
    QCOMPARE(row->source, QStringLiteral("backfill"));
}

void TestPortfolioSnapshots::backfill_reports_suppressed_writes_as_zero() {
    auto& repo = PortfolioRepository::instance();
    const QString d = date(40);

    QVERIFY(repo.save_snapshot(portfolio_id_, 50000.0, 40000.0, 10000.0, 25.0, d).is_ok());
    auto bf = repo.save_backfill_snapshot(portfolio_id_, 1.0, 1.0, 0.0, 0.0, d);
    QVERIFY(bf.is_ok());
    // The affected-row count is what backfill_history's point_count is built
    // from; counting a suppressed upsert as written would make a full no-op
    // backfill report hundreds of phantom points.
    QCOMPARE(bf.value(), 0);
}

void TestPortfolioSnapshots::live_supersedes_backfill() {
    auto& repo = PortfolioRepository::instance();
    const QString d = date(50);

    QVERIFY(repo.save_backfill_snapshot(portfolio_id_, 90000.0, 80000.0, 10000.0, 12.5, d).is_ok());
    // A real valuation replaces the estimate — and flips provenance, so no
    // later backfill can touch this date again.
    QVERIFY(repo.save_snapshot(portfolio_id_, 101000.0, 81000.0, 20000.0, 24.7, d).is_ok());
    auto row = row_for(d);
    QVERIFY(row.has_value());
    QCOMPARE(row->total_value, 101000.0);
    QCOMPARE(row->source, QStringLiteral("live"));

    auto bf = repo.save_backfill_snapshot(portfolio_id_, 1.0, 1.0, 0.0, 0.0, d);
    QVERIFY(bf.is_ok());
    QCOMPARE(bf.value(), 0);
    row = row_for(d);
    QVERIFY(row.has_value());
    QCOMPARE(row->total_value, 101000.0);
    QCOMPARE(row->source, QStringLiteral("live"));
}

void TestPortfolioSnapshots::check_constraint_rejects_unknown_source() {
    // 'Live', 'LIVE', 'live ' would all satisfy source <> 'live' and be
    // destroyed by the next backfill; the CHECK stops them at write time.
    auto r = Database::instance().execute(
        QStringLiteral("INSERT INTO portfolio_snapshots "
                       "(portfolio_id, total_value, total_cost_basis, total_pnl, total_pnl_percent, "
                       " snapshot_date, source) VALUES (?, 1, 1, 0, 0, ?, 'Live')"),
        {portfolio_id_, date(60)});
    QVERIFY(r.is_err());
}

void TestPortfolioSnapshots::migration_reapplies_cleanly() {
    auto& repo = PortfolioRepository::instance();
    const QString d = date(70);
    QVERIFY(repo.save_snapshot(portfolio_id_, 77000.0, 70000.0, 7000.0, 10.0, d).is_ok());

    // Forget that v048 ran, so reopen actually re-executes apply_v048 against
    // a table that already has the column — the tolerate-and-return branch.
    // Without this, MigrationRunner::run() skips version <= current and the
    // branch is dead code under test.
    QVERIFY(Database::instance().execute(
        QStringLiteral("DELETE FROM schema_version WHERE version = 48"), {}).is_ok());

    auto reopened = Database::instance().reopen(dir_.filePath(QStringLiteral("test.db")));
    QVERIFY2(reopened.is_ok(), reopened.is_err() ? reopened.error().c_str() : "");

    const auto row = row_for(d);
    QVERIFY(row.has_value());
    QCOMPARE(row->total_value, 77000.0);
    QCOMPARE(row->source, QStringLiteral("live"));
}

void TestPortfolioSnapshots::migration_reclassifies_legacy_backfill_rows() {
    // Build a pre-v048 database by hand: v006-shaped tables, no schema_version
    // rows, snapshots whose created_at reveals what wrote them. Opening it
    // through Database runs the real chain, so this exercises the migration's
    // reclassification against genuine legacy data.
    const QString legacy_path = dir_.filePath(QStringLiteral("legacy.db"));
    {
        auto seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("seed"));
        seed.setDatabaseName(legacy_path);
        QVERIFY(seed.open());
        QSqlQuery q(seed);
        QVERIFY(q.exec("CREATE TABLE portfolios (id TEXT PRIMARY KEY, name TEXT NOT NULL, "
                       "owner TEXT NOT NULL DEFAULT '', currency TEXT NOT NULL DEFAULT 'USD', "
                       "description TEXT DEFAULT '', created_at TEXT DEFAULT (datetime('now')), "
                       "updated_at TEXT DEFAULT (datetime('now')))"));
        QVERIFY(q.exec("CREATE TABLE portfolio_snapshots (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "portfolio_id TEXT NOT NULL REFERENCES portfolios(id) ON DELETE CASCADE, "
                       "total_value REAL NOT NULL, total_cost_basis REAL NOT NULL, "
                       "total_pnl REAL NOT NULL, total_pnl_percent REAL NOT NULL, "
                       "snapshot_date TEXT NOT NULL, created_at TEXT DEFAULT (datetime('now')), "
                       "UNIQUE(portfolio_id, snapshot_date))"));
        QVERIFY(q.exec("INSERT INTO portfolios (id, name) VALUES ('p1', 'Legacy')"));
        // A real daily snapshot: written on the day it describes.
        QVERIFY(q.exec("INSERT INTO portfolio_snapshots "
                       "(portfolio_id, total_value, total_cost_basis, total_pnl, total_pnl_percent, "
                       " snapshot_date, created_at) "
                       "VALUES ('p1', 100, 80, 20, 25, '2026-05-01', '2026-05-01 21:30:00')"));
        // Old destructive backfill output: a past date written months later.
        QVERIFY(q.exec("INSERT INTO portfolio_snapshots "
                       "(portfolio_id, total_value, total_cost_basis, total_pnl, total_pnl_percent, "
                       " snapshot_date, created_at) "
                       "VALUES ('p1', 55, 80, -25, -31, '2026-05-02', '2026-08-01 12:00:00')"));
        // Timezone edge: written within a day of its date (UTC created_at vs
        // local snapshot_date) — ambiguous, must stay 'live'.
        QVERIFY(q.exec("INSERT INTO portfolio_snapshots "
                       "(portfolio_id, total_value, total_cost_basis, total_pnl, total_pnl_percent, "
                       " snapshot_date, created_at) "
                       "VALUES ('p1', 99, 80, 19, 23, '2026-05-03', '2026-05-04 02:00:00')"));
        seed.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("seed"));

    auto reopened = Database::instance().reopen(legacy_path);
    QVERIFY2(reopened.is_ok(), reopened.is_err() ? reopened.error().c_str() : "");

    const auto live_row = row_for(QStringLiteral("2026-05-01"), QStringLiteral("p1"));
    QVERIFY(live_row.has_value());
    QCOMPARE(live_row->source, QStringLiteral("live"));

    const auto backfilled_row = row_for(QStringLiteral("2026-05-02"), QStringLiteral("p1"));
    QVERIFY(backfilled_row.has_value());
    QCOMPARE(backfilled_row->source, QStringLiteral("backfill"));
    // Reclassified rows are correctable again: the accurate backfill may
    // replace the counterfactual value the old code wrote.
    auto bf = PortfolioRepository::instance().save_backfill_snapshot(
        QStringLiteral("p1"), 87.0, 80.0, 7.0, 8.75, QStringLiteral("2026-05-02"));
    QVERIFY(bf.is_ok());
    QCOMPARE(bf.value(), 1);

    const auto edge_row = row_for(QStringLiteral("2026-05-03"), QStringLiteral("p1"));
    QVERIFY(edge_row.has_value());
    QCOMPARE(edge_row->source, QStringLiteral("live"));
}

QTEST_GUILESS_MAIN(TestPortfolioSnapshots)
#include "test_portfolio_snapshots.moc"

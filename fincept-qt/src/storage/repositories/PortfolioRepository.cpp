// src/storage/repositories/PortfolioRepository.cpp
#include "storage/repositories/PortfolioRepository.h"

#include "core/logging/Logger.h"

#include <QDateTime>
#include <QUuid>

#include <algorithm>
#include <atomic>

namespace fincept {

PortfolioRepository& PortfolioRepository::instance() {
    static PortfolioRepository s;
    return s;
}

// ── Row mappers ──────────────────────────────────────────────────────────────

portfolio::Portfolio PortfolioRepository::map_portfolio(QSqlQuery& q) {
    return {
        q.value(0).toString(), // id
        q.value(1).toString(), // name
        q.value(2).toString(), // owner
        q.value(3).toString(), // currency
        q.value(4).toString(), // description
        q.value(5).toString(), // created_at
        q.value(6).toString(), // updated_at
    };
}

portfolio::PortfolioAsset PortfolioRepository::map_asset(QSqlQuery& q) {
    return {
        q.value(0).toInt(),    // id
        q.value(1).toString(), // portfolio_id
        q.value(2).toString(), // symbol
        q.value(3).toDouble(), // quantity
        q.value(4).toDouble(), // avg_buy_price
        q.value(5).toString(), // first_purchase_date
        q.value(6).toString(), // last_updated
        q.value(7).toString(), // sector
    };
}

portfolio::Transaction PortfolioRepository::map_transaction(QSqlQuery& q) {
    return {
        q.value(0).toString(), // id
        q.value(1).toString(), // portfolio_id
        q.value(2).toString(), // symbol
        q.value(3).toString(), // transaction_type
        q.value(4).toDouble(), // quantity
        q.value(5).toDouble(), // price
        q.value(6).toDouble(), // total_value
        q.value(7).toString(), // transaction_date
        q.value(8).toString(), // notes
        q.value(9).toString(), // created_at
    };
}

portfolio::PortfolioSnapshot PortfolioRepository::map_snapshot(QSqlQuery& q) {
    return {
        q.value(0).toInt(),    // id
        q.value(1).toString(), // portfolio_id
        q.value(2).toDouble(), // total_value
        q.value(3).toDouble(), // total_cost_basis
        q.value(4).toDouble(), // total_pnl
        q.value(5).toDouble(), // total_pnl_percent
        q.value(6).toString(), // snapshot_date
        q.value(7).toString(), // source
    };
}

// ── Portfolios CRUD ──────────────────────────────────────────────────────────

Result<QVector<portfolio::Portfolio>> PortfolioRepository::list_portfolios() {
    return query_list("SELECT id, name, owner, currency, description, created_at, updated_at "
                      "FROM portfolios ORDER BY name",
                      {}, map_portfolio);
}

Result<portfolio::Portfolio> PortfolioRepository::get_portfolio(const QString& id) {
    return query_one("SELECT id, name, owner, currency, description, created_at, updated_at "
                     "FROM portfolios WHERE id = ?",
                     {id}, map_portfolio);
}

Result<QString> PortfolioRepository::create_portfolio(const QString& name, const QString& owner,
                                                      const QString& currency, const QString& description) {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto r = exec_write("INSERT INTO portfolios (id, name, owner, currency, description) VALUES (?, ?, ?, ?, ?)",
                        {id, name, owner, currency, description});
    if (r.is_err())
        return Result<QString>::err(r.error());
    LOG_INFO("PortfolioRepo", QString("Created portfolio '%1' (%2)").arg(name, id));
    return Result<QString>::ok(id);
}

Result<void> PortfolioRepository::update_portfolio(const QString& id, const QString& name, const QString& owner,
                                                   const QString& currency, const QString& description) {
    return exec_write("UPDATE portfolios SET name = ?, owner = ?, currency = ?, description = ?, "
                      "updated_at = datetime('now') WHERE id = ?",
                      {name, owner, currency, description, id});
}

Result<void> PortfolioRepository::delete_portfolio(const QString& id) {
    LOG_INFO("PortfolioRepo", QString("Deleting portfolio %1").arg(id));
    return exec_write("DELETE FROM portfolios WHERE id = ?", {id});
}

// ── Assets CRUD ──────────────────────────────────────────────────────────────

Result<QVector<portfolio::PortfolioAsset>> PortfolioRepository::get_assets(const QString& portfolio_id) {
    return query_list_as<portfolio::PortfolioAsset>(
        "SELECT id, portfolio_id, symbol, quantity, avg_buy_price, first_purchase_date, last_updated, "
        "COALESCE(sector, '') "
        "FROM portfolio_assets WHERE portfolio_id = ? ORDER BY symbol",
        {portfolio_id}, map_asset);
}

Result<qint64> PortfolioRepository::add_asset(const QString& portfolio_id, const QString& symbol, double qty,
                                              double price, const QString& date, const QString& sector) {
    QString purchase_date = date.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : date;

    // Upsert: if symbol already exists in portfolio, update quantity and avg price.
    // Preserve existing sector unless the caller provides a non-empty one.
    auto existing = query_list_as<portfolio::PortfolioAsset>(
        "SELECT id, portfolio_id, symbol, quantity, avg_buy_price, first_purchase_date, last_updated, "
        "COALESCE(sector, '') "
        "FROM portfolio_assets WHERE portfolio_id = ? AND symbol = ?",
        {portfolio_id, symbol.toUpper()}, map_asset);

    if (existing.is_ok() && !existing.value().isEmpty()) {
        auto& asset = existing.value().first();
        double new_qty = asset.quantity + qty;
        double new_avg = ((asset.avg_buy_price * asset.quantity) + (price * qty)) / new_qty;
        QString merged_sector = sector.isEmpty() ? asset.sector : sector;
        auto r = exec_write("UPDATE portfolio_assets SET quantity = ?, avg_buy_price = ?, sector = ?, "
                            "last_updated = datetime('now') WHERE id = ?",
                            {new_qty, new_avg, merged_sector, asset.id});
        if (r.is_err())
            return Result<qint64>::err(r.error());
        return Result<qint64>::ok(static_cast<qint64>(asset.id));
    }

    return exec_insert(
        "INSERT INTO portfolio_assets (portfolio_id, symbol, quantity, avg_buy_price, first_purchase_date, sector) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        {portfolio_id, symbol.toUpper(), qty, price, purchase_date, sector});
}

Result<void> PortfolioRepository::set_asset_sector(const QString& portfolio_id, const QString& symbol,
                                                   const QString& sector) {
    return exec_write("UPDATE portfolio_assets SET sector = ? "
                      "WHERE portfolio_id = ? AND symbol = ?",
                      {sector, portfolio_id, symbol.toUpper()});
}

Result<void> PortfolioRepository::update_asset(const QString& portfolio_id, const QString& symbol, double qty,
                                               double avg_price) {
    return exec_write("UPDATE portfolio_assets SET quantity = ?, avg_buy_price = ?, "
                      "last_updated = datetime('now') WHERE portfolio_id = ? AND symbol = ?",
                      {qty, avg_price, portfolio_id, symbol.toUpper()});
}

Result<void> PortfolioRepository::set_position(const QString& portfolio_id, const QString& symbol, double qty,
                                               double avg_price, const QString& first_purchase_date) {
    // The asset row is a cache of the transaction-log replay, and this is the
    // one writer that syncs it. Sector is deliberately untouched (import
    // hints / SectorResolver own it); first_purchase_date follows the ledger's
    // first BUY when known so the peak-high window tracks an edited opening
    // date, and is preserved otherwise.
    // The date parameter is bound twice: `excluded.*` reflects the value the
    // INSERT would have written, so folding the empty→now() default into the
    // VALUES expression would make every conflict see a non-empty "new" date
    // and overwrite the one being preserved.
    return exec_write("INSERT INTO portfolio_assets "
                      "(portfolio_id, symbol, quantity, avg_buy_price, first_purchase_date) "
                      "VALUES (?, ?, ?, ?, COALESCE(NULLIF(?, ''), datetime('now'))) "
                      "ON CONFLICT(portfolio_id, symbol) DO UPDATE SET "
                      "  quantity            = excluded.quantity, "
                      "  avg_buy_price       = excluded.avg_buy_price, "
                      "  first_purchase_date = COALESCE(NULLIF(?, ''), "
                      "                                 portfolio_assets.first_purchase_date), "
                      "  last_updated        = datetime('now')",
                      {portfolio_id, symbol.toUpper(), qty, avg_price, first_purchase_date, first_purchase_date});
}

Result<void> PortfolioRepository::remove_asset(const QString& portfolio_id, const QString& symbol) {
    return exec_write("DELETE FROM portfolio_assets WHERE portfolio_id = ? AND symbol = ?",
                      {portfolio_id, symbol.toUpper()});
}

// ── Transactions ─────────────────────────────────────────────────────────────

Result<QVector<portfolio::Transaction>> PortfolioRepository::get_transactions(const QString& portfolio_id, int limit) {
    // limit <= 0 reads the whole log. Rows are DESC-ordered, so a finite
    // limit drops the OLDEST rows — for a full-log consumer that means the
    // opening BUYs, which is how a 10k-row export could silently lose the
    // very transactions position reconstruction depends on.
    if (limit <= 0) {
        return query_list_as<portfolio::Transaction>(
            "SELECT id, portfolio_id, symbol, transaction_type, quantity, price, total_value, "
            "transaction_date, notes, created_at "
            "FROM portfolio_transactions WHERE portfolio_id = ? "
            "ORDER BY transaction_date DESC, created_at DESC",
            {portfolio_id}, map_transaction);
    }
    return query_list_as<portfolio::Transaction>(
        "SELECT id, portfolio_id, symbol, transaction_type, quantity, price, total_value, "
        "transaction_date, notes, created_at "
        "FROM portfolio_transactions WHERE portfolio_id = ? "
        "ORDER BY transaction_date DESC, created_at DESC LIMIT ?",
        {portfolio_id, limit}, map_transaction);
}

Result<QVector<portfolio::Transaction>> PortfolioRepository::get_symbol_transactions(const QString& portfolio_id,
                                                                                     const QString& symbol) {
    return query_list_as<portfolio::Transaction>(
        "SELECT id, portfolio_id, symbol, transaction_type, quantity, price, total_value, "
        "transaction_date, notes, created_at "
        "FROM portfolio_transactions WHERE portfolio_id = ? AND symbol = ? "
        "ORDER BY transaction_date DESC",
        {portfolio_id, symbol.toUpper()}, map_transaction);
}

Result<QString> PortfolioRepository::add_transaction(const QString& portfolio_id, const QString& symbol,
                                                     const QString& type, double qty, double price, const QString& date,
                                                     const QString& notes) {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // created_at is the ledger's tie-break for same-dated transactions, and
    // the column default (datetime('now'), 1-second resolution) makes two
    // rows recorded in one import loop indistinguishable — the final UUID
    // tie-break then replays a same-day BUY→SELL pair in random order, which
    // clamps the sell against zero held about half the time. Milliseconds
    // plus a process-monotonic sequence make "the order they were recorded"
    // real.
    static std::atomic<quint64> seq{0};
    const QString created = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")) +
                            QStringLiteral("#%1").arg(seq.fetch_add(1), 9, 10, QLatin1Char('0'));
    auto r = exec_write("INSERT INTO portfolio_transactions "
                        "(id, portfolio_id, symbol, transaction_type, quantity, price, total_value, "
                        " transaction_date, notes, created_at) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        {id, portfolio_id, symbol.toUpper(), type, qty, price, qty * price, date, notes, created});
    if (r.is_err())
        return Result<QString>::err(r.error());
    return Result<QString>::ok(id);
}

Result<void> PortfolioRepository::update_transaction(const QString& id, double qty, double price, const QString& date,
                                                     const QString& notes) {
    return exec_write("UPDATE portfolio_transactions SET quantity = ?, price = ?, total_value = ?, "
                      "transaction_date = ?, notes = ? WHERE id = ?",
                      {qty, price, qty * price, date, notes, id});
}

Result<void> PortfolioRepository::delete_transaction(const QString& id) {
    return exec_write("DELETE FROM portfolio_transactions WHERE id = ?", {id});
}

Result<portfolio::Transaction> PortfolioRepository::get_transaction(const QString& id) {
    auto rows = query_list_as<portfolio::Transaction>(
        "SELECT id, portfolio_id, symbol, transaction_type, quantity, price, total_value, "
        "transaction_date, notes, created_at "
        "FROM portfolio_transactions WHERE id = ?",
        {id}, map_transaction);
    if (rows.is_err())
        return Result<portfolio::Transaction>::err(rows.error());
    if (rows.value().isEmpty())
        return Result<portfolio::Transaction>::err("Transaction not found");
    return Result<portfolio::Transaction>::ok(rows.value().first());
}

// ── Snapshots ────────────────────────────────────────────────────────────────

Result<void> PortfolioRepository::save_snapshot(const QString& portfolio_id, double value, double cost_basis,
                                                double pnl, double pnl_pct, const QString& date) {
    // The live path: a valuation computed from quotes the app actually saw.
    // It may overwrite anything — a same-day live row (self-correction across
    // the day) or a synthetic backfill estimate it supersedes.
    return exec_write("INSERT OR REPLACE INTO portfolio_snapshots "
                      "(portfolio_id, total_value, total_cost_basis, total_pnl, total_pnl_percent, "
                      " snapshot_date, source) "
                      "VALUES (?, ?, ?, ?, ?, ?, 'live')",
                      {portfolio_id, value, cost_basis, pnl, pnl_pct, date});
}

Result<int> PortfolioRepository::save_backfill_snapshot(const QString& portfolio_id, double value, double cost_basis,
                                                        double pnl, double pnl_pct, const QString& date) {
    // The estimate path: a valuation reconstructed from history rather than
    // observed live. It fills dates that have no snapshot and may correct its
    // own earlier estimates (the user added holdings, a longer period was
    // requested), but a 'live' row is a real observation and must survive
    // every backfill. Returns the number of rows actually written (0 when the
    // guard suppressed the upsert) — callers must not count a suppressed
    // write as a backfilled point.
    auto r = db().execute("INSERT INTO portfolio_snapshots "
                          "(portfolio_id, total_value, total_cost_basis, total_pnl, total_pnl_percent, "
                          " snapshot_date, source) "
                          "VALUES (?, ?, ?, ?, ?, ?, 'backfill') "
                          "ON CONFLICT(portfolio_id, snapshot_date) DO UPDATE SET "
                          "  total_value       = excluded.total_value, "
                          "  total_cost_basis  = excluded.total_cost_basis, "
                          "  total_pnl         = excluded.total_pnl, "
                          "  total_pnl_percent = excluded.total_pnl_percent "
                          "WHERE portfolio_snapshots.source <> 'live'",
                          {portfolio_id, value, cost_basis, pnl, pnl_pct, date});
    if (r.is_err())
        return Result<int>::err(r.error());
    return Result<int>::ok(std::max(0, r.value().numRowsAffected()));
}

Result<QVector<portfolio::PortfolioSnapshot>> PortfolioRepository::get_snapshots(const QString& portfolio_id,
                                                                                 int days) {
    return query_list_as<portfolio::PortfolioSnapshot>(
        "SELECT id, portfolio_id, total_value, total_cost_basis, total_pnl, "
        "total_pnl_percent, snapshot_date, source "
        "FROM portfolio_snapshots WHERE portfolio_id = ? "
        "AND snapshot_date >= date('now', '-' || ? || ' days') "
        "ORDER BY snapshot_date ASC",
        {portfolio_id, days}, map_snapshot);
}

} // namespace fincept

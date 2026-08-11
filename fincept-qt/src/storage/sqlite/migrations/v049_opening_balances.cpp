// v049_opening_balances — every position gets a transaction history.
//
// The ledger (PortfolioLedger) derives positions by replaying the transaction
// log, but years of write paths created asset rows with no backing
// transactions: the old MCP add_holding/add_asset wrote the row directly, and
// update_holding still does. For such a row the log and the row disagree
// about what "no transactions" means, and the replay's two readings are both
// wrong: treat the log as truth and the first recorded DIVIDEND deletes a
// 100-share holding (replay of [DIVIDEND] has quantity 0); treat the row as
// truth and a deliberately emptied log can never remove it.
//
// This migration removes the ambiguity: any held position without a single
// transaction receives a synthesized opening BUY at its stored quantity and
// average cost, dated at its first purchase. From here on an empty log has
// exactly one meaning — the position does not exist.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v049(QSqlDatabase& db) {
    QSqlQuery q(db);
    if (!q.exec("INSERT INTO portfolio_transactions "
                "(id, portfolio_id, symbol, transaction_type, quantity, price, total_value, "
                " transaction_date, notes) "
                "SELECT lower(hex(randomblob(16))), a.portfolio_id, upper(a.symbol), 'BUY', "
                "       a.quantity, a.avg_buy_price, a.quantity * a.avg_buy_price, "
                "       COALESCE(NULLIF(a.first_purchase_date, ''), datetime('now')), "
                "       'Opening balance — synthesized from the holdings row (v049)' "
                "FROM portfolio_assets a "
                "WHERE a.quantity > 0 "
                "  AND NOT EXISTS (SELECT 1 FROM portfolio_transactions t "
                "                  WHERE t.portfolio_id = a.portfolio_id "
                "                    AND upper(t.symbol) = upper(a.symbol))"))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v049() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({49, "opening_balances", apply_v049});
}

} // namespace fincept

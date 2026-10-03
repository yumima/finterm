#pragma once
// Fetches everything Ownership shows: the per-stock register and the
// market-wide scans.
//
// Per stock, four independent sources, issued together and rendered as each
// lands, so the fast halves are on screen while EDGAR is still parsing:
//
//   sec_ownership_data.py    Form 4 insider transactions and SC 13D/G stakes,
//                            parsed from EDGAR. Free, no key, slow: each Form 4
//                            is its own XML document at SEC's rate limit.
//   sec_13f_bulk.py          the 13F holders from the local index. ~20ms.
//   yfinance (worker)        share count, float, the vendor's percentages.
//   finra_short_interest.py  the settled short position, twice a month, a
//                            year deep.
//
// Market-wide, three scans over the same local stores plus a daily Form 4
// ingest that this service schedules itself — the scan used to be a button,
// and a screen that is empty until a button is pressed is a screen nobody
// opens twice.

#include "screens/ownership/OwnershipTypes.h"

#include <QHash>
#include <QObject>
#include <optional>
#include <QSet>
#include <QString>

class QTimer;

namespace fincept::services {

class OwnershipService : public QObject {
    Q_OBJECT
  public:
    static OwnershipService& instance();

    // ── Per stock ───────────────────────────────────────────────────────────

    /// Load the register for @p symbol. Emits snapshot_updated once per source
    /// as each returns. Serves a cached snapshot when one is fresh enough —
    /// the underlying data is the slowest-moving on any screen.
    void load(const QString& symbol);
    /// Discard the cached snapshot for @p symbol and fetch again.
    void refresh(const QString& symbol);
    ownership::OwnershipSnapshot snapshot(const QString& symbol) const;
    /// True while any source for @p symbol is still outstanding.
    bool is_loading(const QString& symbol) const;

    /// Fetch the 13F holders for a symbol whose snapshot lacks them — the
    /// case where the register loaded while the index probe was still in
    /// flight. Cheap and idempotent; nothing else is re-fetched.
    void ensure_holders(const QString& symbol);

    enum class HolderSort { BySize, ByWeight };
    /// Re-rank the holder rows. Position size is the default (Bloomberg HDS);
    /// weight in the filer's own book is the second view, and it carries a
    /// book-size floor the script applies.
    void set_holder_sort(const QString& symbol, HolderSort sort);
    HolderSort holder_sort(const QString& symbol) const;

    // ── Market-wide scans ───────────────────────────────────────────────────

    void load_insider_buys(const ownership::InsiderBuyQuery& q);
    const ownership::InsiderBuys& insider_buys() const { return insider_buys_; }
    bool insider_buys_loading() const { return insider_buys_loading_; }

    void load_short_rank(const QString& sort = QStringLiteral("sirio"));
    const ownership::ShortRank& short_rank() const { return short_rank_; }
    bool short_rank_loading() const { return short_rank_loading_; }

    void load_movers(const QString& sort = QStringLiteral("breadth_up"));
    const ownership::Movers& movers() const { return movers_; }
    bool movers_loading() const { return movers_loading_; }

    /// Keep the Form 4 store current: read any unread business day in the
    /// trailing window, a few days per pass, and check again periodically.
    /// Idempotent; safe to call on every screen show.
    void ensure_form4_current();
    bool form4_scanning() const { return form4_scanning_; }
    QString form4_status() const { return form4_status_; }

    // ── Where ownership reaches the reader ──────────────────────────────────

    /// One line per symbol for a watchlist, from the local stores. Cheap.
    void load_watch(const QStringList& symbols);
    const ownership::WatchRows& watch_rows() const { return watch_; }

    /// 13F deadlines, FINRA publication dates, IPO lock-up expiries.
    void load_calendar(int days = 90);
    const ownership::OwnershipCalendar& calendar() const { return calendar_; }

    /// Insider buys on the reader's own holdings and watchlists since the
    /// last check. Runs after every Form 4 scan pass; posts a toast and a
    /// notification per new issuer, once. Idempotent across restarts through
    /// a small stamp file.
    void check_holdings_alerts();

    // ── Filers ──────────────────────────────────────────────────────────────

    /// The largest filers by 13F book, with each one's quarter-over-quarter
    /// moves. Cached for the session once loaded; `force` reloads.
    void load_top_firms(bool force = false);
    const ownership::TopFirms& top_firms() const { return top_firms_; }
    bool top_firms_loading() const { return top_firms_loading_; }
    /// Pull the newest 13F filings straight from EDGAR for the `top` largest
    /// filers — the bulk sets run a quarter behind. Reloads the ranking after.
    void pull_latest_filings(int top = 100);
    bool pulling_latest() const { return pulling_latest_; }

    void search_firms(const QString& query);
    QVector<ownership::Manager> last_firm_results() const { return firm_results_; }
    /// Load a filer's book. `quarter` (ISO date) pins a specific filing —
    /// the LARGEST FUNDS ranking passes the quarter its row describes, which
    /// can be an EDGAR-pulled quarter the default (newest complete bulk
    /// quarter) would skip. Books are cached and signalled under book_key().
    void load_book(const QString& cik, const QString& quarter = {});
    static QString book_key(const QString& cik, const QString& quarter) {
        return quarter.isEmpty() ? cik : cik + QLatin1Char('@') + quarter;
    }
    ownership::ManagerBook book(const QString& key) const;
    bool is_book_loading(const QString& key) const;

    // ── Local 13F index ─────────────────────────────────────────────────────

    bool index_ready() const;
    QString index_status_text() const;
    void build_index();
    void resolve_symbols(int limit = 2000);
    bool index_busy() const { return index_busy_; }
    void check_for_newer_quarter();

  signals:
    void snapshot_updated(QString symbol);
    void load_finished(QString symbol);
    void insider_buys_updated();
    void short_rank_updated();
    void movers_updated();
    void form4_status_changed(QString status);
    void watch_updated();
    void calendar_updated();
    void book_updated(QString cik);
    void firms_found();
    void top_firms_updated();
    /// Progress / result of pull_latest_filings, for a status line.
    void latest_pull_status(QString text);
    void index_changed(QString summary);

  private:
    OwnershipService();

    void fetch_edgar(const QString& symbol);
    void fetch_market(const QString& symbol);
    void fetch_holders(const QString& symbol, HolderSort sort);
    void fetch_short_history(const QString& symbol);
    void fetch_short_volume(const QString& symbol);
    /// Forward returns on the Form 4 rows, once EDGAR has landed.
    void price_transactions(const QString& symbol);
    /// Forward returns on the insider-buys scan rows.
    void price_insider_buys();
    void note_source_done(const QString& symbol, const QString& source);
    void run_form4_scan();
    void probe_index();
    void price_book(const QString& cik);

    QHash<QString, ownership::OwnershipSnapshot> cache_;
    QHash<QString, qint64> fetched_at_;
    QHash<QString, QSet<QString>> pending_;
    QHash<QString, HolderSort> holder_sort_;

    ownership::InsiderBuys insider_buys_;
    bool insider_buys_loading_ = false;
    /// A query that arrived while one was running; issued when it lands, so
    /// the table always ends up matching the controls.
    std::optional<ownership::InsiderBuyQuery> queued_insider_query_;
    ownership::ShortRank short_rank_;
    bool short_rank_loading_ = false;
    ownership::Movers movers_;
    bool movers_loading_ = false;
    ownership::TopFirms top_firms_;
    bool top_firms_loading_ = false;
    bool pulling_latest_ = false;

    ownership::WatchRows watch_;
    bool watch_loading_ = false;
    QStringList queued_watch_;
    ownership::OwnershipCalendar calendar_;
    bool calendar_loading_ = false;
    bool alerts_checking_ = false;
    /// Holdings and watchlist symbols, gathered on the GUI thread.
    QStringList own_symbols() const;

    bool    form4_scanning_ = false;
    QString form4_status_;
    QTimer* form4_timer_ = nullptr;
    int     form4_passes_ = 0;

    QVector<ownership::Manager> firm_results_;
    QHash<QString, ownership::ManagerBook> books_;
    QSet<QString> books_in_flight_;
    QSet<QString> pricing_in_flight_;

    bool    index_busy_ = false;
    mutable bool index_probed_ = false;
    mutable bool index_probing_ = false;
    mutable QString index_status_;
};

} // namespace fincept::services

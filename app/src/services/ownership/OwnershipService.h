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
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

namespace finterm::services {

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

    /// The register for @p symbol answered to ONE caller once its EDGAR half
    /// (Form 4 transactions, insider profiles, 13D/G stakes) has landed —
    /// through load(), so the cache and the in-flight fetch are the screen's.
    /// Always answers after the call returns; edgar_ok / edgar_error say
    /// whether EDGAR answered (a refusal is never an empty register).
    void read_edgar(const QString& symbol, std::function<void(const ownership::OwnershipSnapshot&)> cb);

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

    // ── Reads for the alert checks ──────────────────────────────────────────
    // Local stores only: no network, no recomputation (the scans and the 13F
    // sync keep them current). `done` is always called once, later — never
    // inside the call. Identical reads in flight share one script run. A
    // failed or refused read carries the script's error text in `error`; its
    // rows say nothing, least of all "none".

    /// Scorable open-market buys (Form 4 store) on @p symbols filed in the
    /// last @p days, with the store's reach.
    void query_cluster_buys(const QStringList& symbols, int days, std::function<void(ownership::ClusterBuys)> done);
    /// 13D / 13G filings on @p symbols in the last @p days (stakes store).
    void query_stakes(const QStringList& symbols, int days, std::function<void(ownership::Stakes)> done);
    /// The stored big 13F moves in @p symbols filed in the last @p filed_within
    /// days, active books only — what the sync last worked out.
    void query_big_moves(const QStringList& symbols, int filed_within, std::function<void(ownership::BigMoves)> done);
    /// Each fund's newest 13F (from stored corrections only).
    void query_fund_filings(const QStringList& ciks, std::function<void(ownership::FundFilings)> done);

    /// Holdings and watchlist symbols (upper-case, deduplicated), read from
    /// the signed-in user's store on the GUI thread.
    QStringList own_symbols() const;

    // ── Filers ──────────────────────────────────────────────────────────────

    /// The largest filers by 13F book, with each one's quarter-over-quarter
    /// moves. Cached for the session once loaded; `force` reloads.
    void load_top_firms(bool force = false);

    /// Funds the user added to the Largest Funds list, beyond its top 20 —
    /// kept in settings, in the order added. CIKs as the index keys them.
    struct MyFund {
        QString cik;
        QString name;
    };
    const QVector<MyFund>& my_funds() const { return my_funds_; }
    bool is_my_fund(const QString& cik) const;
    void set_my_fund(const QString& cik, const QString& name, bool on);
    /// The largest single stake changes across every filer. The first call on
    /// a new index state computes them (a few minutes), later ones are local.
    void load_big_moves(const QString& side, const QString& sort, bool active_only, int filed_within = 0);
    /// 13D / 13G filings of the last `days` days from the local stakes store.
    void load_stakes(int days, const QString& kind, bool new_only);
    const ownership::Stakes& stakes() const { return stakes_; }
    bool stakes_loading() const { return stakes_loading_; }
    const ownership::BigMoves& big_moves() const { return big_moves_; }
    bool big_moves_loading() const { return big_moves_loading_; }
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
    /// Keep the 13F index current without being asked: new SEC data sets and
    /// every 13F filed on EDGAR since, a few minutes after launch and every
    /// six hours after (sooner while a capped pass has more to read).
    void start_background_sync();
    QString sync_status() const { return sync_status_; }

  signals:
    void snapshot_updated(QString symbol);
    void load_finished(QString symbol);
    void insider_buys_updated();
    void short_rank_updated();
    void movers_updated();
    void form4_status_changed(QString status);
    /// A Form 4 scan cycle finished and read new filings into the store.
    void form4_store_updated();
    void watch_updated();
    void calendar_updated();
    void book_updated(QString cik);
    void firms_found();
    void top_firms_updated();
    void my_funds_changed();
    void big_moves_updated();
    void stakes_updated();
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

    /// One script read behind the query_* calls: deduplicated while in flight,
    /// answered on a later event-loop turn with the parsed object, or the
    /// error text when the run failed, answered nothing, refused, or did not
    /// answer by its deadline (a lost callback must not wedge later reads).
    using QueryDone = std::function<void(const QJsonObject& answer, const QString& error)>;
    void run_query(const QString& script, const QString& action, const QJsonObject& payload, int timeout_ms,
                   QueryDone done);
    /// Settle the in-flight read @p key if it is still run @p id: every waiter
    /// is answered once, later; a stale run (superseded by a deadline) is ignored.
    void settle_query(const QString& key, quint64 id, const QJsonObject& answer, const QString& error);
    struct InFlightQuery {
        quint64 id = 0;
        QVector<QueryDone> waiters;
    };
    QHash<QString, InFlightQuery> queries_in_flight_;
    quint64 next_query_id_ = 0;

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
    ownership::Stakes stakes_;
    bool stakes_loading_ = false;
    QString stakes_pending_; ///< a request made while one ran: days|kind|new_only
    QTimer* stakes_timer_ = nullptr;
    bool stakes_scanning_ = false;
    void run_stakes_scan();
    ownership::BigMoves big_moves_;
    bool big_moves_loading_ = false;
    QString big_moves_pending_; ///< a request made while one ran: side|sort|active
    ownership::TopFirms top_firms_;
    bool top_firms_loading_ = false;
    bool pulling_latest_ = false;

    ownership::WatchRows watch_;
    bool watch_loading_ = false;
    QStringList queued_watch_;
    ownership::OwnershipCalendar calendar_;
    bool calendar_loading_ = false;
    bool alerts_checking_ = false;

    bool form4_scanning_ = false;
    QString form4_status_;
    QTimer* form4_timer_ = nullptr;
    QTimer* sync_timer_ = nullptr;
    bool syncing_ = false;
    /// A screen found nothing stored and asked for a pass. Asked once: the
    /// sync's own timers (refused, failed, more) pace everything after that.
    bool pending_pass_asked_ = false;
    QVector<MyFund> my_funds_;
    void load_my_funds();
    QJsonArray my_fund_ciks() const;
    void ask_for_pending_pass();
    QString sync_status_;
    void run_13f_sync();
    int form4_passes_ = 0;

    QVector<ownership::Manager> firm_results_;
    QHash<QString, ownership::ManagerBook> books_;
    QSet<QString> books_in_flight_;
    QSet<QString> pricing_in_flight_;

    bool index_busy_ = false;
    mutable bool index_probed_ = false;
    mutable bool index_probing_ = false;
    mutable QString index_status_;
};

} // namespace finterm::services

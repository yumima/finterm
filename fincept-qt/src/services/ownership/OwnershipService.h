#pragma once
// Fetches everything the OWNERSHIP screen shows, for one symbol at a time.
//
// Two independent sources, deliberately not merged into one call:
//
//   sec_ownership_data.py  — Form 4 insider transactions and SC 13D/G stakes,
//                            parsed from EDGAR. Free, no key, but slow: each
//                            Form 4 is its own XML document and SEC asks for
//                            <=10 req/s, so a busy issuer takes tens of
//                            seconds.
//   yfinance_data.py       — institutional holders and short interest. Fast.
//
// They are issued together and rendered as each returns, so the fast half is
// on screen while the slow half is still parsing. Either can fail without
// taking the other down; the snapshot records which one did.

#include "screens/ownership/OwnershipTypes.h"

#include <QHash>
#include <QSet>
#include <QObject>
#include <QString>

namespace fincept::services {

class OwnershipService : public QObject {
    Q_OBJECT
  public:
    static OwnershipService& instance();

    /// Load ownership data for @p symbol. Emits snapshot_updated once per
    /// source as each returns, so the UI fills in progressively.
    ///
    /// Serves a cached snapshot immediately when one is fresh enough — the
    /// underlying data is the slowest-moving on any screen (Form 4s land in
    /// two business days, 13F quarterly, short interest twice monthly), so a
    /// re-visit within the TTL has nothing new to show and would only pay the
    /// EDGAR round-trips again.
    void load(const QString& symbol);

    /// Discard the cached snapshot for @p symbol and fetch again.
    void refresh(const QString& symbol);

    /// The current snapshot for @p symbol, or a default-constructed one.
    ownership::OwnershipSnapshot snapshot(const QString& symbol) const;

    /// True while any source for @p symbol is still outstanding.
    bool is_loading(const QString& symbol) const;

    // ── Firms ───────────────────────────────────────────────────────────────
    //
    // No curated list. It existed only to work around not having the universe;
    // with every filer indexed, "which firms do I track" is answered by
    // searching 10,647 of them instead of maintaining twenty.

    /// Search indexed filers by name. Results arrive on firms_found.
    void search_firms(const QString& query);
    QVector<ownership::Manager> last_firm_results() const { return firm_results_; }

    void load_smart_money(const QString& symbol);

    /// Load the two-axis demand read: every discretionary holder placed on
    /// conviction against direction. Local index query, so it runs on a symbol
    /// change like the holder list.
    void load_demand(const QString& symbol);

    /// Daily short-sale volume from FINRA. The only flow series here that is
    /// not quarterly, so it is fetched alongside the register rather than
    /// behind the index.
    void load_short_volume(const QString& symbol);

    // ── Local 13F index ─────────────────────────────────────────────────────
    //
    // SEC publishes every 13F as a bulk quarterly data set: 10,647 filers and
    // 3.3m positions for one quarter. Ingested into SQLite once, a holder
    // lookup is a local query in ~20ms with no network, which is what makes it
    // affordable to load on a symbol change rather than behind a button. It is
    // also what removes the curated list: Bloomberg's HDS does not curate
    // because it has the whole universe, and so does this.

    /// True when at least one quarter has been ingested locally.
    bool index_ready() const;
    /// Human-readable state of the local index (quarter, filers, rows).
    QString index_status_text() const;
    /// Download and index the newest quarterly data set. Emits index_changed.
    void build_index();
    /// Resolve CUSIP -> ticker for the largest @p limit unmapped securities.
    void resolve_symbols(int limit = 2000);
    bool index_busy() const { return index_busy_; }
    /// Ask SEC whether a data set exists that this index has not ingested.
    /// Without this the index silently ages: it still answers, and nothing
    /// says the answers are a quarter behind.
    void check_for_newer_quarter();

    /// Pull the current quarter straight from EDGAR for the @p top largest
    /// filers. SEC's bulk data sets publish only after their filing window
    /// closes, so they run a full quarter behind — Q2 filings were on EDGAR the
    /// day they were due while the newest bulk set still covered Q1. This
    /// closes that gap for the filers whose weights the screen is about.
    void pull_current_quarter(int top = 400);

    // ── BY FIRM: one manager's whole book ───────────────────────────────────

    /// Fetch @p cik's disclosed equity book and its quarter-over-quarter moves.
    /// Emits book_updated when it lands.
    void load_book(const QString& cik);

    /// The cached book for @p cik, or a default-constructed one.
    ownership::ManagerBook book(const QString& cik) const;

    bool is_book_loading(const QString& cik) const;

  signals:
    /// A source returned and the snapshot changed. Carries the symbol so a
    /// late reply for a symbol the user has navigated away from can be
    /// ignored by the view.
    void snapshot_updated(QString symbol);
    /// Both halves have settled, successfully or not.
    void load_finished(QString symbol);
    /// A manager's book finished loading (or failed — check ManagerBook::error).
    void book_updated(QString cik);
    /// The tracked-manager list changed (seeded or edited).
    void firms_found();
    /// The local 13F index changed state (ingest or symbol resolution).
    void index_changed(QString summary);

  private:
    OwnershipService() = default;

    void load_index_holders(const QString& symbol);
    void probe_index();

    void fetch_edgar(const QString& symbol);
    void fetch_market(const QString& symbol);
    void note_source_done(const QString& symbol, const QString& source);

    QVector<ownership::Manager> firm_results_;

    QHash<QString, ownership::OwnershipSnapshot> cache_;
    QHash<QString, qint64> fetched_at_;   ///< ms since epoch, per symbol
    /// Outstanding sources per symbol, BY NAME rather than as a count.
    ///
    /// A bare counter conflated three independent fetches. Smart money is
    /// opt-in and takes minutes; while it ran, the counter was non-zero, and
    /// load()'s "already in flight" guard then refused to fetch the register at
    /// all — so clicking LOAD 13F POSITIONS before the page had loaded left the
    /// rest of the screen permanently empty. Naming the sources lets each guard
    /// on its own.
    QHash<QString, QSet<QString>> pending_;

    QHash<QString, ownership::ManagerBook> books_;
    QSet<QString> books_in_flight_;
    bool    index_busy_ = false;
    /// The on-disk index has been read successfully at least once.
    mutable bool index_probed_ = false;
    /// A probe is in flight — stops every render() from queueing another.
    mutable bool index_probing_ = false;
    mutable QString index_status_;
};

} // namespace fincept::services

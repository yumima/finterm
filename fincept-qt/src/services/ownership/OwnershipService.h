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

    // ── Tracked 13F managers ────────────────────────────────────────────────
    //
    // Curated rather than "top N by 13F AUM". That ranking fills up with
    // multi-strategy and index books where a position weight is one leg of a
    // hedge or a benchmark artifact, and presenting it as conviction would be
    // the screen inventing confidence the filing cannot support.

    /// The tracked managers. Reads the user's list from the app data directory
    /// when present, otherwise the curated defaults shipped with the script.
    QVector<ownership::Manager> managers() const;

    /// Overwrite the user's manager list and persist it. An empty list deletes
    /// the override so the shipped defaults apply again.
    bool set_managers(const QVector<ownership::Manager>& list);

    /// Path of the editable list, so the UI can point the user at it.
    static QString managers_file_path();

    /// Populate the tracked-manager list from the curated defaults shipped with
    /// the script, resolving each CIK from EDGAR, and persist the result.
    ///
    /// Needed because the defaults live in the script (one source of truth for
    /// the list and its styles) but the CIKs are deliberately not hardcoded
    /// there — a wrong CIK does not fail loudly, it quietly shows a different
    /// firm's portfolio under the right name. Without this the list stays empty
    /// until some other fetch happens to resolve it. Emits managers_changed.
    void seed_default_managers();

    /// Fetch which tracked managers hold @p symbol, at what weight in their own
    /// book. Slow — every manager is several EDGAR round-trips — so it is a
    /// separate call from load(), and the rest of the screen renders without
    /// waiting on it.
    void load_smart_money(const QString& symbol);

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
    void managers_changed();

  private:
    OwnershipService() = default;

    void fetch_edgar(const QString& symbol);
    void fetch_market(const QString& symbol);
    void note_source_done(const QString& symbol, const QString& source);

    mutable QVector<ownership::Manager> managers_cache_;

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
};

} // namespace fincept::services

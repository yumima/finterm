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

    /// Fetch which tracked managers hold @p symbol, at what weight in their own
    /// book. Slow — every manager is several EDGAR round-trips — so it is a
    /// separate call from load(), and the rest of the screen renders without
    /// waiting on it.
    void load_smart_money(const QString& symbol);

  signals:
    /// A source returned and the snapshot changed. Carries the symbol so a
    /// late reply for a symbol the user has navigated away from can be
    /// ignored by the view.
    void snapshot_updated(QString symbol);
    /// Both halves have settled, successfully or not.
    void load_finished(QString symbol);

  private:
    OwnershipService() = default;

    void fetch_edgar(const QString& symbol);
    void fetch_market(const QString& symbol);
    void note_source_done(const QString& symbol);

    mutable QVector<ownership::Manager> managers_cache_;

    QHash<QString, ownership::OwnershipSnapshot> cache_;
    QHash<QString, qint64> fetched_at_;   ///< ms since epoch, per symbol
    QHash<QString, int>    pending_;      ///< outstanding sources, per symbol
};

} // namespace fincept::services

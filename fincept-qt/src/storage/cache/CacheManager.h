#pragma once
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

namespace fincept {

/// SQLite-backed cache. All reads and writes go directly to CacheDatabase (cache.db).
/// No in-memory layer — avoids double-storage and keeps a single source of truth.
class CacheManager : public QObject {
    Q_OBJECT
  public:
    static CacheManager& instance();

    /// `age_sec`: how old the value already is at write time. Shortens the
    /// remaining lifetime (expires_at) while keeping ttl_seconds intact, so
    /// the derived write time (expires_at − ttl_seconds, see try_get_aged)
    /// stays honest for re-puts of aged data — startup hydration being the
    /// case. Passing a pre-reduced TTL instead makes the entry claim it was
    /// written "now".
    void put(const QString& key, const QVariant& value, int ttl_seconds = 300, const QString& category = "general",
             int age_sec = 0);
    /// Returns the cached value (as QString-convertible QVariant) or a null QVariant on miss/expiry.
    QVariant get(const QString& key) const;
    /// Batched get(): one SELECT for many keys. Returned map only contains entries that
    /// existed and were unexpired; missing keys are omitted. Callers that previously
    /// looped get() per key should switch to this — N round-trips → 1 round-trip cuts
    /// GUI-thread latency for portfolio-sized fan-outs (50+ holdings).
    QHash<QString, QString> multi_get(const QStringList& keys) const;
    /// Single-query variant of get(): std::nullopt on miss, value on hit. Prefer this over has()+get()
    /// — those two-round-trips duplicate work since get() already checks expiry.
    std::optional<QString> try_get(const QString& key) const;
    /// try_get() plus the time the value was WRITTEN.
    ///
    /// Derived as expires_at − ttl_seconds, which tracks the most recent
    /// put; `created_at` deliberately survives re-puts and so answers a
    /// different question. Callers that resolve a request straight from
    /// this cache need the write time, or every cache hit — including a
    /// blob hydrated from disk at startup — presents itself as brand new.
    struct Aged {
        QString value;
        QDateTime written_at;
    };
    std::optional<Aged> try_get_aged(const QString& key) const;
    /// Batch/prefix forms of try_get_aged().
    ///
    /// Prefer these over the plain multi_get()/get_prefix() whenever the caller
    /// cares HOW OLD a value is. The alternative — having writers stamp a
    /// timestamp into the payload — puts the invariant in the writers' hands,
    /// and a writer that forgets produces a value that silently reads as
    /// "age unknown". That already happened once: two code paths wrote the
    /// same quote key, only one stamped it, and the un-stamped one disabled a
    /// staleness gate for every symbol it touched. `expires_at - ttl_seconds`
    /// is set by put() itself, so no writer can omit it.
    QHash<QString, Aged> multi_get_aged(const QStringList& keys) const;
    QHash<QString, Aged> get_prefix_aged(const QString& prefix) const;

    /// Derive an Aged from one `unified_cache` row. Shared by the three aged
    /// getters so they cannot drift on how write time is computed.
    static Aged aged_from_row(const QString& value, const QString& expires_at, int ttl_seconds);
    /// Return every unexpired key/value pair whose key begins with `prefix`.
    /// Uses the same sargable range-query trick as remove_prefix() (no LIKE
    /// full-table scan). Intended for cold-start hydration where a service
    /// wants to reload its previously-cached working set on launch.
    QHash<QString, QString> get_prefix(const QString& prefix) const;
    bool has(const QString& key) const;
    void remove(const QString& key);
    void remove_prefix(const QString& prefix);
    void clear();
    void clear_category(const QString& category);

    int entry_count() const;

  private:
    explicit CacheManager(QObject* parent = nullptr);
};

} // namespace fincept

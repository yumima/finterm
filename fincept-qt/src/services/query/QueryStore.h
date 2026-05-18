// src/services/query/QueryStore.h
#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVector>

#include <functional>

namespace fincept::services::query {

/// Generic data-subscription layer that sits between async fetchers and UI
/// consumers. Each "query" is identified by a string key (e.g.
/// "equity:quote:AAPL", "equity:candles:AAPL:5y"); each key carries an
/// independent cached value, in-flight flag, and subscriber list.
///
/// Why this exists — the alternative the codebase grew organically:
///   • Each service emits a broadcast Qt signal on every fetch result.
///   • Each consumer filters incoming signals manually (if sym!=current_ /
///     period!=current_ return).
///   • Each consumer tracks per-category loaded_ booleans to compose a
///     "loading" UI state across multiple signals.
///   • In-flight dedup is a separate global string set.
///   • Several reimplementations of "discard stale callback" patterns
///     (news article body_gen_ token; futures route-timeout; …).
///
/// QueryStore collapses that to: subscribe(key) → receive State tuples.
/// The store routes by key, so consumers can't receive state for a key they
/// didn't ask for (the "filter on receive" bug class disappears
/// structurally). Loading state is part of the State tuple, not a separate
/// boolean. In-flight dedup is intrinsic — the second subscriber to a key
/// piggybacks on the existing fetch. Subscription lifetime is tied to a
/// QObject owner via QObject::destroyed, so late callbacks for dead
/// consumers are silently dropped.
///
/// Type erasure: the cached value is a QVariant. Each service registers its
/// parsed payload types (QuoteData, StockInfo, …) via Q_DECLARE_METATYPE
/// and round-trips them through QVariant. Consumers extract via
/// state.data.value<T>(). This keeps the store header non-templated and
/// usable across services that share no type hierarchy.
class QueryStore : public QObject {
    Q_OBJECT
  public:
    static QueryStore& instance();

    /// Per-subscriber state delivered on every transition.
    struct State {
        /// The parsed value. Null when no data has arrived yet (fresh
        /// subscribe with cold cache) or after explicit invalidate().
        QVariant data;
        /// True while a fetcher is in flight for this key (whether kicked
        /// by this subscriber or another). Composable with `data`:
        ///   data null + loading      → "spinner, no fallback content"
        ///   data set  + loading      → "show stale data, refresh icon"
        ///   data null + !loading     → "permanently empty / never fetched"
        ///   data set  + !loading     → "fresh"
        bool loading = false;
        /// True when `data` came from a cached value past its TTL but inside
        /// the SWR window — caller may render with a 'stale' affordance.
        /// Round 2 always sets this false; Round 3 wires SWR semantics.
        bool is_stale = false;
        /// Non-empty after a failed fetch. The store does not auto-clear
        /// this on subsequent subscribes — caller decides when an error is
        /// recoverable (e.g. by calling invalidate(key)).
        QString error;
    };

    using Resolver = std::function<void(QVariant)>;
    using Rejecter = std::function<void(QString)>;
    /// Fetcher signature: do whatever async work is needed; eventually call
    /// either resolve(parsed) or reject(error_message). May call exactly
    /// once. Subsequent calls are ignored.
    using Fetcher = std::function<void(Resolver, Rejecter)>;
    using Callback = std::function<void(const State&)>;

    /// Subscribe `owner` to the query identified by `key`. The callback is
    /// invoked synchronously on subscribe (one transition) and then again
    /// on every subsequent state change for this key. When `owner` is
    /// destroyed, the subscription is removed automatically — late
    /// callbacks for dead owners are silently dropped.
    ///
    /// If no fetch is in flight and no cached value is available, `fetcher`
    /// is invoked (exactly once) to start the fetch. If a fetch is already
    /// in flight for this key, the new subscriber just waits on it.
    /// If a cached value is available and fresh against `ttl_sec`, it is
    /// delivered immediately and `fetcher` is NOT called.
    ///
    /// Re-subscribing the same owner to the same key is a no-op (avoids
    /// duplicate deliveries). To resubscribe with a different key on the
    /// same owner (e.g. period switch), call subscribe() with the new key —
    /// the old subscription is dropped first.
    void subscribe(QObject* owner, const QString& key, int ttl_sec, int stale_max_sec,
                   Callback callback, Fetcher fetcher);

    /// Mark the cached value for `key` as discarded and kick a fresh fetch
    /// (using the most-recently-registered fetcher for this key). All
    /// subscribers receive a {loading: true} transition first, then the
    /// resolved state.
    void invalidate(const QString& key);

    /// Warm a key without binding a subscriber. The fetcher is invoked if
    /// nothing is cached and no fetch is in flight; otherwise no-op. Useful
    /// for hover-prefetch and adjacent-period speculation.
    void prefetch(const QString& key, int ttl_sec, Fetcher fetcher);

    /// Synchronous cache peek — returns the cached value if fresh, else a
    /// null QVariant. Does NOT trigger a fetch. Used by services that want
    /// to short-circuit a subscribe (e.g. early-return on cache hit before
    /// even building the fetcher closure).
    QVariant peek(const QString& key) const;

    /// Drop `owner`'s subscription to `key`. No-op if not subscribed.
    /// Critical for the period-switch and symbol-switch flows: without it,
    /// a late resolve for the previous key would still deliver to the
    /// owner's callback with stale data (the same filter-on-receive bug
    /// class QueryStore exists to eliminate). The owner is expected to call
    /// this when rebinding to a new key in the same logical slot.
    void unsubscribe(QObject* owner, const QString& key);

    /// Drop every subscription belonging to `owner`. Convenience for the
    /// "throwing away all view state" case (e.g. symbol switch where all
    /// three categories rebind to new keys at once).
    void unsubscribe_all(QObject* owner);

  private:
    explicit QueryStore(QObject* parent = nullptr);
    Q_DISABLE_COPY(QueryStore)

    struct Subscriber {
        QPointer<QObject> owner;  // dead QPointer ⇒ subscription is stale
        Callback callback;
    };

    struct Entry {
        QVariant cached_value;        // null if no data
        QDateTime fetched_at;          // when cached_value was last set
        int ttl_sec = 0;
        int stale_max_sec = 0;         // for Round 3 SWR; ignored in Round 2
        bool inflight = false;
        Fetcher pending_fetcher;       // last fetcher registered; reused on invalidate
        QVector<Subscriber> subscribers;
    };

    /// Deliver `state` to every live subscriber of `key`. Dead subscribers
    /// (owner destroyed) are pruned in the same pass.
    void deliver(const QString& key, const State& state);
    /// Trigger `fetcher`. On resolve, store the value and deliver; on
    /// reject, clear in-flight and deliver an error state. The fetcher
    /// callbacks must be safe to call from any thread — we marshal into
    /// the store's thread via Qt::QueuedConnection if needed.
    void kick_fetch(const QString& key, Fetcher fetcher);
    /// Remove all subscriptions for `owner` across every key. Called from
    /// QObject::destroyed.
    void prune_owner(QObject* owner);

    QHash<QString, Entry> entries_;
};

} // namespace fincept::services::query

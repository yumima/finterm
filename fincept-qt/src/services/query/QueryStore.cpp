// src/services/query/QueryStore.cpp
#include "services/query/QueryStore.h"

#include <algorithm>

#include <QMetaObject>
#include <QThread>

namespace fincept::services::query {

QueryStore& QueryStore::instance() {
    static QueryStore inst;
    return inst;
}

QueryStore::QueryStore(QObject* parent) : QObject(parent) {}

void QueryStore::subscribe(QObject* owner, const QString& key, int ttl_sec, int stale_max_sec,
                            Callback callback, Fetcher fetcher) {
    if (!owner || key.isEmpty() || !callback || !fetcher)
        return;

    const bool is_new_key = !entries_.contains(key);
    auto& entry = entries_[key];
    entry.ttl_sec = ttl_sec;
    entry.stale_max_sec = stale_max_sec;
    entry.pending_fetcher = fetcher;  // remembered for invalidate()-driven re-fetch
    entry.last_accessed = QDateTime::currentDateTime();
    if (is_new_key) evict_if_full();

    // Idempotent re-subscribe: if this owner is already subscribed to this
    // key, just update the callback. Avoids duplicate deliveries when a
    // consumer rebinds during the same logical view (e.g. force-refresh that
    // re-runs set_symbol() with the same args).
    for (auto& sub : entry.subscribers) {
        if (sub.owner == owner) {
            sub.callback = std::move(callback);
            // Replay current state to the rebound callback so it isn't
            // stuck waiting for the next transition.
            State current;
            current.data = entry.cached_value;
            current.loading = entry.inflight;
            // Provenance travels with the replay too — an owner rebinding to
            // the same key must not receive a populated value that claims to
            // have no fetch time.
            current.fetched_at = entry.fetched_at;
            current.is_stale = !entry.cached_value.isNull() && entry.ttl_sec > 0 &&
                               entry.fetched_at.isValid() &&
                               entry.fetched_at.secsTo(QDateTime::currentDateTime()) >= entry.ttl_sec;
            sub.callback(current);
            return;
        }
    }

    // First-time subscription from this owner — wire destruction cleanup so
    // dead subscribers don't accumulate forever. The lambda captures `this`
    // and key by value; on owner destruction we remove subscriptions for
    // that owner across every key (cheap because owners typically only
    // subscribe to a handful of keys).
    connect(owner, &QObject::destroyed, this, [this, owner_ptr = owner](QObject*) {
        prune_owner(owner_ptr);
    }, Qt::DirectConnection);

    entry.subscribers.append({QPointer<QObject>(owner), std::move(callback)});

    // ── Deliver current state immediately to the new subscriber ─────────────
    if (!entry.cached_value.isNull()) {
        // Three states for cached data:
        //   1. Fresh (age < ttl): deliver as-is, no refetch.
        //   2. Stale-but-acceptable (ttl ≤ age < stale_max): deliver with
        //      is_stale=true, kick a background revalidate. This is the SWR
        //      core — the consumer can render last-known data immediately
        //      (instant view) while a fresh fetch runs in the background.
        //   3. Beyond stale_max: discard, treat as cold miss.
        const qint64 age = entry.fetched_at.isValid()
                               ? entry.fetched_at.secsTo(QDateTime::currentDateTime())
                               : INT64_MAX;
        const bool fresh = (ttl_sec <= 0) || (age < ttl_sec);
        const bool acceptable_stale =
            !fresh && stale_max_sec > 0 && age < stale_max_sec;

        if (fresh) {
            State s;
            s.data = entry.cached_value;
            s.loading = entry.inflight;
            s.fetched_at = entry.fetched_at;
            entry.subscribers.last().callback(s);
        } else if (acceptable_stale) {
            // Deliver stale-but-served, then kick revalidate. The consumer
            // sees instant data + a refreshing indicator; when the revalidate
            // resolves, every subscriber (including this one) receives the
            // fresh state.
            State s;
            s.data = entry.cached_value;
            s.is_stale = true;
            s.loading = true;  // background revalidate is in flight
            s.fetched_at = entry.fetched_at;
            entry.subscribers.last().callback(s);
            if (!entry.inflight)
                kick_fetch(key, fetcher);
        } else {
            // Beyond SWR window — treat as cold. Discard cached value to
            // avoid delivering ancient data, then start a fresh fetch.
            entry.cached_value = QVariant();
            entry.fetched_at = QDateTime();
            State s;
            s.loading = true;
            entry.subscribers.last().callback(s);
            if (!entry.inflight)
                kick_fetch(key, fetcher);
        }
    } else if (entry.inflight) {
        // No data yet but a fetch is in flight — emit a loading-only state
        // and let the existing fetch deliver to everyone when it resolves.
        State s;
        s.loading = true;
        entry.subscribers.last().callback(s);
    } else {
        // Cold — kick the fetch and emit a loading state.
        State s;
        s.loading = true;
        entry.subscribers.last().callback(s);
        kick_fetch(key, fetcher);
    }
}

void QueryStore::invalidate(const QString& key) {
    auto it = entries_.find(key);
    if (it == entries_.end())
        return;
    // Drop the cached value so any new subscriber sees a loading state
    // rather than the stale value. If a fetch is already in flight, leave
    // it running — its resolve will populate the new value.
    it.value().cached_value = QVariant();
    it.value().fetched_at = QDateTime();
    if (!it.value().inflight && it.value().pending_fetcher) {
        kick_fetch(key, it.value().pending_fetcher);
    }
}

void QueryStore::revalidate(const QString& key, bool force) {
    auto it = entries_.find(key);
    if (it == entries_.end() || it.value().inflight || !it.value().pending_fetcher)
        return;
    // Respect the key's own TTL. A caller ticking every 20s must not refetch
    // 30-minute fundamentals 90 times an hour: each key refreshes on ITS
    // schedule, so one periodic tick can safely cover a panel whose parts
    // have wildly different refresh economics (quote 60s, candles 10min,
    // fundamentals 30min).
    const auto& e = it.value();
    if (!force && !e.cached_value.isNull() &&
        !revalidate_due(e.fetched_at, e.ttl_sec, QDateTime::currentDateTime())) {
        return;
    }
    // Failure backoff. The TTL gate above never engages for a key with no
    // cached value (a failed cold fetch) and a failure advances fetched_at
    // for nobody — so without this floor a periodic 20s tick retried the
    // full RPC three times a minute for as long as the outage lasted. One
    // attempt per kMinRevalidateSec; an explicit user refresh (force) still
    // goes straight through.
    if (!force && e.last_failed_at.isValid() &&
        e.last_failed_at.secsTo(QDateTime::currentDateTime()) < kMinRevalidateSec) {
        return;
    }
    // Deliberately does NOT clear cached_value: subscribers keep rendering
    // real numbers until the replacement arrives, and a failed refresh
    // leaves them with the last good value instead of a blank panel.
    kick_fetch(key, e.pending_fetcher);
}

bool QueryStore::revalidate_due(const QDateTime& fetched_at, int ttl_sec, const QDateTime& now) {
    if (!fetched_at.isValid())
        return true; // nothing has ever been fetched for this key
    // ttl_sec <= 0 means "never expires" everywhere else in this class
    // (subscribe, peek); it must mean the same here rather than "always due".
    if (ttl_sec <= 0)
        return false;
    return fetched_at.secsTo(now) >= std::max(ttl_sec, kMinRevalidateSec);
}

QDateTime QueryStore::oldest_fetched_at(QObject* owner) const {
    QDateTime oldest;
    if (!owner)
        return oldest;
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
        const Entry& e = it.value();
        if (e.cached_value.isNull() || !e.fetched_at.isValid())
            continue;
        for (const auto& sub : e.subscribers) {
            if (sub.owner == owner) {
                if (!oldest.isValid() || e.fetched_at < oldest)
                    oldest = e.fetched_at;
                break;
            }
        }
    }
    return oldest;
}

void QueryStore::revalidate_owner(QObject* owner) {
    if (!owner)
        return;
    QStringList keys;
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
        for (const auto& sub : it.value().subscribers) {
            if (sub.owner == owner) {
                keys.append(it.key());
                break;
            }
        }
    }
    // Collected first: kick_fetch can resolve synchronously and mutate
    // entries_, which would invalidate the iterator above.
    for (const QString& k : std::as_const(keys))
        revalidate(k);
}

void QueryStore::prefetch(const QString& key, int ttl_sec, Fetcher fetcher) {
    if (key.isEmpty() || !fetcher)
        return;
    const bool is_new_key = !entries_.contains(key);
    auto& entry = entries_[key];
    entry.ttl_sec = ttl_sec;
    entry.pending_fetcher = fetcher;
    entry.last_accessed = QDateTime::currentDateTime();
    if (is_new_key) evict_if_full();
    if (entry.inflight) return;
    // Fresh-cache short-circuit — don't burn a fetch when a recent value
    // exists.
    if (!entry.cached_value.isNull() && ttl_sec > 0 && entry.fetched_at.isValid() &&
        entry.fetched_at.secsTo(QDateTime::currentDateTime()) < ttl_sec) {
        return;
    }
    kick_fetch(key, std::move(fetcher));
}

void QueryStore::unsubscribe(QObject* owner, const QString& key) {
    if (!owner) return;
    auto it = entries_.find(key);
    if (it == entries_.end()) return;
    auto& subs = it.value().subscribers;
    for (int i = subs.size() - 1; i >= 0; --i) {
        if (subs[i].owner == owner)
            subs.removeAt(i);
    }
    // In-flight fetcher continues running — it might be useful for another
    // subscriber, or to populate the cache for a future subscribe. The
    // resolved value writes to cached_value; if nobody is subscribed, it
    // just sits there until the next subscribe(key) hits it.
}

void QueryStore::unsubscribe_all(QObject* owner) {
    if (!owner) return;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        auto& subs = it.value().subscribers;
        for (int i = subs.size() - 1; i >= 0; --i) {
            if (subs[i].owner == owner)
                subs.removeAt(i);
        }
    }
}

QVariant QueryStore::peek(const QString& key) const {
    auto it = entries_.constFind(key);
    if (it == entries_.constEnd())
        return {};
    const auto& e = it.value();
    if (e.cached_value.isNull())
        return {};
    if (e.ttl_sec > 0 && e.fetched_at.isValid() &&
        e.fetched_at.secsTo(QDateTime::currentDateTime()) >= e.ttl_sec) {
        return {};  // expired
    }
    // Cast-away const for the access timestamp — peek is logically
    // const but it touches the LRU watermark so a frequently-peeked
    // entry doesn't get evicted. The cached value itself is unchanged.
    const_cast<Entry&>(e).last_accessed = QDateTime::currentDateTime();
    return e.cached_value;
}

void QueryStore::kick_fetch(const QString& key, Fetcher fetcher) {
    auto& entry = entries_[key];
    if (entry.inflight) return;
    entry.inflight = true;

    // Capture key by value — entries_[key] could rehash under us if the
    // fetcher synchronously triggers another subscribe to a different key.
    Resolver resolver{[this, key](QVariant value, QDateTime fetched_at) {
        // Marshal to the store's thread to keep entries_ access serialized.
        // Qt::QueuedConnection ensures execution happens on the store's
        // event-loop thread regardless of which thread invokes resolver.
        QMetaObject::invokeMethod(this, [this, key, value, fetched_at]() {
            auto it = entries_.find(key);
            if (it == entries_.end()) return;
            it.value().inflight = false;
            it.value().cached_value = value;
            it.value().fetched_at = fetched_at;
            it.value().last_failed_at = QDateTime();  // success clears the backoff
            State s;
            s.data = value;
            s.loading = false;
            s.fetched_at = it.value().fetched_at;
            // Data older than its own TTL was served from a cache; say so.
            s.is_stale = it.value().ttl_sec > 0 &&
                         fetched_at.secsTo(QDateTime::currentDateTime()) >= it.value().ttl_sec;
            deliver(key, s);
        }, Qt::QueuedConnection);
    }};

    auto rejecter = [this, key](QString err) {
        QMetaObject::invokeMethod(this, [this, key, err]() {
            auto it = entries_.find(key);
            if (it == entries_.end()) return;
            it.value().inflight = false;
            it.value().last_failed_at = QDateTime::currentDateTime();
            // Preserve any prior cached_value — the failed fetch shouldn't
            // erase a previously good cache. Subscribers get an error
            // alongside the (possibly null) data they had.
            State s;
            s.data = it.value().cached_value;
            s.loading = false;
            s.error = err;
            // The surviving value is as old as it ever was — the failed
            // fetch must not make it look refreshed. is_stale means
            // "showing cached data past its TTL", so it only applies when
            // there IS data to show; a cold-start failure has none.
            s.fetched_at = it.value().fetched_at;
            s.is_stale = !it.value().cached_value.isNull();
            deliver(key, s);
        }, Qt::QueuedConnection);
    };

    // Run the fetcher immediately (its body decides sync vs async). It may
    // call resolver/rejecter from any thread; the invokeMethod above pulls
    // the result back to ours.
    fetcher(std::move(resolver), std::move(rejecter));
}

void QueryStore::deliver(const QString& key, const State& state) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return;
    it.value().last_accessed = QDateTime::currentDateTime();
    auto& subs = it.value().subscribers;
    // Walk in reverse for cheap erase-during-iteration on dead QPointers.
    for (int i = subs.size() - 1; i >= 0; --i) {
        if (!subs[i].owner) {
            subs.removeAt(i);
            continue;
        }
        // Copy callback before invoking — the callback may synchronously
        // re-subscribe / invalidate, which can resize subs and invalidate
        // our reference.
        auto cb = subs[i].callback;
        cb(state);
    }
}

void QueryStore::evict_if_full() {
    if (entries_.size() <= kMaxEntries) return;

    // Find the oldest entry (by last_accessed) that's safe to drop:
    //   • no live subscribers — otherwise we'd silently stop delivering
    //     to them.
    //   • not inflight — the resolver would still fire and write to a
    //     re-created entry, but the side-effect of evicting an inflight
    //     entry is a wasted subprocess RPC. Skip them.
    //
    // O(n) scan; n bounded by kMaxEntries. Overflow is rare enough that
    // the linear search beats maintaining a separate LRU list.
    auto candidate = entries_.end();
    QDateTime oldest;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        // Live-subscriber check tolerates already-pruned QPointers — we
        // count an entry as "in use" only if at least one QPointer is
        // still alive. Pre-prune dead ones in place so a later deliver
        // doesn't have to.
        auto& subs = it.value().subscribers;
        for (int i = subs.size() - 1; i >= 0; --i) {
            if (!subs[i].owner) subs.removeAt(i);
        }
        if (!subs.isEmpty()) continue;
        if (it.value().inflight) continue;
        if (!oldest.isValid() || it.value().last_accessed < oldest) {
            oldest = it.value().last_accessed;
            candidate = it;
        }
    }
    if (candidate != entries_.end()) {
        entries_.erase(candidate);
    }
    // If no candidate is found, the cache is at capacity but every entry
    // is actively in use. Don't force-evict — let the count drift up
    // until subscriptions drop. The next eviction attempt will retry.
}

void QueryStore::prune_owner(QObject* owner) {
    // QObject::destroyed has already fired by the time we get here — the
    // QPointer in each Subscriber is already null, but we still walk to
    // free the slot rather than leaving stale entries until the next
    // deliver() prunes them lazily. Owners that subscribed to many keys
    // benefit; one-off subscribers are unaffected either way.
    Q_UNUSED(owner);
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        auto& subs = it.value().subscribers;
        for (int i = subs.size() - 1; i >= 0; --i) {
            if (!subs[i].owner)
                subs.removeAt(i);
        }
    }
}

} // namespace fincept::services::query

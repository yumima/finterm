// src/services/query/QueryStore.cpp
#include "services/query/QueryStore.h"

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

    auto& entry = entries_[key];
    entry.ttl_sec = ttl_sec;
    entry.stale_max_sec = stale_max_sec;
    entry.pending_fetcher = fetcher;  // remembered for invalidate()-driven re-fetch

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
        // Cache hit — deliver the cached value first. Whether the cache is
        // stale (past TTL) is a Round-3 concern; Round 2 treats any cached
        // value as authoritative. The caller's callback should already be
        // robust to data + loading=true (concurrent refresh).
        State s;
        s.data = entry.cached_value;
        s.loading = entry.inflight;
        entry.subscribers.last().callback(s);
        // If no fetch is in flight and the cache is older than TTL, kick a
        // refresh so subscribers get fresh data soon. Round 2: simple TTL
        // check; Round 3 will fold this into the SWR path.
        if (!entry.inflight && ttl_sec > 0 && entry.fetched_at.isValid() &&
            entry.fetched_at.secsTo(QDateTime::currentDateTime()) >= ttl_sec) {
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

void QueryStore::prefetch(const QString& key, int ttl_sec, Fetcher fetcher) {
    if (key.isEmpty() || !fetcher)
        return;
    auto& entry = entries_[key];
    entry.ttl_sec = ttl_sec;
    entry.pending_fetcher = fetcher;
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
    return e.cached_value;
}

void QueryStore::kick_fetch(const QString& key, Fetcher fetcher) {
    auto& entry = entries_[key];
    if (entry.inflight) return;
    entry.inflight = true;

    // Capture key by value — entries_[key] could rehash under us if the
    // fetcher synchronously triggers another subscribe to a different key.
    auto resolver = [this, key](QVariant value) {
        // Marshal to the store's thread to keep entries_ access serialized.
        // Qt::QueuedConnection ensures execution happens on the store's
        // event-loop thread regardless of which thread invokes resolver.
        QMetaObject::invokeMethod(this, [this, key, value]() {
            auto it = entries_.find(key);
            if (it == entries_.end()) return;
            it.value().inflight = false;
            it.value().cached_value = value;
            it.value().fetched_at = QDateTime::currentDateTime();
            State s;
            s.data = value;
            s.loading = false;
            deliver(key, s);
        }, Qt::QueuedConnection);
    };

    auto rejecter = [this, key](QString err) {
        QMetaObject::invokeMethod(this, [this, key, err]() {
            auto it = entries_.find(key);
            if (it == entries_.end()) return;
            it.value().inflight = false;
            // Preserve any prior cached_value — the failed fetch shouldn't
            // erase a previously good cache. Subscribers get an error
            // alongside the (possibly null) data they had.
            State s;
            s.data = it.value().cached_value;
            s.loading = false;
            s.error = err;
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

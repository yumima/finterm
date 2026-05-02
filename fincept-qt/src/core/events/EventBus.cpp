#include "core/events/EventBus.h"

#include <QMutexLocker>

namespace fincept {

EventBus& EventBus::instance() {
    static EventBus s;
    return s;
}

EventBus::HandlerId EventBus::subscribe(const QString& event, Handler handler) {
    QMutexLocker lock(&mutex_);
    HandlerId id = next_id_++;
    subscriptions_.append({id, event, std::move(handler)});
    return id;
}

void EventBus::unsubscribe(HandlerId id) {
    QMutexLocker lock(&mutex_);
    subscriptions_.removeIf([id](const Subscription& s) { return s.id == id; });
}

void EventBus::publish(const QString& event, const QVariantMap& data) {
    // Signal goes out synchronously on the calling thread; Qt routes it to
    // any QObject-connected slots according to their connection type.
    emit eventPublished(event, data);

    // Snapshot the matching handlers under the mutex, then drop the lock
    // before invoking. Two reasons:
    //   1. Reentrancy — a handler may subscribe()/unsubscribe()/publish()
    //      during dispatch. Walking subscriptions_ live would invalidate
    //      the iterator; holding the lock during dispatch would deadlock
    //      the recursive subscribe(). With a snapshot, additions/removals
    //      during dispatch take effect on the *next* publish (standard
    //      pub/sub semantics).
    //   2. Cross-thread — MCP tools publish from a worker QThread while
    //      the GUI thread may be subscribing concurrently. The mutex
    //      makes that safe; the brief lock-hold (just to copy std::function
    //      instances) keeps contention negligible in practice.
    QList<Handler> matched;
    {
        QMutexLocker lock(&mutex_);
        matched.reserve(subscriptions_.size());
        for (const auto& sub : subscriptions_) {
            if (sub.event == event)
                matched.append(sub.handler);
        }
    }
    for (const auto& h : matched)
        h(data);
}

} // namespace fincept

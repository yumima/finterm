#include "core/events/EventBus.h"

namespace fincept {

EventBus& EventBus::instance() {
    static EventBus s;
    return s;
}

EventBus::HandlerId EventBus::subscribe(const QString& event, Handler handler) {
    HandlerId id = next_id_++;
    subscriptions_.append({id, event, std::move(handler)});
    return id;
}

void EventBus::unsubscribe(HandlerId id) {
    subscriptions_.removeIf([id](const Subscription& s) { return s.id == id; });
}

void EventBus::publish(const QString& event, const QVariantMap& data) {
    emit eventPublished(event, data);
    // Snapshot the matching handlers before invoking so subscribe/unsubscribe
    // calls *inside* a handler don't mutate the list we're iterating.
    //
    // Concretely: nav.split_alongside ultimately materializes a screen, whose
    // ctor calls EventBus::subscribe() — which would invalidate the iterator
    // if we walked subscriptions_ directly. The previous workaround was to
    // dispatch the offending handler via Qt::QueuedConnection at the call
    // site, but that desynchronised publish-order from delivery-order and
    // broke "publish A then B" patterns. With this snapshot, every publish
    // can be synchronous and reentrant-safe; handlers added during dispatch
    // simply don't fire for the in-flight publish (standard semantics).
    QList<Handler> matched;
    matched.reserve(subscriptions_.size());
    for (const auto& sub : subscriptions_) {
        if (sub.event == event)
            matched.append(sub.handler);
    }
    for (const auto& h : matched)
        h(data);
}

} // namespace fincept

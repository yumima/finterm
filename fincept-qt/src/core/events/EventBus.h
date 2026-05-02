#pragma once
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QVariantMap>

#include <functional>

namespace fincept {

/// Application-wide event bus.
/// Decouples screens from services — anyone can subscribe; anyone can publish.
///
/// Thread-safety:
///   subscribe/unsubscribe/publish are safe from any thread. MCP tools in
///   particular run on a worker QThread and publish into the bus from there.
///   A mutex guards the subscription list; handlers are invoked on the
///   publishing thread *outside* the lock, so subscribers that need to
///   mutate UI state remain responsible for marshalling to the GUI thread
///   (typically via QMetaObject::invokeMethod).
///
/// Reentrancy:
///   A handler may call subscribe()/unsubscribe()/publish() during dispatch
///   without corrupting state. The matching handler list is snapshotted
///   under the mutex before invocation, so handlers added or removed during
///   dispatch take effect on the *next* publish — standard pub/sub semantics.
class EventBus : public QObject {
    Q_OBJECT
  public:
    static EventBus& instance();

    using Handler = std::function<void(const QVariantMap&)>;
    using HandlerId = int;

    HandlerId subscribe(const QString& event, Handler handler);
    void unsubscribe(HandlerId id);
    void publish(const QString& event, const QVariantMap& data = {});

  signals:
    void eventPublished(const QString& event, const QVariantMap& data);

  private:
    EventBus() = default;

    struct Subscription {
        HandlerId id;
        QString event;
        Handler handler;
    };

    mutable QMutex mutex_;
    QList<Subscription> subscriptions_;
    HandlerId next_id_ = 1;
};

} // namespace fincept

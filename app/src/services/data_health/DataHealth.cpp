#include "services/data_health/DataHealth.h"

#include "datahub/DataHub.h"

#include <QMap>
#include <QTimer>

#include <algorithm>

namespace finterm::services {

namespace {
constexpr qint64 kForgetAfterSecs = 10 * 60;
}

DataHealth& DataHealth::instance() {
    static DataHealth s;
    return s;
}

DataHealth::DataHealth() {
    expire_timer_ = new QTimer(this);
    expire_timer_->setInterval(60 * 1000);
    connect(expire_timer_, &QTimer::timeout, this, &DataHealth::expire);
    // A failing refresh reports every symbol it asked for at once: one
    // repaint for the burst, not one per symbol.
    notify_timer_ = new QTimer(this);
    notify_timer_->setSingleShot(true);
    notify_timer_->setInterval(250);
    connect(notify_timer_, &QTimer::timeout, this, &DataHealth::changed);
}

void DataHealth::start() {
    if (started_)
        return;
    started_ = true;
    auto& hub = datahub::DataHub::instance();
    connect(&hub, &datahub::DataHub::topic_error, this, [this](const QString& topic, const QString& error) {
        const auto [group, item] = describe_topic(topic);
        Entry e;
        e.group = group;
        e.item = item;
        // DataHub topics are market data, fetched from Yahoo unless the
        // text names another source.
        e.failure = data_failure::classify(error, QStringLiteral("Yahoo Finance"));
        e.hub = true;
        record(topic, std::move(e));
    });
    // Every publish passes here (thousands a minute): nothing to do unless
    // something is failing. Data arriving for a kind of topic (Quotes, News,
    // Charts…) means the way to its source works again: that kind's
    // network-kind failures (proxy, DNS, connection, timeout, rate limit,
    // refusal) are over, whichever topic they were on — a topic nobody asks
    // again soon must not keep the status bar amber. Other kinds may come
    // from elsewhere (quotes keep ticking while news is blocked), so theirs
    // stay. Failures about the request itself (unknown symbol, no data) stay
    // until that request succeeds.
    connect(&hub, &datahub::DataHub::topic_updated, this, [this](const QString& topic, const QVariant&) {
        if (entries_.isEmpty())
            return;
        bool changed = entries_.remove(topic);
        const QString group = describe_topic(topic).first;
        const QDateTime settled = QDateTime::currentDateTime().addSecs(-2); // not the same burst's errors
        for (auto it = entries_.begin(); it != entries_.end();) {
            const auto k = it->failure.kind;
            const bool network = k != data_failure::Kind::NotFound && k != data_failure::Kind::NoData &&
                                 k != data_failure::Kind::NeedsKey;
            if (it->hub && network && it->group == group && it->last < settled) {
                it = entries_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed)
            notify_soon();
    });
    expire_timer_->start();
}

QPair<QString, QString> DataHealth::describe_topic(const QString& topic) {
    const QStringList parts = topic.split(QLatin1Char(':'));
    const QString head = parts.value(0) + QLatin1Char(':') + parts.value(1);
    static const QMap<QString, QString> names{
        {QStringLiteral("market:quote"), QStringLiteral("Quotes")},
        {QStringLiteral("market:sparkline"), QStringLiteral("Sparklines")},
        {QStringLiteral("market:history"), QStringLiteral("Charts")},
        {QStringLiteral("market:movers"), QStringLiteral("Top movers")},
        {QStringLiteral("market:screener"), QStringLiteral("Screener")},
    };
    if (names.contains(head))
        return {names.value(head), parts.value(2)};
    if (parts.value(0) == QLatin1String("futures"))
        return {QStringLiteral("Futures"), parts.mid(1).join(QLatin1Char(' '))};
    if (parts.value(0) == QLatin1String("cond"))
        return {QStringLiteral("Market conditions"), parts.mid(1).join(QLatin1Char(' '))};
    if (parts.value(0) == QLatin1String("news"))
        return {QStringLiteral("News"), parts.mid(1).join(QLatin1Char(' '))};
    QString group = parts.value(0);
    if (!group.isEmpty())
        group[0] = group[0].toUpper();
    return {group, parts.mid(1).join(QLatin1Char(' '))};
}

void DataHealth::report_failure(const QString& key, const QString& what, const QString& error,
                                const QString& source_hint) {
    Entry e;
    e.group = what;
    e.failure = data_failure::classify(error, source_hint);
    record(key, std::move(e));
}

void DataHealth::report_success(const QString& key) {
    if (entries_.remove(key))
        notify_soon();
}

void DataHealth::record(const QString& key, Entry e) {
    e.last = QDateTime::currentDateTime();
    if (const auto it = entries_.constFind(key); it != entries_.constEnd())
        e.count = it->count + 1;
    else
        e.count = 1;
    entries_.insert(key, std::move(e));
    notify_soon();
}

void DataHealth::notify_soon() {
    if (!notify_timer_->isActive())
        notify_timer_->start();
}

void DataHealth::expire() {
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-kForgetAfterSecs);
    bool dropped = false;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->last < cutoff) {
            it = entries_.erase(it);
            dropped = true;
        } else {
            ++it;
        }
    }
    if (dropped)
        notify_soon();
}

QVector<DataHealth::Problem> DataHealth::problems() const {
    // Group by what failed where: one source failing one way is one problem,
    // however many symbols it hit.
    QMap<QString, Problem> by_cause;
    QMap<QString, QMap<QString, QStringList>> items; // cause → group → items
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
        const Entry& e = it.value();
        const QString cause = e.failure.source + QLatin1Char('|') + QString::number(int(e.failure.kind)) +
                              QLatin1Char('|') + QString::number(e.failure.http_status);
        Problem& p = by_cause[cause];
        if (p.count == 0 || e.last > p.last) {
            p.failure = e.failure; // the newest wording (and raw text) stands for the group
            p.last = e.last;
        }
        p.count += e.count;
        ++p.affected;
        if (e.hub)
            p.keys << it.key();
        if (!e.item.isEmpty())
            items[cause][e.group] << e.item;
        else
            items[cause][e.group]; // a group with no items ("Earnings calendar")
    }
    QVector<Problem> out;
    for (auto it = by_cause.begin(); it != by_cause.end(); ++it) {
        Problem p = it.value();
        const auto& groups = items[it.key()];
        for (auto g = groups.cbegin(); g != groups.cend(); ++g) {
            QStringList list = g.value();
            list.sort();
            constexpr int kShown = 6;
            QString line = g.key();
            if (!list.isEmpty()) {
                line += QStringLiteral(": ") + list.mid(0, kShown).join(QStringLiteral(", "));
                if (list.size() > kShown)
                    line += QStringLiteral(" and %1 more").arg(list.size() - kShown);
            }
            p.items << line;
        }
        out << p;
    }
    std::sort(out.begin(), out.end(), [](const Problem& a, const Problem& b) {
        return a.affected != b.affected ? a.affected > b.affected : a.last > b.last;
    });
    return out;
}

} // namespace finterm::services

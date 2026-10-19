// src/services/markets/IndexAfterHours.cpp
#include "services/markets/IndexAfterHours.h"

#include "core/logging/Logger.h"
#include "python/PythonWorker.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QSet>
#include <QTimeZone>

#include <cmath>

namespace fincept::services {

namespace {

// Every index future Yahoo carries with intraday bars. NQ is the Nasdaq-100,
// not the Composite — the closest traded proxy, and named as such in the
// tooltip. NIY is the yen contract, in the index's own currency.
const QVector<IndexProxy>& proxies() {
    static const QVector<IndexProxy> v = {
        {"^GSPC", "ES=F", "E-mini S&P 500", "America/New_York", "09:30", "16:00", {}, "13:00"},
        {"^DJI", "YM=F", "E-mini Dow", "America/New_York", "09:30", "16:00", {}, "13:00"},
        {"^IXIC", "NQ=F", "E-mini Nasdaq-100", "America/New_York", "09:30", "16:00", {}, "13:00"},
        {"^NDX", "NQ=F", "E-mini Nasdaq-100", "America/New_York", "09:30", "16:00", {}, "13:00"},
        {"^RUT", "RTY=F", "E-mini Russell 2000", "America/New_York", "09:30", "16:00", {}, "13:00"},
        {"^N225", "NIY=F", "Nikkei 225 (yen, CME)", "Asia/Tokyo", "09:00", "15:30", "11:30-12:30"},
    };
    return v;
}

// The daemon's 5-day window bounds this already; checked again here so a
// reference from a different week can never be shown as an after-hours move.
constexpr qint64 kMaxCloseAgeSecs = 5 * 86400;

QString when(qint64 ts, const QString& tz) {
    const QTimeZone zone(tz.toUtf8());
    // EVENT-STAMP: the cash close and the future's last print are instants, not sessions.
    const QDateTime t = QDateTime::fromSecsSinceEpoch(ts).toTimeZone(zone);
    return t.toString(QStringLiteral("ddd d MMM HH:mm ")) + zone.abbreviation(t);
}

QString px(double v) {
    return QLocale(QLocale::English).toString(v, 'f', 2);
}

} // namespace

const IndexProxy* index_proxy_for(const QString& index) {
    for (const auto& p : proxies())
        if (p.index == index)
            return &p;
    return nullptr;
}

void fetch_index_after_hours(const QStringList& indices, QObject* ctx,
                             std::function<void(bool, QHash<QString, IndexAfterHoursQuote>, QString)> cb) {
    bool any = false;
    for (const auto& sym : indices)
        any = any || index_proxy_for(sym);
    if (!any) {
        cb(true, {}, {});
        return;
    }
    QJsonArray pairs;
    for (const auto& p : proxies()) {
        QJsonObject pair{{"index", p.index}, {"proxy", p.proxy}, {"tz", p.tz},
                         {"open", p.open}, {"close", p.close}};
        if (!p.lunch.isEmpty())
            pair.insert(QStringLiteral("break"), p.lunch);
        if (!p.early_close.isEmpty())
            pair.insert(QStringLiteral("early_close"), p.early_close);
        pairs.append(pair);
    }
    QPointer<QObject> guard(ctx);
    // An explicit timeout: without one PythonWorker never calls back from a
    // daemon that doesn't answer, and the caller's in-flight flag sticks.
    python::PythonWorker::instance().submit(
        QStringLiteral("index_after_hours"), QJsonObject{{"pairs", pairs}},
        [guard, cb, wanted = QSet<QString>(indices.begin(), indices.end())](bool ok, QJsonObject result,
                                                                            QString err) {
            if (!guard)
                return;
            if (!ok) {
                LOG_WARN("IndexAfterHours", QString("fetch failed: %1").arg(err.left(120)));
                cb(false, {}, err.left(120));
                return;
            }
            const QJsonArray rows = result.contains(QStringLiteral("_value"))
                                        ? result.value(QStringLiteral("_value")).toArray()
                                        : result.value(QStringLiteral("data")).toArray();
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            QHash<QString, IndexAfterHoursQuote> out;
            for (const auto& v : rows) {
                const auto o = v.toObject();
                // Null pct is the daemon declining to pair prints it can't
                // honestly pair. Absent stays absent: never a 0.00%.
                if (!o.value(QStringLiteral("pct")).isDouble())
                    continue;
                IndexAfterHoursQuote q;
                q.index = o.value(QStringLiteral("symbol")).toString();
                if (!wanted.contains(q.index))
                    continue;
                q.proxy = o.value(QStringLiteral("proxy")).toString();
                q.contract = o.value(QStringLiteral("contract")).toString();
                q.pct = o.value(QStringLiteral("pct")).toDouble();
                q.ref_price = o.value(QStringLiteral("ref_price")).toDouble();
                q.ext_price = o.value(QStringLiteral("ext_price")).toDouble();
                q.ref_ts = static_cast<qint64>(o.value(QStringLiteral("ref_ts")).toDouble());
                q.ext_ts = static_cast<qint64>(o.value(QStringLiteral("ext_ts")).toDouble());
                q.session = o.value(QStringLiteral("session")).toString();
                q.cash_open = o.value(QStringLiteral("cash_open")).toBool();
                if (q.index.isEmpty() || !std::isfinite(q.pct) || q.ref_price <= 0 || q.ref_ts <= 0 ||
                    q.ext_ts <= q.ref_ts || now - q.ref_ts > kMaxCloseAgeSecs)
                    continue;
                out.insert(q.index, q);
            }
            cb(true, out, {});
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

QString index_after_hours_tooltip(const IndexAfterHoursQuote& q, const QString& label) {
    const auto* p = index_proxy_for(q.index);
    const QString tz = p ? p->tz : QStringLiteral("America/New_York");
    const QString contract = q.contract.isEmpty() ? q.proxy : q.contract;
    const QString fut = p ? QStringLiteral("%1 (%2)").arg(contract, p->proxy_name) : contract;
    QString s = QStringLiteral("%1 after-hours (%8), implied by the %2 future.\n\n"
                               "A cash index stops at the bell; its future keeps trading. This is the "
                               "future's move since the cash close, against the future's own price at "
                               "the close, so the futures/cash basis cancels.\n\n"
                               "%3 at the cash close (%4): %5\n%3 at %6: %7")
                    .arg(label, fut, contract, when(q.ref_ts, tz), px(q.ref_price),
                         when(q.ext_ts, tz), px(q.ext_price), q.session);
    if (q.cash_open)
        s += QStringLiteral("\n\nThe cash market is open: this is the move into today's open, frozen "
                            "until the close, and it is already inside CHG% — the two are not additive.");
    if (q.index.compare(QLatin1String("^IXIC")) == 0)
        s += QStringLiteral("\n\nThe Composite has no future; the Nasdaq-100 future stands in for it.");
    s += QStringLiteral("\n\nOne contract month is used for both prices, so a quarterly roll can't "
                        "show up as a move. Yahoo's futures bars run about 10 minutes behind.");
    return s;
}

QString index_after_hours_unavailable_tooltip(const QString& index, const QString& label) {
    if (!index_proxy_for(index))
        return QStringLiteral("%1 has no after-hours number: a cash index stops trading at the bell, and "
                              "the data feed carries no future for it to imply one from.")
            .arg(label);
    return QStringLiteral("%1 after-hours: no sound reading right now — still fetching, the future has "
                          "no print since the close yet (its bars run ~10 min behind), or the index "
                          "feed stalled mid-session. Shown as a dash rather than a guess.")
        .arg(label);
}

} // namespace fincept::services

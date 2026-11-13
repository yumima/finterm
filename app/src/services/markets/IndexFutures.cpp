// src/services/markets/IndexFutures.cpp
#include "services/markets/IndexFutures.h"

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

namespace finterm::services {

namespace {

// Every index future Yahoo carries with intraday bars. NQ is the Nasdaq-100,
// not the Composite — the closest traded future, and named as such in the
// tooltip. NIY is the yen contract, in the index's own currency.
const QVector<IndexFuture>& futures() {
    static const QVector<IndexFuture> v = {
        {"^GSPC", "ES=F", "ES", "E-mini S&P 500"},       {"^DJI", "YM=F", "YM", "E-mini Dow"},
        {"^IXIC", "NQ=F", "NQ", "E-mini Nasdaq-100"},    {"^NDX", "NQ=F", "NQ", "E-mini Nasdaq-100"},
        {"^RUT", "RTY=F", "RTY", "E-mini Russell 2000"}, {"^N225", "NIY=F", "NIY", "Nikkei 225 (yen, CME)"},
    };
    return v;
}

QString when_et(qint64 ts) {
    const QTimeZone et("America/New_York");
    // EVENT-STAMP: a last print is an instant, not a session.
    return QDateTime::fromSecsSinceEpoch(ts).toTimeZone(et).toString(QStringLiteral("ddd d MMM HH:mm 'ET'"));
}

QString px(double v) {
    return QLocale(QLocale::English).toString(v, 'f', 2);
}

} // namespace

const IndexFuture* index_future_for(const QString& index) {
    for (const auto& f : futures())
        if (f.index == index)
            return &f;
    return nullptr;
}

QString contract_label(const IndexFutureQuote& q) {
    return q.contract.section(QLatin1Char('.'), 0, 0);
}

void fetch_index_futures(const QStringList& indices, QObject* ctx,
                         std::function<void(bool, QHash<QString, IndexFutureQuote>, QString)> cb) {
    QSet<QString> wanted;
    for (const auto& sym : indices)
        if (index_future_for(sym))
            wanted.insert(sym);
    if (wanted.isEmpty()) {
        cb(true, {}, {});
        return;
    }
    QJsonArray pairs;
    for (const auto& f : futures())
        pairs.append(QJsonObject{{"index", f.index}, {"proxy", f.proxy}});
    QPointer<QObject> guard(ctx);
    // An explicit timeout: without one PythonWorker never calls back from a
    // daemon that doesn't answer, and the caller's in-flight flag sticks.
    python::PythonWorker::instance().submit(
        QStringLiteral("index_futures"), QJsonObject{{"pairs", pairs}},
        [guard, cb, wanted](bool ok, QJsonObject result, QString err) {
            if (!guard)
                return;
            if (!ok) {
                LOG_WARN("IndexFutures", QString("fetch failed: %1").arg(err.left(120)));
                cb(false, {}, err.left(120));
                return;
            }
            const QJsonArray rows = result.contains(QStringLiteral("_value"))
                                        ? result.value(QStringLiteral("_value")).toArray()
                                        : result.value(QStringLiteral("data")).toArray();
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            QHash<QString, IndexFutureQuote> out;
            for (const auto& v : rows) {
                const auto o = v.toObject();
                IndexFutureQuote q;
                q.index = o.value(QStringLiteral("symbol")).toString();
                q.contract = o.value(QStringLiteral("contract")).toString();
                if (!wanted.contains(q.index) || q.contract.isEmpty())
                    continue;
                q.refused = o.value(QStringLiteral("transient")).toBool();
                q.reason = o.value(QStringLiteral("reason")).toString();
                // Null pct is the daemon declining a pairing it can't stand
                // behind, or a refused quote: the row stays PENDING — named,
                // so its contract streams and its prints fill it — never 0.00%.
                if (o.value(QStringLiteral("pct")).isDouble()) {
                    IndexFutureQuote f = q;
                    f.price = o.value(QStringLiteral("price")).toDouble();
                    f.change = o.value(QStringLiteral("change")).toDouble();
                    f.pct = o.value(QStringLiteral("pct")).toDouble();
                    f.ref_price = o.value(QStringLiteral("ref_price")).toDouble();
                    f.session_date =
                        QDate::fromString(o.value(QStringLiteral("session_date")).toString(), Qt::ISODate);
                    f.ts = static_cast<qint64>(o.value(QStringLiteral("ts")).toDouble());
                    // The daemon already refuses stale prints and missing
                    // closes; this only guards the shape. A last print from
                    // the future would be a clock or decode bug, not a quote.
                    if (std::isfinite(f.pct) && f.price > 0 && f.ref_price > 0 && f.session_date.isValid() &&
                        f.ts > 0 && f.ts <= now + 600)
                        q = f;
                }
                out.insert(q.index, q);
            }
            cb(true, out, {});
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

QString index_future_tooltip(const IndexFutureQuote& q, const QString& label) {
    const auto* f = index_future_for(q.index);
    QString s = QStringLiteral("%1 futures — %2 (%3)\n\n"
                               "Last %4 at %5\n"
                               "Previous close %6 (before the %7 session)\n"
                               "Change %8%9 (%10%11%)")
                    .arg(label, contract_label(q), f ? f->name : q.contract, px(q.price), when_et(q.ts),
                         px(q.ref_price), q.session_date.toString(QStringLiteral("ddd d MMM")), q.change >= 0 ? "+" : "−")
                    .arg(px(std::abs(q.change)), q.pct >= 0 ? "+" : "−", QString::number(std::abs(q.pct), 'f', 2));
    if (q.index == QLatin1String("^IXIC"))
        s += QStringLiteral("\n\nThe Composite has no future; the Nasdaq-100 future stands in for it.");
    if (q.index == QLatin1String("^N225"))
        s += QStringLiteral("\n\nCME contract: the change is since the CME close (17:00 ET), not Tokyo's close.");
    s += QStringLiteral("\n\nYahoo's futures data runs about 10 minutes behind.");
    return s;
}

QString index_future_unavailable_tooltip(const QString& index, const QString& label) {
    if (!index_future_for(index))
        return QStringLiteral("%1 has no futures quote: the data feed carries no future for this index.").arg(label);
    return QStringLiteral("%1 futures: no reliable reading right now (still fetching, or a feed gap).")
        .arg(label);
}

} // namespace finterm::services

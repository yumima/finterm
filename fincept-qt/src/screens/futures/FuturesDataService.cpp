#include "screens/futures/FuturesDataService.h"

#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "screens/futures/FuturesContracts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace fincept::screens::futures {

FuturesDataService& FuturesDataService::instance() {
    static FuturesDataService inst;
    return inst;
}

// Parse the router's standard envelope: {success, data, source, error, timestamp}.
void FuturesDataService::run_router(const QStringList& args,
                                    std::function<void(bool, QJsonObject, QString)> cb) {
    python::PythonRunner::instance().run(
        "futures_router.py", args, [cb = std::move(cb)](python::PythonResult result) {
            if (!result.success) {
                cb(false, QJsonObject{}, QString());
                return;
            }
            const QString json_str = python::extract_json(result.output);
            const auto doc = QJsonDocument::fromJson(json_str.toUtf8());
            if (!doc.isObject()) {
                cb(false, QJsonObject{}, QString());
                return;
            }
            const auto obj = doc.object();
            const bool ok = obj.value("success").toBool(false);
            const QString source = obj.value("source").toString();
            cb(ok, obj, source);
        });
}

static FuturesQuote parse_quote(const QJsonObject& o) {
    FuturesQuote q;
    q.symbol = o.value("symbol").toString();
    q.name = o.value("name").toString(q.symbol);
    q.asset_class = o.value("class").toString();
    q.last = o.value("last").toDouble();
    q.change = o.value("change").toDouble();
    q.change_pct = o.value("change_pct").toDouble();
    q.volume = o.value("volume").toDouble();
    q.open_interest = o.value("open_interest").toDouble();
    q.high = o.value("high").toDouble(q.last);
    q.low = o.value("low").toDouble(q.last);
    return q;
}

void FuturesDataService::fetch_quotes(const QStringList& symbols, QuotesCallback cb) {
    if (symbols.isEmpty()) {
        cb(true, {}, QString());
        return;
    }
    if (source_ == "stooq") {
        fetch_quotes_stooq(symbols, std::move(cb));
        return;
    }
    fetch_quotes_yahoo(symbols, std::move(cb));
}

void FuturesDataService::fetch_quotes_yahoo(const QStringList& symbols, QuotesCallback cb) {
    // Hot path — persistent yfinance daemon. Skips the ~1.5s cold-import
    // that PythonRunner pays per spawn, which dominates wall time when
    // refreshing the 35-contract cache.
    QStringList yf_syms;
    QHash<QString, const ContractDef*> by_yf;
    yf_syms.reserve(symbols.size());
    for (const auto& root : symbols) {
        const auto* def = find_contract(root);
        if (!def) continue;
        const QString yf = yf_symbol_for(root);
        yf_syms.push_back(yf);
        by_yf.insert(yf, def);
    }
    if (yf_syms.isEmpty()) {
        cb(true, {}, "yfinance");
        return;
    }

    QJsonArray syms_arr;
    for (const auto& s : yf_syms) syms_arr.append(s);
    QJsonObject payload;
    payload.insert("symbols", syms_arr);

    python::PythonWorker::instance().submit(
        "batch_quotes", payload,
        [cb = std::move(cb), by_yf](bool ok, QJsonObject result, QString /*err*/) {
            if (!ok) { cb(false, {}, "yfinance"); return; }
            // Daemon returns a bare array; PythonWorker wraps it as {_value: [...]}.
            const QJsonArray quotes = result.contains("_value")
                                          ? result.value("_value").toArray()
                                          : result.value("quotes").toArray();
            QVector<FuturesQuote> rows;
            for (const auto& v : quotes) {
                const auto o = v.toObject();
                const QString yf_sym = o.value("symbol").toString();
                const auto it = by_yf.constFind(yf_sym);
                if (it == by_yf.constEnd()) continue;
                const ContractDef* def = it.value();
                FuturesQuote q;
                q.symbol = def->symbol;
                q.name = def->name;
                q.asset_class = def->asset_class;
                q.last = o.value("price").toDouble();
                q.change = o.value("change").toDouble();
                q.change_pct = o.value("change_percent").toDouble();
                q.volume = o.value("volume").toDouble();
                q.high = o.value("high").toDouble(q.last);
                q.low = o.value("low").toDouble(q.last);
                q.open_interest = 0;  // yfinance does not expose OI
                rows.push_back(q);
            }
            cb(true, rows, "yfinance");
        });
}

void FuturesDataService::fetch_quotes_stooq(const QStringList& symbols, QuotesCallback cb) {
    // Stooq path — single-batch CSV via stooq_data.py. ~300ms total.
    QStringList args{"futures_quotes", symbols.join(",")};
    python::PythonRunner::instance().run(
        "stooq_data.py", args, [cb = std::move(cb)](python::PythonResult result) {
            if (!result.success) { cb(false, {}, "stooq"); return; }
            const QString json_str = python::extract_json(result.output);
            const auto doc = QJsonDocument::fromJson(json_str.toUtf8());
            if (!doc.isObject() || !doc.object().value("success").toBool(false)) {
                cb(false, {}, "stooq");
                return;
            }
            QVector<FuturesQuote> rows;
            for (const auto& v : doc.object().value("data").toArray()) {
                const auto o = v.toObject();
                const QString root = o.value("symbol").toString();
                const auto* def = find_contract(root);
                FuturesQuote q;
                q.symbol = root;
                q.name = def ? def->name : root;
                q.asset_class = def ? def->asset_class : QString();
                q.last = o.value("last").toDouble();
                q.change = o.value("change").toDouble();
                q.change_pct = o.value("change_pct").toDouble();
                q.volume = o.value("volume").toDouble();
                q.open_interest = 0;
                q.high = o.value("high").toDouble(q.last);
                q.low = o.value("low").toDouble(q.last);
                rows.push_back(q);
            }
            cb(true, rows, "stooq");
        });
}

void FuturesDataService::fetch_term_structure(const QString& product, int n_months, TermCallback cb) {
    QStringList args{"term_structure", product, QString::number(n_months)};
    run_router(args, [cb = std::move(cb)](bool ok, QJsonObject obj, QString source) {
        if (!ok) {
            cb(false, {}, source);
            return;
        }
        QVector<TermStructurePoint> pts;
        for (const auto& v : obj.value("data").toArray()) {
            const auto o = v.toObject();
            TermStructurePoint p;
            // CME public payloads use various keys — try several.
            p.month = o.value("month").toString(o.value("contractMonth").toString(o.value("expirationCode").toString()));
            p.last = o.value("last").toDouble(o.value("settlement").toDouble(o.value("settle").toDouble()));
            p.change = o.value("change").toDouble();
            p.volume = o.value("volume").toDouble();
            p.open_interest = o.value("open_interest").toDouble(o.value("openInterest").toDouble());
            pts.push_back(p);
        }
        cb(true, pts, source);
    });
}

void FuturesDataService::fetch_history(const QString& symbol, const QString& period, HistoryCallback cb) {
    if (source_ == "stooq") {
        fetch_history_stooq(symbol, period, std::move(cb));
        return;
    }
    fetch_history_yahoo(symbol, period, std::move(cb));
}

void FuturesDataService::fetch_history_yahoo(const QString& symbol, const QString& period, HistoryCallback cb) {
    const auto* def = find_contract(symbol);
    if (!def) { cb(false, {}, "yfinance"); return; }
    QJsonObject payload;
    payload.insert("symbol", yf_symbol_for(symbol));
    payload.insert("period", period);
    payload.insert("interval", "1d");

    python::PythonWorker::instance().submit(
        "historical_period", payload,
        [cb = std::move(cb)](bool ok, QJsonObject result, QString /*err*/) {
            if (!ok) { cb(false, {}, "yfinance"); return; }
            const QJsonArray points = result.contains("_value")
                                           ? result.value("_value").toArray()
                                           : result.value("history").toArray();
            QVector<HistoryPoint> pts;
            for (const auto& v : points) {
                const auto o = v.toObject();
                HistoryPoint p;
                p.timestamp = static_cast<qint64>(o.value("timestamp").toDouble());
                p.open = o.value("open").toDouble();
                p.high = o.value("high").toDouble();
                p.low = o.value("low").toDouble();
                p.close = o.value("close").toDouble();
                p.volume = o.value("volume").toDouble();
                pts.push_back(p);
            }
            cb(true, pts, "yfinance");
        });
}

void FuturesDataService::fetch_history_stooq(const QString& symbol, const QString& period, HistoryCallback cb) {
    QStringList args{"futures_history", symbol, period};
    python::PythonRunner::instance().run(
        "stooq_data.py", args, [cb = std::move(cb)](python::PythonResult result) {
            if (!result.success) { cb(false, {}, "stooq"); return; }
            const QString json_str = python::extract_json(result.output);
            const auto doc = QJsonDocument::fromJson(json_str.toUtf8());
            if (!doc.isObject() || !doc.object().value("success").toBool(false)) {
                cb(false, {}, "stooq");
                return;
            }
            QVector<HistoryPoint> pts;
            for (const auto& v : doc.object().value("data").toArray()) {
                const auto o = v.toObject();
                HistoryPoint p;
                p.timestamp = static_cast<qint64>(o.value("timestamp").toDouble());
                p.open = o.value("open").toDouble();
                p.high = o.value("high").toDouble();
                p.low = o.value("low").toDouble();
                p.close = o.value("close").toDouble();
                p.volume = o.value("volume").toDouble();
                pts.push_back(p);
            }
            cb(true, pts, "stooq");
        });
}

void FuturesDataService::fetch_heatmap(HeatmapCallback cb) {
    QStringList args{"heatmap"};
    run_router(args, [cb = std::move(cb)](bool ok, QJsonObject obj, QString source) {
        if (!ok) {
            cb(false, {}, source);
            return;
        }
        QHash<QString, QVector<FuturesQuote>> groups;
        const auto data = obj.value("data").toObject();
        const auto group_obj = data.value("groups").toObject();
        for (auto it = group_obj.begin(); it != group_obj.end(); ++it) {
            QVector<FuturesQuote> rows;
            for (const auto& v : it.value().toArray())
                rows.push_back(parse_quote(v.toObject()));
            groups.insert(it.key(), rows);
        }
        cb(true, groups, source);
    });
}

void FuturesDataService::fetch_spread(const QString& leg1, const QString& leg2, SpreadCallback cb) {
    QStringList args{"spread", leg1, leg2};
    run_router(args, [cb = std::move(cb)](bool ok, QJsonObject obj, QString source) {
        if (!ok) {
            cb(false, {}, source);
            return;
        }
        const auto data = obj.value("data").toObject();
        SpreadResult r;
        r.leg1 = parse_quote(data.value("leg1").toObject());
        r.leg2 = parse_quote(data.value("leg2").toObject());
        r.spread = data.value("spread").toDouble();
        r.spread_pct = data.value("spread_pct").toDouble();
        cb(true, r, source);
    });
}

void FuturesDataService::fetch_china_main(const QString& exchange, ChinaListCallback cb) {
    QStringList args{"china_main", exchange.isEmpty() ? "all" : exchange};
    run_router(args, [cb = std::move(cb)](bool ok, QJsonObject obj, QString source) {
        if (!ok) {
            cb(false, {}, source);
            return;
        }
        cb(true, obj.value("data").toArray(), source);
    });
}

} // namespace fincept::screens::futures

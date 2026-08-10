// tools/technicals_series/score_series.cpp
//
// Scores every bar of an indicator series with the real TechnicalRating scorer
// and prints "<timestamp>,<net>" per rated bar.
//
// This exists because the rating could not otherwise be measured. The tab
// scores one bar — the latest — so any study of whether the rating means
// anything had to fall back on a Python re-implementation, and a
// re-implementation only ever proves things about itself. Linking the shipped
// translation unit and walking it over history is the only way the numbers in
// scripts/factor_rating describe the code that actually runs. It is how the
// finding that the full composite (80.1% agreement with a 40-day trend label)
// is beaten by a single ATR-normalised distance from a 200-day average (83.1%)
// was established.
//
// Usage:  score_series < technicals_rows.json
// stdin:  the row array compute_technicals emits, oldest bar first.
#include "services/equity/TechnicalRating.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
using namespace fincept::services::equity;

static void fill_row(IndicatorRow& r, const QJsonObject& o) {
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (it.value().isDouble()) r.set(it.key(), it.value().toDouble());
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QFile in; in.open(stdin, QIODevice::ReadOnly);
    const QJsonArray rows = QJsonDocument::fromJson(in.readAll()).array();
    QTextStream out(stdout);
    static const QList<QPair<QString,QString>> kCols = {
        {"trend","sma_10"},{"trend","sma_20"},{"trend","sma_30"},{"trend","sma_50"},
        {"trend","sma_100"},{"trend","sma_200"},{"trend","ema_12"},{"trend","ema_26"},
        {"trend","wma_9"},{"trend","ichimoku_base"},{"trend","macd"},{"trend","macd_signal"},
        {"trend","cci"},{"trend","adx"},{"trend","aroon_up"},{"trend","aroon_down"},
        {"momentum","rsi"},{"momentum","stoch_k"},{"momentum","stoch_d"},{"momentum","williams_r"},
        {"momentum","roc"},{"momentum","mfi"},{"momentum","ao"},{"momentum","kama"},
        {"volatility","atr"},{"volatility","bb_mavg"},{"volatility","bb_hband"},
        {"volatility","bb_lband"},{"volatility","bb_pband"},{"volatility","bb_wband"},
        {"volume","obv"},{"volume","vwap"},{"volume","cmf"},{"volume","adi"},
    };
    const int lb = technical_rating::kSlopeLookback;
    for (int i = 0; i < rows.size(); ++i) {
        const QJsonObject cur = rows.at(i).toObject();
        RatingInput in2;
        in2.bars = i + 1;
        in2.close = cur.value("close").toDouble();
        fill_row(in2.now, cur);
        if (i >= 1) fill_row(in2.prev, rows.at(i - 1).toObject());
        if (i >= lb) fill_row(in2.back, rows.at(i - lb).toObject());
        QVector<TechIndicator> all;
        for (const auto& kv : kCols) {
            if (!in2.now.has(kv.second)) continue;
            const auto v = technical_rating::score(kv.second, in2);
            TechIndicator ti; ti.name = kv.second; ti.value = in2.now.get(kv.second);
            ti.category = kv.first; ti.signal = v.signal; ti.votes = v.votes;
            ti.rating_bucket = v.bucket; all.append(ti);
        }
        const auto v = technical_rating::aggregate(all, in2);
        if (v.basis.startsWith("Not enough")) continue;
        out << QString::number(static_cast<qint64>(cur.value("timestamp").toDouble()))
            << "," << QString::number(v.net, 'f', 5) << "\n";
    }
    return 0;
}

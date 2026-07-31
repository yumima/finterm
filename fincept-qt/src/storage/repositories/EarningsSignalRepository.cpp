// src/storage/repositories/EarningsSignalRepository.cpp
#include "storage/repositories/EarningsSignalRepository.h"

#include <QDateTime>

namespace fincept {
namespace {

/// SQL NULL → nullopt, so "no data" survives the round trip instead of
/// arriving as a 0.0 the panel would render as a real reading of zero.
std::optional<double> opt_real(QSqlQuery& q, const char* column) {
    const auto v = q.value(QLatin1String(column));
    if (v.isNull())
        return std::nullopt;
    return v.toDouble();
}

QVariant to_variant(const std::optional<double>& v) {
    return v.has_value() ? QVariant(*v) : QVariant(QMetaType(QMetaType::Double));
}

} // namespace

EarningsSignalRepository& EarningsSignalRepository::instance() {
    static EarningsSignalRepository inst;
    return inst;
}

EarningsSignalRecord EarningsSignalRepository::map_row(QSqlQuery& q) {
    EarningsSignalRecord r;
    r.id                = q.value(QStringLiteral("id")).toLongLong();
    r.symbol            = q.value(QStringLiteral("symbol")).toString();
    r.report_ts         = q.value(QStringLiteral("report_ts")).toLongLong();
    r.observed_on       = q.value(QStringLiteral("observed_on")).toString();
    r.captured_at       = q.value(QStringLiteral("captured_at")).toString();
    r.days_to_report    = q.value(QStringLiteral("days_to_report")).toInt();
    r.verdict           = q.value(QStringLiteral("verdict")).toString();
    r.score             = q.value(QStringLiteral("score")).toDouble();
    r.confidence        = q.value(QStringLiteral("confidence")).toDouble();
    r.setup_score       = opt_real(q, "setup_score");
    r.bar_score         = opt_real(q, "bar_score");
    r.expected_move_pct = opt_real(q, "expected_move_pct");
    r.consensus_eps     = opt_real(q, "consensus_eps");
    r.dispersion_pct    = opt_real(q, "dispersion_pct");
    r.runup_5d_pct      = opt_real(q, "runup_5d_pct");
    r.price_at_capture  = opt_real(q, "price_at_capture");
    r.predicted_move_pct = opt_real(q, "predicted_move_pct");
    r.resolved          = q.value(QStringLiteral("resolved")).toInt() != 0;
    r.actual_eps        = opt_real(q, "actual_eps");
    r.surprise_pct      = opt_real(q, "surprise_pct");
    r.actual_move_pct   = opt_real(q, "actual_move_pct");
    const auto hit = q.value(QStringLiteral("direction_hit"));
    if (!hit.isNull())
        r.direction_hit = hit.toInt() != 0;
    r.resolved_at       = q.value(QStringLiteral("resolved_at")).toString();
    return r;
}

qint64 EarningsSignalRepository::observe(const EarningsSignalRecord& r) {
    // OR IGNORE against the (symbol, report_ts, observed_on) unique index: the
    // second visit of the day is not a second observation, and letting it
    // through would weight a day the user happened to revisit more heavily in
    // anything computed from these rows.
    auto res = exec_insert(
        "INSERT OR IGNORE INTO earnings_signal_records "
        "(symbol, report_ts, observed_on, captured_at, days_to_report, verdict, score, "
        " confidence, setup_score, bar_score, expected_move_pct, consensus_eps, "
        " dispersion_pct, runup_5d_pct, price_at_capture, predicted_move_pct) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        {r.symbol.toUpper(), r.report_ts, r.observed_on, r.captured_at, r.days_to_report,
         r.verdict, r.score, r.confidence, to_variant(r.setup_score), to_variant(r.bar_score),
         to_variant(r.expected_move_pct), to_variant(r.consensus_eps),
         to_variant(r.dispersion_pct), to_variant(r.runup_5d_pct),
         to_variant(r.price_at_capture), to_variant(r.predicted_move_pct)});
    return res.is_ok() ? res.value() : 0;
}

QVector<EarningsSignalRecord> EarningsSignalRepository::for_symbol(const QString& symbol) const {
    auto r = query_list("SELECT * FROM earnings_signal_records WHERE symbol = ? "
                        "ORDER BY report_ts DESC, observed_on DESC",
                        {symbol.toUpper()}, map_row);
    return r.is_ok() ? r.value() : QVector<EarningsSignalRecord>{};
}

QVector<EarningsSignalRecord> EarningsSignalRepository::unresolved(const QString& symbol) const {
    auto r = query_list("SELECT * FROM earnings_signal_records "
                        "WHERE symbol = ? AND resolved = 0 ORDER BY report_ts ASC",
                        {symbol.toUpper()}, map_row);
    return r.is_ok() ? r.value() : QVector<EarningsSignalRecord>{};
}

int EarningsSignalRepository::resolve(const QString& symbol, qint64 report_ts,
                                      std::optional<double> actual_eps,
                                      std::optional<double> surprise_pct,
                                      double actual_move_pct) {
    // Every reading about this print resolves together — they were all
    // observations of one event, taken on different days. The hit is judged
    // per row, because each row carries the verdict as it stood that day.
    const auto rows = query_list(
        "SELECT * FROM earnings_signal_records WHERE symbol = ? AND report_ts = ? AND resolved = 0",
        {symbol.toUpper(), report_ts}, map_row);
    if (rows.is_err())
        return 0;

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    int updated = 0;
    for (const auto& row : rows.value()) {
        // HOLD is not a directional call, so it is neither hit nor miss —
        // scoring it either way would flatter or punish the engine for
        // declining to take a side, which is a legitimate answer.
        QVariant hit(QMetaType(QMetaType::Int));
        if (row.verdict == QLatin1String("BUY"))
            hit = actual_move_pct > 0 ? 1 : 0;
        else if (row.verdict == QLatin1String("SELL"))
            hit = actual_move_pct < 0 ? 1 : 0;

        auto w = exec_write(
            "UPDATE earnings_signal_records SET resolved = 1, actual_eps = ?, surprise_pct = ?, "
            "actual_move_pct = ?, direction_hit = ?, resolved_at = ? WHERE id = ? AND resolved = 0",
            {to_variant(actual_eps), to_variant(surprise_pct), actual_move_pct, hit, now, row.id});
        if (w.is_ok())
            ++updated;
    }
    return updated;
}

} // namespace fincept

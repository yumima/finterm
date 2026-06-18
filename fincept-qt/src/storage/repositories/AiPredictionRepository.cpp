// src/storage/repositories/AiPredictionRepository.cpp
#include "storage/repositories/AiPredictionRepository.h"

namespace fincept {

AiPredictionRepository& AiPredictionRepository::instance() {
    static AiPredictionRepository inst;
    return inst;
}

AiPrediction AiPredictionRepository::map_row(QSqlQuery& q) {
    AiPrediction p;
    p.id               = q.value(QStringLiteral("id")).toLongLong();
    p.ticker           = q.value(QStringLiteral("ticker")).toString();
    p.created_at       = q.value(QStringLiteral("created_at")).toString();
    p.model            = q.value(QStringLiteral("model")).toString();
    p.horizon_days     = q.value(QStringLiteral("horizon_days")).toInt();
    p.resolve_date     = q.value(QStringLiteral("resolve_date")).toString();
    p.price_at_pred    = q.value(QStringLiteral("price_at_pred")).toDouble();
    p.direction        = q.value(QStringLiteral("direction")).toString();
    p.target_price     = q.value(QStringLiteral("target_price")).toDouble();
    p.predicted_pct    = q.value(QStringLiteral("predicted_pct")).toDouble();
    p.confidence       = q.value(QStringLiteral("confidence")).toInt();
    p.recommendation   = q.value(QStringLiteral("recommendation")).toString();
    p.rationale        = q.value(QStringLiteral("rationale")).toString();
    p.analysis         = q.value(QStringLiteral("analysis")).toString();
    p.resolved         = q.value(QStringLiteral("resolved")).toInt() != 0;
    p.price_at_resolve = q.value(QStringLiteral("price_at_resolve")).toDouble();
    p.actual_pct       = q.value(QStringLiteral("actual_pct")).toDouble();
    p.correct          = q.value(QStringLiteral("correct")).toInt() != 0;
    p.resolved_at      = q.value(QStringLiteral("resolved_at")).toString();
    return p;
}

qint64 AiPredictionRepository::insert(const AiPrediction& p) {
    auto r = exec_insert(
        "INSERT INTO ai_predictions "
        "(ticker, created_at, model, horizon_days, resolve_date, price_at_pred, "
        " direction, target_price, predicted_pct, confidence, recommendation, "
        " rationale, analysis) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
        {p.ticker, p.created_at, p.model, p.horizon_days, p.resolve_date, p.price_at_pred,
         p.direction, p.target_price, p.predicted_pct, p.confidence, p.recommendation,
         p.rationale, p.analysis});
    return r.is_ok() ? r.value() : 0;
}

QVector<AiPrediction> AiPredictionRepository::for_ticker(const QString& ticker) const {
    auto r = query_list("SELECT * FROM ai_predictions WHERE ticker = ? ORDER BY created_at DESC",
                        {ticker.toUpper()}, map_row);
    return r.is_ok() ? r.value() : QVector<AiPrediction>{};
}

QVector<AiPrediction> AiPredictionRepository::unresolved() const {
    auto r = query_list("SELECT * FROM ai_predictions WHERE resolved = 0 ORDER BY resolve_date ASC",
                        {}, map_row);
    return r.is_ok() ? r.value() : QVector<AiPrediction>{};
}

bool AiPredictionRepository::exists_on_day(const QString& ticker, const QString& ymd) const {
    auto r = db().execute(
        "SELECT 1 FROM ai_predictions WHERE ticker = ? AND substr(created_at, 1, 10) = ? LIMIT 1",
        {ticker.toUpper(), ymd});
    return r.is_ok() && r.value().next();
}

void AiPredictionRepository::resolve(qint64 id, double price_at_resolve, double actual_pct,
                                     bool correct, const QString& resolved_at) {
    // Only ever writes the realized columns — the forecast itself is immutable.
    exec_write(
        "UPDATE ai_predictions SET resolved = 1, price_at_resolve = ?, actual_pct = ?, "
        "correct = ?, resolved_at = ? WHERE id = ? AND resolved = 0",
        {price_at_resolve, actual_pct, correct ? 1 : 0, resolved_at, id});
}

} // namespace fincept

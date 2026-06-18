// src/storage/repositories/AiPredictionRepository.h
#pragma once
#include "storage/repositories/BaseRepository.h"

namespace fincept {

/// One AI stock forecast. The prediction fields are write-once; only the
/// resolution fields are filled later (see AiPredictionRepository::resolve).
struct AiPrediction {
    qint64  id = 0;
    QString ticker;
    QString created_at;       // ISO-8601 UTC
    QString model;
    int     horizon_days = 7;
    QString resolve_date;     // YYYY-MM-DD
    double  price_at_pred = 0.0;
    QString direction;        // "up" | "down" | "flat"
    double  target_price = 0.0;
    double  predicted_pct = 0.0;
    int     confidence = 0;   // 0–100
    QString recommendation;   // buy | accumulate | hold | reduce | sell
    QString rationale;
    QString analysis;

    // Resolution (filled once when the horizon elapses).
    bool    resolved = false;
    double  price_at_resolve = 0.0;
    double  actual_pct = 0.0;
    bool    correct = false;
    QString resolved_at;
};

/// Append-only store of AI forecasts + their realized outcomes. The prediction
/// is immutable; resolve() only writes the realized columns, exactly once.
class AiPredictionRepository : public BaseRepository<AiPrediction> {
  public:
    static AiPredictionRepository& instance();

    /// Insert a new prediction. Returns the new row id (0 on failure).
    qint64 insert(const AiPrediction& p);

    /// All predictions for a ticker, newest first.
    QVector<AiPrediction> for_ticker(const QString& ticker) const;

    /// Every still-unresolved prediction (across all tickers) — the resolution
    /// sweep iterates these and fills the realized columns once their
    /// resolve_date has passed.
    QVector<AiPrediction> unresolved() const;

    /// True if a prediction for `ticker` was already created on calendar day
    /// `ymd` (YYYY-MM-DD). Used to keep the daily auto-forecast to one/day.
    bool exists_on_day(const QString& ticker, const QString& ymd) const;

    /// Record the realized outcome of a prediction. This is the ONLY mutation
    /// of an existing row, and it only touches the resolution columns.
    void resolve(qint64 id, double price_at_resolve, double actual_pct, bool correct,
                 const QString& resolved_at);

  private:
    AiPredictionRepository() = default;
    static AiPrediction map_row(QSqlQuery& q);
};

} // namespace fincept

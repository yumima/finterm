#pragma once

#include "services/equity/EquityResearchModels.h"

#include <QJsonObject>
#include <QStringList>

namespace fincept::services::equity::sentiment {

inline constexpr const char* kProviderId = "adanos-market-sentiment";

QStringList source_ids();
QString source_label(const QString& source_id);
bool source_has_signal(const SentimentSourceSnapshot& snapshot);
/// Single-ticker convenience: takes the first row, which is only correct when
/// the request asked for exactly one ticker.
SentimentSourceSnapshot parse_compare_payload(const QString& source_id, const QJsonObject& payload);
/// Ticker-aware parse. `/compare` accepts up to 10 tickers and sorts the rows
/// by buzz score, so with a batched request the first row is somebody else's
/// stock — the row has to be matched by its `ticker` field.
SentimentSourceSnapshot parse_compare_payload_for(const QString& source_id, const QString& ticker,
                                                  const QJsonObject& payload);
/// Every ticker present in a compare payload, so a caller can cache all of the
/// rows it already paid for rather than just the one it asked about.
QStringList tickers_in_compare_payload(const QJsonObject& payload);
QString compute_source_alignment(const QVector<SentimentSourceSnapshot>& sources);
QJsonObject snapshot_to_json(const MarketSentimentSnapshot& snapshot);
MarketSentimentSnapshot snapshot_from_json(const QJsonObject& object);

} // namespace fincept::services::equity::sentiment

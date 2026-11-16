// src/services/futures/FuturesService.h
#pragma once
// Fetches behind the Futures screen: the forward curve of one root (every
// listed month, one batch quote), where those months settled a week and a
// month ago, and the CFTC's weekly positioning for the market.
//
// Every callback runs on the GUI thread and is dropped if `ctx` is gone. A
// failure is `ok == false` with the reason — never an empty success.

#include "services/futures/FuturesMath.h"

#include <QObject>
#include <QStringList>

#include <functional>

namespace finterm::services::futures {

void fetch_curve(const QString& root, QObject* ctx, std::function<void(bool, Curve, QString)> cb);

void fetch_curve_history(const QStringList& contract_symbols, QObject* ctx,
                         std::function<void(bool, QVector<PastCurve>, QString)> cb);

/// Six months of a calendar spread (`far` minus `near`), on sessions both traded.
void fetch_spread_history(const QString& near_symbol, const QString& far_symbol, QObject* ctx,
                          std::function<void(bool, SpreadHistory, QString)> cb);

/// Three years of weekly reports. Published once a week (Friday, for the
/// Tuesday before), so a copy is kept for six hours.
void fetch_cot(const QString& cftc_code, QObject* ctx, std::function<void(bool, CotReport, QString)> cb);

} // namespace finterm::services::futures

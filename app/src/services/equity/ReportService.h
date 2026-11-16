// src/services/equity/ReportService.h
#pragma once
// Fetches only the ER Report tab needs: the last eight quarters with analyst
// rating changes (daemon `report_extras`), and excerpts of the company's
// latest 10-K and 10-Q (report_filings.py, SEC EDGAR).
//
// Callbacks run on the GUI thread and are dropped if `ctx` is gone. A failure
// is `ok == false` with the reason, never an empty success. Parsing lives
// with the report (screens/equity_research/ReportModel.h).

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace finterm::services::equity {

using ReportJsonCallback = std::function<void(bool ok, QJsonObject result, QString error)>;

void fetch_report_extras(const QString& symbol, QObject* ctx, ReportJsonCallback cb);

/// Slow the first time (a large filing takes half a minute to split); a copy
/// is kept for a day — filings change a few times a year.
void fetch_report_filings(const QString& symbol, QObject* ctx, ReportJsonCallback cb);

/// The companies nearest `symbol` in size within its Yahoo industry — a peer
/// group for a ticker nobody has hand-picked one for.
void fetch_industry_peers(const QString& symbol, QObject* ctx,
                          std::function<void(bool ok, QStringList peers, QString industry_or_error)> cb);

/// A section the model wrote for this company earlier today.
///
/// Kept by (section, symbol, day) rather than by prompt: the prompt carries
/// the live share price, so keyed on it a kept section would almost never be
/// found again and every visit would ask the model anew. The price of that is
/// stated to the reader — the report says when the prose was written.
struct KeptNarrative {
    QString reply;
    QString model;
    QDateTime written_at;
    bool valid() const { return !reply.isEmpty(); }
};
KeptNarrative kept_narrative(const QString& section, const QString& symbol);
/// Call only with a reply that parsed: an unreadable one is not worth a day.
void keep_narrative(const QString& section, const QString& symbol, const QString& reply, const QString& model);
void forget_narrative(const QString& section, const QString& symbol);

/// Remember what a report said (one per company per day, kept about a year),
/// and read back what earlier reports said — newest first, as (date, JSON).
void remember_report(const QString& symbol, const QDate& date, const QJsonObject& snapshot);
QVector<QJsonObject> remembered_reports(const QString& symbol);

} // namespace finterm::services::equity

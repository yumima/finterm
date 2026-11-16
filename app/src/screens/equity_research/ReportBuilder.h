// src/screens/equity_research/ReportBuilder.h
#pragma once
// Assembles the ER Report: everything the app holds on one company, laid out
// as a quarterly investor report — tiles, charts, short text — in HTML that a
// QTextBrowser shows, a printer turns into a PDF and a browser opens as a
// file. The same builder serves all three; only the palette and where the
// chart images go differ.

#include "screens/equity_research/ReportCharts.h"
#include "screens/equity_research/ReportModel.h"
#include "screens/equity_research/ReportPrompts.h"

#include <QDateTime>
#include <QImage>
#include <QString>
#include <QVector>

#include <functional>

namespace finterm::screens::report {

/// One input of the report and how it stands.
struct Source {
    enum class State { Pending, Ready, Failed, None };
    QString name;   ///< what it is, for the reader
    QString origin; ///< where it comes from
    State state = State::Pending;
    QString detail = {}; ///< as-of when ready, the reason when failed
};

struct ReportDoc {
    QString symbol;
    QDateTime generated;

    services::equity::StockInfo info;
    bool has_info = false;
    services::equity::FinancialsData financials;
    bool has_financials = false;
    services::equity::EarningsAnalysis earnings;
    bool has_earnings = false;
    QVector<services::equity::PeerData> peers; ///< the company's own row included
    QVector<services::equity::NewsArticle> news;
    services::equity::TechnicalsData technicals;
    bool has_technicals = false;
    QVector<services::equity::Candle> candles; ///< one year, daily
    QVector<services::equity::Candle> bench;   ///< the benchmark over the same year
    QString bench_symbol;
    Extras extras;
    bool has_extras = false;
    Filings filings;
    InsiderFlow insiders;

    BusinessNarrative business;
    SummaryNarrative summary;
    QString narrative_note; ///< why the prose is absent, or which model wrote it
    QString peers_basis;    ///< where the peer group comes from, in words
    QVector<Source> sources;
};

/// Receives each chart; returns what goes in its <img src>.
using ImageSink = std::function<QString(const QString& id, const QImage& image)>;

Inputs make_inputs(const ReportDoc& doc);

/// The figures handed to the model for the executive summary — every number
/// it is allowed to quote. Only finite values are listed.
QString figures_block(const ReportDoc& doc);

QVector<Headline> headlines(const ReportDoc& doc);

/// `width` is the content width in logical pixels.
QString build_report_html(const ReportDoc& doc, const Palette& pal, int width, qreal dpr, const ImageSink& sink);

/// The report as an A4 PDF on white. Returns the number of pages written, 0
/// when the file could not be produced.
int write_report_pdf(const ReportDoc& doc, const QString& path);

/// The report as one self-contained web page (charts embedded). False when
/// the file could not be written.
bool write_report_html(const ReportDoc& doc, const QString& path);

} // namespace finterm::screens::report

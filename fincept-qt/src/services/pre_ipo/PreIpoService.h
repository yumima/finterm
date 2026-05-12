// src/services/pre_ipo/PreIpoService.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

class QJsonObject;
class QJsonArray;

namespace fincept::services {

/// Singleton service for pre-IPO / private company data.
///
/// Data sources (free, no API key, public):
///   • SEC EDGAR Form D XML        — primary rounds, amount raised, exemption,
///                                   issuer address, related persons.
///   • SEC EDGAR S-1 / F-1         — IPO pipeline, amendment cadence.
///   • SEC EDGAR XBRL companyfacts — revenue, net income, gross margin for
///                                   companies that have filed S-1 with XBRL.
///   • SEC EDGAR N-PORT-P          — mutual-fund fair-value marks (Fidelity,
///                                   T. Rowe Price, BlackRock, Wellington, …).
///
/// Each source is fetched asynchronously via a Python script run by
/// `python::PythonRunner`; results are merged into `PrivateCompany`
/// dossiers keyed by slug id (CIK when known).
class PreIpoService : public QObject {
    Q_OBJECT
  public:
    static PreIpoService& instance();

    /// Trigger initial data load (idempotent).
    void load_data();

    /// Force a refresh (re-runs all fetchers). Emits data_loaded on completion.
    void refresh();

    QVector<pre_ipo::PrivateCompany> companies() const;
    pre_ipo::PrivateCompany company(const QString& id) const;
    void toggle_watch(const QString& id);

    QVector<pre_ipo::FormDFiling> recent_form_d() const;
    QVector<pre_ipo::S1Filing>    ipo_pipeline() const;
    QVector<pre_ipo::Signal>      signal_list() const;
    QVector<pre_ipo::FundEntry>   funds() const;

    bool is_loaded() const { return loaded_; }

  signals:
    void data_loaded(fincept::pre_ipo::PreIpoSummary summary);
    void company_updated(QString id);
    void error_occurred(QString message);
    void progress(QString message);  // "Loading Form D…", "Loading marks…"

  private:
    explicit PreIpoService(QObject* parent = nullptr);

    void run_form_d_fetch();
    void run_nport_marks_fetch();
    void run_s1_pipeline_fetch();

    void parse_form_d_response(const QJsonObject& root);
    void parse_marks_response(const QJsonObject& root);
    void parse_pipeline_response(const QJsonArray& arr);

    void recompute_analytics();
    /// Emit the current data state. Called both incrementally (as each of
    /// the 3 fetches lands) and finally (when all three are done).
    void emit_summary();
    /// Mark loading complete and persist cache. Called once when all
    /// pending bits clear.
    void finalize_load();

    // ── Persistent cache ────────────────────────────────────────────────────
    /// Directory where per-source raw JSON cache files live.
    QString cache_dir() const;
    /// Returns true if at least one cache file was successfully loaded and
    /// parsed; populates companies_/form_d_/pipeline_/funds_ from cache.
    bool   load_from_cache();
    /// Persist the most recent raw response for a single source.
    void   save_cache(const QString& filename, const QJsonDocument& doc);

    // Multi-stage loading guard. We wait for all 3 fetches before emitting,
    // and remember per-source success so a failed fetcher can be retried
    // independently rather than the whole load being marked permanent.
    enum FetchBit { FB_FormD = 1, FB_Marks = 2, FB_S1 = 4, FB_All = 7 };
    int  pending_bits_ = 0;
    int  failed_bits_  = 0;
    bool loading_ = false;

    QVector<pre_ipo::PrivateCompany> companies_;
    QVector<pre_ipo::FormDFiling>    form_d_;
    QVector<pre_ipo::S1Filing>       pipeline_;
    QVector<pre_ipo::Signal>         signals_;
    QVector<pre_ipo::FundEntry>      funds_;
    bool loaded_ = false;
};

} // namespace fincept::services

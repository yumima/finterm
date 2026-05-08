// src/services/pre_ipo/PreIpoService.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace fincept::services {

/// Singleton service for pre-IPO / private company data.
///
/// Data sources (free, no API key):
///   SEC EDGAR Form D — amount raised, date, exemption (no valuation/price)
///   SEC EDGAR S-1/F-1 — IPO pipeline, filing date, underwriters
///
/// Fields not available from free sources show "—" in the UI.
/// Crunchbase/PitchBook integrations (valuation, rounds detail) are
/// paid and not currently integrated.
class PreIpoService : public QObject {
    Q_OBJECT
  public:
    static PreIpoService& instance();

    // ── Data access ───────────────────────────────────────────────────────────

    /// Trigger initial data load (idempotent — safe to call multiple times).
    void load_data();

    /// All companies in the universe.
    QVector<pre_ipo::PrivateCompany> companies() const;

    /// Single company by slug id. Returns default-constructed if not found.
    pre_ipo::PrivateCompany company(const QString& id) const;

    /// Toggle the "watched" flag for a company. Emits company_updated(id).
    void toggle_watch(const QString& id);

    /// Recent Form D SEC filings.
    QVector<pre_ipo::FormDFiling> recent_form_d() const;

    /// Active IPO pipeline (S-1 filers).
    QVector<pre_ipo::S1Filing> ipo_pipeline() const;

    /// True after load_data() has successfully completed.
    bool is_loaded() const { return loaded_; }

  signals:
    void data_loaded(fincept::pre_ipo::PreIpoSummary summary);
    void company_updated(QString id);
    void error_occurred(QString message);

  private:
    explicit PreIpoService(QObject* parent = nullptr);

    void load_from_sec();
    void build_fallback_data();  // offline: known company names, all numeric fields = 0/empty
    void parse_sec_summary(const QJsonObject& root);
    void emit_loaded();

    QVector<pre_ipo::PrivateCompany> companies_;
    QVector<pre_ipo::FormDFiling>    form_d_;
    QVector<pre_ipo::S1Filing>       pipeline_;
    bool loaded_  = false;
    bool loading_ = false;   // guard against concurrent Python spawns
};

} // namespace fincept::services

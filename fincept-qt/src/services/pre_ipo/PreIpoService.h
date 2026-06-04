// src/services/pre_ipo/PreIpoService.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"
#include "services/finnhub/FinnhubService.h"

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

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

    /// Force a full refresh — bypasses the data-based cursors so every source
    /// re-fetches regardless of whether EDGAR shows new filings. Wired to the
    /// user's manual Refresh button. Emits data_loaded on completion.
    void refresh();

    QVector<pre_ipo::PrivateCompany> companies() const;
    /// Zero-copy view of the universe for read-only render paths (called per
    /// search keystroke). Valid only on the main thread between emits — the
    /// reference is invalidated by the next refresh. Use companies() when you
    /// need an owning snapshot.
    const QVector<pre_ipo::PrivateCompany>& companies_ref() const { return companies_; }
    pre_ipo::PrivateCompany company(const QString& id) const;
    void toggle_watch(const QString& id);

    QVector<pre_ipo::FormDFiling> recent_form_d() const;
    QVector<pre_ipo::S1Filing>    ipo_pipeline() const;
    QVector<pre_ipo::Signal>      signal_list() const;
    QVector<pre_ipo::FundEntry>   funds() const;

    /// Multi-exchange IPO calendar from Finnhub (NYSE + Nasdaq + AMEX). The
    /// existing Nasdaq run_nasdaq_ipo_fetch covers Nasdaq only — this fills
    /// in the rest. Empty when FINNHUB_API_KEY is not set.
    QVector<finnhub::FinnhubIPO>    finnhub_ipos()    const;
    /// Upcoming insider-share lockup expiries from Finnhub. Empty when no
    /// API key. The signature pre-IPO data we have nowhere else.
    QVector<finnhub::FinnhubLockup> finnhub_lockups() const;

    bool is_loaded() const { return loaded_; }

  signals:
    void data_loaded(fincept::pre_ipo::PreIpoSummary summary);
    void company_updated(QString id);
    void error_occurred(QString message);
    void progress(QString message);  // "Loading Form D…", "Loading marks…"

  private:
    explicit PreIpoService(QObject* parent = nullptr);

    /// Shared refresh path. `force` bypasses the per-source data cursors (full
    /// re-fetch); the daily timer and initial load call it with force=false so
    /// each source self-gates and only re-fetches when its EDGAR change marker
    /// actually moved.
    void refresh_internal(bool force);

    void run_form_d_fetch();
    void run_nport_marks_fetch();
    void run_s1_pipeline_fetch();
    /// Deep SPV secondary-interest scan (targeted EDGAR full-text searches +
    /// capped XML enrichment). Independent of the 3-bit finalize_load flow —
    /// a slow/failed scan never blocks the core Form D / N-PORT / S-1 load.
    void run_spv_fetch();
    /// Finnhub augmentation — runs alongside the 3-bit SEC flow. Independent
    /// of finalize_load gating: completes when it completes, emits a summary
    /// progressively. No-op when FINNHUB_API_KEY is unset.
    void run_finnhub_calendar_fetch();
    /// Fetch upcoming + recently priced IPOs from the Nasdaq public calendar
    /// API (no auth). Enriches pipeline_ with confirmed IPO dates and adds
    /// any companies not already in the EDGAR pipeline. Independent of the
    /// 3-bit finalize_load flow — emits when done regardless of other fetches.
    void run_nasdaq_ipo_fetch(const QString& yyyymm);

    /// Arm a one-shot timer that fires at the next 05:00 America/New_York
    /// to pre-warm the cache before the user opens the screen. Re-arms
    /// itself on each fire so the cadence holds across DST transitions
    /// without manual offset math.
    void schedule_next_daily_refresh();

    void parse_form_d_response(const QJsonObject& root);
    void parse_marks_response(const QJsonObject& root);
    void parse_pipeline_response(const QJsonArray& arr);
    /// Deserialize the deep SPV scan into spv_raw_ (no company mutation).
    void parse_spv_response(const QJsonObject& root);
    /// Re-join spv_raw_ onto companies_ (clearing+rebuilding each company's
    /// spv_activity), creating stubs for targets not otherwise tracked. Called
    /// from emit_summary() so the join survives any fetch ordering.
    void attach_spv_activity();

    void recompute_analytics();

    // ── Curated valuation seed ────────────────────────────────────────────────
    // SEC Form D / N-PORT don't disclose share price or shares outstanding, so a
    // headline post-money valuation can't be derived from filings. A small hand-
    // maintained JSON (scripts/pre_ipo_valuation_seed.json) supplies last-
    // reported valuations for marquee names, labelled "as reported" in the UI.
    struct ValuationSeed {
        QString id, name, cik;
        QStringList aliases;
        double  last_valuation_usd = 0;
        QDate   as_of;
        QString source_label, source_url, round_name;
        QString sector, description, hq_city, hq_state, hq_country;
        int     founded_year = 0;
        QStringList key_investors, tags;
    };
    /// Load the seed file once (tolerant of a missing/invalid file).
    void load_valuation_seed();
    /// Reconcile seed names into companies_ BEFORE recompute_analytics():
    /// merge fragmented duplicates (canonical-slug stub vs Form-D legal-name
    /// entry), add canonical aliases, create stubs for seeded names with no SEC
    /// footprint, and fill empty descriptive fields. Idempotent across emits.
    void seed_ensure_companies();
    /// Overlay last_valuation_usd + seed_* provenance AFTER recompute_analytics()
    /// (which resets last_valuation_usd to 0 every emit).
    void seed_apply_valuation();
    /// Best single company match for a seed (CIK → id/alias-exact → fuzzy
    /// name). -1 when no match.
    int  find_company_for_seed(const ValuationSeed& s) const;
    /// Normalized-name set used for the FUZZY (last-resort) match tier: the
    /// canonical name plus only multi-word aliases. Single-token aliases are
    /// excluded so a generic word (e.g. "figure") can't bind a seed to an
    /// unrelated company; they still match exactly via id/alias tiers.
    QSet<QString> seed_fuzzy_norms(const ValuationSeed& s) const;

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

    // ── Data-based change cursors ─────────────────────────────────────────────
    // Per-source opaque markers (EDGAR all-time hit counts / newest accessions)
    // persisted to cursors.json. Each fetch passes its prior cursor to Python,
    // which short-circuits with {"unchanged":true} when the marker hasn't moved
    // — so a quiet day costs a cheap probe, not a full scan. Replaces the old
    // single 24h time-to-live gate.
    void   load_cursors();
    void   save_cursors();
    /// Compact JSON payload for a Python action, injecting the stored prior
    /// cursor for `src` as "prev_cursor" (omitted when force_refresh_ is set).
    QString cursor_payload(QJsonObject base, const QString& src) const;

    // Multi-stage loading guard. We wait for all 3 fetches before emitting,
    // and remember per-source success so a failed fetcher can be retried
    // independently rather than the whole load being marked permanent.
    enum FetchBit { FB_FormD = 1, FB_Marks = 2, FB_S1 = 4, FB_All = 7 };
    int  pending_bits_ = 0;
    int  failed_bits_  = 0;
    bool loading_ = false;
    bool force_refresh_ = false;     // set for the duration of a forced refresh
    QJsonObject cursors_;            // per-source change markers (cursors.json)

    QVector<pre_ipo::PrivateCompany> companies_;
    QVector<ValuationSeed>           seed_;      // curated valuation overlay
    bool                             seed_loaded_ = false;
    QVector<pre_ipo::SpvActivity>    spv_raw_;   // side table, re-joined each emit
    QVector<pre_ipo::FormDFiling>    form_d_;
    QVector<pre_ipo::S1Filing>       pipeline_;
    QVector<pre_ipo::Signal>         signals_;
    QVector<pre_ipo::FundEntry>      funds_;
    QVector<finnhub::FinnhubIPO>     finnhub_ipos_;
    QVector<finnhub::FinnhubLockup>  finnhub_lockups_;
    bool loaded_ = false;
};

} // namespace fincept::services

// src/screens/power_trader/PowerTraderService.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace fincept::power_trader {

/// Singleton service owning all STOCK Act congressional trade data and analytics.
class PowerTraderService : public QObject {
    Q_OBJECT
  public:
    static PowerTraderService& instance();

    void load_data();

    /// Active date-range cutoff for trades (in days). Default 90.
    int  days_back() const { return days_back_; }

    /// Change the date-range cutoff. Invalidates any cached summary and
    /// triggers a fresh load — the source scrapes from this many days back
    /// from today. No-op if `days` is the same as the active value. The
    /// chosen value is persisted (QSettings) so it survives restarts.
    void set_days_back(int days);

    // ── Raw data ──────────────────────────────────────────────────────────────
    QVector<CongressMember> members() const;
    QVector<PoliticalTrade> trades()  const;
    QVector<PoliticalTrade> trades_for_member(const QString& member_id) const;
    QVector<PoliticalTrade> trades_for_ticker(const QString& ticker)    const;

    bool      is_loaded()    const { return summary_.loaded; }
    QDateTime last_updated() const { return summary_.last_updated; }

    // ── Portfolio analytics ───────────────────────────────────────────────────
    /// Reconstruct estimated portfolio from trade history.
    /// Amount ranges are approximated via their midpoints (standard industry
    /// practice — see Capitol Trades, Quiver Quant, Senate Stock Watcher).
    MemberPortfolio compute_portfolio(const QString& member_id) const;

    // ── Big-picture analytics ─────────────────────────────────────────────────
    /// Sector exposure across ALL members (sorted by total_est_amount desc).
    QVector<SectorExposure> sector_breakdown() const;

    /// Committee → sector insider correlation signals.
    QVector<CommitteeInsiderSignal> committee_insider_signals() const;

    /// Grouped view: one CommitteeGroup per committee found in member data.
    QVector<CommitteeGroup> committee_groups() const;

    /// Aggregate stats for a single party ("D", "R", "I").
    PartyStats party_stats(const QString& party) const;

    /// All unique committee names found in current member data (sorted).
    QStringList available_committees() const;

    /// All trades where committee_relevance == committee (or any if committee is empty).
    QVector<PoliticalTrade> trades_by_committee(const QString& committee) const;

    /// Filter summary by body (Senate / House only).
    PowerTraderSummary filtered_summary(BodyFilter body) const;

    // ── Insider watch ─────────────────────────────────────────────────────────
    /// Compute multi-factor insider watch list, sorted by insider_score desc.
    QVector<InsiderWatchEntry> insider_watch_list() const;

    // ── Signal Builder ────────────────────────────────────────────────────────
    /// Decompose every trade in the current summary into its 8 raw factor scores.
    /// Results are indexed by trade_id and used by SignalBuilderPanel for
    /// real-time score preview when the user adjusts factor weights.
    QVector<TradeFactorScores> compute_trade_base_scores() const;

    /// Built-in presets exposed for the Signal Builder preset selector.
    static QVector<SignalPreset> builtin_presets();

    // ── Rankings ──────────────────────────────────────────────────────────────
    /// Return all members ranked by the given dimension (rank 1 = best/highest).
    QVector<RankedMember> ranked_members(RankingDimension dim) const;

    // ── Per-member derived stats ──────────────────────────────────────────────
    double net_buyer_amount(const QString& member_id, int days = 90) const;
    double avg_signal_score(const QString& member_id) const;
    double avg_disclosure_lag(const QString& member_id) const;
    /// Ticker + estimated gain% for the member's best single position.
    QPair<QString, double> best_pick(const QString& member_id) const;

    // ── Cabinet (OGE Form 278) ────────────────────────────────────────────────
    void load_cabinet_data();
    bool is_cabinet_loaded() const { return cabinet_.loaded; }
    CabinetSummary cabinet_summary() const { return cabinet_; }
    /// Members sorted by conflict_score descending.
    QVector<CabinetMember> cabinet_conflict_ranking() const;
    /// Cabinet-wide sector exposure (sum of all member holdings by sector).
    QVector<SectorExposure> cabinet_sector_exposure() const;

    /// Real close on or before @p date for @p ticker, or 0 if unavailable.
    /// Current price is just close_on_or_before(today) — the latest real close.
    /// Used by the per-trade drill-down for the "price since the trade" read;
    /// returns 0 (never a fabricated value) when the daily series doesn't
    /// cover the ticker/date.
    double close_on_or_before(const QString& ticker, const QDate& date) const;

  signals:
    void data_loaded(fincept::power_trader::PowerTraderSummary summary);
    void cabinet_data_loaded(fincept::power_trader::CabinetSummary summary);
    void error_occurred(QString error);

  private:
    explicit PowerTraderService(QObject* parent = nullptr);
    Q_DISABLE_COPY(PowerTraderService)

    void on_daemon_response(bool ok, const QJsonObject& result, const QString& error);
    void parse_summary(const QJsonObject& root);
    void parse_cabinet(const QJsonObject& root);

    // ── Real pricing for member returns ───────────────────────────────────────
    // Disclosures report only dollar ranges + dates (no shares/prices), so real
    // P&L is derived by valuing each trade at the real close on its transaction
    // date (shares = $-midpoint / close) and the current price. Fetched async
    // from the yfinance daemon after parse_summary; re-emits data_loaded when
    // ready. Missing prices → that holding/member stays "unpriced" (UI shows
    // "—"); nothing is ever fabricated.
    void   fetch_real_prices();
    void   recompute_member_returns();

    QHash<QString, QMap<QDate, double>>   close_history_;   // ticker → date→close
    qint64 price_epoch_ = 0;  // bumps each load so stale price callbacks drop

    PowerTraderSummary summary_;
    CabinetSummary     cabinet_;
    bool loading_         = false;
    bool cabinet_loading_ = false;
    int  days_back_       = 90;

    QTimer* refresh_timer_ = nullptr;
    static constexpr int kRefreshIntervalMs = 6 * 60 * 60 * 1000;

    // ── Sector classification ─────────────────────────────────────────────────
    static const QHash<QString, QString>& ticker_sector_map();
    static const QHash<QString, QString>& ticker_committee_map();
};

} // namespace fincept::power_trader

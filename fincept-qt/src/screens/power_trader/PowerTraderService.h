// src/screens/power_trader/PowerTraderService.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QJsonObject>
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
    void generate_mock_data();

    static CongressMember make_member(const QString& id, const QString& name,
                                      const QString& party, MemberChamber chamber,
                                      const QString& state, const QStringList& committees,
                                      double net_worth, int trades_ytd,
                                      double portfolio_ret, double spy_ret);

    static PoliticalTrade make_trade(int idx, const CongressMember& member,
                                     const QString& ticker, const QString& asset_name,
                                     TradeDirection direction,
                                     double amount_low, double amount_high,
                                     int txn_days_ago, int lag_days,
                                     double signal_score,
                                     AssetType asset_type = AssetType::Stock);

    PowerTraderSummary summary_;
    CabinetSummary     cabinet_;
    bool loading_         = false;
    bool cabinet_loading_ = false;

    QTimer* refresh_timer_ = nullptr;
    static constexpr int kRefreshIntervalMs = 6 * 60 * 60 * 1000;

    // ── Sector classification ─────────────────────────────────────────────────
    static const QHash<QString, QString>& ticker_sector_map();
    static const QHash<QString, QString>& ticker_committee_map();
};

} // namespace fincept::power_trader

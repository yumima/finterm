// src/screens/power_trader/PowerTraderService.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace fincept::power_trader {

/// Singleton service that owns STOCK Act congressional trade data.
///
/// Currently implemented as a stub that generates realistic mock data so the
/// screen is fully functional before the real Python Senate EFTS pipeline
/// exists. Swapping in real data requires only replacing generate_mock_data()
/// with a PythonWorker::submit("senate_disclosures", ...) call — the rest of
/// the class (signals, cache, refresh timer) is production-ready as-is.
class PowerTraderService : public QObject {
    Q_OBJECT
  public:
    static PowerTraderService& instance();

    // ── Data access ───────────────────────────────────────────────────────────

    /// Trigger a data load. Emits data_loaded() on success, error_occurred() on
    /// failure. Does nothing if a fresh load is already in-flight.
    void load_data();

    QVector<CongressMember> members() const;
    QVector<PoliticalTrade> trades() const;

    QVector<PoliticalTrade> trades_for_member(const QString& member_id) const;
    QVector<PoliticalTrade> trades_for_ticker(const QString& ticker) const;

    bool is_loaded() const { return summary_.loaded; }
    QDateTime last_updated() const { return summary_.last_updated; }

  signals:
    void data_loaded(fincept::power_trader::PowerTraderSummary summary);
    void error_occurred(QString error);

  private:
    explicit PowerTraderService(QObject* parent = nullptr);
    Q_DISABLE_COPY(PowerTraderService)

    void on_daemon_response(bool ok, const QJsonObject& result, const QString& error);
    void parse_summary(const QJsonObject& root);

    // Stub implementation — generates 20 members + 100 trades
    void generate_mock_data();

    static CongressMember make_member(const QString& id,
                                      const QString& name,
                                      const QString& party,
                                      MemberChamber chamber,
                                      const QString& state,
                                      const QStringList& committees,
                                      double net_worth,
                                      int trades_ytd,
                                      double portfolio_ret,
                                      double spy_ret);

    static PoliticalTrade make_trade(int idx,
                                     const CongressMember& member,
                                     const QString& ticker,
                                     const QString& asset_name,
                                     TradeDirection direction,
                                     double amount_low,
                                     double amount_high,
                                     int txn_days_ago,
                                     int lag_days,
                                     double signal_score,
                                     AssetType asset_type = AssetType::Stock);

    PowerTraderSummary summary_;
    bool loading_ = false;

    // Auto-refresh every 6 hours
    QTimer* refresh_timer_ = nullptr;
    static constexpr int kRefreshIntervalMs = 6 * 60 * 60 * 1000;
};

} // namespace fincept::power_trader

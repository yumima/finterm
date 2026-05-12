// src/screens/power_trader/TradesFeedPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QComboBox>
#include <QDateEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// Center panel: filterable chronological feed of political trades.
class TradesFeedPanel : public QWidget {
    Q_OBJECT
  public:
    explicit TradesFeedPanel(QWidget* parent = nullptr);

    void set_trades(const QVector<power_trader::PoliticalTrade>& trades);
    void set_selected_member(const QString& member_id);
    void set_available_committees(const QStringList& committees);

    // Subscriptions: trades feed can highlight + filter to "followed" members
    // (member watchlist) and "followed" tickers. The two sets are owned by
    // PowerTraderScreen and pushed in via these setters; the SUBS pill in
    // this panel's filter row narrows visible rows to subscribed entities.
    void set_member_watchlist(const QSet<QString>& watched);
    void set_ticker_watchlist(const QSet<QString>& tickers);

  signals:
    void member_selected(QString member_id);
    void ticker_subscription_toggled(QString ticker);

  private:
    void build_ui();
    void apply_filters();
    void populate_table(const QVector<power_trader::PoliticalTrade>& visible);

    QLineEdit*   ticker_filter_    = nullptr;
    QComboBox*   party_filter_     = nullptr;
    QComboBox*   chamber_filter_   = nullptr;
    QComboBox*   committee_filter_ = nullptr;
    QDateEdit*   date_from_        = nullptr;
    QDateEdit*   date_to_          = nullptr;
    QPushButton* subs_filter_      = nullptr;  // SUBS toggle pill
    QTableWidget* table_           = nullptr;

    QVector<power_trader::PoliticalTrade> trades_;
    QString selected_member_id_;
    QSet<QString> member_watchlist_;
    QSet<QString> ticker_watchlist_;
};

} // namespace fincept::screens

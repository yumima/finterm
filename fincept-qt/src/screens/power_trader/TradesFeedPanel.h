// src/screens/power_trader/TradesFeedPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QComboBox>
#include <QDateEdit>
#include <QLineEdit>
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

  signals:
    void member_selected(QString member_id);

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
    QTableWidget* table_           = nullptr;

    QVector<power_trader::PoliticalTrade> trades_;
    QString selected_member_id_;
};

} // namespace fincept::screens

// src/screens/power_trader/LeaderboardPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// Left panel: ranked list of congress members by YTD alpha.
class LeaderboardPanel : public QWidget {
    Q_OBJECT
  public:
    explicit LeaderboardPanel(QWidget* parent = nullptr);

    void set_members(const QVector<power_trader::CongressMember>& members);
    void set_selected(const QString& member_id);

  signals:
    void member_selected(QString member_id);

  private:
    void build_ui();
    void populate_table();

    static QString format_pct(double v);

    QTableWidget* table_       = nullptr;
    QVector<power_trader::CongressMember> members_;
    QString selected_id_;
};

} // namespace fincept::screens

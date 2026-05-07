// src/screens/power_trader/MemberDetailPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QScrollArea>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// Right panel: per-member detail — header, stats, committees, trade history.
class MemberDetailPanel : public QWidget {
    Q_OBJECT
  public:
    explicit MemberDetailPanel(QWidget* parent = nullptr);

    void set_member(const power_trader::CongressMember& member,
                    const QVector<power_trader::PoliticalTrade>& trades);
    void clear();

  private:
    void build_ui();
    void populate(const power_trader::CongressMember& member,
                  const QVector<power_trader::PoliticalTrade>& trades);
    void show_placeholder();

    QWidget*      placeholder_    = nullptr;
    QScrollArea*  scroll_area_    = nullptr;
    QWidget*      detail_widget_  = nullptr;

    // Header area
    QLabel*       name_label_     = nullptr;
    QLabel*       party_badge_    = nullptr;
    QLabel*       meta_label_     = nullptr;   // chamber + state

    // Stats row
    QLabel*       alpha_label_    = nullptr;
    QLabel*       return_label_   = nullptr;
    QLabel*       trades_label_   = nullptr;
    QLabel*       net_worth_label_= nullptr;

    // Committees
    QLabel*       committees_label_ = nullptr;

    // Trade history table
    QTableWidget* hist_table_     = nullptr;
};

} // namespace fincept::screens

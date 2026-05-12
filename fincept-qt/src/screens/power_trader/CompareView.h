// src/screens/power_trader/CompareView.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <array>

namespace fincept::screens {

/// Side-by-side compact comparison of 2-3 Congress members.
///
/// Three numbered slots. Each slot renders a stripped-down member card
/// (header, 2x2 stat tiles, top committee, top 5 holdings, last 5 trades).
/// Members are pushed in via add_member(id); the slot count auto-grows up
/// to three. The clear button drops everyone.
class CompareView : public QWidget {
    Q_OBJECT
  public:
    explicit CompareView(QWidget* parent = nullptr);

    void set_summary(const power_trader::PowerTraderSummary& summary);
    void add_member(const QString& member_id);
    void clear();

  signals:
    void member_selected(QString member_id);

  private:
    void build_ui();
    void rebuild_cards();
    QWidget* build_card(int slot, const QString& member_id);
    QWidget* build_empty_slot(int slot);

    power_trader::PowerTraderSummary summary_;
    std::array<QString, 3> slot_member_ids_{};
    QLabel*      empty_label_  = nullptr;
    QWidget*     cards_row_    = nullptr;
    QHBoxLayout* cards_layout_ = nullptr;
    QPushButton* clear_btn_    = nullptr;
};

} // namespace fincept::screens

// src/screens/dashboard/widgets/OwnershipCalendarWidget.h
#pragma once

#include "screens/dashboard/widgets/BaseWidget.h"

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens::widgets {

/// Ownership Calendar — the dated events that move the ownership numbers.
///
/// Three clocks, one list: the 13F filing deadline (45 days after quarter
/// end — the day the holder table can change), FINRA's short-interest
/// publication dates (about ten business days after each settlement), and
/// IPO lock-up expiries (the customary 180 days after pricing, the day a
/// float can double). Soonest first, each row saying what it is and which
/// underlying date it was computed from.
class OwnershipCalendarWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit OwnershipCalendarWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void apply_styles();
    void refresh_data();
    void populate();

    QWidget*     header_widget_ = nullptr;
    QFrame*      header_sep_    = nullptr;
    QScrollArea* scroll_area_   = nullptr;
    QVBoxLayout* list_layout_   = nullptr;
    QLabel*      status_label_  = nullptr;
    QVector<QLabel*> header_labels_;
};

} // namespace fincept::screens::widgets

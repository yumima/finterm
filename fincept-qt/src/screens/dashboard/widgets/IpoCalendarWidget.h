// src/screens/dashboard/widgets/IpoCalendarWidget.h
#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"

#include <QDate>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens::widgets {

/// IPO Calendar Widget — fetches upcoming and recently priced IPOs from
/// the Nasdaq public calendar API (no API key required).
/// Shows this month and next month; sections: UPCOMING then PRICED.
class IpoCalendarWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit IpoCalendarWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;

  private:
    void apply_styles();
    void refresh_data();
    void fetch_month(const QString& yyyymm);
    void on_fetch_done();
    void populate();

    struct IpoEntry {
        QString company;
        QString ticker;
        QString exchange;  // listing exchange (tooltip on ticker)
        QString date_str;
        QDate   date;
        QString price_range;
        QString status; // "upcoming" | "priced"
    };

    QWidget*     header_widget_ = nullptr;
    QFrame*      header_sep_    = nullptr;
    QScrollArea* scroll_area_   = nullptr;
    QVBoxLayout* list_layout_   = nullptr;
    QLabel*      status_label_  = nullptr;
    QVector<QLabel*> header_labels_;

    QVector<IpoEntry> entries_;
    int pending_fetches_ = 0;
    int ok_fetches_ = 0; // how many of this batch's HTTP calls succeeded
};

inline BaseWidget* create_ipo_calendar_widget() {
    return new IpoCalendarWidget;
}

} // namespace fincept::screens::widgets

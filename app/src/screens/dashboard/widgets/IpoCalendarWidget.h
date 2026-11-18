// src/screens/dashboard/widgets/IpoCalendarWidget.h
#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"

#include <QDate>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace finterm::screens::widgets {

/// IPO Calendar Widget — upcoming (next 60 days) and recently priced (last
/// 31 days) IPOs from services::IpoDealsFeed, the same calendar PRIVATE
/// MARKET and What's On show. Sections: UPCOMING then PRICED.
class IpoCalendarWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit IpoCalendarWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;

  private:
    void apply_styles();
    void refresh_data();
    /// The rows from the shared IPO feed (services::IpoDealsFeed).
    void rebuild_entries();
    void populate();

    struct IpoEntry {
        QString company;
        QString ticker;
        QString exchange; // listing exchange (tooltip on ticker)
        QString date_str;
        QDate date;
        QString price_range;
        QString status; // "upcoming" | "priced"
    };

    QWidget* header_widget_ = nullptr;
    QFrame* header_sep_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QVBoxLayout* list_layout_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVector<QLabel*> header_labels_;

    QVector<IpoEntry> entries_;
};

inline BaseWidget* create_ipo_calendar_widget() {
    return new IpoCalendarWidget;
}

} // namespace finterm::screens::widgets

#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"

#include <QJsonArray>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens::widgets {

/// Economic Calendar Widget — fetches this week + next week macro events from
/// ForexFactory's public JSON feed (no API key required).
class EconomicCalendarWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit EconomicCalendarWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;

  private:
    void apply_styles();
    void refresh_data();
    void on_fetch_done(const QJsonArray& week_events);
    void populate(const QJsonArray& events);

    QWidget* header_widget_ = nullptr;
    QFrame* header_sep_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QVBoxLayout* list_layout_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVector<QLabel*> header_labels_;
    QJsonArray last_events_;   // cached for theme-change re-populate
    QJsonArray pending_merge_; // accumulates results from both week fetches
    int pending_fetches_ = 0;  // counts in-flight; populate when it reaches 0
    int ok_fetches_ = 0;       // how many of this batch's HTTP calls succeeded
};

} // namespace fincept::screens::widgets

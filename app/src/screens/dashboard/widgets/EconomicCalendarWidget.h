#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"

#include <QJsonArray>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace finterm::screens::widgets {

/// Economic Calendar Widget — this week's macro events from ForexFactory's
/// public JSON feed (no API key), via the shared, cached fetch in
/// services/calendar/MacroCalendarFeed (the feed rate-limits).
class EconomicCalendarWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit EconomicCalendarWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;

  private:
    void apply_styles();
    void refresh_data(bool force = false);
    void on_fetch_done(const QJsonArray& week_events, bool stale);
    void populate(const QJsonArray& events);

    QWidget* header_widget_ = nullptr;
    QFrame* header_sep_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QVBoxLayout* list_layout_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVector<QLabel*> header_labels_;
    QJsonArray last_events_; // cached for theme-change re-populate
};

} // namespace finterm::screens::widgets

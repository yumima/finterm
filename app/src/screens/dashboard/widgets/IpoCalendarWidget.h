// src/screens/dashboard/widgets/IpoCalendarWidget.h
#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"

#include <QDate>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace finterm::screens::widgets {

/// IPO Calendar Widget — the same calendar and the same windows as PRIVATE
/// MARKET › IPO CALENDAR (services::IpoDealsFeed): PRICED in the last 30
/// days, and deals expected to price in the NEXT 7 / NEXT 30 DAYS — scheduled
/// on the exchange calendar, or estimated for a filed deal from its SEC
/// stage. One tab at a time; an empty window says so.
class IpoCalendarWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit IpoCalendarWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);

    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

  protected:
    void on_theme_changed() override;

  private:
    enum class Window { Priced, Next7, Next30 };

    void apply_styles();
    void refresh_data();
    /// The rows from the shared IPO feed (services::IpoDealsFeed).
    void rebuild_entries();
    void populate();
    void set_window(Window w);

    struct IpoEntry {
        QString company;
        QString ticker;
        QString exchange; // listing exchange (tooltip on ticker)
        QDate date;       // pricing date (priced) or projected pricing date (pending)
        bool pending = false;
        bool firm = false; // scheduled / imminent, not an estimate
        QString basis;     // what an estimate rests on
        QString price_range;
    };
    /// Whether `e` falls in `w` (the screen's rules: a pending deal by its
    /// projected date, a priced one by its pricing date).
    static bool in_window(const IpoEntry& e, Window w);
    int count_in(Window w) const;

    QWidget* header_widget_ = nullptr;
    QFrame* header_sep_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QVBoxLayout* list_layout_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* date_header_ = nullptr;
    QVector<QLabel*> header_labels_;
    QPushButton* tab_priced_ = nullptr;
    QPushButton* tab_next7_ = nullptr;
    QPushButton* tab_next30_ = nullptr;

    Window window_ = Window::Next7;
    QVector<IpoEntry> entries_;
    int undated_pending_ = 0; ///< filed or scheduled, but no date to place it in a window
    bool partial_ = false;    ///< some months of the calendar were not read at all
};

} // namespace finterm::screens::widgets

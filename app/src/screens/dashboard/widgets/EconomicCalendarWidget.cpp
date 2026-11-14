#include "screens/dashboard/widgets/EconomicCalendarWidget.h"

#include "services/calendar/MacroCalendar.h"
#include "services/calendar/MacroCalendarFeed.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace finterm::screens::widgets {

EconomicCalendarWidget::EconomicCalendarWidget(QWidget* parent)
    : BaseWidget("ECONOMIC CALENDAR", parent, ui::colors::CYAN()) {
    auto* vl = content_layout();
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // Column headers
    header_widget_ = new QWidget(this);
    auto* hl = new QHBoxLayout(header_widget_);
    hl->setContentsMargins(8, 4, 8, 4);

    auto make_hdr = [&](const QString& text, int stretch, Qt::Alignment align = Qt::AlignLeft) {
        auto* lbl = new QLabel(text);
        lbl->setAlignment(align);
        header_labels_.append(lbl);
        hl->addWidget(lbl, stretch);
    };
    make_hdr("EVENT", 4);
    make_hdr("CTY", 1);
    make_hdr("WHEN", 2);
    make_hdr("FCST", 1, Qt::AlignRight);
    make_hdr("PREV", 1, Qt::AlignRight);
    make_hdr("IMP", 1, Qt::AlignRight);
    vl->addWidget(header_widget_);

    header_sep_ = new QFrame;
    header_sep_->setFixedHeight(1);
    vl->addWidget(header_sep_);

    // Status label lives OUTSIDE the scrollable list — never deleted by populate().
    status_label_ = new QLabel("Loading…");
    status_label_->setAlignment(Qt::AlignCenter);
    vl->addWidget(status_label_);

    // Scrollable list
    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);
    scroll_area_->hide();

    auto* list_widget = new QWidget(this);
    list_widget->setStyleSheet("background: transparent;");
    list_layout_ = new QVBoxLayout(list_widget);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);
    list_layout_->addStretch(); // rows added by populate()

    scroll_area_->setWidget(list_widget);
    vl->addWidget(scroll_area_, 1);

    // The refresh button means "now": skip the 30-minute fresh cache.
    connect(this, &BaseWidget::refresh_requested, this, [this]() { refresh_data(true); });

    apply_styles();
    set_loading(true);
    refresh_data();
}

void EconomicCalendarWidget::apply_styles() {
    header_widget_->setStyleSheet(QString("background: %1;").arg(ui::colors::BG_RAISED()));
    for (auto* lbl : header_labels_)
        lbl->setStyleSheet(
            QString("color: %1; font-weight: bold; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
    header_sep_->setStyleSheet(QString("background: %1;").arg(ui::colors::BORDER_DIM()));
    scroll_area_->setStyleSheet(
        QString("QScrollArea { border: none; background: transparent; }"
                "QScrollBar:vertical { width: 4px; background: transparent; }"
                "QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(ui::colors::BORDER_MED()));
    status_label_->setStyleSheet(
        QString("color: %1; padding: 16px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
}

void EconomicCalendarWidget::on_theme_changed() {
    apply_styles();
    if (!last_events_.isEmpty())
        populate(last_events_);
}

void EconomicCalendarWidget::refresh_data(bool force) {
    set_loading(true);
    // Shared with News › WHAT'S ON; cached, coalesced, last-good on refusal.
    finterm::macro_calendar::fetch_week(this, [this](const finterm::macro_calendar::FeedResult& r) {
        if (!r.ok) {
            // A failed fetch is not an empty calendar: say so, keep last_events_
            // so a theme change still re-renders previously loaded rows.
            set_loading(false);
            scroll_area_->setVisible(false);
            status_label_->setText("Couldn't load economic calendar");
            status_label_->setVisible(true);
            return;
        }
        on_fetch_done(r.rows, r.stale);
    }, force);
}

void EconomicCalendarWidget::on_fetch_done(const QJsonArray& week_events, bool stale) {
    // ForexFactory "date" is an ISO instant with offset; parse_ff() reads it
    // (it used to be parsed as "MMM d yyyy", which failed for every row: the
    // raw ISO string was shown and nothing sorted or filtered). Show today
    // onward, chronologically — the feed is Sunday-to-Saturday, so without
    // the filter Friday still opened on Sunday's events.
    namespace mc = finterm::macro_calendar;
    const auto events = mc::today_onward(mc::parse_ff(week_events), QDateTime::currentDateTime());
    const QDate today = QDate::currentDate();
    QJsonArray out;
    for (const auto& e : events) {
        QJsonObject mapped;
        mapped["event"] = e.title;
        mapped["country"] = e.country;
        mapped["when"] = mc::short_when(e.when, e.timed, today);
        mapped["when_full"] = e.when.toLocalTime().toString(QStringLiteral("ddd d MMM yyyy HH:mm t"));
        mapped["importance"] = e.importance;
        mapped["actual"] = e.actual;
        mapped["forecast"] = e.forecast;
        mapped["previous"] = e.previous;
        out.append(mapped);
    }

    set_loading(false);
    populate(out);
    setToolTip(stale ? QStringLiteral("ForexFactory refused the refresh; showing the last copy fetched.")
                     : QString());
}

void EconomicCalendarWidget::populate(const QJsonArray& events) {
    last_events_ = events;

    // Clear data rows — status_label_ is in the parent layout, never touched here.
    while (list_layout_->count() > 0) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (events.isEmpty()) {
        status_label_->setText("No more events this week");
        status_label_->setVisible(true);
        scroll_area_->setVisible(false);
        list_layout_->addStretch();
        return;
    }

    status_label_->setVisible(false);
    scroll_area_->setVisible(true);

    bool alt = false;
    int count = 0;

    for (const auto& v : events) {
        if (count >= 25)
            break;
        auto e = v.toObject();

        // Real fields: event, country, date, time, importance, actual, forecast, previous
        QString event_name = e["event"].toString().trimmed();
        if (event_name.isEmpty())
            continue;

        QString country = e["country"].toString().toUpper();
        const QString date_display = e["when"].toString();
        QString actual = e["actual"].toString().trimmed();
        QString forecast = e["forecast"].toString().trimmed();
        QString previous = e["previous"].toString().trimmed();
        int imp_int = e["importance"].toInt(0);

        // Importance color: 0=dim, 1=low/dim, 2=medium/amber, 3=high/red
        QString imp_color = imp_int >= 3   ? ui::colors::NEGATIVE()
                            : imp_int == 2 ? ui::colors::WARNING()
                                           : ui::colors::TEXT_SECONDARY();
        QString imp_text = imp_int >= 3 ? "HIGH" : imp_int == 2 ? "MED" : imp_int == 1 ? "LOW" : "--";

        auto* row = new QWidget(this);
        row->setStyleSheet(QString("background: %1;").arg(alt ? ui::colors::BG_RAISED() : "transparent"));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 4, 8, 4);

        // Event name
        QString display_name = event_name;
        if (display_name.length() > 28)
            display_name = display_name.left(26) + "…";
        auto* ev_lbl = new QLabel(display_name);
        ev_lbl->setToolTip(event_name); // full name on hover
        ev_lbl->setStyleSheet(QString("color: %1; background: transparent;").arg(ui::colors::TEXT_PRIMARY()));
        rl->addWidget(ev_lbl, 4);

        auto* cty_lbl = new QLabel(country);
        cty_lbl->setStyleSheet(QString("color: %1; background: transparent;").arg(ui::colors::CYAN()));
        rl->addWidget(cty_lbl, 1);

        auto* date_lbl = new QLabel(date_display);
        date_lbl->setToolTip(e["when_full"].toString());
        date_lbl->setStyleSheet(QString("color: %1; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
        rl->addWidget(date_lbl, 2);

        // The feed carries forecast and previous; an actual (when a source
        // supplies one) replaces the forecast, in bold.
        auto* fcst_lbl = new QLabel(!actual.isEmpty() ? actual : forecast.isEmpty() ? "--" : forecast);
        fcst_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (!actual.isEmpty())
            fcst_lbl->setToolTip(QStringLiteral("Actual (forecast %1)").arg(forecast.isEmpty() ? "--" : forecast));
        fcst_lbl->setStyleSheet(QString("color: %1; %2 background: transparent;")
                                    .arg(actual.isEmpty() ? ui::colors::TEXT_SECONDARY() : ui::colors::TEXT_PRIMARY(),
                                         actual.isEmpty() ? "" : "font-weight: bold;"));
        rl->addWidget(fcst_lbl, 1);

        auto* prev_lbl = new QLabel(previous.isEmpty() ? "--" : previous);
        prev_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        prev_lbl->setStyleSheet(QString("color: %1; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
        rl->addWidget(prev_lbl, 1);

        auto* imp_lbl = new QLabel(imp_text);
        imp_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        imp_lbl->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent;").arg(imp_color));
        rl->addWidget(imp_lbl, 1);

        list_layout_->addWidget(row);
        alt = !alt;
        ++count;
    }

    list_layout_->addStretch();
}

} // namespace finterm::screens::widgets

#include "screens/dashboard/widgets/EconomicCalendarWidget.h"

#include "network/http/HttpClient.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace fincept::screens::widgets {

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
    make_hdr("DATE", 2);
    make_hdr("ACT", 1, Qt::AlignRight);
    make_hdr("FCST", 1, Qt::AlignRight);
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

    connect(this, &BaseWidget::refresh_requested, this, &EconomicCalendarWidget::refresh_data);

    apply_styles();
    set_loading(true);
    refresh_data();
}

void EconomicCalendarWidget::apply_styles() {
    header_widget_->setStyleSheet(QString("background: %1;").arg(ui::colors::BG_RAISED()));
    for (auto* lbl : header_labels_)
        lbl->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold; background: transparent;")
                               .arg(ui::colors::TEXT_SECONDARY()));
    header_sep_->setStyleSheet(QString("background: %1;").arg(ui::colors::BORDER_DIM()));
    scroll_area_->setStyleSheet(
        QString("QScrollArea { border: none; background: transparent; }"
                "QScrollBar:vertical { width: 4px; background: transparent; }"
                "QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(ui::colors::BORDER_MED()));
    status_label_->setStyleSheet(QString("color: %1; font-size: 12px; padding: 16px; background: transparent;")
                                     .arg(ui::colors::TEXT_SECONDARY()));
}

void EconomicCalendarWidget::on_theme_changed() {
    apply_styles();
    if (!last_events_.isEmpty())
        populate(last_events_);
}

void EconomicCalendarWidget::refresh_data() {
    if (pending_fetches_ > 0) return; // prior fetch still in flight — callbacks will populate

    // ForexFactory public JSON calendar — no API key required.
    // Two requests: this week + next week, merged and sorted before display.
    static const char* kUrls[] = {
        "https://nfs.faireconomy.media/ff_calendar_thisweek.json",
        "https://nfs.faireconomy.media/ff_calendar_nextweek.json",
    };

    set_loading(true);
    pending_merge_ = QJsonArray{};
    pending_fetches_ = 2;

    QPointer<EconomicCalendarWidget> self = this;
    for (const auto* url : kUrls) {
        HttpClient::instance().get(QString(url), [self](Result<QJsonDocument> res) {
            if (!self) return;
            if (res.is_ok() && res.value().isArray())
                self->on_fetch_done(res.value().array());
            else
                self->on_fetch_done({});
        });
    }
}

void EconomicCalendarWidget::on_fetch_done(const QJsonArray& week_events) {
    for (const auto& v : week_events)
        pending_merge_.append(v);

    if (--pending_fetches_ > 0)
        return; // still waiting for the other week

    // Parse "MMM d yyyy" dates so we can sort chronologically.
    // ForexFactory date examples: "May 14 2026", "Jun 2 2026"
    static const char* kMonths[] = {
        "", "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

    auto parse_ff_date = [&](const QString& s) -> QDate {
        const QStringList parts = s.split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 3) return {};
        int mon = 0;
        for (int i = 1; i <= 12; ++i)
            if (parts[0].startsWith(kMonths[i], Qt::CaseInsensitive)) { mon = i; break; }
        return QDate(parts[2].toInt(), mon, parts[1].toInt());
    };

    // Sort by date, then convert to the format populate() expects.
    QVector<QJsonObject> sorted;
    sorted.reserve(pending_merge_.size());
    for (const auto& v : std::as_const(pending_merge_))
        sorted.append(v.toObject());
    std::stable_sort(sorted.begin(), sorted.end(), [&](const QJsonObject& a, const QJsonObject& b) {
        return parse_ff_date(a["date"].toString()) < parse_ff_date(b["date"].toString());
    });

    // Map ForexFactory fields → populate() schema.
    QJsonArray out;
    for (const auto& e : sorted) {
        const QDate d = parse_ff_date(e["date"].toString());
        const QString impact = e["impact"].toString();
        int imp = impact.startsWith("H", Qt::CaseInsensitive) ? 3
                : impact.startsWith("M", Qt::CaseInsensitive) ? 2
                : impact.startsWith("L", Qt::CaseInsensitive) ? 1 : 0;
        QJsonObject mapped;
        mapped["event"]      = e["title"].toString();
        mapped["country"]    = e["country"].toString();
        mapped["date"]       = d.isValid() ? d.toString(Qt::ISODate) : e["date"].toString();
        mapped["time"]       = e["time"].toString();
        mapped["importance"] = imp;
        mapped["actual"]     = e["actual"].toString();
        mapped["forecast"]   = e["forecast"].toString();
        mapped["previous"]   = e["previous"].toString();
        out.append(mapped);
    }

    set_loading(false);
    populate(out);
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
        status_label_->setText("No events available");
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
        QString date = e["date"].toString();
        QString time_str = e["time"].toString().trimmed();
        QString actual = e["actual"].toString().trimmed();
        QString forecast = e["forecast"].toString().trimmed();
        int imp_int = e["importance"].toInt(0);

        // Date: show as MMM-DD
        QString date_display = date;
        if (date.length() == 10) { // YYYY-MM-DD
            QStringList parts = date.split('-');
            if (parts.size() == 3) {
                static const char* months[] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                int m = parts[1].toInt();
                if (m >= 1 && m <= 12)
                    date_display = QString("%1-%2").arg(months[m]).arg(parts[2]);
            }
        }
        if (!time_str.isEmpty())
            date_display += " " + time_str.left(5);

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
        ev_lbl->setStyleSheet(
            QString("color: %1; font-size: 12px; background: transparent;").arg(ui::colors::TEXT_PRIMARY()));
        rl->addWidget(ev_lbl, 4);

        auto* cty_lbl = new QLabel(country);
        cty_lbl->setStyleSheet(QString("color: %1; font-size: 12px; background: transparent;").arg(ui::colors::CYAN()));
        rl->addWidget(cty_lbl, 1);

        auto* date_lbl = new QLabel(date_display);
        date_lbl->setStyleSheet(
            QString("color: %1; font-size: 12px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
        rl->addWidget(date_lbl, 2);

        auto* act_lbl = new QLabel(actual.isEmpty() ? "--" : actual);
        act_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        act_lbl->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold; background: transparent;")
                                   .arg(actual.isEmpty() ? ui::colors::TEXT_SECONDARY() : ui::colors::TEXT_PRIMARY()));
        rl->addWidget(act_lbl, 1);

        auto* fcst_lbl = new QLabel(forecast.isEmpty() ? "--" : forecast);
        fcst_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fcst_lbl->setStyleSheet(
            QString("color: %1; font-size: 12px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
        rl->addWidget(fcst_lbl, 1);

        auto* imp_lbl = new QLabel(imp_text);
        imp_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        imp_lbl->setStyleSheet(
            QString("color: %1; font-size: 12px; font-weight: bold; background: transparent;").arg(imp_color));
        rl->addWidget(imp_lbl, 1);

        list_layout_->addWidget(row);
        alt = !alt;
        ++count;
    }

    list_layout_->addStretch();
}

} // namespace fincept::screens::widgets

// src/screens/dashboard/widgets/IpoCalendarWidget.cpp
#include "screens/dashboard/widgets/IpoCalendarWidget.h"

#include "network/http/HttpClient.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace fincept::screens::widgets {

IpoCalendarWidget::IpoCalendarWidget(QWidget* parent)
    : BaseWidget("IPO CALENDAR", parent, ui::colors::AMBER()) {
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
    make_hdr("COMPANY",  4);
    make_hdr("TICKER",   1);
    make_hdr("DATE",     2);
    make_hdr("PRICE RNG",2, Qt::AlignRight);
    vl->addWidget(header_widget_);

    header_sep_ = new QFrame;
    header_sep_->setFixedHeight(1);
    vl->addWidget(header_sep_);

    // Status label lives OUTSIDE the scrollable list so that populate()'s
    // clear loop can never delete it — eliminating the dangling-pointer crash
    // that occurred when apply_styles() was called after the first populate().
    // Show it (loading/empty state) or hide it (data populated) via setVisible.
    status_label_ = new QLabel("Loading…");
    status_label_->setAlignment(Qt::AlignCenter);
    vl->addWidget(status_label_);

    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);
    scroll_area_->hide(); // hidden until first successful populate()

    auto* list_widget = new QWidget(this);
    list_widget->setStyleSheet("background: transparent;");
    list_layout_ = new QVBoxLayout(list_widget);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);
    list_layout_->addStretch(); // only a stretch initially; rows added by populate()

    scroll_area_->setWidget(list_widget);
    vl->addWidget(scroll_area_, 1);

    connect(this, &BaseWidget::refresh_requested, this, &IpoCalendarWidget::refresh_data);

    apply_styles();
    set_loading(true);
    refresh_data();
}

void IpoCalendarWidget::apply_styles() {
    header_widget_->setStyleSheet(QString("background: %1;").arg(ui::colors::BG_RAISED()));
    for (auto* lbl : header_labels_)
        lbl->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent;"
                                   " font-size: %2px;")
                               .arg(ui::colors::TEXT_SECONDARY())
                               .arg(ui::fonts::font_px(0)));
    header_sep_->setStyleSheet(QString("background: %1;").arg(ui::colors::BORDER_DIM()));
    scroll_area_->setStyleSheet(
        QString("QScrollArea { border: none; background: transparent; }"
                "QScrollBar:vertical { width: 4px; background: transparent; }"
                "QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(ui::colors::BORDER_MED()));
    status_label_->setStyleSheet(
        QString("color: %1; padding: 16px; background: transparent;")
            .arg(ui::colors::TEXT_SECONDARY()));
}

void IpoCalendarWidget::on_theme_changed() {
    apply_styles();
    if (!entries_.isEmpty())
        populate();
}

void IpoCalendarWidget::refresh_data() {
    if (pending_fetches_ > 0) return; // prior fetch still in flight — callbacks will populate
    set_loading(true);
    entries_.clear();
    pending_fetches_ = 2;
    ok_fetches_ = 0;

    const QDate today = QDate::currentDate();
    fetch_month(today.toString("yyyy-MM"));
    fetch_month(today.addMonths(1).toString("yyyy-MM"));
}

void IpoCalendarWidget::fetch_month(const QString& yyyymm) {
    const QString url = QString("https://api.nasdaq.com/api/ipo/calendar?date=%1").arg(yyyymm);

    QPointer<IpoCalendarWidget> self = this;
    HttpClient::instance().get(url, [self](Result<QJsonDocument> res) {
        if (!self) return;

        if (res.is_ok() && res.value().isObject()) {
            ++self->ok_fetches_;
            const auto root  = res.value().object();
            const auto data  = root["data"].toObject();

            // Nasdaq API returns M/d/yyyy (e.g. "5/27/2026"), not yyyy/MM/dd.
            // Invalid parses leave entry.date invalid → sort falls apart.
            auto parse_date = [](const QString& s) -> QDate {
                QDate d = QDate::fromString(s, "M/d/yyyy");
                if (!d.isValid()) d = QDate::fromString(s, "MM/dd/yyyy");
                if (!d.isValid()) d = QDate::fromString(s.left(10), "yyyy/MM/dd");
                return d;
            };

            // ── Upcoming ──────────────────────────────────────────────────
            const auto upcoming_rows =
                data["upcoming"].toObject()["upcomingTable"].toObject()["rows"].toArray();
            for (const auto& v : upcoming_rows) {
                const auto e = v.toObject();
                const QString date_raw = e["expectedPriceDate"].toString();
                IpoEntry entry;
                entry.company    = e["companyName"].toString();
                entry.ticker     = e["proposedTickerSymbol"].toString();
                entry.exchange   = e["proposedExchange"].toString();
                entry.date       = parse_date(date_raw);
                entry.date_str   = date_raw; // keep M/D/YYYY as returned by API
                entry.price_range = e["proposedSharePrice"].toString();
                entry.status     = "upcoming";
                if (!entry.company.isEmpty())
                    self->entries_.append(entry);
            }

            // ── Priced ────────────────────────────────────────────────────
            // Nasdaq API returns priced rows directly under data.priced.rows
            // (NOT under a nested "pricedTable" object — that was a parser bug
            // that hid every priced IPO including today's). Fields also differ
            // from the upcoming table: pricedDate / proposedSharePrice / proposedExchange.
            const auto priced_rows = data["priced"].toObject()["rows"].toArray();
            for (const auto& v : priced_rows) {
                const auto e = v.toObject();
                const QString date_raw = e["pricedDate"].toString();
                IpoEntry entry;
                entry.company    = e["companyName"].toString();
                entry.ticker     = e["proposedTickerSymbol"].toString();
                entry.exchange   = e["proposedExchange"].toString();
                entry.date       = parse_date(date_raw);
                entry.date_str   = entry.date.isValid()
                                       ? entry.date.toString("MMM d")
                                       : date_raw.left(10);
                entry.price_range = e["proposedSharePrice"].toString();
                entry.status     = "priced";
                if (!entry.company.isEmpty())
                    self->entries_.append(entry);
            }
        }

        self->on_fetch_done();
    });
}

void IpoCalendarWidget::on_fetch_done() {
    if (--pending_fetches_ > 0)
        return;

    // Both month requests failed (Nasdaq API down / offline). Surface an error
    // via the persistent status label rather than populate()'s "No IPO data
    // available", which would wrongly read as "feed loaded, no IPOs scheduled".
    if (ok_fetches_ == 0) {
        set_loading(false);
        scroll_area_->setVisible(false);
        status_label_->setText("Couldn't load IPO calendar");
        status_label_->setVisible(true);
        return;
    }

    // Sort: upcoming closest-date first (e.g. May 14 before May 15),
    // then priced by date descending (most recently priced at top of its section).
    std::stable_sort(entries_.begin(), entries_.end(), [](const IpoEntry& a, const IpoEntry& b) {
        if (a.status != b.status)
            return a.status == "upcoming"; // upcoming section before priced
        if (!a.date.isValid()) return false;
        if (!b.date.isValid()) return true;
        return a.status == "upcoming" ? a.date < b.date : a.date > b.date;
    });

    set_loading(false);
    populate();
}

void IpoCalendarWidget::populate() {
    // Clear data rows. status_label_ lives in the parent layout — never touched here.
    while (list_layout_->count() > 0) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (entries_.isEmpty()) {
        status_label_->setText("No IPO data available");
        status_label_->setVisible(true);
        scroll_area_->setVisible(false);
        list_layout_->addStretch();
        return;
    }

    status_label_->setVisible(false);
    scroll_area_->setVisible(true);

    // Per-section caps: upcoming entries can flood the list when the calendar
    // has 30+ pending deals, which previously pushed today's priced IPOs off
    // the bottom. Cap each section independently so both stay visible.
    constexpr int kMaxUpcoming = 25;
    constexpr int kMaxPriced   = 25;
    int upcoming_shown = 0;
    int priced_shown   = 0;

    QString current_status;
    bool alt = false;

    for (const auto& entry : std::as_const(entries_)) {
        // Section caps
        if (entry.status == "upcoming") {
            if (upcoming_shown >= kMaxUpcoming) continue;
        } else if (entry.status == "priced") {
            if (priced_shown >= kMaxPriced) continue;
        }

        // Explicit font-size on every row label — without it the labels inherit
        // from the qApp font, which renders smaller than DataTable rows in
        // adjacent widgets. Pin to font_px(0) so IPO rows line up visually with
        // the rest of the dashboard.
        const QString fs = QString("font-size:%1px;").arg(ui::fonts::font_px(0));

        // Section header when status changes — UPCOMING above future-dated rows,
        // PRICED above the recently-priced group below it.
        if (entry.status != current_status) {
            current_status = entry.status;
            const QString label = current_status == "upcoming" ? "── UPCOMING ──" : "── PRICED ──";
            auto* sec = new QLabel(label);
            sec->setStyleSheet(QString("color: %1; font-weight: bold; "
                                        "padding: 4px 8px; background: %2; %3")
                                   .arg(current_status == "upcoming" ? ui::colors::AMBER() : ui::colors::POSITIVE(),
                                        ui::colors::BG_RAISED(), fs));
            list_layout_->addWidget(sec);
            alt = false;
        }

        auto* row = new QWidget(this);
        row->setStyleSheet(
            QString("background: %1;").arg(alt ? ui::colors::BG_RAISED() : "transparent"));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 3, 8, 3);

        // Company (truncated)
        QString company = entry.company;
        if (company.length() > 22)
            company = company.left(20) + "…";
        auto* co_lbl = new QLabel(company);
        co_lbl->setToolTip(entry.company);
        co_lbl->setStyleSheet(
            QString("color: %1; background: transparent; %2")
                .arg(ui::colors::TEXT_PRIMARY(), fs));
        rl->addWidget(co_lbl, 4);

        auto* tk_lbl = new QLabel(entry.ticker.isEmpty() ? "—" : entry.ticker);
        if (!entry.exchange.isEmpty())
            tk_lbl->setToolTip(entry.exchange); // exchange shown on hover
        tk_lbl->setStyleSheet(
            QString("color: %1; font-weight: bold; background: transparent; %2")
                .arg(ui::colors::AMBER(), fs));
        rl->addWidget(tk_lbl, 1);

        auto* dt_lbl = new QLabel(entry.date_str.isEmpty() ? "—" : entry.date_str);
        dt_lbl->setStyleSheet(
            QString("color: %1; background: transparent; %2")
                .arg(ui::colors::TEXT_SECONDARY(), fs));
        rl->addWidget(dt_lbl, 2);

        auto* pr_lbl = new QLabel(entry.price_range.isEmpty() ? "—" : entry.price_range);
        pr_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pr_lbl->setStyleSheet(
            QString("color: %1; background: transparent; %2")
                .arg(ui::colors::TEXT_SECONDARY(), fs));
        rl->addWidget(pr_lbl, 2);

        list_layout_->addWidget(row);
        alt = !alt;
        if (entry.status == "upcoming")    ++upcoming_shown;
        else if (entry.status == "priced") ++priced_shown;
    }

    list_layout_->addStretch();
}

} // namespace fincept::screens::widgets

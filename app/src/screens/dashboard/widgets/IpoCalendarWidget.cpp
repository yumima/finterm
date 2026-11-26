// src/screens/dashboard/widgets/IpoCalendarWidget.cpp
#include "ui/VisibleRefresh.h"
#include "screens/dashboard/widgets/IpoCalendarWidget.h"

#include <algorithm>

#include "services/pre_ipo/IpoDealsFeed.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace finterm::screens::widgets {

IpoCalendarWidget::IpoCalendarWidget(QWidget* parent) : BaseWidget("IPO CALENDAR", parent, ui::colors::AMBER()) {
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
    make_hdr("COMPANY", 4);
    make_hdr("TICKER", 1);
    make_hdr("DATE", 2);
    make_hdr("PRICE RNG", 2, Qt::AlignRight);
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
    // Every half hour while on screen (and on return): refresh_data() keeps
    // the rows shown while the shared feed re-reads.
    ui::refresh_while_visible(this, 30 * 60, [this] { refresh_data(); });

    auto& feed = services::IpoDealsFeed::instance();
    connect(&feed, &services::IpoDealsFeed::updated, this, &IpoCalendarWidget::rebuild_entries);
    connect(&feed, &services::IpoDealsFeed::answered, this,
            [this](bool enrich, bool ok, const QJsonObject&, const QString&) {
                // Nothing held and the calendar couldn't be read: say so,
                // rather than "No IPO data", which reads as a quiet calendar.
                if (ok || enrich || services::IpoDealsFeed::instance().loaded())
                    return;
                set_loading(false);
                scroll_area_->setVisible(false);
                status_label_->setText("Couldn't load IPO calendar");
                status_label_->setVisible(true);
            });

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
        QString("color: %1; padding: 16px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
}

void IpoCalendarWidget::on_theme_changed() {
    apply_styles();
    if (!entries_.isEmpty())
        populate();
}

void IpoCalendarWidget::refresh_data() {
    ui::mark_refreshed(this);
    auto& feed = services::IpoDealsFeed::instance();
    // The same calendar the PRIVATE MARKET screen and What's On show, read by
    // the one feed (no request while a fresh answer is held).
    feed.load();
    if (feed.loaded()) {
        rebuild_entries();
    } else if (feed.inflight(false)) {
        set_loading(true); // its answer (or failure) ends this
    } else {
        // Nothing held and nothing running (a failure's cool-off).
        set_loading(false);
        scroll_area_->setVisible(false);
        status_label_->setText("Couldn't load IPO calendar");
        status_label_->setVisible(true);
    }
}

void IpoCalendarWidget::rebuild_entries() {
    const auto& feed = services::IpoDealsFeed::instance();
    if (!feed.loaded())
        return; // still loading, or the failure is shown
    const QDate today = QDate::currentDate();
    entries_.clear();
    unlisted_upcoming_ = 0;
    unnamed_upcoming_ = 0;
    // months Nasdaq did not answer: "none" is then not known
    partial_ = !feed.latest_root().value("months_failed").toArray().isEmpty(); // not EDGAR gaps or cached months
    // The newest rows (a plain fast pass carries today's pricings even while
    // EDGAR is failing); dates as the feed parses them for every surface.
    const QJsonArray rows = feed.latest_root().value("deals").toArray();
    for (const auto& v : rows) {
        const services::IpoDeal d = services::IpoDealsFeed::parse_deal(v.toObject());
        IpoEntry entry;
        entry.company = d.company;
        entry.ticker = d.ticker;
        entry.exchange = d.exchange;
        entry.price_range = d.price_range;
        if (d.status == QLatin1String("upcoming")) {
            // Scheduled on the exchange calendar. No date = its date slipped
            // (overdue / late): not "upcoming" in any sense a reader can use.
            if (!d.when.isValid() || today.daysTo(d.when) < -3 || today.daysTo(d.when) > 60) {
                ++unlisted_upcoming_; // still said: "None scheduled" must not hide it
                continue;
            }
            entry.status = QStringLiteral("upcoming");
            entry.date = d.when;
        } else if (d.status == QLatin1String("priced")) {
            if (!d.priced.isValid() || d.priced.daysTo(today) > 31)
                continue;
            entry.status = QStringLiteral("priced");
            entry.date = d.priced;
        } else {
            continue;
        }
        entry.date_str = entry.date.isValid() ? entry.date.toString("MMM d") : QString();
        if (!entry.company.isEmpty())
            entries_.append(entry);
        else if (entry.status == QLatin1String("upcoming"))
            ++unnamed_upcoming_; // a scheduled deal without a name is still not "none"
    }

    // Sort: upcoming closest-date first (e.g. May 14 before May 15),
    // then priced by date descending (most recently priced at top of its section).
    std::stable_sort(entries_.begin(), entries_.end(), [](const IpoEntry& a, const IpoEntry& b) {
        if (a.status != b.status)
            return a.status == "upcoming"; // upcoming section before priced
        if (!a.date.isValid())
            return false;
        if (!b.date.isValid())
            return true;
        return a.status == "upcoming" ? a.date < b.date : a.date > b.date;
    });

    set_loading(false);
    populate();
}

void IpoCalendarWidget::populate() {
    // Clear data rows. status_label_ lives in the parent layout — never touched here.
    while (list_layout_->count() > 0) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }


    status_label_->setVisible(false);
    scroll_area_->setVisible(true);

    // Per-section caps: upcoming entries can flood the list when the calendar
    // has 30+ pending deals, which previously pushed today's priced IPOs off
    // the bottom. Cap each section independently so both stay visible.
    constexpr int kMaxUpcoming = 25;
    constexpr int kMaxPriced = 25;
    int upcoming_shown = 0;
    int priced_shown = 0;

    QString current_status;
    bool alt = false;

    // No deal scheduled is an answer, not a gap: say it, or a PRICED-only tile
    // reads as a calendar that stopped updating. Nasdaq's calendar, the only
    // source of scheduled dates, lists about a week ahead.
    const bool any_upcoming = std::any_of(entries_.cbegin(), entries_.cend(),
                                          [](const auto& e) { return e.status == QLatin1String("upcoming"); });
    if (!any_upcoming) {
        const QString fs = QString("font-size:%1px;").arg(ui::fonts::font_px(0));
        auto* sec = new QLabel(QStringLiteral("── UPCOMING ──"));
        sec->setStyleSheet(QString("color: %1; font-weight: bold; padding: 4px 8px; background: %2; %3")
                               .arg(ui::colors::AMBER(), ui::colors::BG_RAISED(), fs));
        list_layout_->addWidget(sec);
        auto* none = new QLabel(
            partial_ ? QStringLiteral("None found — the calendar was only partly read; there may be more.")
            : unlisted_upcoming_ + unnamed_upcoming_ > 0
                ? QStringLiteral("None listed here — %1 on Nasdaq's calendar %2: PRIVATE MARKET › IPO CALENDAR.")
                      .arg(unlisted_upcoming_ + unnamed_upcoming_)
                      .arg(unnamed_upcoming_ == 0 ? QStringLiteral("with a slipped or later date")
                           : unlisted_upcoming_ == 0 ? QStringLiteral("without a company name yet")
                                                     : QStringLiteral("with a slipped date or no name yet"))
                : QStringLiteral("None scheduled — Nasdaq's calendar lists about a week ahead. "
                                 "Estimates for filed deals: PRIVATE MARKET › IPO CALENDAR."));
        none->setWordWrap(true);
        none->setStyleSheet(
            QString("color: %1; padding: 4px 8px; %2").arg(ui::colors::TEXT_SECONDARY(), fs));
        list_layout_->addWidget(none);
    }

    for (const auto& entry : std::as_const(entries_)) {
        // Section caps
        if (entry.status == "upcoming") {
            if (upcoming_shown >= kMaxUpcoming)
                continue;
        } else if (entry.status == "priced") {
            if (priced_shown >= kMaxPriced)
                continue;
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
        row->setStyleSheet(QString("background: %1;").arg(alt ? ui::colors::BG_RAISED() : "transparent"));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 3, 8, 3);

        // Company (truncated)
        QString company = entry.company;
        if (company.length() > 22)
            company = company.left(20) + "…";
        auto* co_lbl = new QLabel(company);
        co_lbl->setToolTip(entry.company);
        co_lbl->setStyleSheet(QString("color: %1; background: transparent; %2").arg(ui::colors::TEXT_PRIMARY(), fs));
        rl->addWidget(co_lbl, 4);

        auto* tk_lbl = new QLabel(entry.ticker.isEmpty() ? "—" : entry.ticker);
        if (!entry.exchange.isEmpty())
            tk_lbl->setToolTip(entry.exchange); // exchange shown on hover
        tk_lbl->setStyleSheet(
            QString("color: %1; font-weight: bold; background: transparent; %2").arg(ui::colors::AMBER(), fs));
        rl->addWidget(tk_lbl, 1);

        auto* dt_lbl = new QLabel(entry.date_str.isEmpty() ? "—" : entry.date_str);
        dt_lbl->setStyleSheet(QString("color: %1; background: transparent; %2").arg(ui::colors::TEXT_SECONDARY(), fs));
        rl->addWidget(dt_lbl, 2);

        auto* pr_lbl = new QLabel(entry.price_range.isEmpty() ? "—" : entry.price_range);
        pr_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pr_lbl->setStyleSheet(QString("color: %1; background: transparent; %2").arg(ui::colors::TEXT_SECONDARY(), fs));
        rl->addWidget(pr_lbl, 2);

        list_layout_->addWidget(row);
        alt = !alt;
        if (entry.status == "upcoming")
            ++upcoming_shown;
        else if (entry.status == "priced")
            ++priced_shown;
    }

    if (priced_shown == 0) {
        const QString fs = QString("font-size:%1px;").arg(ui::fonts::font_px(0));
        auto* sec = new QLabel(QStringLiteral("── PRICED ──"));
        sec->setStyleSheet(QString("color: %1; font-weight: bold; padding: 4px 8px; background: %2; %3")
                               .arg(ui::colors::POSITIVE(), ui::colors::BG_RAISED(), fs));
        list_layout_->addWidget(sec);
        auto* none = new QLabel(partial_ ? QStringLiteral("None found — the calendar was only partly read.")
                                         : QStringLiteral("None in the last 31 days."));
        none->setStyleSheet(QString("color: %1; padding: 4px 8px; %2").arg(ui::colors::TEXT_SECONDARY(), fs));
        list_layout_->addWidget(none);
    }
    list_layout_->addStretch();
}

} // namespace finterm::screens::widgets

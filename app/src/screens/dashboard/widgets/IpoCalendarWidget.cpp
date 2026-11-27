// src/screens/dashboard/widgets/IpoCalendarWidget.cpp
#include "ui/VisibleRefresh.h"
#include "screens/dashboard/widgets/IpoCalendarWidget.h"

#include "core/errors/DataFailure.h"

#include <algorithm>

#include "services/pre_ipo/IpoDealsFeed.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QHBoxLayout>
#include <QJsonObject>

namespace finterm::screens::widgets {

namespace {
// PRIVATE MARKET › IPO CALENDAR's windows (IpoWatchView: kDaysWeek, kDays30).
constexpr qint64 kNextWeekDays = 7;
constexpr qint64 kNextMonthDays = 30;
constexpr qint64 kPricedDays = 30;
constexpr int kMaxRows = 50;
} // namespace

IpoCalendarWidget::IpoCalendarWidget(const QJsonObject& cfg, QWidget* parent)
    : BaseWidget("IPO CALENDAR", parent, ui::colors::AMBER()) {
    auto* vl = content_layout();
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // ── Window tabs ──────────────────────────────────────────────────────────
    auto* tab_bar = new QWidget(this);
    tab_bar->setFixedHeight(24);
    auto* tl = new QHBoxLayout(tab_bar);
    tl->setContentsMargins(4, 2, 4, 2);
    tl->setSpacing(0);
    auto make_tab = [&](Window w) {
        auto* b = new QPushButton(this);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, w]() { set_window(w); });
        tl->addWidget(b, 1);
        return b;
    };
    tab_priced_ = make_tab(Window::Priced);
    tab_next7_ = make_tab(Window::Next7);
    tab_next30_ = make_tab(Window::Next30);
    vl->addWidget(tab_bar);

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
    date_header_ = header_labels_.last();
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
    status_label_->setWordWrap(true); // a failure says why — a sentence, not a word
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
            [this](bool enrich, bool ok, const QJsonObject&, const QString& why) {
                // Nothing held and the calendar couldn't be read: say so —
                // and why — rather than "No IPO data", which reads as a
                // quiet calendar.
                if (ok || enrich || services::IpoDealsFeed::instance().loaded())
                    return;
                set_loading(false);
                scroll_area_->setVisible(false);
                const auto f = data_failure::classify(why, QStringLiteral("Nasdaq"));
                status_label_->setText(data_failure::one_line(QStringLiteral("the IPO calendar"), f));
                status_label_->setToolTip(QStringLiteral("What to do: %1\n\n%2").arg(f.action, f.raw));
                status_label_->setVisible(true);
            });

    apply_config(cfg); // styles the tabs too
    set_loading(true);
    refresh_data();
}

QJsonObject IpoCalendarWidget::config() const {
    return QJsonObject{{"window", window_ == Window::Priced  ? "priced"
                                  : window_ == Window::Next30 ? "next30"
                                                              : "next7"}};
}

void IpoCalendarWidget::apply_config(const QJsonObject& cfg) {
    const QString w = cfg.value("window").toString();
    window_ = w == QLatin1String("priced") ? Window::Priced : w == QLatin1String("next30") ? Window::Next30 : Window::Next7;
    apply_styles();
    if (services::IpoDealsFeed::instance().loaded())
        populate();
}

void IpoCalendarWidget::set_window(Window w) {
    if (w == window_)
        return;
    window_ = w;
    emit config_changed(config());
    apply_styles();
    if (services::IpoDealsFeed::instance().loaded())
        populate();
}

bool IpoCalendarWidget::in_window(const IpoEntry& e, Window w) {
    if (!e.date.isValid())
        return false;
    const QDate today = QDate::currentDate();
    if (w == Window::Priced) {
        const qint64 ago = e.date.daysTo(today);
        return !e.pending && ago >= 0 && ago <= kPricedDays;
    }
    const qint64 ahead = today.daysTo(e.date);
    return e.pending && ahead >= 0 && ahead <= (w == Window::Next7 ? kNextWeekDays : kNextMonthDays);
}

int IpoCalendarWidget::count_in(Window w) const {
    return int(std::count_if(entries_.cbegin(), entries_.cend(), [w](const IpoEntry& e) { return in_window(e, w); }));
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

    // Same tab look as EARNINGS CALENDAR's THIS WEEK | PORTFOLIO.
    const QString active = QString("QPushButton { background:%1; color:%2; border:none; font-weight:bold;"
                                   "  font-size:%3; padding:2px; }")
                               .arg(ui::colors::AMBER_DIM(), ui::colors::AMBER(), ui::fonts::ui_px());
    const QString idle =
        QString("QPushButton { background:transparent; color:%1; border:none; font-size:%2; padding:2px; }"
                "QPushButton:hover { color:%3; }")
            .arg(ui::colors::TEXT_SECONDARY(), ui::fonts::ui_px(), ui::colors::TEXT_PRIMARY());
    const bool loaded = services::IpoDealsFeed::instance().loaded();
    auto label = [&](const char* name, Window w) {
        return loaded ? QStringLiteral("%1 %2").arg(QLatin1String(name)).arg(count_in(w)) : QString::fromLatin1(name);
    };
    tab_priced_->setText(label("PRICED", Window::Priced));
    tab_next7_->setText(label("NEXT 7 DAYS", Window::Next7));
    tab_next30_->setText(label("NEXT 30 DAYS", Window::Next30));
    tab_priced_->setToolTip(QStringLiteral("Priced in the last 30 days"));
    tab_next7_->setToolTip(QStringLiteral("Expected to price in the next 7 days — scheduled, or estimated for a "
                                          "filed deal (~)"));
    tab_next30_->setToolTip(QStringLiteral("Expected to price in the next 30 days — scheduled, or estimated for a "
                                           "filed deal (~)"));
    tab_priced_->setStyleSheet(window_ == Window::Priced ? active : idle);
    tab_next7_->setStyleSheet(window_ == Window::Next7 ? active : idle);
    tab_next30_->setStyleSheet(window_ == Window::Next30 ? active : idle);
    if (date_header_)
        date_header_->setText(window_ == Window::Priced ? QStringLiteral("PRICED") : QStringLiteral("EXPECTED"));
}

void IpoCalendarWidget::on_theme_changed() {
    apply_styles();
    if (services::IpoDealsFeed::instance().loaded())
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
    entries_.clear();
    undated_pending_ = 0;
    // The answer PRIVATE MARKET › IPO CALENDAR shows (the EDGAR-enriched one
    // once it landed: the projected dates of filed deals rest on it).
    partial_ = !feed.root().value("months_failed").toArray().isEmpty(); // months Nasdaq did not answer
    for (const services::IpoDeal& d : feed.deals()) {
        if (d.company.isEmpty())
            continue; // the screen lists no deal without a name either
        IpoEntry e;
        e.company = d.company;
        e.ticker = d.ticker;
        e.exchange = d.exchange;
        e.price_range = d.price_range;
        if (d.pending()) {
            // Its projected pricing date: scheduled, imminent or estimated.
            // None (long shot, dormant, overdue): in no forward window.
            if (!d.when.isValid()) {
                ++undated_pending_;
                continue;
            }
            e.pending = true;
            e.date = d.when;
            e.firm = d.firm();
            e.basis = d.basis;
        } else if (d.status == QLatin1String("priced")) {
            e.date = d.priced;
        } else {
            continue; // withdrawn
        }
        entries_.append(e);
    }
    // Pending soonest first, a firm date before an estimate on the same day
    // (as the screen sorts); priced most recent first.
    std::stable_sort(entries_.begin(), entries_.end(), [](const IpoEntry& a, const IpoEntry& b) {
        if (a.pending != b.pending)
            return a.pending;
        if (a.date != b.date)
            return a.pending ? a.date < b.date : a.date > b.date;
        return a.pending && a.firm && !b.firm;
    });
    status_label_->setToolTip(QString()); // an earlier failure's

    set_loading(false);
    apply_styles(); // the counts on the tabs
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

    // Explicit font-size on every row label — without it the labels inherit
    // from the qApp font, which renders smaller than DataTable rows in
    // adjacent widgets.
    const QString fs = QString("font-size:%1px;").arg(ui::fonts::font_px(0));
    int shown = 0;
    int more = 0;
    bool alt = false;
    for (const auto& entry : std::as_const(entries_)) {
        if (!in_window(entry, window_))
            continue;
        if (shown >= kMaxRows) {
            ++more;
            continue;
        }
        auto* row = new QWidget(this);
        row->setStyleSheet(QString("background: %1;").arg(alt ? ui::colors::BG_RAISED() : "transparent"));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 3, 8, 3);

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

        // An estimate is marked "~" and says what it rests on.
        const bool estimate = entry.pending && !entry.firm;
        auto* dt_lbl = new QLabel((estimate ? QStringLiteral("~") : QString()) + entry.date.toString("MMM d"));
        if (estimate)
            dt_lbl->setToolTip(entry.basis.isEmpty() ? QStringLiteral("Estimated") : QStringLiteral("Estimated: ") +
                                                                                         entry.basis);
        else if (entry.pending)
            dt_lbl->setToolTip(QStringLiteral("Scheduled"));
        dt_lbl->setStyleSheet(QString("color: %1; background: transparent; %2").arg(ui::colors::TEXT_SECONDARY(), fs));
        rl->addWidget(dt_lbl, 2);

        auto* pr_lbl = new QLabel(entry.price_range.isEmpty() ? "—" : entry.price_range);
        pr_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pr_lbl->setStyleSheet(QString("color: %1; background: transparent; %2").arg(ui::colors::TEXT_SECONDARY(), fs));
        rl->addWidget(pr_lbl, 2);

        list_layout_->addWidget(row);
        alt = !alt;
        ++shown;
    }

    if (more > 0) {
        auto* rest = new QLabel(QStringLiteral("+%1 more: PRIVATE MARKET › IPO CALENDAR.").arg(more));
        rest->setStyleSheet(QString("color: %1; padding: 4px 8px; %2").arg(ui::colors::TEXT_SECONDARY(), fs));
        list_layout_->addWidget(rest);
    }

    // An empty window is an answer, not a gap: say so, and where to look.
    if (shown == 0) {
        QString text;
        if (partial_)
            text = QStringLiteral("None found — the calendar was only partly read; there may be more.");
        else if (window_ == Window::Priced)
            text = QStringLiteral("No IPOs priced in the last 30 days.");
        else if (window_ == Window::Next7) {
            text = QStringLiteral("No IPOs expected in the next 7 days.");
            if (const int later = count_in(Window::Next30); later > 0)
                text += QStringLiteral(" %1 in the next 30 days.").arg(later);
        } else {
            text = QStringLiteral("No IPOs expected in the next 30 days.");
        }
        if (window_ != Window::Priced && undated_pending_ > 0)
            text += QStringLiteral(" %1 filed without a date yet: PRIVATE MARKET › IPO CALENDAR.").arg(undated_pending_);
        auto* none = new QLabel(text);
        none->setWordWrap(true);
        none->setStyleSheet(QString("color: %1; padding: 8px; %2").arg(ui::colors::TEXT_SECONDARY(), fs));
        list_layout_->addWidget(none);
    }
    list_layout_->addStretch();
}

} // namespace finterm::screens::widgets

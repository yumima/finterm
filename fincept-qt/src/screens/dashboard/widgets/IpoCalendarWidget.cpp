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

    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);

    auto* list_widget = new QWidget(this);
    list_widget->setStyleSheet("background: transparent;");
    list_layout_ = new QVBoxLayout(list_widget);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);

    status_label_ = new QLabel("Loading...");
    status_label_->setAlignment(Qt::AlignCenter);
    list_layout_->addWidget(status_label_);
    list_layout_->addStretch();

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
        lbl->setStyleSheet(QString("color: %1; font-size: 9px; font-weight: bold; background: transparent;")
                               .arg(ui::colors::TEXT_SECONDARY()));
    header_sep_->setStyleSheet(QString("background: %1;").arg(ui::colors::BORDER_DIM()));
    scroll_area_->setStyleSheet(
        QString("QScrollArea { border: none; background: transparent; }"
                "QScrollBar:vertical { width: 4px; background: transparent; }"
                "QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(ui::colors::BORDER_MED()));
    status_label_->setStyleSheet(
        QString("color: %1; font-size: 10px; padding: 16px; background: transparent;")
            .arg(ui::colors::TEXT_SECONDARY()));
}

void IpoCalendarWidget::on_theme_changed() {
    apply_styles();
    if (!entries_.isEmpty())
        populate();
}

void IpoCalendarWidget::refresh_data() {
    set_loading(true);
    entries_.clear();
    pending_fetches_ = 2;

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
            const auto root  = res.value().object();
            const auto data  = root["data"].toObject();

            // Helper to parse a date string like "2026/05/15" → QDate
            auto parse_date = [](const QString& s) -> QDate {
                return QDate::fromString(s, "yyyy/MM/dd");
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
                entry.date       = parse_date(date_raw);
                entry.date_str   = entry.date.isValid()
                                       ? entry.date.toString("MMM d")
                                       : date_raw.left(10);
                entry.price_range = e["proposedSharePrice"].toString();
                entry.status     = "upcoming";
                if (!entry.company.isEmpty())
                    self->entries_.append(entry);
            }

            // ── Priced ────────────────────────────────────────────────────
            const auto priced_rows =
                data["priced"].toObject()["pricedTable"].toObject()["rows"].toArray();
            for (const auto& v : priced_rows) {
                const auto e = v.toObject();
                const QString date_raw = e["ipoDate"].toString();
                IpoEntry entry;
                entry.company    = e["companyName"].toString();
                entry.ticker     = e["proposedTickerSymbol"].toString();
                entry.date       = parse_date(date_raw);
                entry.date_str   = entry.date.isValid()
                                       ? entry.date.toString("MMM d")
                                       : date_raw.left(10);
                entry.price_range = e["dealPrice"].toString();
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

    // Sort: upcoming first (by date asc), then priced (by date desc)
    std::stable_sort(entries_.begin(), entries_.end(), [](const IpoEntry& a, const IpoEntry& b) {
        if (a.status != b.status)
            return a.status == "upcoming"; // upcoming before priced
        return a.status == "upcoming" ? a.date < b.date : a.date > b.date;
    });

    set_loading(false);
    populate();
}

void IpoCalendarWidget::populate() {
    // Clear list (keep the trailing stretch)
    while (list_layout_->count() > 0) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (entries_.isEmpty()) {
        status_label_ = new QLabel("No IPO data available");
        status_label_->setAlignment(Qt::AlignCenter);
        status_label_->setStyleSheet(
            QString("color: %1; font-size: 10px; padding: 16px; background: transparent;")
                .arg(ui::colors::TEXT_SECONDARY()));
        list_layout_->addWidget(status_label_);
        list_layout_->addStretch();
        return;
    }

    QString current_status;
    bool alt = false;
    int count = 0;

    for (const auto& entry : std::as_const(entries_)) {
        if (count >= 30) break;

        // Section header when status changes
        if (entry.status != current_status) {
            current_status = entry.status;
            const QString label = current_status == "upcoming" ? "── UPCOMING ──" : "── PRICED ──";
            auto* sec = new QLabel(label);
            sec->setStyleSheet(
                QString("color: %1; font-size: 9px; font-weight: bold; "
                        "padding: 4px 8px; background: %2;")
                    .arg(current_status == "upcoming" ? ui::colors::AMBER() : ui::colors::POSITIVE(),
                         ui::colors::BG_RAISED()));
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
            QString("color: %1; font-size: 10px; background: transparent;")
                .arg(ui::colors::TEXT_PRIMARY()));
        rl->addWidget(co_lbl, 4);

        auto* tk_lbl = new QLabel(entry.ticker.isEmpty() ? "—" : entry.ticker);
        tk_lbl->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;")
                .arg(ui::colors::AMBER()));
        rl->addWidget(tk_lbl, 1);

        auto* dt_lbl = new QLabel(entry.date_str.isEmpty() ? "—" : entry.date_str);
        dt_lbl->setStyleSheet(
            QString("color: %1; font-size: 9px; background: transparent;")
                .arg(ui::colors::TEXT_SECONDARY()));
        rl->addWidget(dt_lbl, 2);

        auto* pr_lbl = new QLabel(entry.price_range.isEmpty() ? "—" : entry.price_range);
        pr_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pr_lbl->setStyleSheet(
            QString("color: %1; font-size: 9px; background: transparent;")
                .arg(ui::colors::TEXT_SECONDARY()));
        rl->addWidget(pr_lbl, 2);

        list_layout_->addWidget(row);
        alt = !alt;
        ++count;
    }

    list_layout_->addStretch();
}

} // namespace fincept::screens::widgets

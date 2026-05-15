#include "screens/pre_ipo/IpoWatchView.h"

#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::screens::widgets {

namespace {
// Months back / forward to pull from Nasdaq. Each month is one HTTP call,
// so we deliberately stay tight — the API has rate-limiting and we want
// the first paint to be quick.
constexpr int kMonthsBack    = 3;
constexpr int kMonthsForward = 12;
} // namespace

const char* IpoWatchView::bucket_title(Bucket b) {
    switch (b) {
        case BucketThisWeek:     return "THIS WEEK";
        case BucketThisMonth:    return "THIS MONTH";
        case BucketNext6Months:  return "NEXT 6 MONTHS";
        case BucketNext12Months: return "NEXT 12 MONTHS";
        case BucketPriced:       return "RECENTLY PRICED";
        default:                 return "";
    }
}

IpoWatchView::IpoWatchView(QWidget* parent) : QWidget(parent) {
    build_ui();
    apply_styles();
    refresh();
}

void IpoWatchView::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Filter bar ───────────────────────────────────────────────────────────
    filter_bar_ = new QWidget(this);
    filter_bar_->setFixedHeight(36);
    auto* fl = new QHBoxLayout(filter_bar_);
    fl->setContentsMargins(12, 4, 12, 4);
    fl->setSpacing(8);

    auto* search_lbl = new QLabel("FILTER:");
    fl->addWidget(search_lbl);

    search_ = new QLineEdit;
    search_->setPlaceholderText("Company name, ticker, exchange…");
    search_->setFixedHeight(24);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& q) {
        last_query_ = q.trimmed();
        populate();
    });
    fl->addWidget(search_, 1);

    status_lbl_ = new QLabel("Loading…");
    fl->addWidget(status_lbl_);

    // Explicit refresh — needed because IpoWatchView fetches via HttpClient,
    // not the DataHub, so the app-level wake-on-resume hook can't trigger it.
    refresh_btn_ = new QPushButton(QStringLiteral("↻ Refresh"));
    refresh_btn_->setFixedHeight(24);
    refresh_btn_->setCursor(Qt::PointingHandCursor);
    refresh_btn_->setToolTip("Re-fetch the Nasdaq IPO calendar");
    connect(refresh_btn_, &QPushButton::clicked, this, &IpoWatchView::refresh);
    fl->addWidget(refresh_btn_);

    root->addWidget(filter_bar_);

    // ── Body: splitter [bucket column | detail pane] ─────────────────────────
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setHandleWidth(1);
    splitter_->setChildrenCollapsible(false);

    // Left side: scrollable column of bucket tables
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    bucket_host_ = new QWidget;
    bucket_col_ = new QVBoxLayout(bucket_host_);
    bucket_col_->setContentsMargins(8, 8, 8, 8);
    bucket_col_->setSpacing(12);

    for (int i = 0; i < BucketCount; ++i) {
        auto* header = new QLabel(bucket_title(static_cast<Bucket>(i)));
        header->setObjectName("ipoBucketHeader");
        header->setFixedHeight(22);
        table_headers_[i] = header;

        auto* t = new QTableWidget(0, 7);
        t->setHorizontalHeaderLabels({"COMPANY", "TICKER", "EXCH", "DATE", "PRICE", "SHARES", "DEAL SIZE"});
        t->verticalHeader()->setVisible(false);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setAlternatingRowColors(true);
        t->setSortingEnabled(true);
        t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int c = 1; c < t->columnCount(); ++c)
            t->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
        t->setShowGrid(false);
        connect(t, &QTableWidget::cellClicked, this, [this, t](int row, int /*col*/) {
            auto* item = t->item(row, 0);
            if (!item) return;
            const int idx = item->data(Qt::UserRole).toInt();
            if (idx >= 0 && idx < entries_.size())
                on_row_selected(entries_.at(idx));
        });
        tables_[i] = t;

        bucket_col_->addWidget(header);
        bucket_col_->addWidget(t);
    }
    bucket_col_->addStretch();
    scroll_->setWidget(bucket_host_);
    splitter_->addWidget(scroll_);

    // Right side: detail pane
    detail_pane_ = new QWidget;
    auto* dl = new QVBoxLayout(detail_pane_);
    dl->setContentsMargins(12, 12, 12, 12);
    dl->setSpacing(6);

    detail_html_lbl_ = new QLabel("Select a row to see details.");
    detail_html_lbl_->setTextFormat(Qt::RichText);
    detail_html_lbl_->setWordWrap(true);
    detail_html_lbl_->setAlignment(Qt::AlignTop);
    detail_html_lbl_->setOpenExternalLinks(true);
    dl->addWidget(detail_html_lbl_);
    dl->addStretch();

    splitter_->addWidget(detail_pane_);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 1);

    root->addWidget(splitter_, 1);
}

void IpoWatchView::apply_styles() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    filter_bar_->setStyleSheet(
        QString("background:%1;border-bottom:1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    if (search_)
        search_->setStyleSheet(
            QString("QLineEdit{background:%1;color:%2;border:1px solid %3;border-radius:2px;padding:2px 6px;}"
                    "QLineEdit:focus{border-color:%4;}")
                .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                     ui::colors::AMBER()));
    if (status_lbl_)
        status_lbl_->setStyleSheet(
            QString("color:%1;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    if (refresh_btn_)
        refresh_btn_->setStyleSheet(
            QString("QPushButton{background:%1;color:%2;border:1px solid %2;border-radius:2px;"
                    "padding:0 10px;font-weight:bold;}"
                    "QPushButton:hover{background:%2;color:%3;}")
                .arg(ui::colors::BG_SURFACE(), ui::colors::AMBER(), ui::colors::BG_BASE()));

    const QString header_qss =
        QString("color:%1;font-weight:bold;letter-spacing:1px;background:%2;padding:2px 8px;border-radius:2px;")
            .arg(ui::colors::AMBER(), ui::colors::BG_RAISED());
    for (int i = 0; i < BucketCount; ++i) {
        if (table_headers_[i]) table_headers_[i]->setStyleSheet(header_qss);
        if (tables_[i])
            tables_[i]->setStyleSheet(
                QString("QTableWidget{background:%1;color:%2;border:1px solid %3;gridline-color:%3;"
                        "alternate-background-color:%4;}"
                        "QHeaderView::section{background:%5;color:%6;border:0;padding:4px 8px;font-weight:bold;}"
                        "QTableWidget::item:selected{background:%7;color:%8;}")
                    .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                         ui::colors::BG_RAISED(), ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(),
                         ui::colors::AMBER(), ui::colors::BG_BASE()));
    }
    if (detail_html_lbl_)
        detail_html_lbl_->setStyleSheet(
            QString("color:%1;background:%2;").arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_SURFACE()));
    if (detail_pane_)
        detail_pane_->setStyleSheet(
            QString("background:%1;border-left:1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
}

// ── Fetching ─────────────────────────────────────────────────────────────────

void IpoWatchView::refresh() {
    if (pending_fetches_ > 0)
        return; // an in-flight refresh's callbacks will repopulate
    entries_.clear();
    status_lbl_->setText("Loading…");

    const QDate today = QDate::currentDate();
    pending_fetches_ = kMonthsBack + kMonthsForward + 1; // inclusive of current month
    for (int m = -kMonthsBack; m <= kMonthsForward; ++m)
        fetch_month(today.addMonths(m).toString("yyyy-MM"));
}

void IpoWatchView::fetch_month(const QString& yyyymm) {
    const QString url = QString("https://api.nasdaq.com/api/ipo/calendar?date=%1").arg(yyyymm);
    QPointer<IpoWatchView> self = this;
    HttpClient::instance().get(url, [self, yyyymm](Result<QJsonDocument> res) {
        if (!self) return;
        if (res.is_ok() && res.value().isObject()) {
            const auto root = res.value().object();
            const auto data = root["data"].toObject();
            auto parse_date = [](const QString& s) -> QDate {
                QDate d = QDate::fromString(s, "M/d/yyyy");
                if (!d.isValid()) d = QDate::fromString(s, "MM/dd/yyyy");
                if (!d.isValid()) d = QDate::fromString(s.left(10), "yyyy/MM/dd");
                return d;
            };

            // Upcoming rows — these power the THIS WEEK / THIS MONTH / NEXT 6/12 MONTHS buckets.
            const auto upcoming_rows =
                data["upcoming"].toObject()["upcomingTable"].toObject()["rows"].toArray();
            for (const auto& v : upcoming_rows) {
                const auto e = v.toObject();
                Entry x;
                x.company    = e["companyName"].toString();
                x.ticker     = e["proposedTickerSymbol"].toString();
                x.exchange   = e["proposedExchange"].toString();
                x.date_raw   = e["expectedPriceDate"].toString();
                x.date       = parse_date(x.date_raw);
                x.price_range = e["proposedSharePrice"].toString();
                x.shares_raw = e["sharesOffered"].toString();
                x.deal_size  = format_deal_size(x.price_range, x.shares_raw);
                x.status     = "upcoming";
                if (!x.company.isEmpty()) self->entries_.append(std::move(x));
            }
            // Priced — recent IPOs that have begun trading. Critical for the
            // RECENTLY PRICED bucket the user explicitly asked for.
            const auto priced_rows = data["priced"].toObject()["rows"].toArray();
            for (const auto& v : priced_rows) {
                const auto e = v.toObject();
                Entry x;
                x.company    = e["companyName"].toString();
                x.ticker     = e["proposedTickerSymbol"].toString();
                x.exchange   = e["proposedExchange"].toString();
                x.date_raw   = e["pricedDate"].toString();
                x.date       = parse_date(x.date_raw);
                x.price_range = e["proposedSharePrice"].toString();
                x.shares_raw = e["sharesOffered"].toString();
                x.deal_size  = format_deal_size(x.price_range, x.shares_raw);
                x.status     = "priced";
                if (!x.company.isEmpty()) self->entries_.append(std::move(x));
            }
        } else {
            LOG_WARN("IpoWatch", "Nasdaq fetch failed for " + yyyymm);
        }
        self->on_fetch_done();
    });
}

void IpoWatchView::on_fetch_done() {
    if (--pending_fetches_ > 0) return;

    // Dedup — adjacent months sometimes return the same priced rows; key on
    // company+date so we don't show the same deal twice.
    QHash<QString, int> seen;
    QVector<Entry> uniq;
    uniq.reserve(entries_.size());
    for (const auto& e : entries_) {
        const QString key = e.company + "|" + e.date_raw + "|" + e.status;
        if (seen.contains(key)) continue;
        seen.insert(key, 1);
        uniq.append(e);
    }
    entries_ = std::move(uniq);

    populate();
    status_lbl_->setText(
        QString("%1 IPOs · Updated %2")
            .arg(entries_.size())
            .arg(QDateTime::currentDateTime().toString("HH:mm")));
}

// ── Bucketing + rendering ────────────────────────────────────────────────────

IpoWatchView::Bucket IpoWatchView::bucket_for(const Entry& e) const {
    if (e.status == "priced") return BucketPriced;
    if (!e.date.isValid())    return BucketNext12Months;
    const QDate today = QDate::currentDate();
    const qint64 days = today.daysTo(e.date);
    if (days < 0)         return BucketPriced;          // expected date already passed
    if (days <= 7)        return BucketThisWeek;
    if (e.date.month() == today.month() && e.date.year() == today.year())
        return BucketThisMonth;
    if (days <= 30 * 6)   return BucketNext6Months;
    return BucketNext12Months;
}

void IpoWatchView::fill_table(QTableWidget* table, const QVector<int>& indices) {
    table->setSortingEnabled(false);
    table->setRowCount(indices.size());
    for (int i = 0; i < indices.size(); ++i) {
        const int gi = indices.at(i);
        const Entry& e = entries_.at(gi);
        auto* co = new QTableWidgetItem(e.company);
        co->setData(Qt::UserRole, gi);   // index back into entries_
        co->setToolTip(e.company);
        table->setItem(i, 0, co);
        table->setItem(i, 1, new QTableWidgetItem(e.ticker.isEmpty() ? "—" : e.ticker));
        table->setItem(i, 2, new QTableWidgetItem(e.exchange.isEmpty() ? "—" : e.exchange));
        const QString date_disp = e.date.isValid() ? e.date.toString("MMM d, yyyy") : e.date_raw;
        table->setItem(i, 3, new QTableWidgetItem(date_disp));
        table->setItem(i, 4, new QTableWidgetItem(e.price_range.isEmpty() ? "—" : e.price_range));
        table->setItem(i, 5, new QTableWidgetItem(e.shares_raw.isEmpty() ? "—" : e.shares_raw));
        table->setItem(i, 6, new QTableWidgetItem(e.deal_size.isEmpty() ? "—" : e.deal_size));
    }
    table->setSortingEnabled(true);
    // Default-sort: priced bucket newest first, others soonest first.
    const bool priced_bucket = !indices.isEmpty() && entries_.at(indices.first()).status == "priced";
    table->sortItems(3, priced_bucket ? Qt::DescendingOrder : Qt::AscendingOrder);
    table->resizeRowsToContents();
}

void IpoWatchView::populate() {
    QVector<int> buckets[BucketCount];
    const QString q = last_query_.toLower();
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_.at(i);
        if (!q.isEmpty()) {
            if (!e.company.toLower().contains(q) &&
                !e.ticker.toLower().contains(q) &&
                !e.exchange.toLower().contains(q))
                continue;
        }
        buckets[bucket_for(e)].append(i);
    }
    for (int i = 0; i < BucketCount; ++i) {
        const auto& indices = buckets[i];
        table_headers_[i]->setText(
            QString("%1 · %2").arg(bucket_title(static_cast<Bucket>(i))).arg(indices.size()));
        // Collapse empty buckets — keeps the column dense on wide screens.
        const bool any = !indices.isEmpty();
        table_headers_[i]->setVisible(any);
        tables_[i]->setVisible(any);
        if (any) fill_table(tables_[i], indices);
    }
}

void IpoWatchView::on_row_selected(const Entry& e) {
    detail_html_lbl_->setText(detail_html(e));
}

QString IpoWatchView::detail_html(const Entry& e) const {
    // SEC EDGAR search URL for the company — opens the issuer's filing list,
    // typically including S-1 / 424B prospectuses for the IPO.
    const QString edgar = QStringLiteral("https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=") +
                          QUrl::toPercentEncoding(e.company) + "&type=S-1&dateb=&owner=include&count=20";
    // yfinance quote page link for priced deals — lets the user jump to live data.
    const QString yahoo = e.ticker.isEmpty()
                              ? QString()
                              : QStringLiteral("https://finance.yahoo.com/quote/") + e.ticker;

    QString h;
    h += "<style>"
         "body{font-family:Consolas,Menlo,monospace;}"
         "h3{margin:0 0 6px 0;}"
         "table{margin-top:6px;border-collapse:collapse;}"
         "td{padding:3px 8px 3px 0;vertical-align:top;}"
         ".k{color:#999;text-transform:uppercase;font-size:10px;letter-spacing:.5px;}"
         "</style>";
    h += "<h3>" + e.company.toHtmlEscaped() + "</h3>";
    h += "<table>";
    auto row = [&](const QString& k, const QString& v) {
        h += "<tr><td class='k'>" + k + "</td><td>" + (v.isEmpty() ? "—" : v.toHtmlEscaped()) + "</td></tr>";
    };
    row("Status",   e.status == "priced" ? "Recently priced" : "Upcoming");
    row("Ticker",   e.ticker);
    row("Exchange", e.exchange);
    row("Date",     e.date.isValid() ? e.date.toString("MMMM d, yyyy") : e.date_raw);
    row("Price",    e.price_range);
    row("Shares",   e.shares_raw);
    row("Deal size",e.deal_size);
    h += "</table>";
    h += "<p style='margin-top:10px;'>"
         "<a href='" + edgar + "'>SEC EDGAR filings</a>";
    if (!yahoo.isEmpty())
        h += " &nbsp;·&nbsp; <a href='" + yahoo + "'>Yahoo Finance quote</a>";
    h += "</p>";
    return h;
}

// "$15.00-$17.00" + "10,000,000" → "$160M" (midpoint × shares, rounded).
QString IpoWatchView::format_deal_size(const QString& price_range, const QString& shares) {
    if (price_range.isEmpty() || shares.isEmpty())
        return {};
    // Strip "$" and ","; parse the price midpoint from "lo-hi" or just "lo".
    // Nasdaq returns both "$15.00-$17.00" and "$15.00 - $17.00" (with spaces)
    // — trim each split fragment so QString::toDouble doesn't choke on the
    // trailing space.
    QString pr = price_range;
    pr.remove('$');
    const QStringList parts = pr.split('-', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    bool ok_lo = false, ok_hi = false;
    const double lo = parts.at(0).trimmed().toDouble(&ok_lo);
    const double hi = parts.at(parts.size() > 1 ? 1 : 0).trimmed().toDouble(&ok_hi);
    if (!ok_lo) return {};
    const double mid = (ok_hi && parts.size() > 1) ? (lo + hi) / 2.0 : lo;
    QString sh = shares; sh.remove(',');
    bool ok_sh = false;
    const double n = sh.trimmed().toDouble(&ok_sh);
    if (!ok_sh || n <= 0) return {};
    const double dollars = mid * n;
    if (dollars >= 1e9) return QString("$%1B").arg(dollars / 1e9, 0, 'f', 2);
    if (dollars >= 1e6) return QString("$%1M").arg(dollars / 1e6, 0, 'f', 0);
    return QString("$%1K").arg(dollars / 1e3, 0, 'f', 0);
}

} // namespace fincept::screens::widgets

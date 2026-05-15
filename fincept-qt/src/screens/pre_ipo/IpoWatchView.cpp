#include "screens/pre_ipo/IpoWatchView.h"

#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "services/markets/MarketDataService.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
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
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens::widgets {

namespace {
constexpr int kMonthsBack    = 3;   // pricedDate window for KPIs / RECENTLY PRICED
constexpr int kMonthsForward = 12;  // forward window: pulls This Week / This Month / 6 / 12 mo

// Time-window threshold helpers (days).
constexpr qint64 kDaysWeek    = 7;
constexpr qint64 kDays30      = 30;
constexpr qint64 kDays6Months = 30 * 6;
constexpr qint64 kDays12Mo    = 30 * 12;

// Small layout constants — chosen for density on 1920+ widths but graceful on 1440.
constexpr int kTitleHeight   = 38;
constexpr int kKpiHeight     = 44;
constexpr int kFilterHeight  = 36;
constexpr int kKpiCellMinW   = 130;
} // namespace

const char* IpoWatchView::lens_label(Lens l) {
    switch (l) {
        case LensCalendar:    return "CALENDAR";
        case LensPerformance: return "PERFORMANCE";
        case LensBookrunners: return "BOOKRUNNERS";
        default:              return "";
    }
}

// ── Construction ─────────────────────────────────────────────────────────────

IpoWatchView::IpoWatchView(QWidget* parent) : QWidget(parent) {
    build_ui();
    apply_styles();
    refresh();
}

void IpoWatchView::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    build_title_bar(root);
    build_kpi_strip(root);
    build_filter_row(root);
    build_workspace(root);
}

void IpoWatchView::build_title_bar(QVBoxLayout* root) {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(kTitleHeight);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(0);

    title_lbl_ = new QLabel("IPO WATCH");
    h->addWidget(title_lbl_);

    h->addSpacing(20);

    // Lens tabs — top-right "tab" feel like Bloomberg's function selector.
    for (int i = 0; i < LensCount; ++i) {
        auto* b = new QPushButton(lens_label(static_cast<Lens>(i)));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(28);
        b->setProperty("lensIdx", i);
        connect(b, &QPushButton::clicked, this, [this, i]() {
            active_lens_ = static_cast<Lens>(i);
            for (int j = 0; j < LensCount; ++j)
                if (lens_btns_[j]) lens_btns_[j]->setChecked(j == i);
            if (active_lens_ == LensPerformance) enrich_priced_with_quotes();
            render();
        });
        lens_btns_[i] = b;
        h->addWidget(b);
    }
    lens_btns_[LensCalendar]->setChecked(true);

    h->addStretch();

    status_lbl_ = new QLabel("Loading…");
    h->addWidget(status_lbl_);

    refresh_btn_ = new QPushButton(QStringLiteral("↻"));
    refresh_btn_->setFixedSize(28, 24);
    refresh_btn_->setCursor(Qt::PointingHandCursor);
    refresh_btn_->setToolTip("Re-fetch the Nasdaq IPO calendar");
    connect(refresh_btn_, &QPushButton::clicked, this, &IpoWatchView::refresh);
    h->addWidget(refresh_btn_);

    root->addWidget(bar);
}

void IpoWatchView::build_kpi_strip(QVBoxLayout* root) {
    kpi_strip_ = new QWidget(this);
    kpi_strip_->setFixedHeight(kKpiHeight);
    auto* h = new QHBoxLayout(kpi_strip_);
    h->setContentsMargins(12, 4, 12, 4);
    h->setSpacing(0);

    auto add_cell = [&](QLabel*& target, const QString& seed) {
        auto* w = new QWidget;
        w->setMinimumWidth(kKpiCellMinW);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(8, 2, 8, 2);
        v->setSpacing(0);
        target = new QLabel(seed);
        target->setProperty("kpi", true);
        v->addWidget(target);
        h->addWidget(w);
    };
    add_cell(kpi_week_,  "THIS WEEK —");
    add_cell(kpi_month_, "THIS MONTH —");
    add_cell(kpi_pop_,   "30D POP —");
    add_cell(kpi_above_, "ABOVE —");
    add_cell(kpi_in_,    "IN-RANGE —");
    add_cell(kpi_below_, "BELOW —");
    h->addStretch();

    root->addWidget(kpi_strip_);
}

void IpoWatchView::build_filter_row(QVBoxLayout* root) {
    filter_row_ = new QWidget(this);
    filter_row_->setFixedHeight(kFilterHeight);
    auto* h = new QHBoxLayout(filter_row_);
    h->setContentsMargins(12, 4, 12, 4);
    h->setSpacing(8);

    auto make_chip = [&](const QString& placeholder, QStringList opts) -> QComboBox* {
        auto* c = new QComboBox;
        c->setFixedHeight(24);
        c->setMinimumWidth(110);
        c->setProperty("chip", true);
        c->setToolTip(placeholder);
        c->addItems(std::move(opts));
        h->addWidget(c);
        return c;
    };
    status_chip_ = make_chip("Status",   {"ALL", "UPCOMING", "PRICED"});
    window_chip_ = make_chip("Window",   {"ALL UPCOMING", "THIS WEEK", "NEXT 30 DAYS",
                                          "NEXT 6 MONTHS", "NEXT 12 MONTHS", "PAST 30 DAYS (PRICED)"});
    exch_chip_   = make_chip("Exchange", {"ALL", "NASDAQ", "NYSE", "AMEX"});
    size_chip_   = make_chip("Deal Size",{"ALL", "< $100M", "$100-500M", "$500M+"});

    h->addSpacing(8);
    auto* lbl = new QLabel("⌕");
    lbl->setStyleSheet(QString("color:%1;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    h->addWidget(lbl);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Company, ticker, bookrunner…");
    search_->setFixedHeight(24);
    h->addWidget(search_, 1);

    connect(status_chip_, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        status_filter_ = s.toLower();
        render();
    });
    connect(window_chip_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        time_window_ = static_cast<TimeWindow>(i);
        render();
    });
    connect(exch_chip_, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        exch_filter_ = s == "ALL" ? "all" : s;
        render();
    });
    connect(size_chip_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        static const char* keys[] = {"all", "<100", "100-500", "500+"};
        size_filter_ = keys[std::min(i, 3)];
        render();
    });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& q) {
        search_query_ = q.trimmed().toLower();
        render();
    });

    root->addWidget(filter_row_);
}

void IpoWatchView::build_workspace(QVBoxLayout* root) {
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setHandleWidth(1);
    splitter_->setChildrenCollapsible(true);  // user can drag the detail rail to 0

    // ── Table ──
    table_ = new QTableWidget;
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setSortingEnabled(true);
    table_->setShowGrid(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int /*col*/) {
        auto* it = table_->item(row, 0);
        if (!it) return;
        const int idx = it->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < entries_.size())
            render_detail(&entries_.at(idx));
    });
    splitter_->addWidget(table_);

    // ── Detail rail ──
    build_detail_rail(splitter_);

    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({900, 360});

    root->addWidget(splitter_, 1);
}

void IpoWatchView::build_detail_rail(QSplitter* splitter) {
    detail_pane_ = new QWidget;
    auto* v = new QVBoxLayout(detail_pane_);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(6);

    detail_html_ = new QLabel("Select a row to see deal detail, research links and sector comps.");
    detail_html_->setTextFormat(Qt::RichText);
    detail_html_->setWordWrap(true);
    detail_html_->setAlignment(Qt::AlignTop);
    detail_html_->setOpenExternalLinks(true);

    auto* sc = new QScrollArea;
    sc->setWidget(detail_html_);
    sc->setWidgetResizable(true);
    sc->setFrameShape(QFrame::NoFrame);
    v->addWidget(sc, 1);

    splitter->addWidget(detail_pane_);
}

void IpoWatchView::apply_styles() {
    using ui::colors::AMBER;
    using ui::colors::BG_BASE;
    using ui::colors::BG_RAISED;
    using ui::colors::BG_SURFACE;
    using ui::colors::BORDER_DIM;
    using ui::colors::TEXT_PRIMARY;
    using ui::colors::TEXT_SECONDARY;

    setStyleSheet(QString("background:%1;").arg(BG_BASE()));

    if (title_lbl_)
        title_lbl_->setStyleSheet(
            QString("color:%1;font-size:14px;font-weight:800;letter-spacing:2px;background:transparent;")
                .arg(AMBER()));

    const QString lens_qss =
        QString("QPushButton{background:transparent;color:%1;border:none;padding:0 14px;"
                "font-size:12px;font-weight:700;letter-spacing:1px;"
                "border-bottom:2px solid transparent;}"
                "QPushButton:checked{color:%2;border-bottom-color:%2;}"
                "QPushButton:hover:!checked{color:%3;}")
            .arg(TEXT_SECONDARY(), AMBER(), TEXT_PRIMARY());
    for (int i = 0; i < LensCount; ++i)
        if (lens_btns_[i]) lens_btns_[i]->setStyleSheet(lens_qss);

    if (refresh_btn_)
        refresh_btn_->setStyleSheet(
            QString("QPushButton{background:%1;color:%2;border:1px solid %2;border-radius:2px;font-weight:bold;}"
                    "QPushButton:hover{background:%2;color:%3;}")
                .arg(BG_SURFACE(), AMBER(), BG_BASE()));
    if (status_lbl_)
        status_lbl_->setStyleSheet(
            QString("color:%1;font-size:11px;background:transparent;padding-right:10px;")
                .arg(TEXT_SECONDARY()));

    if (kpi_strip_)
        kpi_strip_->setStyleSheet(
            QString("background:%1;border-bottom:1px solid %2;border-top:1px solid %2;")
                .arg(BG_SURFACE(), BORDER_DIM()));

    const QString kpi_qss =
        QString("QLabel{color:%1;font-size:11px;background:transparent;}").arg(TEXT_PRIMARY());
    for (QLabel* l : {kpi_week_, kpi_month_, kpi_pop_, kpi_above_, kpi_in_, kpi_below_})
        if (l) l->setStyleSheet(kpi_qss);

    if (filter_row_)
        filter_row_->setStyleSheet(QString("background:%1;").arg(BG_RAISED()));
    const QString chip_qss =
        QString("QComboBox{background:%1;color:%2;border:1px solid %3;border-radius:2px;padding:0 6px;}"
                "QComboBox::drop-down{border:none;width:14px;}"
                "QComboBox QAbstractItemView{background:%1;color:%2;selection-background-color:%4;}")
            .arg(BG_BASE(), TEXT_PRIMARY(), BORDER_DIM(), AMBER());
    for (QComboBox* c : {status_chip_, window_chip_, exch_chip_, size_chip_})
        if (c) c->setStyleSheet(chip_qss);
    if (search_)
        search_->setStyleSheet(
            QString("QLineEdit{background:%1;color:%2;border:1px solid %3;border-radius:2px;padding:2px 6px;}"
                    "QLineEdit:focus{border-color:%4;}")
                .arg(BG_BASE(), TEXT_PRIMARY(), BORDER_DIM(), AMBER()));

    if (table_)
        table_->setStyleSheet(
            QString("QTableWidget{background:%1;color:%2;border:0;gridline-color:%3;"
                    "alternate-background-color:%4;}"
                    "QHeaderView::section{background:%5;color:%6;border:0;padding:4px 8px;"
                    "font-weight:bold;font-size:10px;}"
                    "QTableWidget::item:selected{background:%7;color:%8;}")
                .arg(BG_BASE(), TEXT_PRIMARY(), BORDER_DIM(), BG_RAISED(), BG_RAISED(),
                     TEXT_SECONDARY(), AMBER(), BG_BASE()));
    if (detail_pane_)
        detail_pane_->setStyleSheet(
            QString("background:%1;border-left:1px solid %2;").arg(BG_SURFACE(), BORDER_DIM()));
    if (detail_html_)
        detail_html_->setStyleSheet(
            QString("color:%1;background:%2;").arg(TEXT_PRIMARY(), BG_SURFACE()));
}

// ── Networking ───────────────────────────────────────────────────────────────

void IpoWatchView::refresh() {
    if (pending_fetches_ > 0) return;
    entries_.clear();
    if (status_lbl_) status_lbl_->setText("Loading…");

    const QDate today = QDate::currentDate();
    pending_fetches_ = kMonthsBack + kMonthsForward + 1;
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
            auto parse_bookrunners = [](const QString& raw) -> QStringList {
                // Nasdaq returns "JP Morgan, Goldman Sachs, Morgan Stanley" — split + trim.
                QStringList out;
                for (const auto& s : raw.split(',', Qt::SkipEmptyParts))
                    out.append(s.trimmed());
                return out;
            };

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
                x.shares     = parse_shares(x.shares_raw);
                x.deal_size  = format_deal_size(x.price_range, x.shares_raw);
                bool ok_mid = false;
                const double mid = parse_price_mid(x.price_range, &ok_mid);
                x.deal_size_dollars = (ok_mid && x.shares > 0) ? mid * x.shares : 0.0;
                x.all_bookrunners = parse_bookrunners(e["leadFirmName"].toString());
                x.bookrunner      = x.all_bookrunners.value(0);
                x.status     = "upcoming";
                if (!x.company.isEmpty()) self->entries_.append(std::move(x));
            }
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
                x.final_price_raw = x.price_range; // priced API returns the final price here
                bool ok_fp = false;
                x.final_price = parse_price_mid(x.price_range, &ok_fp);
                x.shares_raw = e["sharesOffered"].toString();
                x.shares     = parse_shares(x.shares_raw);
                x.deal_size  = format_deal_size(x.price_range, x.shares_raw);
                x.deal_size_dollars = (ok_fp && x.shares > 0) ? x.final_price * x.shares : 0.0;
                x.all_bookrunners = parse_bookrunners(e["leadFirmName"].toString());
                x.bookrunner      = x.all_bookrunners.value(0);
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

    // Dedup — adjacent months sometimes return the same priced rows.
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

    if (status_lbl_)
        status_lbl_->setText(
            QString("%1 deals · %2").arg(entries_.size())
                .arg(QDateTime::currentDateTime().toString("HH:mm")));

    render();
}

// Lazily fetch yfinance quotes for priced tickers so the PERFORMANCE lens can
// show pop %. Single batch_quotes call — cheap. Only re-fetches symbols whose
// perf_fetched is false, so subsequent lens switches are no-ops.
void IpoWatchView::enrich_priced_with_quotes() {
    QStringList syms;
    for (const auto& e : entries_)
        if (e.status == "priced" && !e.ticker.isEmpty() && !e.perf_fetched)
            syms.append(e.ticker);
    if (syms.isEmpty()) return;

    QPointer<IpoWatchView> self = this;
    services::MarketDataService::instance().fetch_quotes(
        syms, [self](bool ok, QVector<services::QuoteData> quotes) {
            if (!self || !ok) return;
            QHash<QString, double> price_map;
            for (const auto& q : quotes) price_map.insert(q.symbol, q.price);
            for (auto& e : self->entries_) {
                if (e.status != "priced" || e.ticker.isEmpty()) continue;
                auto it = price_map.constFind(e.ticker);
                if (it == price_map.constEnd()) continue;
                e.last_price = it.value();
                e.pop_pct = (e.final_price > 0)
                    ? (e.last_price - e.final_price) / e.final_price * 100.0 : 0;
                e.perf_fetched = true;
            }
            if (self->active_lens_ == LensPerformance) self->render();
        });
}

// ── Rendering ────────────────────────────────────────────────────────────────

bool IpoWatchView::entry_passes_filters(const Entry& e) const {
    if (status_filter_ != "all") {
        if (e.status != status_filter_) return false;
    }
    if (exch_filter_ != "all" && !e.exchange.contains(exch_filter_, Qt::CaseInsensitive)) return false;
    if (size_filter_ != "all") {
        const double m = e.deal_size_dollars / 1e6;
        if      (size_filter_ == "<100"    && !(m < 100))            return false;
        else if (size_filter_ == "100-500" && !(m >= 100 && m < 500)) return false;
        else if (size_filter_ == "500+"    && !(m >= 500))           return false;
    }
    // Time window: only apply when status is "upcoming" (or ALL where the entry is upcoming).
    // PAST 30 DAYS applies to priced.
    const QDate today = QDate::currentDate();
    if (e.status == "upcoming" && e.date.isValid()) {
        const qint64 d = today.daysTo(e.date);
        switch (time_window_) {
            case TW_ThisWeek:  if (!(d >= 0 && d <= kDaysWeek)) return false; break;
            case TW_30Days:    if (!(d >= 0 && d <= kDays30))    return false; break;
            case TW_6Months:   if (!(d >= 0 && d <= kDays6Months)) return false; break;
            case TW_12Months:  if (!(d >= 0 && d <= kDays12Mo))  return false; break;
            case TW_Past30Days: return false; // upcoming row in "past 30d" filter
            case TW_AllUpcoming: default: break;
        }
    } else if (e.status == "priced") {
        // Apply only the "past 30d" window; other windows keep priced visible.
        if (time_window_ == TW_Past30Days && e.date.isValid()) {
            const qint64 d = e.date.daysTo(today);
            if (!(d >= 0 && d <= kDays30)) return false;
        }
    }
    if (!search_query_.isEmpty()) {
        if (!e.company.toLower().contains(search_query_) &&
            !e.ticker.toLower().contains(search_query_) &&
            !e.exchange.toLower().contains(search_query_) &&
            !e.bookrunner.toLower().contains(search_query_))
            return false;
    }
    return true;
}

QVector<int> IpoWatchView::filtered_indices() const {
    QVector<int> out;
    out.reserve(entries_.size());
    for (int i = 0; i < entries_.size(); ++i)
        if (entry_passes_filters(entries_.at(i)))
            out.append(i);
    return out;
}

void IpoWatchView::render() {
    render_kpis();
    switch (active_lens_) {
        case LensCalendar:    render_calendar(); break;
        case LensPerformance: render_performance(); break;
        case LensBookrunners: render_bookrunners(); break;
        default: break;
    }
}

void IpoWatchView::render_kpis() {
    using ui::colors::AMBER;
    using ui::colors::POSITIVE;
    using ui::colors::NEGATIVE;
    using ui::colors::TEXT_SECONDARY;

    const QDate today = QDate::currentDate();
    int week_n = 0, month_n = 0;
    double week_$ = 0, month_$ = 0;
    int priced_30 = 0; double pop_sum = 0;
    int n_priced_with_pop = 0;
    // Above/in/below range — for priced entries we'd need filed range AND
    // final price. Nasdaq's priced rows only carry the final price; we don't
    // have the original range. Approximate "above-range" buckets using
    // performance-since-IPO instead: pop>0 ≈ market validated, pop<0 ≈ broke.
    int abv = 0, inr = 0, blw = 0;

    for (const auto& e : entries_) {
        if (e.status == "upcoming" && e.date.isValid()) {
            const qint64 d = today.daysTo(e.date);
            if (d >= 0 && d <= kDaysWeek)  { ++week_n;  week_$  += e.deal_size_dollars; }
            if (d >= 0 && d <= kDays30)    { ++month_n; month_$ += e.deal_size_dollars; }
        } else if (e.status == "priced" && e.date.isValid()) {
            const qint64 d = e.date.daysTo(today);
            if (d >= 0 && d <= kDays30) {
                ++priced_30;
                if (e.perf_fetched && e.final_price > 0) {
                    pop_sum += e.pop_pct; ++n_priced_with_pop;
                    if      (e.pop_pct >  2)  ++abv;
                    else if (e.pop_pct < -2) ++blw;
                    else                      ++inr;
                }
            }
        }
    }

    auto fmt = [](int n, double dollars) {
        return QString("%1 · %2").arg(n).arg(format_money(dollars));
    };
    auto pct = [](int part, int whole) -> QString {
        if (whole == 0) return "—";
        return QString::number(part * 100.0 / whole, 'f', 0) + "%";
    };

    if (kpi_week_)
        kpi_week_->setText(QString("<b>THIS WEEK</b><br>%1").arg(fmt(week_n, week_$)));
    if (kpi_month_)
        kpi_month_->setText(QString("<b>THIS MONTH</b><br>%1").arg(fmt(month_n, month_$)));
    if (kpi_pop_) {
        const QString s = (n_priced_with_pop == 0)
            ? "—"
            : QString("%1%2%").arg(pop_sum / n_priced_with_pop >= 0 ? "+" : "")
                              .arg(pop_sum / n_priced_with_pop, 0, 'f', 1);
        const QString color = (n_priced_with_pop == 0)
            ? QString(TEXT_SECONDARY)
            : (pop_sum >= 0 ? QString(POSITIVE) : QString(NEGATIVE));
        kpi_pop_->setText(QString("<b>30D AVG POP</b><br><span style='color:%1;'>%2</span>").arg(color, s));
    }
    if (kpi_above_)
        kpi_above_->setText(QString("<b>ABOVE (pop&gt;2%%)</b><br><span style='color:%1;'>%2 (%3)</span>")
                                .arg(POSITIVE).arg(abv).arg(pct(abv, n_priced_with_pop)));
    if (kpi_in_)
        kpi_in_->setText(QString("<b>NEAR-RANGE</b><br>%1 (%2)").arg(inr).arg(pct(inr, n_priced_with_pop)));
    if (kpi_below_)
        kpi_below_->setText(QString("<b>BELOW (pop&lt;-2%%)</b><br><span style='color:%1;'>%2 (%3)</span>")
                                .arg(NEGATIVE).arg(blw).arg(pct(blw, n_priced_with_pop)));
}

void IpoWatchView::render_calendar() {
    using ui::colors::AMBER;
    using ui::colors::POSITIVE;
    using ui::colors::NEGATIVE;

    const QStringList headers{"COMPANY", "TKR", "EXCH", "DATE", "STATUS",
                              "PRICE / RANGE", "SHARES", "DEAL SIZE", "BOOKRUNNER"};
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);

    const QVector<int> indices = filtered_indices();
    table_->setRowCount(indices.size());
    for (int i = 0; i < indices.size(); ++i) {
        const int gi = indices.at(i);
        const Entry& e = entries_.at(gi);
        auto* co = new QTableWidgetItem(e.company);
        co->setData(Qt::UserRole, gi);
        co->setToolTip(e.company);
        table_->setItem(i, 0, co);
        table_->setItem(i, 1, new QTableWidgetItem(e.ticker.isEmpty() ? "—" : e.ticker));
        table_->setItem(i, 2, new QTableWidgetItem(e.exchange.isEmpty() ? "—" : e.exchange));
        table_->setItem(i, 3, new QTableWidgetItem(
            e.date.isValid() ? e.date.toString("MMM d, yyyy") : e.date_raw));
        auto* st = new QTableWidgetItem(e.status.toUpper());
        st->setForeground(QBrush(QColor(e.status == "priced" ? POSITIVE() : AMBER())));
        table_->setItem(i, 4, st);
        table_->setItem(i, 5, new QTableWidgetItem(e.price_range.isEmpty() ? "—" : e.price_range));
        table_->setItem(i, 6, new QTableWidgetItem(e.shares_raw.isEmpty() ? "—" : e.shares_raw));
        table_->setItem(i, 7, new QTableWidgetItem(e.deal_size.isEmpty() ? "—" : e.deal_size));
        table_->setItem(i, 8, new QTableWidgetItem(e.bookrunner.isEmpty() ? "—" : e.bookrunner));
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < table_->columnCount(); ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    // Default sort: ascending date — upcoming-soonest at top.
    table_->sortItems(3, Qt::AscendingOrder);
}

void IpoWatchView::render_performance() {
    using ui::colors::POSITIVE;
    using ui::colors::NEGATIVE;
    using ui::colors::TEXT_SECONDARY;

    const QStringList headers{"COMPANY", "TKR", "PRICED", "OFFER", "LAST", "POP %", "DEAL $", "BOOKRUNNER"};
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);

    // Only priced entries that pass filters.
    QVector<int> rows;
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_.at(i);
        if (e.status != "priced") continue;
        if (!entry_passes_filters(e)) continue;
        rows.append(i);
    }
    table_->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const int gi = rows.at(r);
        const Entry& e = entries_.at(gi);
        auto* co = new QTableWidgetItem(e.company);
        co->setData(Qt::UserRole, gi);
        co->setToolTip(e.company);
        table_->setItem(r, 0, co);
        table_->setItem(r, 1, new QTableWidgetItem(e.ticker.isEmpty() ? "—" : e.ticker));
        table_->setItem(r, 2, new QTableWidgetItem(
            e.date.isValid() ? e.date.toString("MMM d") : e.date_raw));
        table_->setItem(r, 3, new QTableWidgetItem(
            e.final_price > 0 ? QString("$%1").arg(e.final_price, 0, 'f', 2) : "—"));
        table_->setItem(r, 4, new QTableWidgetItem(
            e.perf_fetched && e.last_price > 0
                ? QString("$%1").arg(e.last_price, 0, 'f', 2) : "—"));
        QString pop_str;
        QColor  pop_col(TEXT_SECONDARY());
        if (e.perf_fetched && e.final_price > 0) {
            pop_str = QString("%1%2%").arg(e.pop_pct >= 0 ? "+" : "").arg(e.pop_pct, 0, 'f', 1);
            pop_col = QColor(e.pop_pct >= 0 ? POSITIVE() : NEGATIVE());
        } else {
            pop_str = "…";
        }
        auto* pop = new QTableWidgetItem(pop_str);
        pop->setForeground(QBrush(pop_col));
        table_->setItem(r, 5, pop);
        table_->setItem(r, 6, new QTableWidgetItem(e.deal_size.isEmpty() ? "—" : e.deal_size));
        table_->setItem(r, 7, new QTableWidgetItem(e.bookrunner.isEmpty() ? "—" : e.bookrunner));
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < table_->columnCount(); ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    table_->sortItems(5, Qt::DescendingOrder); // pop desc
}

void IpoWatchView::render_bookrunners() {
    using ui::colors::POSITIVE;
    using ui::colors::AMBER;

    // Aggregate per bookrunner. We only count rows that pass the filters, so
    // the same filter row drives this lens too — useful for "who priced
    // anything above $500M this month?" style queries.
    struct Agg { int upcoming = 0; int priced = 0; double dollars = 0; };
    QHash<QString, Agg> by_bank;
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_.at(i);
        if (!entry_passes_filters(e)) continue;
        const QString key = e.bookrunner.isEmpty() ? QStringLiteral("Unknown") : e.bookrunner;
        Agg& a = by_bank[key];
        if (e.status == "upcoming") ++a.upcoming; else ++a.priced;
        a.dollars += e.deal_size_dollars;
    }

    const QStringList headers{"BOOKRUNNER", "UPCOMING", "PRICED", "TOTAL", "$ VOLUME"};
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->setRowCount(by_bank.size());
    int r = 0;
    QList<QString> keys = by_bank.keys();
    std::sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) {
        return by_bank[a].dollars > by_bank[b].dollars;
    });
    for (const QString& bank : keys) {
        const Agg& a = by_bank.value(bank);
        table_->setItem(r, 0, new QTableWidgetItem(bank));
        table_->setItem(r, 1, new QTableWidgetItem(QString::number(a.upcoming)));
        table_->setItem(r, 2, new QTableWidgetItem(QString::number(a.priced)));
        table_->setItem(r, 3, new QTableWidgetItem(QString::number(a.upcoming + a.priced)));
        table_->setItem(r, 4, new QTableWidgetItem(format_money(a.dollars)));
        ++r;
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < table_->columnCount(); ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    table_->sortItems(4, Qt::DescendingOrder);
}

// ── Detail rail ──────────────────────────────────────────────────────────────

void IpoWatchView::render_detail(const Entry* e) {
    if (!detail_html_) return;
    if (!e) {
        detail_html_->setText("Select a row to see deal detail, research links and sector comps.");
        return;
    }
    // Research links — manual research one click away, no API keys required.
    const QString company_q = QUrl::toPercentEncoding(e->company);
    const QString edgar_s1 =
        "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=" + company_q +
        "&type=S-1&dateb=&owner=include&count=20";
    const QString edgar_all =
        "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=" + company_q +
        "&type=&dateb=&owner=include&count=40";
    const QString edgar_144 =
        "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=" + company_q +
        "&type=144&dateb=&owner=include&count=20";
    const QString yahoo = e->ticker.isEmpty()
        ? QString()
        : ("https://finance.yahoo.com/quote/" + e->ticker);
    const QString gnews = "https://news.google.com/search?q=" + company_q + "%20IPO";
    const QString gsearch = "https://www.google.com/search?q=" +
                            QUrl::toPercentEncoding(e->company + " founders CEO");
    const QString wiki = "https://en.wikipedia.org/wiki/Special:Search?search=" + company_q;
    const QString crunchbase = "https://www.crunchbase.com/textsearch?q=" + company_q;
    const QString linkedin = "https://www.linkedin.com/search/results/companies/?keywords=" + company_q;

    auto bookrun_html = [&]() -> QString {
        if (e->all_bookrunners.isEmpty()) return "—";
        QStringList shown;
        for (const auto& b : e->all_bookrunners) shown << b.toHtmlEscaped();
        return shown.join(" · ");
    };

    // Sector comps approximation: priced deals within ±90 days of this one's
    // date that share an exchange (best proxy we have without sector). Shows
    // count + average pop.
    int comp_count = 0; double comp_pop_sum = 0; int comp_pop_n = 0;
    QVector<const Entry*> comp_top;
    for (const auto& other : entries_) {
        if (&other == e) continue;
        if (other.status != "priced") continue;
        if (other.exchange != e->exchange && !e->exchange.isEmpty()) continue;
        if (e->date.isValid() && other.date.isValid()) {
            const qint64 d = std::abs(e->date.daysTo(other.date));
            if (d > 90) continue;
        }
        ++comp_count;
        if (other.perf_fetched) { comp_pop_sum += other.pop_pct; ++comp_pop_n; }
        if (comp_top.size() < 5) comp_top.append(&other);
    }

    QString h;
    h += "<style>"
         "body{font-family:Consolas,Menlo,monospace;font-size:11px;}"
         "h3{margin:0 0 8px 0;font-size:13px;}"
         "table.kv{border-collapse:collapse;}"
         "table.kv td{padding:2px 8px 2px 0;vertical-align:top;}"
         ".k{color:#999;font-size:10px;letter-spacing:.5px;}"
         ".sec{color:#d97706;font-weight:bold;font-size:10px;letter-spacing:1px;"
         "  margin-top:10px;display:block;}"
         "hr{border:none;border-top:1px solid #333;}"
         "a{color:#d97706;text-decoration:none;}"
         "a:hover{text-decoration:underline;}"
         ".pos{color:#10b981;}"
         ".neg{color:#ef4444;}"
         "</style>";
    h += "<h3>" + e->company.toHtmlEscaped() + "</h3>";

    h += "<table class='kv'>";
    auto kv = [&](const QString& k, const QString& v) {
        h += "<tr><td class='k'>" + k + "</td><td>" + (v.isEmpty() ? "—" : v) + "</td></tr>";
    };
    kv("STATUS",   e->status == "priced" ? QStringLiteral("<b>PRICED</b>") : QStringLiteral("UPCOMING"));
    kv("TICKER",   e->ticker.toHtmlEscaped());
    kv("EXCHANGE", e->exchange.toHtmlEscaped());
    kv("DATE",     e->date.isValid() ? e->date.toString("MMMM d, yyyy") : e->date_raw.toHtmlEscaped());
    kv(e->status == "priced" ? "FINAL PRICE" : "FILED RANGE", e->price_range.toHtmlEscaped());
    if (e->status == "priced" && e->perf_fetched && e->final_price > 0) {
        const QString cls = e->pop_pct >= 0 ? "pos" : "neg";
        kv("LAST PRICE", QString("$%1").arg(e->last_price, 0, 'f', 2));
        kv("POP SINCE IPO",
           QString("<span class='%1'>%2%3%</span>")
               .arg(cls, e->pop_pct >= 0 ? "+" : "")
               .arg(e->pop_pct, 0, 'f', 1));
    }
    kv("SHARES",    e->shares_raw.toHtmlEscaped());
    kv("DEAL SIZE", e->deal_size.toHtmlEscaped());
    kv("BOOKRUNNER", bookrun_html());
    h += "</table>";

    // Lock-up estimate — 180 days post-pricing is the industry-standard cap.
    if (e->status == "priced" && e->date.isValid()) {
        const QDate lockup_end = e->date.addDays(180);
        const qint64 days_left = QDate::currentDate().daysTo(lockup_end);
        h += "<span class='sec'>LOCK-UP (est. 180 days)</span><br>";
        if (days_left > 0)
            h += QString("Expires ~%1 (in %2 days)")
                     .arg(lockup_end.toString("MMM d, yyyy")).arg(days_left);
        else
            h += QString("Expired %1").arg(lockup_end.toString("MMM d, yyyy"));
    }

    h += "<span class='sec'>RESEARCH LINKS</span><br>";
    h += "<a href='" + edgar_s1  + "'>SEC: S-1 / 424B</a><br>";
    h += "<a href='" + edgar_all + "'>SEC: all filings</a><br>";
    h += "<a href='" + edgar_144 + "'>SEC: Form 144 (insider sales)</a><br>";
    if (!yahoo.isEmpty()) h += "<a href='" + yahoo + "'>Yahoo Finance quote</a><br>";
    h += "<a href='" + gnews + "'>Google News</a><br>";
    h += "<a href='" + wiki + "'>Wikipedia</a><br>";
    h += "<a href='" + linkedin + "'>LinkedIn (company)</a><br>";
    h += "<a href='" + crunchbase + "'>Crunchbase (funding history)</a><br>";
    h += "<a href='" + gsearch + "'>Google: founders &amp; CEO</a><br>";

    h += "<span class='sec'>COMPS — same exchange, ±90 days priced</span><br>";
    if (comp_count == 0) {
        h += "No priced comparables in the current dataset.";
    } else {
        h += QString("%1 deals").arg(comp_count);
        if (comp_pop_n > 0) {
            const double avg = comp_pop_sum / comp_pop_n;
            const QString cls = avg >= 0 ? "pos" : "neg";
            h += QString(" · avg pop <span class='%1'>%2%3%</span>")
                     .arg(cls, avg >= 0 ? "+" : "").arg(avg, 0, 'f', 1);
        }
        h += "<br>";
        for (const auto* c : comp_top) {
            QString pop;
            if (c->perf_fetched && c->final_price > 0) {
                const QString cls = c->pop_pct >= 0 ? "pos" : "neg";
                pop = QString(" <span class='%1'>%2%3%</span>")
                          .arg(cls, c->pop_pct >= 0 ? "+" : "")
                          .arg(c->pop_pct, 0, 'f', 1);
            }
            h += "&nbsp;&nbsp;" + c->company.toHtmlEscaped() + " (" + c->ticker.toHtmlEscaped() + ")" + pop + "<br>";
        }
    }

    detail_html_->setText(h);
}

// ── Parsing helpers ──────────────────────────────────────────────────────────

QString IpoWatchView::format_money(double dollars) {
    if (dollars <= 0)   return QStringLiteral("—");
    if (dollars >= 1e9) return QString("$%1B").arg(dollars / 1e9, 0, 'f', 2);
    if (dollars >= 1e6) return QString("$%1M").arg(dollars / 1e6, 0, 'f', 0);
    return QString("$%1K").arg(dollars / 1e3, 0, 'f', 0);
}

QString IpoWatchView::format_deal_size(const QString& price_range, const QString& shares) {
    if (price_range.isEmpty() || shares.isEmpty()) return {};
    bool ok = false;
    const double mid = parse_price_mid(price_range, &ok);
    if (!ok) return {};
    const double n = parse_shares(shares);
    if (n <= 0) return {};
    return format_money(mid * n);
}

// Parse "$15.00-$17.00" / "$17.00" / "$15.00 - $17.00" → midpoint.
double IpoWatchView::parse_price_mid(const QString& price_range, bool* ok_out) {
    if (ok_out) *ok_out = false;
    if (price_range.isEmpty()) return 0;
    QString pr = price_range;
    pr.remove('$');
    const QStringList parts = pr.split('-', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return 0;
    bool ok_lo = false, ok_hi = false;
    const double lo = parts.at(0).trimmed().toDouble(&ok_lo);
    const double hi = parts.at(parts.size() > 1 ? 1 : 0).trimmed().toDouble(&ok_hi);
    if (!ok_lo) return 0;
    if (ok_out) *ok_out = true;
    return (ok_hi && parts.size() > 1) ? (lo + hi) / 2.0 : lo;
}

double IpoWatchView::parse_shares(const QString& shares) {
    QString s = shares; s.remove(',');
    bool ok = false;
    const double n = s.trimmed().toDouble(&ok);
    return ok ? n : 0;
}

} // namespace fincept::screens::widgets

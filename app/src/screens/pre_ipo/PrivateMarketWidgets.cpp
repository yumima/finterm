// src/screens/pre_ipo/PrivateMarketWidgets.cpp
#include "screens/pre_ipo/PrivateMarketWidgets.h"

#include "core/events/EventBus.h"
#include "screens/pre_ipo/PrivateMarketCharts.h"
#include "screens/pre_ipo/PrivateMarketFormat.h"
#include "services/pre_ipo/ClarityService.h"
#include "services/pre_ipo/IpoDealsFeed.h"
#include "services/pre_ipo/PreIpoService.h"
#include "ui/tables/DataTable.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace finterm::screens::widgets {

namespace pm = finterm::pre_ipo::pm;
using namespace finterm::pre_ipo::pmfmt;
using pre_ipo::PrivateCompany;

// ── Shared ───────────────────────────────────────────────────────────────────

QString pm_ipo_cell(const PrivateCompany& c, QString* tip, QString* color, qint64* sort_key,
                    const services::IpoDealIndex* idx) {
    using namespace ui::colors;
    const QDate today = QDate::currentDate();
    *sort_key = 0x7fffffffLL;
    const services::IpoDealIndex& index = idx ? *idx : services::IpoDealsFeed::instance().index();
    if (const services::IpoDeal* e = index.deal_for(c)) {
        if (e->pending()) {
            *sort_key = e->when.isValid() ? today.daysTo(e->when) : 50'000;
            *tip = e->basis.isEmpty() ? QStringLiteral("On the IPO calendar") : e->basis;
            if (e->firm() && e->when.isValid()) {
                *color = POSITIVE();
                return QStringLiteral("PRICES ") + e->when.toString("MMM d");
            }
            if (e->kind == "estimate" && e->when.isValid()) {
                *color = AMBER();
                return QStringLiteral("EST. ~") + e->when.toString("MMM yyyy");
            }
            *color = INFO();
            return QStringLiteral("S-1 ON FILE");
        }
        if (e->status == "priced") {
            *sort_key = 100'000;
            *color = POSITIVE();
            *tip = QStringLiteral("Priced %1 at $%2").arg(e->priced.toString("MMM d, yyyy"), e->price_range);
            return QStringLiteral("PRICED ") + (e->ticker.isEmpty() ? e->priced.toString("MMM d") : e->ticker);
        }
        if (e->status == "withdrawn") {
            *sort_key = 200'000;
            *color = NEGATIVE();
            *tip = QStringLiteral("Registration withdrawn");
            return QStringLiteral("WITHDRAWN");
        }
    }
    if (!c.listed_ticker.isEmpty()) {
        *sort_key = 100'000;
        *color = POSITIVE();
        *tip = QStringLiteral("Trading as %1").arg(c.listed_ticker);
        return QStringLiteral("LISTED ") + c.listed_ticker;
    }
    if (c.s1.first_filed.isValid()) {
        *sort_key = 60'000;
        if (c.s1.withdrawn) {
            *color = NEGATIVE();
            *tip = QStringLiteral("S-1 withdrawn");
            return QStringLiteral("WITHDRAWN");
        }
        *color = INFO();
        *tip = QStringLiteral("S-1 first filed %1 — not yet on the exchange calendar")
                   .arg(c.s1.first_filed.toString("MMM d, yyyy"));
        return QStringLiteral("S-1 FILED");
    }
    *tip = QStringLiteral("No public registration on file");
    *color = TEXT_SECONDARY();
    return QStringLiteral("PRIVATE");
}

bool pm_is_public(const PrivateCompany& c, const services::IpoDealIndex* idx) {
    const QDate today = QDate::currentDate();
    if (pre_ipo::listing_state(c.listed_ticker, c.listed_on, today) == pre_ipo::ListingState::Listed)
        return true;
    bool strong = false;
    const services::IpoDealIndex& index = idx ? *idx : services::IpoDealsFeed::instance().index();
    const services::IpoDeal* e = index.deal_for(c, &strong);
    return e && strong && e->status == "priced" && e->priced.isValid() &&
           e->priced.daysTo(today) > pre_ipo::kListedGraceDays;
}

namespace {
/// True when `w` sits inside the PRIVATE MARKET screen (IpoWatchView sets
/// the property), so a click opens beside the list instead of navigating.
bool inside_private_market(QWidget* w) {
    for (QWidget* p = w; p; p = p->parentWidget())
        if (p->property("privateMarketHost").toBool())
            return true;
    return false;
}
} // namespace

namespace {
struct PendingOpen {
    QVariantMap req;
    qint64 at_ms = 0;
};
PendingOpen& pending_open() {
    static PendingOpen p;
    return p;
}
constexpr qint64 kPendingOpenMs = 10'000; // a click is answered now or not at all

void open_request(QWidget* from, const QVariantMap& req) {
    if (inside_private_market(from)) {
        EventBus::instance().publish("private_market.open", req);
        return;
    }
    // The screen is switched to asynchronously and may be built only then:
    // park the request for its showEvent. When it is already on screen (a
    // split beside the Dashboard) no show follows — the queued publish below
    // reaches it, and the view clears the parked copy.
    pending_open() = {req, QDateTime::currentMSecsSinceEpoch()};
    EventBus::instance().publish("nav.switch_screen", QVariantMap{{"screen_id", "pre_ipo"}});
    QTimer::singleShot(0, qApp, [req]() { EventBus::instance().publish("private_market.open", req); });
}
} // namespace

QVariantMap pm_take_pending_open() {
    PendingOpen p = pending_open();
    pending_open() = {};
    if (p.req.isEmpty() || QDateTime::currentMSecsSinceEpoch() - p.at_ms > kPendingOpenMs)
        return {};
    return p.req;
}

void pm_open_company(QWidget* from, const QString& company_id) {
    if (!company_id.isEmpty())
        open_request(from, QVariantMap{{"company_id", company_id}});
}

void pm_open_deal(QWidget* from, const QString& deal_id) {
    if (!deal_id.isEmpty())
        open_request(from, QVariantMap{{"deal_id", deal_id}});
}

// ── PmTableWidget ────────────────────────────────────────────────────────────

PmTableWidget::PmTableWidget(const QString& title, const QStringList& headers, QWidget* parent)
    : BaseWidget(title, parent, ui::colors::CYAN()) {
    table_ = new ui::DataTable;
    table_->set_headers(headers);
    table_->setSortingEnabled(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    const int company_col = std::max(0, int(headers.indexOf(QStringLiteral("COMPANY"))));
    table_->horizontalHeader()->setSectionResizeMode(company_col, QHeaderView::Stretch);
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    content_layout()->addWidget(table_, 1);
    empty_ = new QLabel;
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setStyleSheet(QString("color:%1;padding:12px;").arg(ui::colors::TEXT_SECONDARY()));
    content_layout()->addWidget(empty_);
    empty_->hide();

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= shown_.size())
            return;
        const Row& r = shown_.at(row);
        if (!r.deal_id.isEmpty())
            pm_open_deal(this, r.deal_id);
        else
            pm_open_company(this, r.company_id);
    });

    coalesce_ = new QTimer(this);
    coalesce_->setSingleShot(true);
    // PreIpoService broadcasts on every marketplace landing (about once a
    // second while pages are read); a ranked list need not repaint that often.
    coalesce_->setInterval(1500);
    connect(coalesce_, &QTimer::timeout, this, &PmTableWidget::rebuild);
    auto& svc = services::PreIpoService::instance();
    connect(&svc, &services::PreIpoService::data_loaded, this, [this](pre_ipo::PreIpoSummary) { schedule(); });
    connect(&services::IpoDealsFeed::instance(), &services::IpoDealsFeed::updated, this, &PmTableWidget::schedule);
    connect(this, &BaseWidget::refresh_requested, this, []() {
        services::ClarityService::instance().refresh();
        services::IpoDealsFeed::instance().load();
    });
    schedule();
}

void PmTableWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    // Inside the PRIVATE MARKET screen the screen loads both sources (and
    // hands the feed its calendar). On the Dashboard nothing else does: ask
    // once per session for the companies (load_data re-reads the whole cache
    // and broadcasts on every call) and let the feed decide for the calendar.
    if (!inside_private_market(this)) {
        auto& svc = services::PreIpoService::instance();
        if (svc.companies_ref().isEmpty())
            svc.load_data(); // asked while there is nothing to show; idempotent while loading
        services::IpoDealsFeed::instance().load();
    }
    schedule();
}

void PmTableWidget::schedule() {
    if (!coalesce_->isActive())
        coalesce_->start();
}

void PmTableWidget::rebuild() {
    if (!isVisible())
        return; // repainted on show
    shown_ = rows();
    QVector<QStringList> cells;
    cells.reserve(shown_.size());
    for (const auto& r : shown_)
        cells.append(r.cells);
    table_->set_rows(cells);
    // set_rows reuses items when the shape is unchanged: reset every cell's
    // colour, or a row inherits the colour of the row that was there before.
    const QString plain = ui::colors::TEXT_PRIMARY();
    for (int i = 0; i < shown_.size(); ++i) {
        for (int c = 0; c < shown_.at(i).cells.size(); ++c)
            table_->set_cell_color(i, c, plain);
        for (const auto& [col, color] : shown_.at(i).colors)
            table_->set_cell_color(i, col, color);
    }
    const bool empty = shown_.isEmpty();
    table_->setVisible(!empty);
    empty_->setVisible(empty);
    if (empty)
        empty_->setText(empty_text());
}

// ── Upcoming IPOs ────────────────────────────────────────────────────────────

PmUpcomingIposWidget::PmUpcomingIposWidget(QWidget* parent)
    : PmTableWidget("UPCOMING IPOs", {"WHEN", "COMPANY", "TICKER", "RANGE", "DEAL", "BASIS"}, parent) {}

QString PmUpcomingIposWidget::empty_text() const {
    return services::IpoDealsFeed::instance().loaded()
               ? QStringLiteral("No operating-company IPO scheduled or estimated in the next 60 days.")
               : QStringLiteral("Loading the IPO calendar…");
}

QVector<PmTableWidget::Row> PmUpcomingIposWidget::rows() const {
    const QDate today = QDate::currentDate();
    QVector<const services::IpoDeal*> up;
    for (const auto& e : services::IpoDealsFeed::instance().deals())
        if (e.pending() && !e.spac && e.when.isValid() && today.daysTo(e.when) >= -3 && today.daysTo(e.when) <= 60 &&
            (e.firm() || e.kind == "estimate"))
            up.append(&e);
    // Firm dates first (they are the news), then by date.
    std::sort(up.begin(), up.end(), [](const auto* a, const auto* b) {
        if (a->firm() != b->firm())
            return a->firm();
        return a->when < b->when;
    });
    QVector<Row> out;
    for (const auto* e : up) {
        Row r;
        r.cells = {(e->firm() ? QString() : QStringLiteral("~")) + e->when.toString("MMM d"), e->company,
                   e->ticker.isEmpty() ? QStringLiteral("—") : e->ticker,
                   e->price_range.isEmpty() ? QStringLiteral("—") : e->price_range, money_usd(e->deal_size_usd),
                   e->firm() ? (e->kind == "scheduled" ? QStringLiteral("scheduled") : QStringLiteral("on EDGAR"))
                             : QStringLiteral("estimate")};
        r.colors = {{0, e->firm() ? ui::colors::TEXT_PRIMARY() : ui::colors::TEXT_SECONDARY()},
                    {1, ui::colors::CYAN()},
                    {5, e->firm() ? ui::colors::POSITIVE() : ui::colors::TEXT_SECONDARY()}};
        r.deal_id = e->deal_id;
        out.append(r);
    }
    return out;
}

// ── Movers ───────────────────────────────────────────────────────────────────

PmMoversWidget::PmMoversWidget(bool gainers, QWidget* parent)
    : PmTableWidget(gainers ? "TOP GAINERS · 6M" : "TOP LOSERS · 6M", {"COMPANY", "PRICE", "6M", "VS ROUND"},
                    parent),
      gainers_(gainers) {}

QString PmMoversWidget::empty_text() const {
    return QStringLiteral("Loading marketplace prices…");
}

QVector<PmTableWidget::Row> PmMoversWidget::rows() const {
    QVector<const PrivateCompany*> priced;
    for (const auto& c : services::PreIpoService::instance().companies_ref())
        if (c.market.has_price() && finite(c.market.chg_6m_pct) && !pm_is_public(c) &&
            (gainers_ ? c.market.chg_6m_pct > 0 : c.market.chg_6m_pct < 0))
            priced.append(&c);
    std::sort(priced.begin(), priced.end(), [this](const auto* a, const auto* b) {
        return gainers_ ? a->market.chg_6m_pct > b->market.chg_6m_pct : a->market.chg_6m_pct < b->market.chg_6m_pct;
    });
    QVector<Row> out;
    for (int i = 0; i < std::min<int>(25, priced.size()); ++i) {
        const auto* c = priced.at(i);
        const auto* lr = pm::last_priced_round(c->financing);
        const double vs = lr ? pm::premium_pct(c->market.price, lr->pps) : pm::kNaN;
        Row r;
        r.cells = {c->name, pps_text(c->market.price), pct_text(c->market.chg_6m_pct), pct_text(vs)};
        r.colors = {{0, ui::colors::CYAN()},
                    {2, c->market.chg_6m_pct >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()}};
        if (finite(vs))
            r.colors.append({3, vs >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()});
        r.company_id = c->id;
        out.append(r);
    }
    return out;
}

// ── Most valuable ────────────────────────────────────────────────────────────

PmMostValuableWidget::PmMostValuableWidget(QWidget* parent)
    : PmTableWidget("MOST VALUABLE PRIVATE COMPANIES", {"COMPANY", "VALUATION", "PRICE", "1Y", "IPO"}, parent) {}

QVector<PmTableWidget::Row> PmMostValuableWidget::rows() const {
    struct V {
        const PrivateCompany* c;
        double v;
        QString what;
    };
    QVector<V> vs;
    for (const auto& c : services::PreIpoService::instance().companies_ref()) {
        if (pm_is_public(c))
            continue;
        QString what;
        const double v = headline_valuation(c, &what);
        if (pm::usable(v))
            vs.append({&c, v, what});
    }
    std::sort(vs.begin(), vs.end(), [](const V& a, const V& b) { return a.v > b.v; });
    QVector<Row> out;
    for (int i = 0; i < std::min<int>(30, vs.size()); ++i) {
        const auto& x = vs.at(i);
        QString tip, color;
        qint64 key = 0;
        const QString ipo = pm_ipo_cell(*x.c, &tip, &color, &key);
        Row r;
        // A muted valuation is the last round's post-money, not implied by a price.
        r.cells = {x.c->name, money_usd(x.v), pps_text(x.c->market.price), pct_text(x.c->market.chg_1y_pct), ipo};
        r.colors = {{0, ui::colors::CYAN()},
                    {1, x.what == "implied" ? ui::colors::TEXT_PRIMARY() : ui::colors::TEXT_SECONDARY()},
                    {4, color}};
        if (finite(x.c->market.chg_1y_pct))
            r.colors.append({3, x.c->market.chg_1y_pct >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()});
        r.company_id = x.c->id;
        out.append(r);
    }
    return out;
}

// ── Recent rounds ────────────────────────────────────────────────────────────

PmRecentRoundsWidget::PmRecentRoundsWidget(QWidget* parent)
    : PmTableWidget("RECENT ROUNDS & TENDERS",
                    {"MONTH", "COMPANY", "ROUND", "RAISED", "POST-MONEY", "$/SH"}, parent) {}

QString PmRecentRoundsWidget::empty_text() const {
    return QStringLiteral("Funding histories load as each company page is read (one a second, in the background).");
}

QVector<PmTableWidget::Row> PmRecentRoundsWidget::rows() const {
    struct X {
        const PrivateCompany* c;
        const pre_ipo::FinancingRound* r;
    };
    QVector<X> xs;
    const QDate cutoff = QDate::currentDate().addMonths(-6);
    const QDate from(cutoff.year(), cutoff.month(), 1);
    for (const auto& c : services::PreIpoService::instance().companies_ref())
        for (const auto& r : c.financing)
            if (r.month.isValid() && r.month >= from)
                xs.append({&c, &r});
    std::sort(xs.begin(), xs.end(), [](const X& a, const X& b) {
        if (a.r->month != b.r->month)
            return a.r->month > b.r->month;
        return (finite(a.r->post_money_usd) ? a.r->post_money_usd : 0) >
               (finite(b.r->post_money_usd) ? b.r->post_money_usd : 0);
    });
    QVector<Row> out;
    for (int i = 0; i < std::min<int>(40, xs.size()); ++i) {
        const auto& x = xs.at(i);
        Row r;
        r.cells = {x.r->month.toString("MMM yyyy"), x.c->name,
                   x.r->series.isEmpty() ? QStringLiteral("—") : x.r->series, money_usd(x.r->raised_usd),
                   money_usd(x.r->post_money_usd), pps_text(x.r->pps)};
        r.colors = {{1, ui::colors::CYAN()}};
        if (x.r->tender)
            r.colors.append({2, ui::colors::AMBER()});
        r.company_id = x.c->id;
        out.append(r);
    }
    return out;
}

// ── C50 ──────────────────────────────────────────────────────────────────────

PmC50Widget::PmC50Widget(QWidget* parent) : BaseWidget("C50 · PRIVATE MARKET INDEX", parent, ui::colors::CYAN()) {
    head_ = new QLabel;
    head_->setTextFormat(Qt::RichText);
    head_->setWordWrap(true);
    head_->setContentsMargins(6, 4, 6, 0);
    head_->setToolTip(QStringLiteral("Clarity Liquidity Index: an equal-weight price index of the 50 most liquid "
                                     "late-stage private companies on Clarity (formerly Hiive), from indications "
                                     "and trades on its platform — the direction of the pre-IPO market, not a "
                                     "tradable level."));
    content_layout()->addWidget(head_);
    chart_host_ = new QWidget;
    auto* v = new QVBoxLayout(chart_host_);
    v->setContentsMargins(4, 0, 4, 4);
    content_layout()->addWidget(chart_host_, 1);
    connect(&services::ClarityService::instance(), &services::ClarityService::updated, this, &PmC50Widget::rebuild);
    connect(this, &BaseWidget::refresh_requested, this, []() { services::ClarityService::instance().refresh(); });
    rebuild();
}

void PmC50Widget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    services::ClarityService::instance().load();
    rebuild();
}

void PmC50Widget::rebuild() {
    const auto& cl = services::ClarityService::instance();
    const auto& idx = cl.index();
    if (finite(idx.level)) {
        // Inline colours: the tile has no stylesheet classes.
        auto pct = [](double p) {
            if (!finite(p))
                return QString("<span style='color:%1;'>—</span>").arg(ui::colors::TEXT_SECONDARY());
            return QString("<span style='color:%1;'>%2</span>")
                .arg(p >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE(), pct_text(p));
        };
        const QString k = ui::colors::TEXT_SECONDARY();
        head_->setText(QString("<span style='font-size:16px;font-weight:bold;'>%1</span>&nbsp;&nbsp;"
                               "<span style='color:%7;'>3M</span> %2 &nbsp;<span style='color:%7;'>6M</span> %3 "
                               "&nbsp;<span style='color:%7;'>1Y</span> %4 &nbsp;<span style='color:%7;'>ALL</span> "
                               "%5 &nbsp;<span style='color:%7;'>%6</span>")
                           .arg(QString::number(idx.level, 'f', 2), pct(idx.chg_3m_pct), pct(idx.chg_6m_pct),
                                pct(idx.chg_1y_pct), pct(idx.chg_max_pct),
                                idx.updated.isValid() ? QStringLiteral("as of ") + idx.updated.toString("MMM d")
                                                      : QString(),
                                k));
    } else {
        head_->setText(QString("<span style='color:%1;'>%2</span>")
                           .arg(ui::colors::TEXT_SECONDARY(),
                                cl.errors().isEmpty() ? QStringLiteral("Loading the marketplace index…")
                                                      : cl.errors().join(" · ").toHtmlEscaped()));
    }
    if (idx.points.size() == chart_points_ && (idx.points.isEmpty() || idx.points.last().day == chart_last_))
        return;
    chart_points_ = idx.points.size();
    chart_last_ = idx.points.isEmpty() ? QDate() : idx.points.last().day;
    auto* lay = chart_host_->layout();
    while (lay->count() > 0) {
        QLayoutItem* it = lay->takeAt(0);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    if (idx.points.size() >= 2) {
        const QVector<pre_ipo::PricePoint> pts = idx.points;
        lay->addWidget(new RangeChart([pts](int days, QString*) { return pm_charts::index_history(pts, days); }, 365));
    }
}

} // namespace finterm::screens::widgets

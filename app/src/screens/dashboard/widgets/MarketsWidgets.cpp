#include "screens/dashboard/widgets/MarketsWidgets.h"

#include "core/util/BarTime.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "screens/markets/MarketChartDialog.h"
#include "screens/markets/world/WorldMarketsView.h"
#include "services/markets/ConditionService.h"
#include "services/markets/LastKnownQuote.h"
#include "services/markets/MarketConditions.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/tables/DataTable.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPointer>

#include <cmath>

namespace finterm::screens::widgets {

namespace cnd = services::conditions;
using services::ConditionService;

namespace {

struct ClassRow {
    const char* cls;
    const char* symbol; ///< the benchmark
    const char* label;
    enum Read { Risk, Stress, Direction } read;
    const char* up;   ///< Direction: the word for a rising trend
    const char* down; ///< … and a falling one
};

/// The asset classes and the one benchmark each is read from.
const ClassRow kRows[] = {
    {"EQUITIES", "^GSPC", "S&P 500", ClassRow::Risk, "", ""},
    {"VOLATILITY", "^VIX", "VIX", ClassRow::Stress, "", ""},
    {"CREDIT", "FRED:BAMLH0A0HYM2", "HY OAS", ClassRow::Stress, "", ""},
    {"RATES", "^TNX", "US 10Y yield", ClassRow::Direction, "RISING", "FALLING"},
    {"DOLLAR", "DX-Y.NYB", "Dollar index", ClassRow::Direction, "STRONGER", "WEAKER"},
    {"OIL", "CL=F", "WTI crude", ClassRow::Direction, "TREND ↑", "TREND ↓"},
    {"COPPER", "HG=F", "Copper", ClassRow::Direction, "TREND ↑", "TREND ↓"},
    {"GOLD", "GC=F", "Gold", ClassRow::Direction, "TREND ↑", "TREND ↓"},
    {"CRYPTO", "BTC-USD", "Bitcoin", ClassRow::Direction, "TREND ↑", "TREND ↓"},
};

QString topic_of(const QString& sym) {
    return cnd::is_fred_key(sym) ? ConditionService::fred_topic(sym.mid(5)) : ConditionService::stats_topic(sym);
}

QVector<cnd::DatedValue> dated(const QJsonObject& o, const char* key) {
    QVector<cnd::DatedValue> out;
    for (const auto& v : o.value(QLatin1String(key)).toArray()) {
        const QJsonArray a = v.toArray();
        const QDate d = QDate::fromString(a.at(0).toString(), Qt::ISODate);
        if (d.isValid() && a.at(1).isDouble())
            out.append({d, a.at(1).toDouble()});
    }
    return out;
}

} // namespace

// ── Asset-class conditions ─────────────────────────────────────────────────

MarketRegimeWidget::MarketRegimeWidget(QWidget* parent)
    : BaseWidget(QStringLiteral("ASSET-CLASS CONDITIONS"), parent, ui::colors::AMBER()) {
    ConditionService::instance().ensure_registered_with_hub();
    table_ = new ui::DataTable;
    table_->set_headers({"CLASS", "BENCHMARK", "LAST", "1Y PCTL", "READ"});
    auto* h = table_->horizontalHeader();
    h->setSectionResizeMode(QHeaderView::ResizeToContents);
    h->setSectionResizeMode(1, QHeaderView::Stretch);
    if (auto* it = table_->horizontalHeaderItem(4))
        it->setToolTip(QStringLiteral(
            "One read per class, from its benchmark's own readings — no forecast:\n"
            "EQUITIES: RISK-ON when trending up (ADX ≥ 25, +DI) above its 200-day; RISK-OFF trending down below it.\n"
            "VOLATILITY / CREDIT: STRESS at or above the 80th percentile of the past year, CALM at or below the "
            "20th.\nOthers: the trend (ADX ≥ 25) and its direction, RANGE under 20, MIXED between."));
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row >= 0 && row < static_cast<int>(std::size(kRows)))
            MarketChartDialog::show_for(QString::fromLatin1(kRows[row].symbol),
                                        QString::fromUtf8(kRows[row].label), this);
    });
    content_layout()->addWidget(table_, 1);
    connect(this, &BaseWidget::refresh_requested, this, [this]() {
        QStringList topics;
        for (const auto& r : kRows)
            topics << topic_of(QString::fromLatin1(r.symbol));
        ConditionService::instance().mark_forced(topics);
        datahub::DataHub::instance().request(topics, true);
    });
}

void MarketRegimeWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!subscribed_)
        subscribe_all();
}

void MarketRegimeWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (!subscribed_)
        return;
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    subscribed_ = false;
}

void MarketRegimeWidget::subscribe_all() {
    auto& hub = datahub::DataHub::instance();
    QPointer<MarketRegimeWidget> self = this;
    QStringList quotes;
    for (const auto& r : kRows) {
        const QString sym = QString::fromLatin1(r.symbol);
        const QString t = topic_of(sym);
        if (!cond_.contains(t))
            if (const QJsonObject held = ConditionService::instance().last_known(t); !held.isEmpty())
                cond_.insert(t, held);
        hub.subscribe(this, t, [self, t](const QVariant& v) {
            if (!self || !v.canConvert<QJsonObject>())
                return;
            self->cond_.insert(t, v.toJsonObject());
            self->fresh_.insert(t);
            self->render_.request();
        });
        if (!cnd::is_series_key(sym)) {
            quotes << sym;
            hub.subscribe(this, QStringLiteral("market:quote:") + sym, [self, sym](const QVariant& v) {
                if (!self || !v.canConvert<services::QuoteData>())
                    return;
                self->quotes_.insert(sym, v.value<services::QuoteData>());
                self->render_.request();
            });
        }
    }
    services::seed_last_known_quotes(quotes, quotes_);
    subscribed_ = true;
    render_.request();
}

void MarketRegimeWidget::render() {
    QVector<QStringList> rows;
    QVector<QPair<QString, QString>> colors; // last, read
    QVector<QString> tips;
    for (const auto& r : kRows) {
        const QString sym = QString::fromLatin1(r.symbol);
        const QString t = topic_of(sym);
        const QJsonObject c = cond_.value(t);
        double last = cnd::kNaN, p1 = cnd::kNaN, vs = cnd::kNaN;
        auto regime = cnd::Regime::Unknown;
        if (cnd::is_series_key(sym)) {
            const auto pts = dated(c, "points");
            if (!pts.isEmpty()) {
                last = pts.last().value;
                QVector<double> w;
                const QDate start = pts.last().date.addYears(-1);
                if (pts.first().date <= start.addDays(10)) {
                    for (const auto& x : pts)
                        if (x.date > start)
                            w.append(x.value);
                    p1 = cnd::percent_rank(w, last);
                }
            }
        } else {
            const auto q = quotes_.constFind(sym);
            const bool live = q != quotes_.constEnd() && q->price > 0 && !services::last_known_is_stale(sym);
            const auto lr = cnd::live_reading(dated(c, "closes"), live ? q->price : cnd::kNaN,
                                              core::bartime::market_today_et());
            last = lr.last;
            p1 = lr.pctile_1y;
            vs = lr.vs_sma200_pct;
            regime = cnd::regime_from_key(c.value("regime").toString());
        }
        QString read = ui::formatting::placeholder();
        QString color = ui::colors::TEXT_SECONDARY();
        switch (r.read) {
            case ClassRow::Risk:
                if (regime == cnd::Regime::TrendUp && vs > 0) {
                    read = QStringLiteral("RISK-ON");
                    color = ui::colors::POSITIVE();
                } else if (regime == cnd::Regime::TrendDown && vs < 0) {
                    read = QStringLiteral("RISK-OFF");
                    color = ui::colors::NEGATIVE();
                } else if (regime != cnd::Regime::Unknown) {
                    read = cnd::regime_label(regime);
                }
                break;
            case ClassRow::Stress:
                if (std::isfinite(p1)) {
                    read = p1 >= 80 ? QStringLiteral("STRESS") : p1 <= 20 ? QStringLiteral("CALM") : QStringLiteral("NORMAL");
                    color = p1 >= 80 ? ui::colors::NEGATIVE() : p1 <= 20 ? ui::colors::POSITIVE() : color;
                }
                break;
            case ClassRow::Direction:
                if (regime == cnd::Regime::TrendUp)
                    read = QString::fromLatin1(r.up);
                else if (regime == cnd::Regime::TrendDown)
                    read = QString::fromLatin1(r.down);
                else if (regime != cnd::Regime::Unknown)
                    read = cnd::regime_label(regime);
                color = ui::colors::TEXT_PRIMARY();
                break;
        }
        const int dp = std::isfinite(last) && std::abs(last) < 10 ? 3 : 2;
        rows.append({QString::fromLatin1(r.cls), QString::fromUtf8(r.label),
                     std::isfinite(last) ? QLocale(QLocale::English).toString(last, 'f', dp)
                                         : ui::formatting::placeholder(),
                     std::isfinite(p1) ? QString::number(std::lround(p1)) : ui::formatting::placeholder(), read});
        colors.append({fresh_.contains(t) ? ui::colors::WHITE() : ui::colors::TEXT_SECONDARY(), color});
        tips.append(QStringLiteral("%1 — read from %2 (%3). Click for its chart.")
                        .arg(QString::fromLatin1(r.cls), QString::fromUtf8(r.label), sym));
    }
    table_->set_rows(rows);
    for (int i = 0; i < rows.size(); ++i) {
        for (int col = 0; col < 5; ++col)
            if (auto* it = table_->item(i, col)) {
                it->setToolTip(tips[i]);
                it->setForeground(QColor(col == 2 ? colors[i].first : col == 4 ? colors[i].second
                                                                              : ui::colors::TEXT_PRIMARY()));
            }
    }
}

// ── World board ────────────────────────────────────────────────────────────

WorldBoardWidget::WorldBoardWidget(const QString& part, QWidget* parent)
    : BaseWidget(part.isEmpty()                                        ? QStringLiteral("WORLD MARKETS")
                 : part == world::WorldMarketsView::kSessionsPart ? QStringLiteral("SESSIONS · OVERNIGHT")
                                                                       : part,
                 parent, ui::colors::CYAN()) {
    view_ = part.isEmpty() ? new world::WorldMarketsView(this) : new world::WorldMarketsView({part}, this);
    content_layout()->setContentsMargins(0, 0, 0, 0);
    content_layout()->addWidget(view_, 1);
    connect(view_, &world::WorldMarketsView::symbol_activated, this,
            [this](const QString& sym, const QString& name) { MarketChartDialog::show_for(sym, name, this); });
    connect(this, &BaseWidget::refresh_requested, this, [this]() { view_->refresh(true); });
}

// ── Strategy vs its bitcoin ────────────────────────────────────────────────

MstrPremiumWidget::MstrPremiumWidget(QWidget* parent)
    : BaseWidget(QStringLiteral("MSTR · PREMIUM TO ITS BITCOIN"), parent, ui::colors::AMBER()) {
    ConditionService::instance().ensure_registered_with_hub();
    body_ = new QLabel;
    body_->setTextFormat(Qt::RichText);
    body_->setWordWrap(true);
    body_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    body_->setCursor(Qt::PointingHandCursor);
    body_->setStyleSheet(QStringLiteral("padding:8px;background:transparent;"));
    MarketChartDialog::make_clickable(body_);
    MarketChartDialog::set_target(body_, QStringLiteral("MSTR"), QStringLiteral("Strategy (MSTR)"));
    content_layout()->addWidget(body_, 1);
    connect(this, &BaseWidget::refresh_requested, this, [this]() {
        datahub::DataHub::instance().request(ConditionService::kMstrTopic, true);
        fetch_cap();
    });
}

void MstrPremiumWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (subscribed_)
        return;
    auto& hub = datahub::DataHub::instance();
    QPointer<MstrPremiumWidget> self = this;
    if (holding_.isEmpty())
        holding_ = ConditionService::instance().last_known(ConditionService::kMstrTopic);
    hub.subscribe(this, ConditionService::kMstrTopic, [self](const QVariant& v) {
        if (!self || !v.canConvert<QJsonObject>())
            return;
        self->holding_ = v.toJsonObject();
        self->holding_error_.clear();
        self->render_.request();
    });
    hub.subscribe_errors(this, ConditionService::kMstrTopic, [self](const QString& err) {
        if (!self)
            return;
        self->holding_error_ = err;
        self->render_.request();
    });
    QHash<QString, services::QuoteData> seed;
    services::seed_last_known_quotes({"BTC-USD", "MSTR"}, seed);
    btc_ = seed.value("BTC-USD");
    mstr_ = seed.value("MSTR");
    for (const QString sym : {"BTC-USD", "MSTR"})
        hub.subscribe(this, QStringLiteral("market:quote:") + sym, [self, sym](const QVariant& v) {
            if (!self || !v.canConvert<services::QuoteData>())
                return;
            (sym == QLatin1String("MSTR") ? self->mstr_ : self->btc_) = v.value<services::QuoteData>();
            self->render_.request();
        });
    subscribed_ = true;
    fetch_cap();
    render_.request();
}

void MstrPremiumWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (!subscribed_)
        return;
    datahub::DataHub::instance().unsubscribe(this);
    datahub::DataHub::instance().unsubscribe_errors(this, ConditionService::kMstrTopic);
    subscribed_ = false;
}

void MstrPremiumWidget::fetch_cap() {
    QPointer<MstrPremiumWidget> self = this;
    // The shared company-info read (Equity Research's own): shares outstanding.
    services::MarketDataService::instance().fetch_info_raw(
        QStringLiteral("MSTR"), [self](const services::MarketDataService::InfoReply& r) {
            if (!self)
                return;
            const double implied = r.info.value("implied_shares_outstanding").toDouble(cnd::kNaN);
            const double shares = r.info.value("shares_outstanding").toDouble(cnd::kNaN);
            // impliedSharesOutstanding counts both share classes; fall back
            // to the class-A count, which understates the company.
            self->market_cap_ = std::isfinite(implied) && implied > 0 ? implied
                                : std::isfinite(shares) && shares > 0 ? shares
                                                                     : cnd::kNaN;
            self->cap_error_ = r.ok ? (std::isfinite(self->market_cap_) ? QString() : QStringLiteral("no share count"))
                                    : r.error;
            self->render_.request();
        });
}

void MstrPremiumWidget::render() {
    auto fmt = [](double v, int dp) {
        return std::isfinite(v) ? QLocale(QLocale::English).toString(v, 'f', dp) : ui::formatting::placeholder();
    };
    const double held = holding_.value("btc").toDouble(cnd::kNaN);
    const double shares = market_cap_;
    const double cap = std::isfinite(shares) && mstr_.price > 0 ? shares * mstr_.price : cnd::kNaN;
    const double btc_value = std::isfinite(held) && btc_.price > 0 ? held * btc_.price : cnd::kNaN;
    const double mnav = std::isfinite(cap) && std::isfinite(btc_value) && btc_value > 0 ? cap / btc_value : cnd::kNaN;
    const QString sec = ui::colors::TEXT_SECONDARY(), pri = ui::colors::TEXT_PRIMARY();
    QString html = QStringLiteral("<div style='color:%1;font-size:11px'>mNAV · market cap ÷ bitcoin held</div>"
                                  "<div style='color:%2;font-size:28px;font-weight:700'>%3</div>")
                       .arg(sec, std::isfinite(mnav) ? (mnav >= 1 ? ui::colors::AMBER() : ui::colors::CYAN()) : sec,
                            std::isfinite(mnav) ? fmt(mnav, 2) + QStringLiteral("×") : ui::formatting::placeholder());
    if (std::isfinite(mnav))
        html += QStringLiteral("<div style='color:%1'>%2 to its bitcoin</div>")
                    .arg(pri, (mnav >= 1 ? QStringLiteral("+") : QString()) + fmt((mnav - 1) * 100, 1) +
                                  QStringLiteral("% ") + (mnav >= 1 ? QStringLiteral("premium") : QStringLiteral("discount")));
    html += QStringLiteral("<table style='color:%1;margin-top:6px'>").arg(pri);
    auto row = [&](const QString& k, const QString& v) {
        html += QStringLiteral("<tr><td style='color:%1;padding-right:10px'>%2</td><td>%3</td></tr>").arg(sec, k, v);
    };
    row(QStringLiteral("Bitcoin held"),
        std::isfinite(held) ? fmt(held, 0) + QStringLiteral(" BTC · as of ") + holding_.value("as_of").toString()
                            : (holding_error_.isEmpty() ? QStringLiteral("loading…") : holding_error_.toHtmlEscaped()));
    row(QStringLiteral("× BTC price"), fmt(btc_.price, 0));
    row(QStringLiteral("= bitcoin value"), std::isfinite(btc_value) ? QStringLiteral("$") + fmt(btc_value / 1e9, 1) + "B" : ui::formatting::placeholder());
    row(QStringLiteral("MSTR market cap"),
        std::isfinite(cap) ? QStringLiteral("$") + fmt(cap / 1e9, 1) + QStringLiteral("B (") + fmt(shares / 1e6, 1) +
                                 QStringLiteral("M sh × ") + fmt(mstr_.price, 2) + ")"
                           : (cap_error_.isEmpty() ? QStringLiteral("loading…") : cap_error_.toHtmlEscaped()));
    html += QStringLiteral("</table><div style='color:%1;font-size:10px;margin-top:6px'>Holdings: Strategy's weekly 8-K "
                           "BTC Update (SEC EDGAR); shares: Yahoo; prices live. Equity value only — debt and "
                           "preferred are not counted. Click for MSTR's chart.</div>")
                .arg(ui::colors::TEXT_TERTIARY());
    body_->setText(html);
}

} // namespace finterm::screens::widgets

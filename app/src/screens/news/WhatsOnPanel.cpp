#include "screens/news/WhatsOnPanel.h"

#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "python/PythonRunner.h"
#include "services/calendar/EarningsCalendarFeed.h"
#include "services/calendar/MacroCalendarFeed.h"
#include "services/economics/EconomicsService.h"
#include "services/news/NewsPromo.h"
#include "services/pre_ipo/IpoDealsFeed.h"
#include "storage/cache/CacheManager.h"
#include "storage/repositories/PortfolioRepository.h"
#include "storage/repositories/WatchlistRepository.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <QButtonGroup>
#include <algorithm>
#include <cmath>
#include <optional>

namespace finterm::screens {

namespace {
namespace mc = finterm::macro_calendar;
namespace wo = finterm::whats_on;
namespace ta = finterm::treasury_auctions;
namespace mr = finterm::macro_releases;

constexpr int kRefreshMs = 30 * 60 * 1000; // sources change a few times a day
constexpr int kTickMs = 30 * 1000;         // countdown labels
constexpr int kHeadlineCount = 6;
constexpr int kEventsAheadDays = 45;       // earnings/ex-div window: this week → ~6 weeks out
constexpr const char* kItemProp = "whatsOnIndex";
constexpr int kBriefSource = -2;   ///< "whatsOnSource" of the weekly-brief row (-1: a headline)
constexpr int kSectionSource = -3; ///< a section header (click folds it)
constexpr const char* kCollapsedKey = "news/whats_on_collapsed";
constexpr const char* kMineOnlyKey = "news/whats_on_mine_only";
constexpr const char* kMinImpactKey = "news/whats_on_min_impact"; ///< 2 HIGH, 1 MED+, 0 ALL
// EconomicsService dispatch for the H.15 yields. Same script/command/args as
// the Federal Reserve panel's "Treasury Rates" so both read one cache entry.
constexpr const char* kFedSourceId = "federal_reserve";
constexpr const char* kFedScript = "federal_reserve_data.py";
constexpr const char* kYieldsCommand = "treasury_rates";
constexpr const char* kYieldsRequestId = "whatson_treasury_rates";

QString kind_color(const QString& kind) {
    if (kind == QLatin1String("FED"))
        return ui::colors::AMBER();
    if (kind == QLatin1String("LABOR"))
        return ui::colors::CYAN();
    if (kind == QLatin1String("INFLATION"))
        return ui::colors::WARNING();
    if (kind == QLatin1String("GROWTH"))
        return ui::colors::POSITIVE();
    if (kind == QLatin1String("IPO") || kind == QLatin1String("EARNINGS"))
        return ui::colors::INFO();
    if (kind == QLatin1String("RATES"))
        return ui::colors::CYAN();
    if (kind == QLatin1String("INSIDER") || kind == QLatin1String("EX-DIV"))
        return ui::colors::POSITIVE();
    return ui::colors::TEXT_SECONDARY();
}

QString pct(double fraction_or_pct, bool is_fraction) {
    return QString::number(is_fraction ? fraction_or_pct * 100.0 : fraction_or_pct, 'f', 2) + QLatin1Char('%');
}

QString section_header_text(wo::Section s, int rows, bool collapsed) {
    return QStringLiteral("%1 %2%3")
        .arg(collapsed ? QStringLiteral("▸") : QStringLiteral("▾"), wo::section_title(s),
             rows > 0 ? QStringLiteral("  ·  %1").arg(rows) : QString());
}
} // namespace

WhatsOnPanel::WhatsOnPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("whatsOnPanel");
    nam_ = new QNetworkAccessManager(this);

    QSettings settings;
    for (const auto& v : settings.value(kCollapsedKey).toList())
        collapsed_.insert(v.toInt());
    mine_only_ = settings.value(kMineOnlyKey, false).toBool();
    min_impact_ = static_cast<wo::Impact>(std::clamp(settings.value(kMinImpactKey, 2).toInt(), 0, 2));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(6);
    // Impact selector: what a row needs to be listed. HIGH keeps the drawer
    // to the handful of events that move the whole market.
    struct Tier {
        const char* label;
        wo::Impact min;
        const char* tip;
    };
    static const Tier kTiers[] = {
        {"HIGH", wo::Impact::High,
         "Only HIGH-impact events: FOMC, CPI, payrolls, unemployment, GDP, PCE,\n"
         "$500B+ earnings, IPOs raising $10B+."},
        {"MED+", wo::Impact::Med,
         "HIGH and MED: also other US data that matters, $50B+ earnings, IPOs raising $1B+,\n"
         "10Y+ auctions, market holidays, and MED events on your stocks."},
        {"ALL", wo::Impact::Low,
         "Everything in the sources: every country's releases, bill auctions, month end,\n"
         "options expiry, small IPOs."},
    };
    auto* tiers = new QButtonGroup(this);
    tiers->setExclusive(true);
    for (const auto& t : kTiers) {
        auto* b = new QPushButton(QString::fromLatin1(t.label), this);
        b->setObjectName("whatsOnFilter");
        b->setCheckable(true);
        b->setChecked(t.min == min_impact_);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(QString::fromUtf8(t.tip) + QStringLiteral("\n\n") + wo::impact_rules());
        const wo::Impact min = t.min;
        connect(b, &QPushButton::clicked, this, [this, min]() {
            if (min_impact_ == min)
                return;
            min_impact_ = min;
            QSettings().setValue(kMinImpactKey, static_cast<int>(min));
            fetch_fred(false); // ALL lists more releases, which can need more series
            render();
        });
        tiers->addButton(b);
        bar->addWidget(b);
    }
    bar->addSpacing(6);
    mine_btn_ = new QPushButton(QStringLiteral("◆ MY STOCKS"), this);
    mine_btn_->setObjectName("whatsOnFilter");
    mine_btn_->setCheckable(true);
    mine_btn_->setChecked(mine_only_);
    mine_btn_->setCursor(Qt::PointingHandCursor);
    mine_btn_->setToolTip(QStringLiteral("Only events on your watchlists and portfolios"));
    connect(mine_btn_, &QPushButton::toggled, this, [this](bool on) {
        mine_only_ = on;
        QSettings().setValue(kMineOnlyKey, on);
        render();
    });
    bar->addWidget(mine_btn_);
    bar->addStretch();
    auto* refresh_btn = new QPushButton(QStringLiteral("↻"), this);
    refresh_btn->setObjectName("whatsOnFilter");
    refresh_btn->setFixedWidth(28);
    refresh_btn->setCursor(Qt::PointingHandCursor);
    refresh_btn->setToolTip(QStringLiteral("Refresh calendar, earnings, auctions, rates and IPOs"));
    connect(refresh_btn, &QPushButton::clicked, this, [this]() { refresh(true); });
    // IPOs come from the one calendar feed PRIVATE MARKET and the dashboard
    // tile read too.
    auto& ipo_feed = services::IpoDealsFeed::instance();
    connect(&ipo_feed, &services::IpoDealsFeed::updated, this, [this]() {
        ipo_deals_ = services::IpoDealsFeed::instance().latest_root().value("deals").toArray();
        clear_error(QStringLiteral("IPOs"));
        render();
    });
    connect(&ipo_feed, &services::IpoDealsFeed::answered, this,
            [this](bool enrich, bool ok, const QJsonObject&, const QString& why) {
                if (!ok && !enrich && !services::IpoDealsFeed::instance().loaded())
                    set_error(QStringLiteral("IPOs"), why.isEmpty() ? QStringLiteral("IPO calendar unavailable") : why);
            });
    // 2y / 10y yields come from EconomicsService, the owner of
    // federal_reserve_data.py. Its result_ready is a broadcast: take only the
    // answers to What's On's own request id (the Economics screen's Federal
    // Reserve panel asks with "fed_*" ids and ignores this one).
    connect(&services::EconomicsService::instance(), &services::EconomicsService::result_ready, this,
            [this](const QString& request_id, const services::EconomicsResult& r) {
                if (request_id != QLatin1String(kYieldsRequestId) || r.source_id != QLatin1String(kFedSourceId))
                    return;
                apply_yields_result(r);
            });
    bar->addWidget(refresh_btn);
    root->addLayout(bar);

    rates_label_ = new QLabel(this);
    rates_label_->setObjectName("whatsOnRates");
    rates_label_->setWordWrap(true);
    rates_label_->setTextFormat(Qt::RichText);
    root->addWidget(rates_label_);

    next_label_ = new QLabel(this);
    next_label_->setObjectName("whatsOnNext");
    next_label_->setWordWrap(true);
    next_label_->setTextFormat(Qt::RichText);
    next_label_->setToolTip(wo::impact_rules());
    next_label_->hide();
    root->addWidget(next_label_);

    body_ = new QVBoxLayout;
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(2);
    root->addLayout(body_);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("whatsOnStatus");
    status_label_->setWordWrap(true);
    root->addWidget(status_label_);

    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, this, [this]() {
        if (isVisible())
            refresh();
    });
    timer_->start();

    tick_ = new QTimer(this);
    tick_->setInterval(kTickMs);
    connect(tick_, &QTimer::timeout, this, [this]() {
        if (isVisible())
            tick();
    });
    tick_->start();

    apply_styles();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this](const ui::ThemeTokens&) {
        apply_styles();
        render();
    });
    render();
}

void WhatsOnPanel::apply_styles() {
    // Same family and size as the feed's headline rows (NewsFeedDelegate:
    // QFont(DATA_FAMILY, TINY) — a POINT size), so the drawer reads like the
    // feed instead of in the old 11px drawer type.
    const QString font = QStringLiteral("font-family:'%1'; font-size:%2pt;")
                             .arg(QString(ui::fonts::DATA_FAMILY), QString::number(ui::fonts::TINY));
    setStyleSheet(
        QStringLiteral("#whatsOnPanel QLabel { %1 color:%2; background:transparent; }"
                       "#whatsOnPanel QLabel#whatsOnSection { color:%3; font-weight:700; "
                       "  border-bottom:1px solid %4; padding:6px 0 2px; }"
                       "#whatsOnPanel QLabel#whatsOnWhen { color:%5; }"
                       "#whatsOnPanel QLabel#whatsOnFigures { color:%5; }"
                       "#whatsOnPanel QLabel#whatsOnDetail { color:%5; padding:2px 0 4px 8px; }"
                       "#whatsOnPanel QLabel#whatsOnStatus { color:%5; }"
                       "#whatsOnPanel QLabel#whatsOnRates { color:%2; padding:2px 0; }"
                       "#whatsOnPanel QLabel#whatsOnNext { color:%2; padding:3px 4px; border:1px solid %4; }"
                       "#whatsOnPanel QWidget#whatsOnRow { background:transparent; border-bottom:1px solid %4; }"
                       "#whatsOnPanel QWidget#whatsOnRow:hover { background:%6; }"
                       "#whatsOnPanel QPushButton#whatsOnFilter { %1 color:%5; background:transparent; "
                       "  border:1px solid %4; padding:1px 6px; }"
                       "#whatsOnPanel QPushButton#whatsOnFilter:checked { color:%3; border-color:%3; }")
            .arg(font, ui::colors::TEXT_PRIMARY(), ui::colors::AMBER(), ui::colors::BORDER_DIM(),
                 ui::colors::TEXT_SECONDARY(), ui::colors::BG_HOVER()));
}

void WhatsOnPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!last_refresh_.isValid() || last_refresh_.msecsTo(QDateTime::currentDateTime()) > kRefreshMs)
        refresh();
    else
        tick();
}

void WhatsOnPanel::refresh(bool force) {
    last_refresh_ = QDateTime::currentDateTime();
    load_user_symbols();
    fetch_macro(force);
    fetch_fomc(force);
    fetch_rates(force);
    fetch_ipos(force);
    fetch_events(force);
    fetch_auctions(force);
}

void WhatsOnPanel::set_error(const QString& source, const QString& why) {
    clear_error(source);
    errors_ << source + QStringLiteral(": ") + why;
    LOG_WARN("WhatsOn", source + ": " + why);
    render();
}

void WhatsOnPanel::clear_error(const QString& source) {
    errors_.erase(std::remove_if(errors_.begin(), errors_.end(),
                                 [&](const QString& e) { return e.startsWith(source + QStringLiteral(": ")); }),
                  errors_.end());
}

void WhatsOnPanel::load_user_symbols() {
    // Local SQLite reads, the same way the Watchlist and Portfolio screens
    // load them — small, and no network.
    QStringList syms;
    if (auto wl = WatchlistRepository::instance().list_all(); wl.is_ok()) {
        for (const auto& w : wl.value())
            if (auto st = WatchlistRepository::instance().get_stocks(w.id); st.is_ok())
                for (const auto& s : st.value())
                    syms << s.symbol;
    }
    if (auto pf = PortfolioRepository::instance().list_portfolios(); pf.is_ok()) {
        for (const auto& p : pf.value())
            if (auto as = PortfolioRepository::instance().get_assets(p.id); as.is_ok())
                for (const auto& a : as.value())
                    syms << a.symbol;
    }
    mine_ = wo::norm_symbols(syms);
}

// ── Sources ─────────────────────────────────────────────────────────────────

void WhatsOnPanel::fetch_macro(bool force) {
    // Same feed as the dashboard's ECONOMIC CALENDAR, through the shared
    // cached fetch: the feed answers 429 to repeated requests.
    mc::fetch_week(
        this,
        [this, force](const mc::FeedResult& r) {
            if (!r.ok) {
                set_error(QStringLiteral("Calendar"), QStringLiteral("ForexFactory unavailable (%1)").arg(r.error));
                return;
            }
            macro_ = mc::parse_ff(r.rows);
            if (r.stale)
                set_error(QStringLiteral("Calendar"),
                          QStringLiteral("refresh refused (%1) — showing the last copy").arg(r.error));
            else
                clear_error(QStringLiteral("Calendar"));
            fetch_fred(force);
            render();
        },
        force);
}

// Each source is cached on disk (CacheManager) for about as long as it takes
// to change, so reopening the drawer, a second News screen or an app restart
// does not re-fetch the Fed's page or start two Python interpreters again.
namespace {
constexpr const char* kFomcKey = "whatson:fomc:v1";
constexpr const char* kEffrKey = "whatson:effr:v1";
constexpr const char* kEventsKey = "whatson:company_events:v2:"; // + window + symbol-set hash (v2: no earnings)
constexpr const char* kTdUpKey = "whatson:td:upcoming:v1";
constexpr const char* kTdDoneKey = "whatson:td:auctioned:v1";
constexpr const char* kFredKey = "whatson:fred:v1:"; // + series id
constexpr int kFomcSec = 12 * 3600; // a published calendar
constexpr int kEffrSec = 3600;      // one print a day
constexpr int kEventsSec = 60 * 60;  // the script caches each day itself (4 h / 7 d)
constexpr int kTdSec = 60 * 60;      // announcements land a few days ahead; results at the close
constexpr int kFredSec = 20 * 60;    // short: an actual appears within the hour after 08:30

std::optional<QJsonDocument> cached_doc(const QString& key) {
    if (const auto hit = CacheManager::instance().try_get(key)) {
        const auto d = QJsonDocument::fromJson(hit->toUtf8());
        if (!d.isNull())
            return d;
    }
    return std::nullopt;
}
void put_doc(const QString& key, const QJsonDocument& d, int ttl) {
    CacheManager::instance().put(key, QString::fromUtf8(d.toJson(QJsonDocument::Compact)), ttl,
                                 QStringLiteral("whatson"));
}
} // namespace

void WhatsOnPanel::apply_fomc(const QVector<QDate>& dates) {
    fomc_ = dates;
    clear_error(QStringLiteral("FOMC"));
    render();
}

void WhatsOnPanel::fetch_fomc(bool force) {
    if (!force) {
        if (const auto d = cached_doc(kFomcKey); d && d->isArray()) {
            QVector<QDate> dates;
            for (const auto& v : d->array())
                dates << QDate::fromString(v.toString(), Qt::ISODate);
            apply_fomc(dates);
            return;
        }
    }
    QNetworkRequest req(QUrl(QStringLiteral("https://www.federalreserve.gov/monetarypolicy/fomccalendars.htm")));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("finterm/%1").arg(QCoreApplication::applicationVersion()));
    req.setTransferTimeout(20000);
    auto* reply = nam_->get(req);
    QPointer<WhatsOnPanel> self = this;
    connect(reply, &QNetworkReply::finished, this, [self, reply]() {
        reply->deleteLater();
        if (!self)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            self->set_error(QStringLiteral("FOMC"), reply->errorString());
            return;
        }
        const auto dates = mc::parse_fomc_calendar(QString::fromUtf8(reply->readAll()));
        if (dates.isEmpty()) {
            self->set_error(QStringLiteral("FOMC"), QStringLiteral("meeting calendar unreadable"));
            return;
        }
        QJsonArray arr;
        for (const QDate& d : dates)
            arr.append(d.toString(Qt::ISODate));
        put_doc(kFomcKey, QJsonDocument(arr), kFomcSec);
        self->apply_fomc(dates);
    });
}

void WhatsOnPanel::apply_effr(const QJsonObject& r) {
    const double lo = r.value("targetRateFrom").toDouble(NAN);
    const double hi = r.value("targetRateTo").toDouble(NAN);
    const double effr = r.value("percentRate").toDouble(NAN);
    policy_range_ = (std::isfinite(lo) && std::isfinite(hi))
                        ? QStringLiteral("%1–%2%").arg(lo, 0, 'f', 2).arg(hi, 0, 'f', 2)
                        : QString();
    effr_ = std::isfinite(effr) ? pct(effr, false) : QString();
    effr_date_ = r.value("effectiveDate").toString();
    clear_error(QStringLiteral("Fed funds"));
    render_rates();
}

bool WhatsOnPanel::apply_yields(const QJsonArray& rows) {
    for (int i = rows.size() - 1; i >= 0; --i) {
        const auto o = rows[i].toObject();
        const QJsonValue y2 = o.value("year_2"), y10 = o.value("year_10");
        if (y2.isDouble() && y10.isDouble()) {
            y2_ = pct(y2.toDouble(), true);
            y10_ = pct(y10.toDouble(), true);
            yields_date_ = o.value("date").toString();
            clear_error(QStringLiteral("Treasury yields"));
            render_rates();
            return true;
        }
    }
    return false;
}

void WhatsOnPanel::fetch_rates(bool force) {
    QPointer<WhatsOnPanel> self = this;
    // Policy range + effective rate: NY Fed reference-rate API (free JSON) —
    // the same source federal_reserve_data.py reads for the Economics screen.
    const auto effr_hit = force ? std::nullopt : cached_doc(kEffrKey);
    if (effr_hit && effr_hit->isObject()) {
        apply_effr(effr_hit->object());
    } else {
        HttpClient::instance().get(
            QStringLiteral("https://markets.newyorkfed.org/api/rates/unsecured/effr/last/1.json"),
            [self](Result<QJsonDocument> res) {
                if (!self)
                    return;
                const auto rows = res.is_ok() ? res.value().object().value("refRates").toArray() : QJsonArray{};
                if (rows.isEmpty()) {
                    self->set_error(QStringLiteral("Fed funds"), QStringLiteral("NY Fed unavailable"));
                    return;
                }
                const QJsonObject r = rows.first().toObject();
                put_doc(kEffrKey, QJsonDocument(r), kEffrSec);
                self->apply_effr(r);
            });
    }
    // 2y / 10y: H.15 from federal_reserve_data.py treasury_rates, through its
    // owner EconomicsService (and its cache). The answer — synchronous on a
    // cache hit — arrives via result_ready, filtered in the constructor.
    auto& econ = services::EconomicsService::instance();
    if (force)
        econ.invalidate(QString::fromLatin1(kFedScript), QString::fromLatin1(kYieldsCommand), {});
    econ.execute(QString::fromLatin1(kFedSourceId), QString::fromLatin1(kFedScript),
                 QString::fromLatin1(kYieldsCommand), {}, QString::fromLatin1(kYieldsRequestId));
}

void WhatsOnPanel::apply_yields_result(const services::EconomicsResult& r) {
    if (!r.success) {
        set_error(QStringLiteral("Treasury yields"), r.error.isEmpty() ? QStringLiteral("script failed") : r.error);
        return;
    }
    if (apply_yields(r.data.value("data").toArray()))
        return;
    set_error(QStringLiteral("Treasury yields"), r.data.value("error").toString(QStringLiteral("no recent H.15 data")));
}

void WhatsOnPanel::fetch_ipos(bool force) {
    auto& feed = services::IpoDealsFeed::instance();
    if (feed.loaded()) {
        ipo_deals_ = feed.latest_root().value("deals").toArray();
        clear_error(QStringLiteral("IPOs"));
        render();
    }
    feed.load(force); // a no-op while a fresh answer is held
}

void WhatsOnPanel::apply_events(const QJsonObject& root) {
    events_ = root; // dividends + insider clusters (earnings come from the feed)
    if (const int df = root.value("div_days_failed").toArray().size(); df > 0)
        set_error(QStringLiteral("Ex-dividend"), QStringLiteral("%1 days unanswered by Nasdaq").arg(df));
    else
        clear_error(QStringLiteral("Ex-dividend"));
    render();
}

void WhatsOnPanel::fetch_events(bool force) {
    const QDate today = QDate::currentDate();
    const QDate from = wo::week_start(today);
    const QDate to = today.addDays(kEventsAheadDays);

    // Earnings: the one feed the dashboard's earnings tile reads too.
    QPointer<WhatsOnPanel> self = this;
    earnings_calendar::fetch_days(
        this, from, to,
        [self](const earnings_calendar::DaysResult& r) {
            if (!self)
                return;
            if (!r.ok) {
                // Nothing answered: keep what is shown and say why.
                self->set_error(QStringLiteral("Earnings"), r.error);
                return;
            }
            // Cut once, on arrival, to what this panel can show (large caps
            // and the user's names) — the feed hands every company over.
            self->earnings_ = wo::earnings_that_matter(r.rows, self->mine_);
            // A day Nasdaq did not answer is NOT a day without earnings: say
            // so instead of letting the gap read as a quiet day.
            if (!r.failed_days.isEmpty())
                self->set_error(QStringLiteral("Earnings"),
                                QStringLiteral("Nasdaq did not answer for %1 of %2 days — those days may be missing")
                                    .arg(r.failed_days.size())
                                    .arg(r.days_total));
            else
                self->clear_error(QStringLiteral("Earnings"));
            self->render();
        },
        force);

    // Dividends and insider clusters: the user's own names only.
    QStringList syms(mine_.begin(), mine_.end());
    syms.sort();
    const QString sym_arg = syms.join(QLatin1Char(','));
    const QString key =
        QString::fromLatin1(kEventsKey) + from.toString(Qt::ISODate) + QLatin1Char(':') +
        QString::fromLatin1(QCryptographicHash::hash(sym_arg.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    if (!force) {
        if (const auto d = cached_doc(key); d && d->isObject()) {
            apply_events(d->object());
            return;
        }
    }
    QStringList args = {QStringLiteral("company_events"), from.toString(Qt::ISODate), to.toString(Qt::ISODate)};
    if (!sym_arg.isEmpty())
        args << sym_arg;
    // ~1 Nasdaq request per weekday on a cold cache, paced: well over the
    // default 30 s process deadline the first time.
    python::PythonRunner::instance().run( // D1-exempt: this panel is the only reader of the user's company events
        QStringLiteral("whats_on_calendar.py"), args,
        [self, key](python::PythonResult r) {
            if (!self)
                return;
            const QJsonObject root = QJsonDocument::fromJson(python::extract_json(r.output).toUtf8()).object();
            if (!r.success || root.isEmpty() || root.contains("error")) {
                // Dividends AND insider clusters come from this one call.
                self->set_error(QStringLiteral("Company events"),
                                root.value("error").toString(r.success ? QStringLiteral("calendar unreadable")
                                                                       : QStringLiteral("script failed")));
                return;
            }
            self->clear_error(QStringLiteral("Company events"));
            // A partial answer is cached briefly so the missing days are retried.
            const bool partial = !root.value("div_days_failed").toArray().isEmpty();
            put_doc(key, QJsonDocument(root), partial ? 10 * 60 : kEventsSec);
            self->apply_events(root);
        },
        {}, 180'000);
}

void WhatsOnPanel::apply_auctions() {
    auctions_ = ta::parse(td_upcoming_, td_auctioned_);
    render();
}

void WhatsOnPanel::fetch_auctions(bool force) {
    QPointer<WhatsOnPanel> self = this;
    struct Src {
        const char* key;
        QString url;
        QJsonArray WhatsOnPanel::*slot;
        QString error_source; ///< one per endpoint: one answering must not clear the other's refusal
    };
    const Src srcs[] = {
        {kTdUpKey, QStringLiteral("https://www.treasurydirect.gov/TA_WS/securities/upcoming?format=json"),
         &WhatsOnPanel::td_upcoming_, QStringLiteral("Auctions (upcoming)")},
        {kTdDoneKey, QStringLiteral("https://www.treasurydirect.gov/TA_WS/securities/auctioned?format=json&days=10"),
         &WhatsOnPanel::td_auctioned_, QStringLiteral("Auctions (results)")},
    };
    for (const auto& s : srcs) {
        if (!force) {
            if (const auto d = cached_doc(s.key); d && d->isArray()) {
                this->*s.slot = d->array();
                apply_auctions();
                continue;
            }
        }
        const QString key = QString::fromLatin1(s.key);
        auto slot = s.slot;
        const QString err = s.error_source;
        HttpClient::instance().get(s.url, [self, key, slot, err](Result<QJsonDocument> res) {
            if (!self)
                return;
            if (!res.is_ok() || !res.value().isArray()) {
                // Keep the last copy (if any) on screen; the refusal is stated.
                self->set_error(err, QStringLiteral("TreasuryDirect unavailable"));
                return;
            }
            put_doc(key, res.value(), kTdSec);
            self->*slot = res.value().array();
            self->clear_error(err);
            self->apply_auctions();
        });
    }
}

void WhatsOnPanel::fetch_fred(bool force) {
    // Only the series behind releases already out this week (a handful).
    const QDateTime now = QDateTime::currentDateTime();
    const QStringList need = wo::fred_series_needed(macro_, now, wo::week_start(now.date()), min_impact_ > wo::Impact::Low);
    QStringList fetch;
    for (const QString& id : need) {
        if (!force) {
            if (const auto hit = CacheManager::instance().try_get(QString::fromLatin1(kFredKey) + id)) {
                fred_[id] = mr::parse_fred_csv(*hit);
                continue;
            }
        }
        if (!fred_inflight_.contains(id))
            fetch << id;
    }
    if (fetch.isEmpty())
        return;
    for (const auto& id : fetch)
        fred_inflight_.insert(id);
    // FRED's public graph download (no API key), through the script: FRED's
    // edge resets the stream for the app's own User-Agent.
    QPointer<WhatsOnPanel> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("whats_on_calendar.py"),
        {QStringLiteral("fred"), fetch.join(QLatin1Char(',')),
         now.date().addYears(-2).addMonths(-2).toString(Qt::ISODate)},
        [self, fetch](python::PythonResult r) {
            if (!self)
                return;
            for (const auto& id : fetch)
                self->fred_inflight_.remove(id);
            const QJsonObject root = QJsonDocument::fromJson(python::extract_json(r.output).toUtf8()).object();
            const QJsonObject series = root.value("series").toObject();
            QStringList failed;
            for (const auto& id : fetch) {
                const QString csv = series.value(id).toString();
                const auto obs = mr::parse_fred_csv(csv);
                if (obs.isEmpty()) {
                    failed << id; // refused or unreadable: the actual stays absent
                    continue;
                }
                CacheManager::instance().put(QString::fromLatin1(kFredKey) + id, csv, kFredSec,
                                             QStringLiteral("whatson"));
                self->fred_[id] = obs;
            }
            if (failed.isEmpty())
                self->clear_error(QStringLiteral("FRED"));
            else
                self->set_error(QStringLiteral("FRED"),
                                QStringLiteral("%1 unavailable — those actuals not shown").arg(failed.join(", ")));
            self->render();
        },
        {}, 60'000);
}

void WhatsOnPanel::set_headlines(const QVector<services::NewsArticle>& ranked) {
    QVector<services::NewsArticle> keep;
    for (const auto& a : ranked) {
        if (keep.size() >= kHeadlineCount)
            break;
        if (!news::is_promo_spam(a.headline))
            keep.append(a);
    }
    // Rebuilding the rows on every feed tick is wasted work when the top
    // stories did not change.
    bool same = keep.size() == headlines_.size();
    for (int i = 0; same && i < keep.size(); ++i)
        same = keep[i].id == headlines_[i].id;
    if (same)
        return;
    headlines_ = keep;
    render();
}

// ── Rendering ───────────────────────────────────────────────────────────────

void WhatsOnPanel::render_rates() {
    QStringList parts;
    if (!policy_range_.isEmpty())
        parts << (effr_.isEmpty() ? QStringLiteral("Fed funds <b>%1</b>").arg(policy_range_)
                                  : QStringLiteral("Fed funds <b>%1</b> (effective %2)").arg(policy_range_, effr_));
    else if (!effr_.isEmpty())
        parts << QStringLiteral("Fed funds effective %1").arg(effr_);
    if (!y2_.isEmpty())
        parts << QStringLiteral("2Y <b>%1</b>").arg(y2_);
    if (!y10_.isEmpty())
        parts << QStringLiteral("10Y <b>%1</b>").arg(y10_);
    const QDate today = QDate::currentDate();
    for (const QDate& d : std::as_const(fomc_)) {
        if (d >= today) {
            parts << QStringLiteral("next FOMC %1").arg(wo::days_away(d, today));
            break;
        }
    }
    rates_label_->setText(parts.isEmpty() ? QStringLiteral("Loading rates…") : parts.join(QStringLiteral(" · ")));
    QStringList asof;
    if (!effr_date_.isEmpty())
        asof << QStringLiteral("EFFR as of %1 (NY Fed)").arg(effr_date_);
    if (!yields_date_.isEmpty())
        asof << QStringLiteral("yields as of %1 (Fed H.15)").arg(yields_date_);
    rates_label_->setToolTip(asof.join(QLatin1Char('\n')));
}

QVector<wo::Item> WhatsOnPanel::collect_items(const QDateTime& now) const {
    const QDate today = now.toLocalTime().date();
    const QDate from = wo::week_start(today);
    // TreasuryDirect is the authority on US auctions; the calendar's own US
    // "10-y Bond Auction" rows would repeat them. Other countries' auctions
    // (Bund, JGB — shown with the filter off) have no other source and stay.
    QVector<mc::MacroEvent> macro = macro_;
    if (!auctions_.isEmpty())
        macro.erase(std::remove_if(macro.begin(), macro.end(),
                                   [](const mc::MacroEvent& e) {
                                       return (e.country == QLatin1String("USD") || e.country == QLatin1String("US")) &&
                                              e.title.contains(QLatin1String("Auction"));
                                   }),
                    macro.end());
    QVector<wo::Item> items = wo::items_from_macro(macro, now, min_impact_ > wo::Impact::Low, from, &fred_);
    items += wo::items_from_fomc(fomc_, today, items);
    items += wo::items_from_ipo_deals(ipo_deals_, today);
    items += wo::items_from_earnings(earnings_, from, mine_);
    items += wo::items_from_dividends(events_.value("dividends").toArray(), from);
    items += wo::items_from_insiders(events_.value("insiders").toArray(), from,
                                     events_.value("insider_last_scanned").toString());
    items += wo::items_from_auctions(auctions_, from);
    // The month after this one is enough horizon for market dates.
    items += wo::items_from_market_dates(
        market_calendar::market_dates(today, QDate(today.year(), today.month(), 1).addMonths(2).addDays(-1)));
    return items;
}

void WhatsOnPanel::open_symbol(const QString& symbol) {
    if (symbol.isEmpty())
        return;
    // Right beside this drawer (NewsScreen's peek pane), not a dock elsewhere.
    emit open_beside(QStringLiteral("equity_research"), QVariantMap{{"symbol", symbol}});
}

QWidget* WhatsOnPanel::make_row(const wo::Row& r) {
    const wo::Item& item = r.head();
    const bool grouped = r.items.size() > 1 || item.source == wo::Item::Source::Earnings;
    auto* row = new QWidget;
    row->setObjectName("whatsOnRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setCursor(Qt::PointingHandCursor);
    auto* v = new QVBoxLayout(row);
    v->setContentsMargins(2, 3, 2, 3);
    v->setSpacing(1);

    // Line 1: the date (always the calendar date; a countdown only as a hint),
    // your-names marker, impact tag. Line 2: what — kind + title. Line 3: the
    // numbers in words.
    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);
    auto* when = new QLabel(wo::when_label(item, QDateTime::currentDateTime()));
    when->setObjectName("whatsOnWhen");
    when->setWordWrap(true);
    when_labels_.append({when, item});
    top->addWidget(when, 1);
    if (r.mine()) {
        auto* m = new QLabel(QStringLiteral("◆"));
        m->setStyleSheet(QStringLiteral("color:%1;").arg(ui::colors::AMBER()));
        m->setToolTip(QStringLiteral("In your watchlists or portfolios"));
        top->addWidget(m, 0, Qt::AlignTop);
    }
    if (const QString tag = wo::impact_tag(r.impact()); !tag.isEmpty()) {
        auto* imp = new QLabel(tag);
        const QString c = r.impact() == wo::Impact::High ? ui::colors::NEGATIVE() : ui::colors::WARNING();
        imp->setStyleSheet(QStringLiteral("color:%1; font-weight:700; border:1px solid %1; padding:0 3px;").arg(c));
        imp->setToolTip(wo::impact_rules());
        top->addWidget(imp, 0, Qt::AlignTop);
    }
    v->addLayout(top);
    const QString kind_html = QStringLiteral("<span style=\"color:%1; font-weight:700;\">%2</span>&nbsp; ")
                                  .arg(kind_color(item.kind), item.kind.toHtmlEscaped());

    QStringList details;
    if (grouped && item.source == wo::Item::Source::Earnings) {
        // One row per session: the largest names (yours first) as links to
        // Equity Research, then "+N more" — the row's detail lists them all.
        // Reported: the EPS surprise beside the name.
        QStringList parts;
        const int shown = wo::earnings_shown(r);
        for (int i = 0; i < shown; ++i) {
            const auto& it = r.items[i];
            const QString label = it.name.isEmpty() ? it.symbol : it.name;
            QString s = QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none;%3\">%4%5</a>")
                            .arg(it.symbol.toHtmlEscaped(), it.mine ? ui::colors::AMBER() : ui::colors::TEXT_PRIMARY(),
                                 it.mine || it.impact == wo::Impact::High ? QStringLiteral(" font-weight:700;")
                                                                          : QString(),
                                 it.mine ? QStringLiteral("◆") : QString(), label.toHtmlEscaped());
            if (it.released && std::isfinite(it.surprise_pct))
                s += QStringLiteral(" <span style=\"color:%1;\">%2%3%</span>")
                         .arg(it.surprise_pct >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE(),
                              it.surprise_pct >= 0 ? QStringLiteral("+") : QString(),
                              QString::number(it.surprise_pct, 'f', 1));
            parts << s;
        }
        for (const auto& it : r.items)
            details << it.detail;
        QString text = kind_html + parts.join(QStringLiteral(" · "));
        if (const int more = int(r.items.size()) - shown; more > 0)
            text += QStringLiteral(" <span style=\"color:%1;\">+%2 more</span>")
                        .arg(ui::colors::TEXT_SECONDARY())
                        .arg(more);
        auto* title = new QLabel(text);
        title->setTextFormat(Qt::RichText);
        title->setWordWrap(true);
        title->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        connect(title, &QLabel::linkActivated, this, [this](const QString& sym) { open_symbol(sym); });
        v->addWidget(title);
        row->setToolTip(QStringLiteral("Click a company for Equity Research"));
    } else {
        QStringList titles, figs;
        for (const auto& it : r.items) {
            titles << it.title;
            if (!it.figures.isEmpty())
                figs << (r.items.size() > 1 ? it.title.section(QLatin1Char(' '), 0, 0) + QLatin1Char(' ') + it.figures
                                            : it.figures);
            details << it.detail;
        }
        // Grouped bills: "13W · 26W bill auctions".
        QString text = titles.join(QStringLiteral(" · "));
        if (r.items.size() > 1 && item.source == wo::Item::Source::Auction) {
            QStringList terms;
            for (const auto& it : r.items)
                terms << it.title.section(QLatin1Char(' '), 0, 0);
            text = terms.join(QStringLiteral(" · ")) + QStringLiteral(" bill auctions");
        }
        auto* title = new QLabel(kind_html + text.toHtmlEscaped());
        title->setTextFormat(Qt::RichText);
        title->setWordWrap(true);
        v->addWidget(title);
        if (!figs.isEmpty()) {
            auto* fig = new QLabel(figs.join(QStringLiteral(" · ")));
            fig->setObjectName("whatsOnFigures");
            fig->setWordWrap(true);
            v->addWidget(fig);
        }
        row->setToolTip(item.source == wo::Item::Source::Ipo
                            ? QStringLiteral("Open in Private Market › IPO Calendar\n%1")
                                  .arg(item.detail)
                        : !item.symbol.isEmpty()            ? QStringLiteral("Open %1 in Equity Research\n\n%2")
                                                                  .arg(item.symbol, item.detail)
                                                            : QStringLiteral("Click for details"));
    }
    auto* detail = new QLabel(details.join(QStringLiteral("\n\n")));
    detail->setObjectName("whatsOnDetail");
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail->setVisible(expanded_.contains(item.row_key()));
    v->addWidget(detail);

    row->setProperty("whatsOnSource", static_cast<int>(item.source));
    row->setProperty("whatsOnKey", item.key);
    row->setProperty("whatsOnDeal", item.deal_id);
    row->setProperty("whatsOnRowKey", item.row_key());
    row->setProperty("whatsOnSymbol", grouped ? QString() : item.symbol);
    row->installEventFilter(this);
    return row;
}

QWidget* WhatsOnPanel::make_headline_row(const services::NewsArticle& a, int index) {
    auto* row = new QWidget;
    row->setObjectName("whatsOnRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setCursor(Qt::PointingHandCursor);
    auto* v = new QVBoxLayout(row);
    v->setContentsMargins(2, 3, 2, 3);
    v->setSpacing(1);
    auto* h = new QLabel(a.headline);
    h->setWordWrap(true);
    v->addWidget(h);
    auto* meta = new QLabel(a.time.isEmpty() ? a.source : a.source + QStringLiteral(" · ") + a.time);
    meta->setObjectName("whatsOnWhen");
    v->addWidget(meta);
    row->setProperty(kItemProp, index);
    row->setProperty("whatsOnSource", -1);
    row->installEventFilter(this);
    return row;
}

QWidget* WhatsOnPanel::make_brief_row() {
    // The weekly insider-buying brief is published each Saturday; reading it
    // and announcing it happen on the SOCIAL screen.
    auto* row = new QWidget;
    row->setObjectName("whatsOnRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setCursor(Qt::PointingHandCursor);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(2, 3, 2, 3);
    h->setSpacing(6);
    auto* kind = new QLabel(QStringLiteral("BRIEF"));
    kind->setStyleSheet(QStringLiteral("color:%1; font-weight:700;").arg(ui::colors::AMBER()));
    h->addWidget(kind);
    auto* title = new QLabel(QStringLiteral("Weekly brief — insider buying →"));
    title->setWordWrap(true);
    h->addWidget(title, 1);
    row->setToolTip(QStringLiteral("Opens SOCIAL with the latest published issue"));
    row->setProperty("whatsOnSource", kBriefSource);
    row->installEventFilter(this);
    return row;
}

bool WhatsOnPanel::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() != QEvent::MouseButtonRelease || static_cast<QMouseEvent*>(e)->button() != Qt::LeftButton)
        return QWidget::eventFilter(obj, e);
    auto* row = qobject_cast<QWidget*>(obj);
    if (!row || !row->property("whatsOnSource").isValid())
        return QWidget::eventFilter(obj, e);
    const int src = row->property("whatsOnSource").toInt();
    if (src == kSectionSource) {
        const int s = row->property("whatsOnSection").toInt();
        if (!collapsed_.remove(s))
            collapsed_.insert(s);
        QVariantList keep;
        for (int c : std::as_const(collapsed_))
            keep << c;
        QSettings().setValue(kCollapsedKey, keep);
        // Deferred: the header being clicked is deleted by the rebuild.
        QMetaObject::invokeMethod(this, [this]() { render(); }, Qt::QueuedConnection);
        return true;
    }
    if (src == kBriefSource) {
        emit open_beside(QStringLiteral("social"), {});
        return true;
    }
    if (src == -1) {
        const int i = row->property(kItemProp).toInt();
        if (i >= 0 && i < headlines_.size())
            emit article_clicked(headlines_[i]);
        return true;
    }
    if (src == static_cast<int>(wo::Item::Source::Ipo)) {
        emit open_beside(QStringLiteral("pre_ipo"), QVariantMap{{"deal_id", row->property("whatsOnDeal").toString()},
                                                                {"key", row->property("whatsOnKey").toString()}});
        return true;
    }
    if (const QString sym = row->property("whatsOnSymbol").toString(); !sym.isEmpty()) {
        open_symbol(sym);
        return true;
    }
    if (auto* detail = row->findChild<QLabel*>(QStringLiteral("whatsOnDetail"))) {
        detail->setVisible(!detail->isVisible());
        const QString k = row->property("whatsOnRowKey").toString();
        if (detail->isVisible())
            expanded_.insert(k);
        else
            expanded_.remove(k);
    }
    return true;
}

void WhatsOnPanel::tick() {
    const QDateTime now = QDateTime::currentDateTime();
    if (rendered_day_ != now.date()) { // a new day re-buckets everything
        render();
        return;
    }
    for (const auto& [label, item] : std::as_const(when_labels_))
        if (label)
            label->setText(wo::when_label(item, now));
    if (next_up_) {
        // The event passed: find the next one (the same test wo::next_up uses).
        if (!wo::is_ahead(*next_up_, now)) {
            render();
            return;
        }
        // MY STOCKS: the line follows the list under it — the next event on
        // your names, whatever its impact.
        next_label_->setText(
            QStringLiteral("<span style=\"color:%1; font-weight:700;\">%2</span>&nbsp; %3")
                .arg(mine_only_ ? ui::colors::AMBER() : ui::colors::NEGATIVE(),
                     mine_only_ ? QStringLiteral("NEXT ◆") : QStringLiteral("NEXT HIGH"),
                     wo::next_text(*next_up_, now).toHtmlEscaped()));
    }
}

void WhatsOnPanel::render() {
    // Rebuild the body. Rows are cheap and the sources change rarely.
    while (body_->count() > 0) {
        auto* it = body_->takeAt(0);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    when_labels_.clear();
    render_rates();

    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.toLocalTime().date();
    rendered_day_ = today;
    const QVector<wo::Item> items = collect_items(now);
    // MY STOCKS: the line follows the list under it, so it honours the impact selector too.
    if (mine_only_) {
        QVector<wo::Item> listed;
        for (const auto& i : items)
            if (i.impact >= min_impact_)
                listed += i;
        next_up_ = wo::next_up(listed, now, true);
    } else {
        next_up_ = wo::next_up(items, now, false);
    }
    next_label_->setVisible(next_up_.has_value());
    next_label_->setToolTip(mine_only_ ? QStringLiteral("Next event on your portfolios and watchlists")
                                       : QStringLiteral("The next HIGH-impact event\n\n") + wo::impact_rules());
    QMap<wo::Section, int> hidden;
    const auto sections = wo::sectionize(items, today, wo::Filter{mine_only_, min_impact_}, &hidden);

    auto section = [this](wo::Section s, int rows) {
        const bool folded = collapsed_.contains(static_cast<int>(s));
        auto* l = new QLabel(section_header_text(s, rows, folded));
        l->setObjectName("whatsOnSection");
        l->setCursor(Qt::PointingHandCursor);
        l->setToolTip(folded ? QStringLiteral("Show") : QStringLiteral("Fold"));
        l->setProperty("whatsOnSource", kSectionSource);
        l->setProperty("whatsOnSection", static_cast<int>(s));
        l->installEventFilter(this);
        body_->addWidget(l);
        return !folded;
    };
    auto none = [this](const QString& text) {
        auto* l = new QLabel(text);
        l->setObjectName("whatsOnWhen");
        body_->addWidget(l);
    };
    if (mine_only_ && mine_.isEmpty())
        none(QStringLiteral("No stocks in your portfolios or watchlists yet."));
    for (auto s : {wo::Section::Earlier, wo::Section::Today, wo::Section::ThisWeek, wo::Section::ThisMonth,
                   wo::Section::Later}) {
        const auto it = sections.constFind(s);
        const int n = it == sections.constEnd() ? 0 : int(it->size());
        if (s == wo::Section::Earlier && n == 0)
            continue; // nothing came out yet this week (Monday morning)
        if (!section(s, n))
            continue;
        if (n == 0) {
            // THIS WEEK always holds the brief row, so say what is empty.
            none(hidden.value(s) > 0 ? (min_impact_ == wo::Impact::High ? QStringLiteral("Nothing HIGH-impact")
                                                                         : QStringLiteral("Nothing MED or HIGH"))
                 : mine_only_                                                   ? QStringLiteral("Nothing on your stocks")
                 : s == wo::Section::ThisMonth || s == wo::Section::Later ? QStringLiteral("Nothing scheduled in the sources yet")
                 : s == wo::Section::ThisWeek                                   ? QStringLiteral("No releases scheduled")
                                                                                : QStringLiteral("Nothing scheduled"));
        } else {
            for (const auto& row : *it)
                body_->addWidget(make_row(row));
        }
        if (const int h = hidden.value(s); h > 0) {
            auto* l = new QLabel(QStringLiteral("+%1 more under %2")
                                     .arg(h)
                                     .arg(min_impact_ == wo::Impact::High ? QStringLiteral("MED+ / ALL")
                                                                          : QStringLiteral("ALL")));
            l->setObjectName("whatsOnWhen");
            l->setWordWrap(true);
            l->setToolTip(QStringLiteral("Events below the impact you picked\n") +
                          wo::impact_rules());
            body_->addWidget(l);
        }
        if (s == wo::Section::ThisWeek && !mine_only_)
            body_->addWidget(make_brief_row());
    }

    QLabel* head = new QLabel(QStringLiteral("TOP HEADLINES"));
    head->setObjectName("whatsOnSection");
    body_->addWidget(head);
    if (headlines_.isEmpty())
        none(QStringLiteral("Waiting for the feed…"));
    for (int i = 0; i < headlines_.size(); ++i)
        body_->addWidget(make_headline_row(headlines_[i], i));

    QStringList notes = errors_;
    notes << QStringLiteral("Macro: ForexFactory (this + next week) · actuals: FRED · FOMC: federalreserve.gov · "
                            "Rates: NY Fed, Fed H.15 · Earnings, ex-div, IPOs: Nasdaq · Auctions: TreasuryDirect · "
                            "Market dates: NYSE rules, computed · Times ET");
    status_label_->setText(notes.join(QLatin1Char('\n')));
    tick();
}

} // namespace finterm::screens

#include "screens/news/WhatsOnPanel.h"

#include "core/events/EventBus.h"
#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "python/PythonRunner.h"
#include "services/calendar/MacroCalendarFeed.h"
#include "storage/cache/CacheManager.h"
#include "services/news/NewsPromo.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>

namespace finterm::screens {

namespace {
namespace mc = finterm::macro_calendar;
namespace wo = finterm::whats_on;

constexpr int kRefreshMs = 30 * 60 * 1000; // sources change a few times a day
constexpr int kHeadlineCount = 6;
constexpr const char* kItemProp = "whatsOnIndex";
constexpr int kBriefSource = -2; ///< "whatsOnSource" of the weekly-brief row (-1: a headline)

QString kind_color(const QString& kind) {
    if (kind == QLatin1String("FED"))
        return ui::colors::AMBER();
    if (kind == QLatin1String("LABOR"))
        return ui::colors::CYAN();
    if (kind == QLatin1String("INFLATION"))
        return ui::colors::WARNING();
    if (kind == QLatin1String("GROWTH"))
        return ui::colors::POSITIVE();
    if (kind == QLatin1String("IPO"))
        return ui::colors::INFO();
    return ui::colors::TEXT_SECONDARY();
}

QString pct(double fraction_or_pct, bool is_fraction) {
    return QString::number(is_fraction ? fraction_or_pct * 100.0 : fraction_or_pct, 'f', 2) + QLatin1Char('%');
}
} // namespace

WhatsOnPanel::WhatsOnPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("whatsOnPanel");
    nam_ = new QNetworkAccessManager(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(6);
    filter_btn_ = new QPushButton(QStringLiteral("US TOP-TIER"), this);
    filter_btn_->setObjectName("whatsOnFilter");
    filter_btn_->setCheckable(true);
    filter_btn_->setChecked(true);
    filter_btn_->setCursor(Qt::PointingHandCursor);
    filter_btn_->setToolTip(QStringLiteral("On: US high-impact releases plus Fed, labour, inflation and growth data.\n"
                                           "Off: every country and impact level in the calendar."));
    connect(filter_btn_, &QPushButton::toggled, this, [this](bool on) {
        top_tier_only_ = on;
        render();
    });
    bar->addWidget(filter_btn_);
    bar->addStretch();
    auto* refresh_btn = new QPushButton(QStringLiteral("↻"), this);
    refresh_btn->setObjectName("whatsOnFilter");
    refresh_btn->setFixedWidth(28);
    refresh_btn->setCursor(Qt::PointingHandCursor);
    refresh_btn->setToolTip(QStringLiteral("Refresh calendar, rates and IPOs"));
    connect(refresh_btn, &QPushButton::clicked, this, [this]() { refresh(true); });
    bar->addWidget(refresh_btn);
    root->addLayout(bar);

    rates_label_ = new QLabel(this);
    rates_label_->setObjectName("whatsOnRates");
    rates_label_->setWordWrap(true);
    rates_label_->setTextFormat(Qt::RichText);
    root->addWidget(rates_label_);

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
}

void WhatsOnPanel::refresh(bool force) {
    last_refresh_ = QDateTime::currentDateTime();
    fetch_macro(force);
    fetch_fomc(force);
    fetch_rates(force);
    fetch_ipos(force);
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

// ── Sources ─────────────────────────────────────────────────────────────────

void WhatsOnPanel::fetch_macro(bool force) {
    // Same feed as the dashboard's ECONOMIC CALENDAR, through the shared
    // cached fetch: the feed answers 429 to repeated requests.
    mc::fetch_week(
        this,
        [this](const mc::FeedResult& r) {
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
constexpr const char* kYieldsKey = "whatson:yields:v1";
constexpr const char* kIpoKey = "whatson:ipos:v1";
constexpr int kFomcSec = 12 * 3600; // a published calendar
constexpr int kEffrSec = 3600;      // one print a day
constexpr int kYieldsSec = 3 * 3600;
constexpr int kIpoSec = 30 * 60;

std::optional<QJsonDocument> cached_doc(const char* key) {
    if (const auto hit = CacheManager::instance().try_get(QString::fromLatin1(key))) {
        const auto d = QJsonDocument::fromJson(hit->toUtf8());
        if (!d.isNull())
            return d;
    }
    return std::nullopt;
}
void put_doc(const char* key, const QJsonDocument& d, int ttl) {
    CacheManager::instance().put(QString::fromLatin1(key), QString::fromUtf8(d.toJson(QJsonDocument::Compact)),
                                 ttl, QStringLiteral("whatson"));
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
    // 2y / 10y: H.15 via the existing federal_reserve_data.py treasury_rates.
    const auto y_hit = force ? std::nullopt : cached_doc(kYieldsKey);
    if (y_hit && y_hit->isArray() && apply_yields(y_hit->array()))
        return;
    const QString start = QDate::currentDate().addDays(-14).toString(Qt::ISODate);
    python::PythonRunner::instance().run(
        QStringLiteral("federal_reserve_data.py"), {QStringLiteral("treasury_rates"), start},
        [self](python::PythonResult r) {
            if (!self)
                return;
            const QJsonObject root = QJsonDocument::fromJson(python::extract_json(r.output).toUtf8()).object();
            const QJsonArray rows = root.value("data").toArray();
            if (self->apply_yields(rows)) {
                put_doc(kYieldsKey, QJsonDocument(rows), kYieldsSec);
                return;
            }
            self->set_error(QStringLiteral("Treasury yields"),
                            root.value("error").toString(r.success ? QStringLiteral("no recent H.15 data")
                                                                   : QStringLiteral("script failed")));
        });
}

void WhatsOnPanel::fetch_ipos(bool force) {
    if (!force) {
        if (const auto d = cached_doc(kIpoKey); d && d->isArray()) {
            ipo_deals_ = d->array();
            clear_error(QStringLiteral("IPOs"));
            render();
            return;
        }
    }
    QPointer<WhatsOnPanel> self = this;
    // The same calendar IPO Watch shows (fast pass: Nasdaq calendar only).
    python::PythonRunner::instance().run(
        QStringLiteral("ipo_calendar.py"), {QStringLiteral("calendar")}, [self](python::PythonResult r) {
            if (!self)
                return;
            const QJsonObject root = QJsonDocument::fromJson(python::extract_json(r.output).toUtf8()).object();
            if (!r.success || root.isEmpty() || root.contains("error")) {
                self->set_error(QStringLiteral("IPOs"),
                                root.value("error").toString(QStringLiteral("IPO calendar unavailable")));
                return;
            }
            self->ipo_deals_ = root.value("deals").toArray();
            put_doc(kIpoKey, QJsonDocument(self->ipo_deals_), kIpoSec);
            self->clear_error(QStringLiteral("IPOs"));
            self->render();
        });
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
        parts << QStringLiteral("Fed funds <b>%1</b>").arg(policy_range_);
    if (!effr_.isEmpty())
        parts << QStringLiteral("EFFR %1").arg(effr_);
    if (!y2_.isEmpty())
        parts << QStringLiteral("2Y <b>%1</b>").arg(y2_);
    if (!y10_.isEmpty())
        parts << QStringLiteral("10Y <b>%1</b>").arg(y10_);
    const QDate today = QDate::currentDate();
    for (const QDate& d : std::as_const(fomc_)) {
        if (d >= today) {
            parts << QStringLiteral("next FOMC %1 (%2d)")
                         .arg(QLocale(QLocale::English).toString(d, QStringLiteral("MMM d")))
                         .arg(today.daysTo(d));
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

QWidget* WhatsOnPanel::make_row(const wo::Item& item) {
    const QDate today = QDate::currentDate();
    auto* row = new QWidget;
    row->setObjectName("whatsOnRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setCursor(Qt::PointingHandCursor);
    auto* v = new QVBoxLayout(row);
    v->setContentsMargins(2, 3, 2, 3);
    v->setSpacing(1);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);
    auto* when = new QLabel(mc::short_when(item.when, item.timed, today));
    when->setObjectName("whatsOnWhen");
    top->addWidget(when);
    auto* kind = new QLabel(item.kind);
    kind->setStyleSheet(QStringLiteral("color:%1; font-weight:700;").arg(kind_color(item.kind)));
    top->addWidget(kind);
    top->addStretch();
    v->addLayout(top);

    auto* title = new QLabel(item.title);
    title->setWordWrap(true);
    v->addWidget(title);
    if (!item.figures.isEmpty()) {
        auto* fig = new QLabel(item.figures);
        fig->setObjectName("whatsOnFigures");
        fig->setWordWrap(true);
        v->addWidget(fig);
    }
    auto* detail = new QLabel(item.detail);
    detail->setObjectName("whatsOnDetail");
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail->hide();
    v->addWidget(detail);

    row->setToolTip(item.source == wo::Item::Source::Ipo ? QStringLiteral("Open in IPO Watch")
                                                         : QStringLiteral("Click for details"));
    row->setProperty("whatsOnSource", static_cast<int>(item.source));
    row->setProperty("whatsOnKey", item.key);
    row->setProperty("whatsOnDeal", item.deal_id);
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
    row->setToolTip(QStringLiteral("Published each Saturday. Opens SOCIAL with the latest published issue and "
                                   "what has been posted."));
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
    if (src == kBriefSource) {
        EventBus::instance().publish("nav.switch_screen", QVariantMap{{"screen_id", "social"}});
        return true;
    }
    if (src < 0) {
        const int i = row->property(kItemProp).toInt();
        if (i >= 0 && i < headlines_.size())
            emit article_clicked(headlines_[i]);
        return true;
    }
    if (src == static_cast<int>(wo::Item::Source::Ipo)) {
        const QString key = row->property("whatsOnKey").toString();
        const QString deal = row->property("whatsOnDeal").toString();
        // Beside the news, as the app's other "open in X" gestures do.
        // nav.split_alongside materializes the screen synchronously; the
        // selection is still queued so it lands after any layout settling.
        EventBus::instance().publish("nav.split_alongside", QVariantMap{{"screen_id", "pre_ipo"}});
        QMetaObject::invokeMethod(
            this,
            [key, deal]() {
                EventBus::instance().publish("ipo_watch.select", QVariantMap{{"deal_id", deal}, {"key", key}});
            },
            Qt::QueuedConnection);
        return true;
    }
    if (auto* detail = row->findChild<QLabel*>(QStringLiteral("whatsOnDetail")))
        detail->setVisible(!detail->isVisible());
    return true;
}

void WhatsOnPanel::render() {
    // Rebuild the body. Rows are cheap and the sources change rarely.
    while (body_->count() > 0) {
        auto* it = body_->takeAt(0);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    render_rates();

    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.toLocalTime().date();
    QVector<wo::Item> items = wo::items_from_macro(macro_, now, top_tier_only_);
    items += wo::items_from_fomc(fomc_, today, items);
    items += wo::items_from_ipo_deals(ipo_deals_, today);
    const auto buckets = wo::bucketize(items, today);

    auto section = [this](const QString& title) {
        auto* l = new QLabel(title);
        l->setObjectName("whatsOnSection");
        body_->addWidget(l);
    };
    for (auto b : {mc::Bucket::Today, mc::Bucket::ThisWeek, mc::Bucket::ThisMonth, mc::Bucket::Upcoming}) {
        const auto it = buckets.constFind(b);
        section(mc::bucket_title(b));
        if (it == buckets.constEnd() || it->isEmpty()) {
            // THIS WEEK always holds the brief row, so say what is empty.
            auto* none = new QLabel(b == mc::Bucket::ThisMonth || b == mc::Bucket::Upcoming
                                        ? QStringLiteral("Nothing scheduled in the sources yet")
                                    : b == mc::Bucket::ThisWeek ? QStringLiteral("No releases scheduled")
                                                                : QStringLiteral("Nothing scheduled"));
            none->setObjectName("whatsOnWhen");
            body_->addWidget(none);
        } else {
            for (const auto& item : *it)
                body_->addWidget(make_row(item));
        }
        if (b == mc::Bucket::ThisWeek)
            body_->addWidget(make_brief_row());
    }

    section(QStringLiteral("TOP HEADLINES"));
    if (headlines_.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("Waiting for the feed…"));
        none->setObjectName("whatsOnWhen");
        body_->addWidget(none);
    }
    for (int i = 0; i < headlines_.size(); ++i)
        body_->addWidget(make_headline_row(headlines_[i], i));

    QStringList notes = errors_;
    notes << QStringLiteral("Macro: ForexFactory (this week) · FOMC: federalreserve.gov · Rates: NY Fed, Fed H.15 · "
                            "IPOs: Nasdaq");
    status_label_->setText(notes.join(QLatin1Char('\n')));
}

} // namespace finterm::screens

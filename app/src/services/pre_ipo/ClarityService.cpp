// src/services/pre_ipo/ClarityService.cpp
#include "services/pre_ipo/ClarityService.h"

#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "python/PythonRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace finterm::services {

using namespace finterm::pre_ipo;

namespace {

constexpr qint64 kIndexMaxAgeSecs = 6 * 60 * 60;
constexpr int kBatch = 25;
constexpr int kBatchTimeoutMs = 240'000; // 25 pages at one a second, plus slow answers
constexpr int kOneTimeoutMs = 60'000;
constexpr int kMaxBackground = 200;     // queued at once
constexpr qint64 kReaskMs = 12LL * 3600 * 1000; // the script's page TTL
constexpr qint64 kFailCoolMs = 10LL * 60 * 1000; // background, after no answer
constexpr qint64 kFailRetryClickMs = 60LL * 1000; // a click, after no answer
// Follows the script's SCHEMA_V: a record parsed by an older script is not
// hydrated (2: IPO / acquisition dates as ISO).
constexpr int kCacheVersion = 2;

double num(const QJsonValue& v) {
    return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

QDate iso(const QJsonValue& v) {
    return QDate::fromString(v.toString(), Qt::ISODate);
}

QVector<PricePoint> points(const QJsonValue& v) {
    QVector<PricePoint> out;
    for (const auto& p : v.toArray()) {
        const QJsonArray a = p.toArray();
        if (a.size() < 2)
            continue;
        const QDate d = iso(a.at(0));
        const double x = num(a.at(1));
        if (d.isValid() && std::isfinite(x) && x > 0)
            out.append({d, x});
    }
    std::sort(out.begin(), out.end(), [](const PricePoint& a, const PricePoint& b) { return a.day < b.day; });
    return out;
}

QStringList strings(const QJsonValue& v) {
    QStringList out;
    for (const auto& s : v.toArray())
        if (!s.toString().isEmpty())
            out << s.toString();
    return out;
}

QString cache_path() {
    return AppPaths::cache() + "/pre_ipo/clarity.json";
}

} // namespace

ClarityService& ClarityService::instance() {
    static ClarityService inst;
    return inst;
}

ClarityService::ClarityService(QObject* parent) : QObject(parent) {
    notify_timer_ = new QTimer(this);
    notify_timer_->setSingleShot(true);
    notify_timer_->setInterval(250);
    connect(notify_timer_, &QTimer::timeout, this, &ClarityService::updated);
}

void ClarityService::notify() {
    if (!notify_timer_->isActive())
        notify_timer_->start();
}

// ── Parsing ──────────────────────────────────────────────────────────────────

ClarityMember ClarityService::parse_member(const QJsonObject& m) {
    ClarityMember x;
    x.rank = m.value("rank").toInt();
    x.rank_change = m.value("rank_change").toInt();
    x.name = m.value("name").toString();
    x.slug = m.value("slug").toString();
    x.description = m.value("description").toString();
    x.price = num(m.value("price"));
    x.chg_6m_pct = num(m.value("chg_6m"));
    x.last_round_valuation = num(m.value("last_round_valuation"));
    for (const auto& v : m.value("sparkline").toArray())
        if (v.isDouble())
            x.sparkline.append(v.toDouble());
    return x;
}

ClarityIndex ClarityService::parse_index(const QJsonObject& idx) {
    ClarityIndex x;
    x.level = num(idx.value("level"));
    x.updated = iso(idx.value("updated"));
    x.chg_3m_pct = num(idx.value("chg_3m"));
    x.chg_6m_pct = num(idx.value("chg_6m"));
    x.chg_1y_pct = num(idx.value("chg_1y"));
    x.chg_max_pct = num(idx.value("chg_max"));
    x.points = points(idx.value("points"));
    for (const auto& m : idx.value("members").toArray())
        x.members.append(parse_member(m.toObject()));
    return x;
}

ClarityCompany ClarityService::parse_company(const QJsonObject& c) {
    ClarityCompany x;
    x.slug = c.value("slug").toString();
    x.name = c.value("name").toString();
    x.description = c.value("description").toString();
    x.tags = strings(c.value("tags"));
    x.tradeable = c.value("tradeable").toBool();
    x.ipo_date = iso(c.value("ipo_date"));
    x.public_ticker = c.value("public_ticker").toString();
    x.acquired = !c.value("acquired_at").toString().isEmpty();
    x.ipo_price = num(c.value("ipo_price"));
    x.founded = c.value("founded").toString();
    x.employees = c.value("employees").toString();
    x.hq = c.value("hq").toString();
    x.website = c.value("website").toString();
    x.total_raised = num(c.value("total_raised"));
    x.key_investors = strings(c.value("key_investors"));
    const double at = num(c.value("fetched_at"));
    if (std::isfinite(at))
        x.fetched_at = QDateTime::fromSecsSinceEpoch(qint64(at)); // EVENT-STAMP: when the page was read (an age check)

    MarketQuote& q = x.quote;
    q.source = QStringLiteral("Clarity");
    q.slug = x.slug;
    q.price = num(c.value("price"));
    q.as_of = iso(c.value("price_date"));
    q.chg_3m_pct = num(c.value("chg_3m"));
    q.chg_6m_pct = num(c.value("chg_6m"));
    q.chg_1y_pct = num(c.value("chg_1y"));
    q.chg_max_pct = num(c.value("chg_max"));
    q.points = points(c.value("points"));
    q.c50 = c.value("c50").toBool();
    q.stale = c.value("stale").toBool();
    q.detailed = true;
    // The page's "price date" is the day the page was rendered, not the day
    // the model last moved: date the quote by its newest series point when
    // there is one, so a frozen series does not read as today's price.
    if (!q.points.isEmpty())
        q.as_of = q.points.last().day;

    for (const auto& rv : c.value("rounds").toArray()) {
        const QJsonObject r = rv.toObject();
        FinancingRound f;
        const QString month = r.value("month").toString();
        f.month = QDate::fromString(month + "-01", Qt::ISODate);
        f.series = r.value("series").toString();
        f.tender = r.value("tender").toBool();
        f.raised_usd = num(r.value("raised"));
        f.post_money_usd = num(r.value("post_money"));
        f.pps = num(r.value("pps"));
        f.investors = strings(r.value("investors"));
        x.rounds.append(f);
    }
    // Newest first, stably (Clarity's own order breaks same-month ties).
    std::stable_sort(x.rounds.begin(), x.rounds.end(), [](const FinancingRound& a, const FinancingRound& b) {
        if (!a.month.isValid() || !b.month.isValid())
            return a.month.isValid() && !b.month.isValid();
        return a.month > b.month;
    });
    return x;
}

// ── Cache ────────────────────────────────────────────────────────────────────

void ClarityService::hydrate() {
    if (hydrated_)
        return;
    hydrated_ = true;
    QFile f(cache_path());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value("v").toInt() != kCacheVersion)
        return;
    // The raw answer is kept with it: save() writes raw_index_, and an index
    // hydrated without it was written back empty on the next save.
    const QJsonObject idx = root.value("index").toObject();
    if (idx.value("index").isObject()) {
        apply_index(idx);
        raw_index_ = idx;
    }
    const QJsonObject cos = root.value("companies").toObject();
    for (auto it = cos.begin(); it != cos.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        raw_companies_.insert(it.key(), o);
        companies_.insert(it.key(), parse_company(o));
    }
    LOG_INFO("Clarity", QString("cache: %1 companies, index %2")
                            .arg(companies_.size())
                            .arg(index_.valid() ? "present" : "absent"));
    notify();
}

void ClarityService::save() {
    QJsonObject cos;
    for (auto it = raw_companies_.cbegin(); it != raw_companies_.cend(); ++it)
        cos.insert(it.key(), it.value());
    QJsonObject root{{"v", kCacheVersion}, {"index", raw_index_}, {"companies", cos}};
    QDir().mkpath(QFileInfo(cache_path()).absolutePath());
    QFile f(cache_path());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ── Loading ──────────────────────────────────────────────────────────────────

void ClarityService::load() {
    hydrate();
    if (!python::PythonRunner::instance().is_available())
        return;
    if (!index_.valid() || universe_.isEmpty() || !index_at_.isValid() ||
        index_at_.secsTo(QDateTime::currentDateTime()) > kIndexMaxAgeSecs)
        run_index();
    else
        pump();
}

bool ClarityService::failed_recently(const QString& slug) const {
    const auto it = failed_at_.constFind(slug);
    return it != failed_at_.constEnd() && QDateTime::currentMSecsSinceEpoch() - *it < kFailCoolMs;
}

void ClarityService::refresh() {
    unavailable_.clear();
    failed_at_.clear();
    asked_at_.clear();
    refresh_until_ms_ = QDateTime::currentMSecsSinceEpoch() + 15LL * 60 * 1000;
    run_index(0.25);
}

void ClarityService::run_index(double max_age_h) {
    if (index_inflight_)
        return;
    index_inflight_ = true;
    QPointer<ClarityService> self = this;
    const QString payload =
        QString::fromUtf8(QJsonDocument(QJsonObject{{"max_age_h", max_age_h}}).toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run( // D1-exempt: page read on screen open, cached by the script
        QStringLiteral("clarity_market.py"), {QStringLiteral("index"), payload},
        [self](python::PythonResult r) {
            if (!self)
                return;
            self->index_inflight_ = false;
            const QJsonObject root = QJsonDocument::fromJson(python::extract_json(r.output).toUtf8()).object();
            if (!r.success || root.isEmpty() || root.contains("error") || !root.value("index").isObject()) {
                // Not an answer: the cached index stands.
                const QString why = root.value("error").toString(r.success ? "no index in the answer" : r.error);
                LOG_WARN("Clarity", "index read failed: " + why.left(200));
                self->errors_ = {QStringLiteral("Clarity index unavailable (%1)").arg(why.left(80))};
                self->notify();
                self->pump();
                return;
            }
            self->apply_index(root);
            self->raw_index_ = root;
            self->save();
            self->notify();
            self->pump();
        },
        {}, 90'000);
}

void ClarityService::apply_index(const QJsonObject& root) {
    index_ = parse_index(root.value("index").toObject());
    // Undated = unknown age = stale: an answer without its fetch time must
    // not pass as fresh and suppress the next read.
    const double at = num(root.value("fetched_at"));
    index_at_ = std::isfinite(at) ? QDateTime::fromSecsSinceEpoch(qint64(at)) : QDateTime(); // EVENT-STAMP: fetch time
    errors_.clear();
    for (const auto& e : root.value("errors").toArray())
        errors_ << e.toString();
    universe_total_ = root.value("universe_total").toInt();

    listed_.clear();
    QSet<QString> seen;
    for (const auto& m : std::as_const(index_.members)) {
        listed_.append(m);
        seen.insert(m.slug);
    }
    for (const auto& b : root.value("browse").toArray()) {
        ClarityMember m = parse_member(b.toObject());
        if (!m.slug.isEmpty() && !seen.contains(m.slug)) {
            listed_.append(m);
            seen.insert(m.slug);
        }
    }
    universe_ = seen;
    for (const auto& s : root.value("slugs").toArray())
        universe_.insert(s.toString());
    universe_.remove(QString());
    if (universe_total_ < universe_.size())
        universe_total_ = universe_.size();

    QStringList w;
    for (const auto& m : std::as_const(listed_))
        w << m.slug;
    want(w);
}

void ClarityService::want(const QStringList& slugs) {
    bool added = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& s : slugs) {
        if (s.isEmpty() || queued_.contains(s) || inflight_slugs_.contains(s) || unavailable_.contains(s) ||
            failed_recently(s))
            continue;
        if (const auto it = asked_at_.constFind(s); it != asked_at_.constEnd() && now - *it < kReaskMs)
            continue;
        if (queue_.size() >= kMaxBackground)
            break;
        queue_ << s;
        queued_.insert(s);
        added = true;
    }
    if (added)
        pump();
}

void ClarityService::fetch_now(const QString& slug) {
    if (slug.isEmpty() || inflight_slugs_.contains(slug) || unavailable_.contains(slug))
        return;
    if (const auto it = failed_at_.constFind(slug);
        it != failed_at_.constEnd() && QDateTime::currentMSecsSinceEpoch() - *it < kFailRetryClickMs)
        return;
    const auto it = companies_.constFind(slug);
    if (it != companies_.constEnd() && it->fetched_at.isValid() &&
        it->fetched_at.secsTo(QDateTime::currentDateTime()) < 12 * 3600)
        return;
    run_companies({slug}, /*background*/ false);
}

void ClarityService::pump() {
    // The index read comes first: it decides what the background reads are.
    if (batch_inflight_ || index_inflight_ || queue_.isEmpty())
        return;
    if (!python::PythonRunner::instance().is_available())
        return;
    QStringList batch;
    while (!queue_.isEmpty() && batch.size() < kBatch) {
        const QString s = queue_.takeFirst();
        queued_.remove(s);
        if (inflight_slugs_.contains(s))
            continue;
        batch << s;
    }
    if (!batch.isEmpty())
        run_companies(batch, /*background*/ true);
}

void ClarityService::run_companies(const QStringList& slugs, bool background) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& s : slugs) {
        inflight_slugs_.insert(s);
        asked_at_.insert(s, now);
    }
    if (background)
        batch_inflight_ = true;
    // No notify here: a repaint per request start rebuilt the company list
    // under the reader. The view paints its own "reading…" for a click.
    const int max_age_h = now < refresh_until_ms_ ? 1 : 12;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"slugs", QJsonArray::fromStringList(slugs)}, {"max_age_h", max_age_h}})
            .toJson(QJsonDocument::Compact));
    QPointer<ClarityService> self = this;
    python::PythonRunner::instance().run( // D1-exempt: page reads, cached 12 h by the script
        QStringLiteral("clarity_market.py"), {QStringLiteral("companies"), payload},
        [self, slugs, background](python::PythonResult r) {
            if (!self)
                return;
            for (const auto& s : slugs)
                self->inflight_slugs_.remove(s);
            if (background)
                self->batch_inflight_ = false;
            const QJsonObject root = QJsonDocument::fromJson(python::extract_json(r.output).toUtf8()).object();
            if (!r.success || root.isEmpty() || root.contains("error")) {
                LOG_WARN("Clarity", "company read failed: " + root.value("error").toString(r.error).left(200));
                // Not an answer about these companies: cool off, then ask
                // again (want() honours failed_at_, so the re-join that
                // notify() triggers cannot re-queue them at once).
                const qint64 t = QDateTime::currentMSecsSinceEpoch();
                for (const auto& s : slugs)
                    self->failed_at_.insert(s, t);
                self->notify();
                if (background)
                    QTimer::singleShot(30'000, self.data(), [self]() {
                        if (self)
                            self->pump();
                    });
                return;
            }
            self->apply_companies(root, slugs);
            if (background)
                self->pump();
        },
        {}, background ? kBatchTimeoutMs : kOneTimeoutMs);
}

void ClarityService::apply_companies(const QJsonObject& root, const QStringList& asked) {
    const QJsonObject cos = root.value("companies").toObject();
    for (auto it = cos.begin(); it != cos.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        raw_companies_.insert(it.key(), o);
        companies_.insert(it.key(), parse_company(o));
    }
    const QJsonObject errs = root.value("errors").toObject();
    const qint64 t = QDateTime::currentMSecsSinceEpoch();
    for (const auto& s : asked) {
        if (cos.contains(s)) {
            failed_at_.remove(s);
            continue;
        }
        // A 404 is an answer (no such page); anything else is not, and is
        // asked again after a cool-off.
        if (errs.value(s).toString() == QLatin1String("not_found")) {
            if (!companies_.contains(s))
                unavailable_.insert(s);
        } else {
            failed_at_.insert(s, t);
        }
    }
    LOG_INFO("Clarity", QString("%1 company pages read (%2 not available)").arg(cos.size()).arg(errs.size()));
    save();
    notify();
}

} // namespace finterm::services

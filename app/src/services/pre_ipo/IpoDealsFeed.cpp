// src/services/pre_ipo/IpoDealsFeed.cpp
#include "services/pre_ipo/IpoDealsFeed.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "storage/cache/CacheManager.h"
#include "screens/pre_ipo/IpoFormat.h"
#include "screens/pre_ipo/PrivateMarketFormat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

namespace finterm::services {

namespace pm = finterm::pre_ipo::pm;

using pre_ipo::pmfmt::name_key;
using pre_ipo::pmfmt::strip_digits_cik;

namespace {
// The last answer, so a restart paints at once (before Python is up).
constexpr auto kSnapshotKey = "ipo_feed:latest:v1";
constexpr int kSnapshotSecs = 30 * 60;
constexpr qint64 kFreshSecs = 30 * 60;
} // namespace

IpoDealsFeed& IpoDealsFeed::instance() {
    static IpoDealsFeed inst;
    return inst;
}

IpoDealsFeed::IpoDealsFeed(QObject* parent) : QObject(parent) {
    if (const auto hit = CacheManager::instance().try_get(QString::fromLatin1(kSnapshotKey))) {
        const QJsonObject root = QJsonDocument::fromJson(hit->toUtf8()).object();
        if (!root.isEmpty()) {
            take(root, /*persist=*/false);
            answered_at_ = QDateTime(); // a snapshot is shown, not trusted as fresh
        }
    }
}

IpoDeal IpoDealsFeed::parse_deal(const QJsonObject& o) {
    auto d = [&](const char* k) { return QDate::fromString(o.value(k).toString(), Qt::ISODate); };
    IpoDeal x;
    x.deal_id = o.value("deal_id").toString();
    x.company = o.value("company").toString();
    x.ticker = o.value("ticker").toString();
    x.exchange = o.value("exchange").toString();
    x.cik = o.value("cik").toString();
    x.status = o.value("status").toString();
    x.spac = o.value("spac").toBool();
    x.price_range = o.value("price_range").toString();
    x.filed = d("filed_date");
    x.priced = d("priced_date");
    const double shares = o.value("shares").isDouble() ? o.value("shares").toDouble() : 0;
    if (x.status == "priced") {
        const double fp = o.value("final_price").isDouble() ? o.value("final_price").toDouble() : 0;
        if (fp > 0)
            x.price_range = QString::number(fp, 'f', 2);
        const double offer = o.value("offer_amount").isDouble() ? o.value("offer_amount").toDouble() : 0;
        x.deal_size_usd = (fp > 0 && shares > 0) ? fp * shares : offer;
    } else if (x.pending()) {
        // Both ends of the range must parse (see IpoFormat: a one-sided parse
        // understated deals); the fee-table maximum is never read.
        bool ok = false;
        const double mid = pre_ipo::fmt::price_mid(x.price_range, &ok);
        x.deal_size_usd = (ok && shares > 0) ? mid * shares : 0;
    }
    const QJsonObject p = o.value("projection").toObject();
    x.kind = p.value("kind").toString();
    x.basis = p.value("basis").toString();
    if (x.pending()) {
        x.when = QDate::fromString(p.value("date").toString(), Qt::ISODate);
        if (!x.when.isValid())
            x.when = QDate::fromString(p.value("lo").toString(), Qt::ISODate);
    } else if (x.status == "priced") {
        x.when = x.priced;
    } else if (x.status == "withdrawn") {
        x.when = d("withdrawn_date");
    }
    return x;
}

void IpoDealIndex::build(const QVector<IpoDeal>& deals) {
    deals_ = deals;
    by_key_.clear();
    // Best deal per key: pending > priced > withdrawn, newer first (a
    // re-filing after an RW beats the dead registration).
    auto rank = [](const IpoDeal& e) {
        const int s = e.pending() ? 3 : e.status == "priced" ? 2 : 1;
        const QDate d = e.when.isValid() ? e.when : e.filed;
        return qint64(s) * 10'000'000 + (d.isValid() ? d.toJulianDay() % 10'000'000 : 0);
    };
    auto put = [&](const QString& key, int idx) {
        const auto it = by_key_.constFind(key);
        if (it == by_key_.constEnd() || rank(deals_.at(idx)) > rank(deals_.at(*it)))
            by_key_.insert(key, idx);
    };
    for (int i = 0; i < deals_.size(); ++i) {
        const IpoDeal& e = deals_.at(i);
        if (e.spac)
            continue;
        if (const QString c = strip_digits_cik(e.cik); !c.isEmpty())
            put(QStringLiteral("cik:") + c, i);
        if (const QString n = name_key(e.company); !n.isEmpty())
            put(QStringLiteral("name:") + n, i);
    }
}

void IpoDealsFeed::take(const QJsonObject& root, bool persist) {
    const bool enriched = root.value("enriched").toBool();
    latest_ = root;
    answered_at_ = QDateTime::currentDateTime();
    if (persist)
        CacheManager::instance().put(QString::fromLatin1(kSnapshotKey),
                                 QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)),
                                 kSnapshotSecs, QStringLiteral("ipo"));
    // A plain fast pass carries no EDGAR stages: taking it as root() over an
    // enriched answer would wipe every stage-based date until the enrich
    // lands. It still becomes latest_root() for the plain row lists.
    if (enriched || !this->enriched()) {
        root_ = root;
        deals_.clear();
        const QJsonArray arr = root.value("deals").toArray();
        deals_.reserve(arr.size());
        for (const auto& v : arr)
            deals_.append(parse_deal(v.toObject()));
        index_.build(deals_);
        if (enriched) {
            // The EDGAR picture's own read time — a fast pass served from the
            // script's cache may hand back one that is days old.
            const QDateTime at = QDateTime::fromString(root.value("enriched_at").toString(), Qt::ISODate);
            enriched_at_ = at.isValid() ? at : QDateTime::currentDateTime();
        }
    }
    emit updated();
}

void IpoDealsFeed::load(bool force) {
    const QDateTime now = QDateTime::currentDateTime();
    if (!force) {
        const bool rows_fresh = answered_at_.isValid() && answered_at_.secsTo(now) < kFreshSecs;
        const bool edgar_fresh = (enriched_at_.isValid() && enriched_at_.secsTo(now) < kFreshSecs) || enrich_inflight_;
        if (rows_fresh && edgar_fresh)
            return;
        if (failed_at_.isValid() && failed_at_.secsTo(now) < 5 * 60 && answered_at_.isValid())
            return;
    }
    request(/*enrich=*/false);
}

void IpoDealsFeed::request(bool enrich, bool chain_enrich) {
    bool& inflight = enrich ? enrich_inflight_ : fast_inflight_;
    if (inflight)
        return;
    inflight = true;
    // The fast pass takes the interactive lane: when the script's cache holds
    // the last enrich's answer it returns it in well under a second. The
    // enrich (minutes cold, SEC at 2.5 req/s) runs in the ordinary queue.
    // PythonRunner queues a call made before Python is located and answers a
    // failure when there is none, so every request ends in `answered`.
    QPointer<IpoDealsFeed> self = this;
    auto on_done = [self, enrich, chain_enrich](python::PythonResult r) {
        if (!self)
            return;
        (enrich ? self->enrich_inflight_ : self->fast_inflight_) = false;
        const QJsonObject root = QJsonDocument::fromJson(python::extract_json(r.output).toUtf8()).object();
        const bool ok = r.success && !root.isEmpty() && !root.contains("error");
        const QString why = ok ? QString()
                               : root.value("error").toString(r.success ? QStringLiteral("unreadable output")
                                                                        : r.error.isEmpty()
                                                                              ? QStringLiteral("script failed")
                                                                              : r.error.left(200));
        if (ok) {
            self->failed_at_ = QDateTime();
            self->take(root);
        } else {
            LOG_WARN("IpoDealsFeed", QString("ipo_calendar %1 failed: %2")
                                         .arg(enrich ? "enrich" : "calendar", why.left(200)));
            self->failed_at_ = QDateTime::currentDateTime();
        }
        // Start the enrich BEFORE telling anyone, so a listener reading
        // inflight(true) for its status line sees it running.
        if (!enrich) {
            const QDateTime at = self->enriched_at_;
            if (chain_enrich || !at.isValid() || at.secsTo(QDateTime::currentDateTime()) >= kFreshSecs)
                self->request(/*enrich=*/true);
        }
        emit self->answered(enrich, ok, root, why);
    };
    if (enrich)
        python::PythonRunner::instance().run( // D1-exempt: the one runner of the calendar (this feed)
            QStringLiteral("ipo_calendar.py"), {QStringLiteral("enrich")}, std::move(on_done), {}, 600'000);
    else
        python::PythonRunner::instance().run_interactive( // D1-exempt: the one runner of the calendar
            QStringLiteral("ipo_calendar.py"), {QStringLiteral("calendar")}, std::move(on_done), 90'000);
}

const IpoDeal* IpoDealIndex::deal_for(const pre_ipo::PrivateCompany& c, bool* strong) const {
    if (strong)
        *strong = false;
    auto find = [&](const QString& key) -> const IpoDeal* {
        const auto it = by_key_.constFind(key);
        return it == by_key_.constEnd() ? nullptr : &deals_.at(*it);
    };
    if (const QString cik = strip_digits_cik(c.cik); !cik.isEmpty())
        if (const IpoDeal* e = find(QStringLiteral("cik:") + cik)) {
            if (strong)
                *strong = true;
            return e;
        }
    QStringList names{c.name};
    names += c.aliases;
    for (const auto& n : names) {
        const QString nk = name_key(n);
        if (nk.isEmpty())
            continue;
        const IpoDeal* e = find(QStringLiteral("name:") + nk);
        if (!e)
            continue;
        if (nk.contains('-')) {
            if (strong)
                *strong = true;
            return e;
        }
        if (e->pending() && e->deal_size_usd >= 100e6)
            return e;
    }
    return nullptr;
}

} // namespace finterm::services

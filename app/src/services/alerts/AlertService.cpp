#include "services/alerts/AlertService.h"

#include "auth/AuthManager.h"
#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "python/PythonRunner.h"
#include "services/markets/MarketDataService.h"
#include "services/ownership/OwnershipService.h"
#include "storage/repositories/SettingsRepository.h"
#include "storage/sqlite/Database.h"

#include <QJsonDocument>
#include <QPointer>
#include <QUuid>

#include <cmath>

namespace finterm::alerts {

namespace {

const QString kRulesKey = QStringLiteral("alerts.rules");
const QString kStateKey = QStringLiteral("alerts.state");
const QString kCategory = QStringLiteral("alerts");
constexpr int kInsiderFirstCheckMs = 20'000;
constexpr int kInsiderRecheckMs = 30 * 60'000; // a local sqlite read; the store itself moves every 6 h
constexpr int kSwitchesRefreshMs = 60'000;
constexpr int kNudgeMaxStaleDays = 7; // a warm store: catching up is a day or two of reads

QString quote_topic(const QString& sym) {
    return QStringLiteral("market:quote:") + sym;
}

QString session_name(int market_hours) {
    switch (market_hours) {
        case 0:
            return QStringLiteral("pre-market print");
        case 2:
            return QStringLiteral("post-market print");
        case 4:
            return QStringLiteral("overnight print");
        default:
            return QStringLiteral("regular session");
    }
}

QString money(double v) {
    if (!std::isfinite(v))
        return QStringLiteral("value not reported");
    if (std::abs(v) >= 1e6)
        return QStringLiteral("$%1M").arg(v / 1e6, 0, 'f', 1);
    if (std::abs(v) >= 1e3)
        return QStringLiteral("$%1K").arg(v / 1e3, 0, 'f', 0);
    return QStringLiteral("$%1").arg(v, 0, 'f', 0);
}

QString short_date(const QDate& d) {
    return d.isValid() ? d.toString(QStringLiteral("d MMM")) : QStringLiteral("—");
}

} // namespace

AlertService& AlertService::instance() {
    static AlertService s;
    return s;
}

AlertService::AlertService() {
    insider_timer_ = new QTimer(this);
    insider_timer_->setInterval(kInsiderRecheckMs);
    connect(insider_timer_, &QTimer::timeout, this, &AlertService::check_insiders);
    switches_timer_ = new QTimer(this);
    switches_timer_->setInterval(kSwitchesRefreshMs);
    connect(switches_timer_, &QTimer::timeout, this, [this]() {
        refresh_delivery_switches();
        sweep_cached();
    });

    connect(&services::MarketDataService::instance(), &services::MarketDataService::stream_ticks, this,
            &AlertService::on_ticks);
    // A Form 4 scan cycle that read new filings is the moment a cluster can
    // appear; the 30-minute timer only covers a scan run elsewhere.
    connect(&services::OwnershipService::instance(), &services::OwnershipService::form4_store_updated, this,
            &AlertService::check_insiders);

    auto& auth = auth::AuthManager::instance();
    connect(&auth, &auth::AuthManager::logged_out, this, &AlertService::stop);
    // A different user unlocking re-reads that user's rules (start() is a
    // no-op when the same user's store is already loaded).
    connect(&auth, &auth::AuthManager::terminal_unlocked, this, &AlertService::start);
    connect(&auth, &auth::AuthManager::auth_state_changed, this, [this]() {
        if (auth::AuthManager::instance().is_authenticated())
            start();
    });
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void AlertService::start() {
    auto& db = Database::instance();
    if (!db.is_open())
        return;
    if (running_ && db.path() == db_path_)
        return;
    if (running_)
        stop();
    if (!load()) {
        // Not "no rules": an unreadable store. Watch nothing and write nothing
        // until it reads; try again shortly.
        LOG_WARN("Alerts", "could not read the alert rules — retrying in 10 s");
        QTimer::singleShot(10'000, this, &AlertService::start);
        return;
    }
    running_ = true;
    refresh_delivery_switches();
    resubscribe();
    switches_timer_->start();
    insider_timer_->start();
    QTimer::singleShot(kInsiderFirstCheckMs, this, &AlertService::check_insiders);
    LOG_INFO("Alerts", QString("watching %1 alert rule(s) while the app runs").arg(set_.rules.size()));
    emit rules_changed();
}

void AlertService::stop() {
    datahub::DataHub::instance().unsubscribe(this);
    quote_syms_.clear();
    first_seen_.clear();
    in_extended_.clear();
    set_ = RuleSet{};
    db_path_.clear();
    form4_scanned_to_.clear();
    nudged_scan_ = false;
    insider_recheck_ = false;
    running_ = false;
    insider_timer_->stop();
    switches_timer_->stop();
    emit rules_changed();
}

namespace {
/// nullopt when the store could not be read (SettingsRepository::get folds a
/// failed query into "" — indistinguishable from "no rules yet", and saving
/// over that would erase the user's rules and announced filings).
std::optional<QString> read_setting(const QString& key) {
    auto r = Database::instance().execute(QStringLiteral("SELECT value FROM settings WHERE key = ?"), {key});
    if (r.is_err())
        return std::nullopt;
    auto& q = r.value();
    return q.next() ? q.value(0).toString() : QString();
}
} // namespace

bool AlertService::load() {
    const auto rules = read_setting(kRulesKey);
    const auto states = read_setting(kStateKey);
    if (!rules || !states)
        return false;
    set_ = ruleset_from_json(QJsonDocument::fromJson(rules->toUtf8()).array(),
                             QJsonDocument::fromJson(states->toUtf8()).object());
    db_path_ = Database::instance().path();
    return true;
}

bool AlertService::store_is_ours() const {
    // Sign-out closes the user's database before announcing it, and another
    // user's may be open by the time a deferred write runs: write only into
    // the store the rules came from.
    auto& db = Database::instance();
    return running_ && db.is_open() && db.path() == db_path_;
}

void AlertService::save_rules() {
    if (!store_is_ours())
        return;
    SettingsRepository::instance().set(
        kRulesKey, QString::fromUtf8(QJsonDocument(rules_to_json(set_)).toJson(QJsonDocument::Compact)), kCategory);
}

void AlertService::save_states() {
    if (!store_is_ours())
        return;
    SettingsRepository::instance().set(
        kStateKey, QString::fromUtf8(QJsonDocument(states_to_json(set_)).toJson(QJsonDocument::Compact)), kCategory);
}

void AlertService::refresh_delivery_switches() {
    price_on_ = notifications::NotificationService::trigger_enabled(notifications::NotifTrigger::PriceAlert);
    insider_on_ = notifications::NotificationService::trigger_enabled(notifications::NotifTrigger::OwnershipAlert);
}

// ── Rules ────────────────────────────────────────────────────────────────────

std::optional<Rule> AlertService::rule(const QString& id) const {
    for (const auto& r : set_.rules)
        if (r.id == id)
            return r;
    return std::nullopt;
}

QString AlertService::add_rule(Rule r) {
    r.symbol = r.symbol.trimmed().toUpper();
    if (!running_ || !r.valid())
        return {};
    r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.created = QDateTime::currentDateTimeUtc();
    set_.rules.push_back(r);
    set_.states.insert(r.id, RuleState{});
    save_rules();
    save_states();
    resubscribe();
    sweep_cached();
    emit rules_changed();
    if (r.kind == Kind::InsiderCluster)
        check_insiders();
    return r.id;
}

bool AlertService::update_rule(const Rule& in) {
    Rule r = in;
    r.symbol = r.symbol.trimmed().toUpper();
    if (!running_ || !r.valid())
        return false;
    for (auto& cur : set_.rules) {
        if (cur.id != r.id)
            continue;
        r.created = cur.created;
        if (edit_restarts(cur, r)) {
            // A new level (or kind, or prints) makes the old armed side
            // meaningless. A day-move rule keeps its session key so an edit
            // cannot fire it twice in one session; nor does an unchanged save.
            set_.states.insert(r.id, RuleState{});
            first_seen_.remove(r.id);
        }
        cur = r;
        save_rules();
        save_states();
        resubscribe();
        sweep_cached();
        emit rules_changed();
        if (r.kind == Kind::InsiderCluster)
            check_insiders();
        return true;
    }
    return false;
}

void AlertService::set_paused(const QString& id, bool paused) {
    for (auto& r : set_.rules) {
        if (r.id != id || r.paused == paused)
            continue;
        r.paused = paused;
        if (!paused) {
            // Resuming starts over from the next price: a crossing that happened
            // while paused was not watched, so it is not announced late.
            RuleState& s = state_ref(id);
            s.arm = Arm::Pending;
            first_seen_.remove(id);
        }
        save_rules();
        save_states();
        resubscribe();
        sweep_cached();
        emit rules_changed();
        return;
    }
}

void AlertService::remove_rule(const QString& id) {
    for (int i = 0; i < set_.rules.size(); ++i) {
        if (set_.rules[i].id != id)
            continue;
        set_.rules.removeAt(i);
        set_.states.remove(id);
        first_seen_.remove(id);
        save_rules();
        save_states();
        resubscribe();
        emit rules_changed();
        return;
    }
}

bool AlertService::own_names_insider_on() const {
    for (const auto& r : set_.rules)
        if (r.kind == Kind::InsiderCluster && r.scope == Scope::OwnNames && !r.paused)
            return true;
    return false;
}

void AlertService::set_own_names_insider(bool on) {
    if (on == own_names_insider_on())
        return;
    QString existing;
    for (const auto& r : set_.rules)
        if (r.kind == Kind::InsiderCluster && r.scope == Scope::OwnNames)
            existing = r.id;
    if (!existing.isEmpty()) {
        set_paused(existing, !on);
        if (on)
            check_insiders();
        return;
    }
    if (on) {
        Rule r;
        r.kind = Kind::InsiderCluster;
        r.scope = Scope::OwnNames;
        add_rule(r);
    }
}

bool AlertService::has_active_insider_rule() const {
    for (const auto& r : set_.rules)
        if (r.kind == Kind::InsiderCluster && !r.paused)
            return true;
    return false;
}

bool AlertService::covers_insider_clusters(const QString& symbol) const {
    const QString sym = symbol.trimmed().toUpper();
    for (const auto& r : set_.rules) {
        if (r.kind != Kind::InsiderCluster || r.paused)
            continue;
        // The legacy alert only ever looks at the user's own names, which is
        // exactly what an OwnNames rule covers.
        if (r.scope == Scope::OwnNames || r.symbol == sym)
            return true;
    }
    return false;
}

QSet<QString> AlertService::insider_symbols() const {
    QSet<QString> out;
    bool own = false;
    for (const auto& r : set_.rules) {
        if (r.kind != Kind::InsiderCluster || r.paused)
            continue;
        if (r.scope == Scope::OwnNames)
            own = true;
        else
            out.insert(r.symbol);
    }
    if (own)
        for (const auto& s : services::OwnershipService::instance().own_symbols())
            out.insert(s);
    return out;
}

std::optional<double> AlertService::current_price(const QString& symbol) const {
    const QVariant v = datahub::DataHub::instance().peek_raw(quote_topic(symbol.trimmed().toUpper()));
    if (!v.canConvert<services::QuoteData>())
        return std::nullopt;
    const double p = v.value<services::QuoteData>().price;
    if (!std::isfinite(p) || p <= 0)
        return std::nullopt;
    return p;
}

// ── Price inputs ─────────────────────────────────────────────────────────────

void AlertService::resubscribe() {
    QSet<QString> want;
    if (running_)
        for (const auto& r : set_.rules)
            if (!r.paused && (r.is_price() || r.kind == Kind::DayMove))
                want.insert(r.symbol);
    auto& hub = datahub::DataHub::instance();
    QSet<QString> gone = quote_syms_;
    gone.subtract(want);
    QSet<QString> added = want;
    added.subtract(quote_syms_);
    for (const auto& sym : std::as_const(gone)) {
        hub.unsubscribe(this, quote_topic(sym));
        quote_syms_.remove(sym);
        in_extended_.remove(sym);
    }
    for (const auto& sym : std::as_const(added)) {
        quote_syms_.insert(sym);
        // Subscribing is also what keeps the quote polled and on the live
        // stream (MarketDataService streams every subscribed quote topic).
        hub.subscribe(this, quote_topic(sym), [this, sym](const QVariant& v) {
            if (v.canConvert<services::QuoteData>())
                on_quote(sym, v.value<services::QuoteData>());
        });
    }
}

void AlertService::sweep_cached() {
    if (!running_)
        return;
    auto& hub = datahub::DataHub::instance();
    for (const auto& sym : QStringList(quote_syms_.begin(), quote_syms_.end())) {
        const QVariant v = hub.peek_raw(quote_topic(sym));
        if (v.canConvert<services::QuoteData>())
            on_quote(sym, v.value<services::QuoteData>()); // gates on has_live_quote itself
    }
}

void AlertService::on_quote(const QString& sym, const services::QuoteData& q) {
    if (!running_ || !price_on_)
        return; // muted in Settings: nothing is consumed, so nothing is lost
    // A quote replayed from disk at start-up may be a previous session's;
    // only one polled (or ticked) in this process is evidence.
    if (!services::MarketDataService::instance().has_live_quote(sym))
        return;
    if (!std::isfinite(q.price) || q.price <= 0)
        return;
    const QVector<Rule> rules = set_.rules; // delivery may re-enter (UI refresh)
    QStringList changed;
    for (const auto& r : rules) {
        if (r.paused || r.symbol != sym)
            continue;
        RuleState& s = state_ref(r.id);
        if (r.is_price()) {
            // After the bell the polled price is the close while the tape
            // trades on: an extended-hours rule follows the prints instead.
            if (r.session == Session::Extended && !us_regular_session(QDateTime::currentDateTimeUtc()) &&
                in_extended_.value(sym, false))
                continue;
            const bool first = !first_seen_.contains(r.id);
            first_seen_.insert(r.id);
            const Step st = step_cross(r, s, q.price);
            if (st.fired) {
                s.last_fired = QDateTime::currentDateTimeUtc();
                s.last_value = q.price;
                deliver_cross(r, q.price,
                              first ? QStringLiteral("first price seen since finterm started")
                                    : QStringLiteral("regular session"));
            }
            if (st.changed)
                changed << r.id;
        } else if (r.kind == Kind::DayMove) {
            const double baseline = q.price - q.change; // NaN change → NaN baseline → no information
            const Step st = step_day_move(r, s, q.change_pct, baseline);
            if (st.fired) {
                s.last_fired = QDateTime::currentDateTimeUtc();
                s.last_value = q.change_pct;
                deliver_move(r, q.change_pct, baseline, q.price);
            }
            if (st.changed)
                changed << r.id;
        }
    }
    if (!changed.isEmpty()) {
        save_states(); // only on a transition — never per tick
        for (const auto& id : changed)
            emit state_changed(id);
    }
}

void AlertService::on_ticks(const QVector<services::StreamTick>& ticks) {
    if (!running_ || quote_syms_.isEmpty())
        return;
    auto& hub = datahub::DataHub::instance();
    auto& mds = services::MarketDataService::instance();
    QStringList changed;
    for (const auto& t : ticks) {
        if (!quote_syms_.contains(t.symbol))
            continue;
        if (t.market_hours == 1) {
            // Regular prints reach the rules through the quote they update.
            in_extended_.insert(t.symbol, false);
            continue;
        }
        in_extended_.insert(t.symbol, true);
        if (!price_on_ || !mds.has_live_quote(t.symbol))
            continue;
        const QVariant cur = hub.peek_raw(quote_topic(t.symbol));
        if (!cur.canConvert<services::QuoteData>())
            continue;
        const services::QuoteData q = cur.value<services::QuoteData>();
        if (!extended_print_ok(q.price, t.price, t.change))
            continue; // paired with a close the quote has not reached — wait for the poll
        const QVector<Rule> rules = set_.rules;
        for (const auto& r : rules) {
            if (r.paused || !r.is_price() || r.session != Session::Extended || r.symbol != t.symbol)
                continue;
            RuleState& s = state_ref(r.id);
            first_seen_.insert(r.id);
            const Step st = step_cross(r, s, t.price);
            if (st.fired) {
                s.last_fired = QDateTime::currentDateTimeUtc();
                s.last_value = t.price;
                deliver_cross(r, t.price, session_name(t.market_hours));
            }
            if (st.changed)
                changed << r.id;
        }
    }
    if (!changed.isEmpty()) {
        save_states();
        changed.removeDuplicates();
        for (const auto& id : changed)
            emit state_changed(id);
    }
}

// ── Insider clusters ─────────────────────────────────────────────────────────

void AlertService::check_insiders() {
    if (!running_ || !has_active_insider_rule())
        return;
    if (insider_checking_) {
        insider_recheck_ = true; // a scan landed mid-check: look again after
        return;
    }
    refresh_delivery_switches();
    if (!insider_on_)
        return; // muted: the filings stay unannounced until the switch is on
    const QSet<QString> covered = insider_symbols();
    if (covered.isEmpty())
        return;
    insider_checking_ = true;
    QStringList syms(covered.begin(), covered.end());
    syms.sort();
    const QJsonObject payload{{QStringLiteral("symbols"), QJsonArray::fromStringList(syms)},
                              {QStringLiteral("days"), kInsiderQueryDays}};
    const QString path = db_path_;
    QPointer<AlertService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("ownership_watch.py"),
        {QStringLiteral("cluster_rows"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))},
        [self, covered, path](python::PythonResult result) {
            if (!self)
                return;
            self->insider_checking_ = false;
            const bool again = self->insider_recheck_;
            self->insider_recheck_ = false;
            if (!self->running_ || self->db_path_ != path)
                return; // signed out, or another user's rules now
            // A failed read is not "no cluster": nothing is concluded from it.
            if (!result.success) {
                LOG_WARN("Alerts", "insider check failed: " + result.error.trimmed().right(160));
                return;
            }
            const QJsonObject o =
                QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
            if (o.isEmpty() || o.contains(QStringLiteral("error"))) {
                LOG_WARN("Alerts", "insider check refused: " + o.value(QStringLiteral("error")).toString());
                return;
            }
            self->form4_scanned_to_ = o.value(QStringLiteral("scanned_to")).toString();
            QVector<InsiderBuy> rows;
            for (const auto& v : o.value(QStringLiteral("rows")).toArray()) {
                const QJsonObject r = v.toObject();
                InsiderBuy b;
                b.symbol = r.value(QStringLiteral("symbol")).toString().toUpper();
                b.accession = r.value(QStringLiteral("accession")).toString();
                b.insider = r.value(QStringLiteral("insider")).toString();
                b.filed = QDate::fromString(r.value(QStringLiteral("filed")).toString(), Qt::ISODate);
                b.traded = QDate::fromString(r.value(QStringLiteral("traded")).toString(), Qt::ISODate);
                b.value = r.value(QStringLiteral("value")).toDouble(kNaN); // null stays unknown
                rows.push_back(b);
            }
            const QDate today = QDate::currentDate();
            bool dirty = false;
            const auto events = step_insider(rows, covered, self->set_.insider_seen, today, &dirty);
            dirty |= prune_seen(self->set_.insider_seen, today);
            QStringList fired_rules;
            for (const auto& ev : events) {
                const QString who = QStringLiteral("%1 insiders").arg(ev.insiders);
                const QString span = ev.first_trade == ev.last_trade
                                         ? QStringLiteral("on %1").arg(short_date(ev.first_trade))
                                         : QStringLiteral("between %1 and %2")
                                               .arg(short_date(ev.first_trade), short_date(ev.last_trade));
                notifications::NotificationRequest req;
                req.title = QStringLiteral("Insider cluster buy — %1").arg(ev.symbol);
                req.message =
                    QStringLiteral("%1: %2 bought on the open market %3 (%4); newest Form 4 filed %5. "
                                   "Cluster = %6+ insiders within %7 days; 10% owners and 10b5-1 plan "
                                   "trades excluded. A filing, not a call.")
                        .arg(ev.symbol, who, span, money(ev.value), short_date(ev.newest_filed))
                        .arg(ownership::kClusterMinInsiders)
                        .arg(ownership::kClusterWindowDays);
                req.level = notifications::NotifLevel::Alert;
                req.trigger = notifications::NotifTrigger::OwnershipAlert;
                req.symbol = ev.symbol;
                notifications::NotificationService::instance().send(req);
                for (const auto& r : self->set_.rules) {
                    if (r.kind != Kind::InsiderCluster || r.paused)
                        continue;
                    if (r.scope == Scope::OwnNames || r.symbol == ev.symbol) {
                        RuleState& s = self->state_ref(r.id);
                        s.last_fired = QDateTime::currentDateTimeUtc();
                        s.last_value = ev.insiders;
                        fired_rules << r.id;
                    }
                }
            }
            if (dirty || !fired_rules.isEmpty())
                self->save_states();
            fired_rules.removeDuplicates();
            for (const auto& id : fired_rules)
                emit self->state_changed(id);

            // Keep the store current with the Ownership feature's own daily
            // read — but only when it is already warm. A cold store is a month
            // of EDGAR reads, which is the OWNERSHIP screen's to start, not a
            // background alert's.
            const QDate scanned = QDate::fromString(self->form4_scanned_to_, Qt::ISODate);
            if (!self->nudged_scan_ && scanned.isValid() && scanned.daysTo(today) <= kNudgeMaxStaleDays) {
                self->nudged_scan_ = true;
                services::OwnershipService::instance().ensure_form4_current();
            }
            emit self->rules_changed(); // the manager's Form 4 note
            if (again)
                self->check_insiders();
        },
        /*on_line=*/{}, 60'000);
}

// ── Delivery ─────────────────────────────────────────────────────────────────

void AlertService::deliver_cross(const Rule& r, double price, const QString& source) {
    const bool above = r.kind == Kind::PriceAbove;
    notifications::NotificationRequest req;
    req.title = QStringLiteral("%1 %2 %3").arg(r.symbol, above ? QStringLiteral("above") : QStringLiteral("below"),
                                               fmt_price(r.level));
    req.message = QStringLiteral("%1 at %2 crossed %3 your %4 level (%5). Fires once; re-arms after it is back %6 %7.")
                      .arg(r.symbol, fmt_price(price), above ? QStringLiteral("above") : QStringLiteral("below"),
                           fmt_price(r.level), source, above ? QStringLiteral("below") : QStringLiteral("above"),
                           fmt_price(rearm_price(r)));
    req.level = notifications::NotifLevel::Alert;
    req.trigger = notifications::NotifTrigger::PriceAlert;
    req.symbol = r.symbol;
    notifications::NotificationService::instance().send(req);
}

void AlertService::deliver_move(const Rule& r, double change_pct, double baseline, double price) {
    const QString signed_pct = (change_pct >= 0 ? QStringLiteral("+") : QString()) +
                               QString::number(change_pct, 'f', 2) + QLatin1Char('%');
    notifications::NotificationRequest req;
    // "on the day", not "today": a quote polled before the open still
    // describes the session that closed last.
    req.title = QStringLiteral("%1 %2 on the day").arg(r.symbol, signed_pct);
    req.message = QStringLiteral("%1 is %2 %3 vs the previous close of %4 (last %5) — beyond your ±%6% alert. "
                                 "Once per session.")
                      .arg(r.symbol, change_pct >= 0 ? QStringLiteral("up") : QStringLiteral("down"),
                           QStringLiteral("%1%").arg(std::abs(change_pct), 0, 'f', 2), fmt_price(baseline),
                           fmt_price(price), QString::number(r.pct, 'g', 4));
    req.level = notifications::NotifLevel::Alert;
    req.trigger = notifications::NotifTrigger::PriceAlert;
    req.symbol = r.symbol;
    notifications::NotificationService::instance().send(req);
}

void AlertService::fire(const QString& rule_id, const QString& title, const QString& message, const QString& symbol,
                        notifications::NotifTrigger trigger, double value) {
    if (!running_ || !rule(rule_id))
        return;
    RuleState& s = state_ref(rule_id);
    s.last_fired = QDateTime::currentDateTimeUtc();
    s.last_value = value;
    save_states();
    notifications::NotificationRequest req;
    req.title = title;
    req.message = message;
    req.level = notifications::NotifLevel::Alert;
    req.trigger = trigger;
    req.symbol = symbol;
    notifications::NotificationService::instance().send(req);
    emit state_changed(rule_id);
}

} // namespace finterm::alerts

#include "services/alerts/AlertService.h"

#include "auth/AuthManager.h"
#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
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
    if (std::abs(v) >= 1e9) // 13F moves run to billions: "$2939.3M" read as a typo
        return QStringLiteral("$%1B").arg(v / 1e9, 0, 'f', 1);
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
    connect(insider_timer_, &QTimer::timeout, this, &AlertService::check_filings);
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
    QTimer::singleShot(kInsiderFirstCheckMs + 5'000, this, &AlertService::check_filings);
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

bool AlertService::own_names_on(Kind k) const {
    for (const auto& r : set_.rules)
        if (r.kind == k && r.scope == Scope::OwnNames && !r.paused)
            return true;
    return false;
}

void AlertService::set_own_names(Kind k, bool on) {
    if (k == Kind::InsiderCluster) {
        set_own_names_insider(on);
        return;
    }
    if (on == own_names_on(k))
        return;
    for (const auto& r : set_.rules)
        if (r.kind == k && r.scope == Scope::OwnNames) {
            set_paused(r.id, !on);
            if (on)
                check_filings();
            return;
        }
    if (on) {
        Rule r;
        r.kind = k;
        r.scope = Scope::OwnNames;
        add_rule(r);
        check_filings();
    }
}

bool AlertService::fund_followed(const QString& cik) const {
    for (const auto& r : set_.rules)
        if (r.kind == Kind::FundFiled && r.symbol == cik.trimmed().toUpper() && !r.paused)
            return true;
    return false;
}

void AlertService::set_fund_followed(const QString& cik, const QString& name, bool on) {
    const QString c = cik.trimmed().toUpper();
    for (const auto& r : set_.rules)
        if (r.kind == Kind::FundFiled && r.symbol == c) {
            if (on)
                set_paused(r.id, false);
            else
                remove_rule(r.id);
            return;
        }
    if (on) {
        Rule r;
        r.kind = Kind::FundFiled;
        r.symbol = c;
        r.label = name;
        add_rule(r);
        check_filings(); // a filing from the last week alerts at once; older ones are history
    }
}

QSet<QString> AlertService::covered(Kind k) const {
    QSet<QString> out;
    bool own = false;
    for (const auto& r : set_.rules) {
        if (r.kind != k || r.paused)
            continue;
        if (r.scope == Scope::OwnNames && k != Kind::FundFiled)
            own = true;
        else
            out.insert(r.symbol);
    }
    if (own)
        for (const auto& s : services::OwnershipService::instance().own_symbols())
            out.insert(s.toUpper());
    return out;
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
    const QString path = db_path_;
    QPointer<AlertService> self = this;
    services::OwnershipService::instance().query_cluster_buys(
        QStringList(covered.begin(), covered.end()), kInsiderQueryDays,
        [self, covered, path](const ownership::ClusterBuys& found) {
            if (!self)
                return;
            self->insider_checking_ = false;
            const bool again = self->insider_recheck_;
            self->insider_recheck_ = false;
            if (!self->running_ || self->db_path_ != path)
                return; // signed out, or another user's rules now
            // A failed or refused read is not "no cluster": nothing is concluded from it.
            if (!found.error.isEmpty()) {
                LOG_WARN("Alerts", "insider check failed: " + found.error);
                return;
            }
            self->form4_scanned_to_ = found.scanned_to.toString(Qt::ISODate);
            const QDate today = QDate::currentDate();
            bool dirty = false;
            const auto events = step_insider(found.rows, covered, self->set_.insider_seen, today, &dirty);
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
        });
}

// ── Stakes, fund moves, fund filings ────────────────────────────────────────

namespace {
QString pct_text(const std::optional<double>& v) {
    return v ? QStringLiteral("%1%").arg(*v, 0, 'f', 1) : QStringLiteral("an unstated share");
}
QString quarter_text(const QDate& q) {
    return q.isValid() ? QStringLiteral("Q%1 %2").arg((q.month() - 1) / 3 + 1).arg(q.year()) : QString();
}
} // namespace

void AlertService::check_filings() {
    if (!running_)
        return;
    if (filings_checking_) {
        filings_recheck_ = true; // a rule was added mid-pass: look again after it
        return;
    }
    refresh_delivery_switches();
    if (!insider_on_)
        return; // the Ownership alerts switch in Settings covers all filing alerts
    const QSet<QString> stake_syms = covered(Kind::StakeFiling);
    const QSet<QString> move_syms = covered(Kind::FundMove);
    const QSet<QString> funds = covered(Kind::FundFiled);
    if (stake_syms.isEmpty() && move_syms.isEmpty() && funds.isEmpty())
        return;
    filings_checking_ = true;
    filings_pending_ = 0;
    const QString path = db_path_;
    QPointer<AlertService> self = this;
    auto done_one = [self]() {
        if (!self || --self->filings_pending_ > 0)
            return;
        self->filings_checking_ = false;
        if (prune_seen(self->set_.insider_seen, QDate::currentDate()))
            self->save_states();
        if (self->filings_recheck_) {
            self->filings_recheck_ = false;
            QTimer::singleShot(0, self, [self]() {
                if (self)
                    self->check_filings();
            });
        }
    };
    // Every read answers once, later. A failed or refused read is not
    // "nothing new": nothing is concluded from it.
    auto usable = [self, path, done_one](const char* what, const QString& error) {
        if (!self)
            return false;
        done_one();
        if (!self->running_ || self->db_path_ != path)
            return false; // signed out, or another user's rules now
        if (!error.isEmpty()) {
            LOG_WARN("Alerts", QStringLiteral("%1 check failed: %2").arg(QLatin1String(what), error));
            return false;
        }
        return true;
    };
    auto list = [](const QSet<QString>& s) { return QStringList(s.begin(), s.end()); };
    auto& own = services::OwnershipService::instance();

    if (!stake_syms.isEmpty()) {
        ++filings_pending_;
        own.query_stakes(list(stake_syms), kInsiderFreshDays + 7, [self, usable, stake_syms](const ownership::Stakes& found) {
            if (!usable("stake", found.error))
                return;
            QVector<FilingEvent> rows;
            QHash<QString, QPair<QString, QString>> text;
            for (const auto& r : found.rows) {
                if (!stake_is_news(r.activist, r.amendment, r.pct.value_or(kNaN), r.prev_pct.value_or(kNaN)))
                    continue;
                FilingEvent e{QStringLiteral("stake:") + r.accession, r.ticker.toUpper(), r.filed};
                const QString form = r.activist ? QStringLiteral("13D") : QStringLiteral("13G");
                const QString was = r.prev_pct ? QStringLiteral(" (was %1)").arg(pct_text(r.prev_pct)) : QString();
                text.insert(e.key,
                            {QStringLiteral("%1 %2 — %3")
                                 .arg(form, r.amendment ? QStringLiteral("amended") : QStringLiteral("filed"), e.subject),
                             QStringLiteral("%1 reports %2 of %3%4%5; filed %6. %7 A filing, not a call.")
                                 .arg(r.filer, pct_text(r.pct), e.subject, was,
                                      r.event.isValid() ? QStringLiteral(", event date %1").arg(short_date(r.event))
                                                        : QString(),
                                      short_date(e.filed),
                                      r.activist ? QStringLiteral("A 13D means the holder may seek influence.")
                                                 : QStringLiteral("A 13G is a passive holder."))});
                rows.push_back(e);
            }
            bool dirty = false;
            const auto events = step_filings(rows, stake_syms, self->set_.insider_seen, QDate::currentDate(), &dirty);
            self->deliver_filings(events, Kind::StakeFiling, text);
            if (dirty)
                self->save_states();
        });
    }

    if (!move_syms.isEmpty()) {
        ++filings_pending_;
        own.query_big_moves(list(move_syms), kInsiderFreshDays + 7, [self, usable, move_syms](const ownership::BigMoves& found) {
            if (!usable("13F move", found.error))
                return;
            QVector<FilingEvent> rows;
            QHash<QString, QPair<QString, QString>> text;
            for (const auto& r : found.rows) {
                FilingEvent e{QStringLiteral("move:") + r.url + QLatin1Char('|') + r.cusip, r.ticker.toUpper(), r.filed};
                // The share of the book is unknown when the filer's book value
                // is: said nothing about, never "0.0%".
                const QString of_book =
                    r.book_pct ? QStringLiteral(" (%1% of its book)").arg(*r.book_pct, 0, 'f', 1) : QString();
                text.insert(e.key,
                            {QStringLiteral("13F: %1 %2 %3").arg(r.manager, r.kind, e.subject),
                             QStringLiteral("%1 %2 %3 — about %4%5, %6 vs %7; filed %8. "
                                            "Estimated from quarter-end holdings; a filing, not a call.")
                                 .arg(r.manager, r.kind, e.subject,
                                      (r.flow >= 0 ? QStringLiteral("+") : QStringLiteral("−")) + money(std::fabs(r.flow)),
                                      of_book, quarter_text(r.quarter), quarter_text(r.prior_quarter),
                                      short_date(e.filed))});
                rows.push_back(e);
            }
            bool dirty = false;
            const auto events = step_filings(rows, move_syms, self->set_.insider_seen, QDate::currentDate(), &dirty);
            self->deliver_filings(events, Kind::FundMove, text);
            if (dirty)
                self->save_states();
        });
    }

    if (!funds.isEmpty()) {
        ++filings_pending_;
        own.query_fund_filings(list(funds), [self, usable, funds](const ownership::FundFilings& found) {
            if (!usable("fund filing", found.error))
                return;
            QVector<FilingEvent> rows;
            QHash<QString, QPair<QString, QString>> text;
            auto move = [](const std::optional<ownership::FirmMoveTop>& m) {
                if (!m)
                    return QStringLiteral("—");
                return QStringLiteral("%1 %2%3")
                    .arg(m->ticker.isEmpty() ? m->issuer : m->ticker,
                         m->value >= 0 ? QStringLiteral("+") : QStringLiteral("−"), money(std::fabs(m->value)));
            };
            for (const auto& f : found.rows) {
                FilingEvent e{QStringLiteral("fund:") + f.accession, f.cik, f.filed};
                text.insert(e.key,
                            {QStringLiteral("13F filed — %1").arg(f.manager),
                             QStringLiteral("%1 filed its %2 13F on %3. Biggest buy: %4. Biggest sell: %5 "
                                            "(estimated dollars).")
                                 .arg(f.manager, quarter_text(f.quarter), short_date(e.filed), move(f.top_buy),
                                      move(f.top_sell))});
                rows.push_back(e);
            }
            bool dirty = false;
            const auto events = step_filings(rows, funds, self->set_.insider_seen, QDate::currentDate(), &dirty);
            self->deliver_filings(events, Kind::FundFiled, text);
            if (dirty)
                self->save_states();
        });
    }
}

void AlertService::deliver_filings(const QVector<FilingEvent>& events, Kind kind,
                                   const QHash<QString, QPair<QString, QString>>& text) {
    QStringList fired;
    for (const auto& ev : events) {
        const auto t = text.value(ev.key);
        notifications::NotificationRequest req;
        req.title = t.first;
        req.message = t.second;
        req.level = notifications::NotifLevel::Alert;
        req.trigger = notifications::NotifTrigger::OwnershipAlert;
        if (kind != Kind::FundFiled)
            req.symbol = ev.subject;
        notifications::NotificationService::instance().send(req);
        for (const auto& r : set_.rules) {
            if (r.kind != kind || r.paused)
                continue;
            if ((r.scope == Scope::OwnNames && kind != Kind::FundFiled) || r.symbol == ev.subject) {
                RuleState& s = state_ref(r.id);
                s.last_fired = QDateTime::currentDateTimeUtc();
                fired << r.id;
            }
        }
    }
    fired.removeDuplicates();
    if (!fired.isEmpty())
        save_states();
    for (const auto& id : fired)
        emit state_changed(id);
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

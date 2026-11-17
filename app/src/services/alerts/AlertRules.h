#pragma once
// AlertRules.h — the pure half of user alerts: what a rule is, when it fires,
// when it re-arms, and how rules and their fired state survive a restart.
//
// Header-only, Qt Core only, no service or widget, so every guard below is
// unit-tested (tests/services/test_alert_rules.cpp) without a running app.
//
// The four guarantees, each enforced here and nowhere else:
//
//  1. A price crossing fires ONCE. The rule then sits "tripped" and re-arms
//     only after the price goes back across the level by the hysteresis band
//     (kRearmBandFrac of the level — 0.5%), so a print wobbling on the level
//     cannot fire it again and again.
//  2. A day move beyond ±N% fires once per SESSION per rule. A session is
//     identified by the quote's own baseline — its previous close, price −
//     change — not by the wall clock: a quote polled at 2 a.m. still belongs
//     to the session that closed the evening before, and a date-keyed rule
//     would announce yesterday's move a second time after midnight.
//  3. An insider cluster alert fires once per FILING: every accession that
//     made up an announced cluster is remembered, and a later cluster fires
//     only when it contains an accession not announced before.
//  4. Absent is never zero. A NaN / missing price, change, baseline or level
//     is "no information" — nothing fires, nothing re-arms, and a stored rule
//     whose level is missing is dropped, not read as a level of 0.
//
// Extension point — date-based reminders (earnings dates, after the calendar
// change lands): add a Kind, its kind_key()/kind_from_key() spelling, a
// describe() line and a step function here, then a source in AlertService
// that calls AlertService::fire(). Stored rules of a kind this build does not
// know are kept verbatim (see RuleSet::unknown) so an older build never drops
// a newer build's rules.

#include "screens/ownership/OwnershipFlags.h" // kClusterWindowDays / kClusterMinInsiders
#include "services/markets/LivePrice.h"      // tick_pairs_with

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTimeZone>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace finterm::alerts {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ── Thresholds, named so they can be argued with ────────────────────────────
/// A tripped crossing re-arms once the price is back across the level by this
/// fraction of the level (0.5%: 0.95 on a 190.00 level).
inline constexpr double kRearmBandFrac = 0.005;
/// An insider filing older than this (by FILED date) is history, not news: it
/// never fires, however late the store caught up with it.
inline constexpr int kInsiderFreshDays = 7;
/// Rows the insider query reads: the cluster window plus the fresh window, so
/// a fresh filing can join a cluster that began a month earlier.
inline constexpr int kInsiderQueryDays = ownership::kClusterWindowDays + kInsiderFreshDays + 3;
/// Announced accessions are remembered this long — longer than any row the
/// query can return, so a remembered filing can never come back as new.
inline constexpr int kInsiderSeenKeepDays = 120;

enum class Kind {
    PriceAbove,
    PriceBelow,
    DayMove,
    InsiderCluster,
    StakeFiling, ///< a 13D, 13D amendment that moved the stake, or new 13G on a covered name
    FundMove,    ///< a big 13F stake change in a covered name (BIG MOVES' corrected rows)
    FundFiled    ///< a followed fund filed its 13F; `symbol` holds the fund's CIK
};

/// Whether a kind is one of the filing alerts (insider, stake, fund): they
/// fire once per filing, never on a price.
inline bool is_filing_kind(Kind k) {
    return k == Kind::InsiderCluster || k == Kind::StakeFiling || k == Kind::FundMove || k == Kind::FundFiled;
}

/// A 13D amendment is news when the stake moved at least this many points;
/// an amendment that restates the same percentage (a new address, a joint
/// filer added) is not.
inline constexpr double kStakeMovePts = 1.0;
/// Which prints a price rule watches.
enum class Session {
    Regular, ///< regular-session quotes only (09:30–16:00 ET for US listings)
    Extended ///< also pre-market, post-market and overnight prints
};
/// What an insider rule covers.
enum class Scope {
    Symbol,  ///< the rule's own symbol
    OwnNames ///< every symbol in the user's watchlists and portfolios
};
enum class Arm {
    Pending, ///< no usable price seen since the rule was (re)started
    Armed,   ///< on the near side of the level: the next crossing fires
    Tripped  ///< on the far side: fired (or started there); waits for the band
};

struct Rule {
    QString id;
    Kind kind = Kind::PriceAbove;
    QString symbol; ///< upper-case; empty only for Scope::OwnNames
    double level = kNaN;     ///< price rules
    double pct = kNaN;       ///< day-move rules: threshold in percent, > 0
    Session session = Session::Regular;
    Scope scope = Scope::Symbol;
    bool paused = false;
    QDateTime created;
    QString label; ///< FundFiled: the fund's name, for the rules table

    bool is_price() const { return kind == Kind::PriceAbove || kind == Kind::PriceBelow; }
    bool valid() const {
        switch (kind) {
            case Kind::PriceAbove:
            case Kind::PriceBelow:
                return !symbol.isEmpty() && std::isfinite(level) && level > 0;
            case Kind::DayMove:
                return !symbol.isEmpty() && std::isfinite(pct) && pct > 0;
            case Kind::InsiderCluster:
            case Kind::StakeFiling:
            case Kind::FundMove:
                return scope == Scope::OwnNames || !symbol.isEmpty();
            case Kind::FundFiled:
                return !symbol.isEmpty(); // the fund's CIK
        }
        return false;
    }
};

struct RuleState {
    Arm arm = Arm::Pending;
    /// Day move: the session (baseline = previous close) the rule last fired in.
    double fired_baseline = kNaN;
    QDateTime last_fired;
    double last_value = kNaN; ///< the price / % that fired it
};

// ── Spelling (storage keys and UI text) ─────────────────────────────────────

inline QString kind_key(Kind k) {
    switch (k) {
        case Kind::PriceAbove:
            return QStringLiteral("price_above");
        case Kind::PriceBelow:
            return QStringLiteral("price_below");
        case Kind::DayMove:
            return QStringLiteral("day_move");
        case Kind::InsiderCluster:
            return QStringLiteral("insider_cluster");
        case Kind::StakeFiling:
            return QStringLiteral("stake_filing");
        case Kind::FundMove:
            return QStringLiteral("fund_move");
        case Kind::FundFiled:
            return QStringLiteral("fund_filed");
    }
    return {};
}

inline std::optional<Kind> kind_from_key(const QString& s) {
    if (s == QLatin1String("price_above"))
        return Kind::PriceAbove;
    if (s == QLatin1String("price_below"))
        return Kind::PriceBelow;
    if (s == QLatin1String("day_move"))
        return Kind::DayMove;
    if (s == QLatin1String("insider_cluster"))
        return Kind::InsiderCluster;
    if (s == QLatin1String("stake_filing"))
        return Kind::StakeFiling;
    if (s == QLatin1String("fund_move"))
        return Kind::FundMove;
    if (s == QLatin1String("fund_filed"))
        return Kind::FundFiled;
    return std::nullopt;
}

inline QString arm_key(Arm a) {
    switch (a) {
        case Arm::Armed:
            return QStringLiteral("armed");
        case Arm::Tripped:
            return QStringLiteral("tripped");
        case Arm::Pending:
            break;
    }
    return QStringLiteral("pending");
}

inline Arm arm_from_key(const QString& s) {
    if (s == QLatin1String("armed"))
        return Arm::Armed;
    if (s == QLatin1String("tripped"))
        return Arm::Tripped;
    return Arm::Pending;
}

/// A price as a person would write it: 2 decimals, 4 below a dollar.
inline QString fmt_price(double v) {
    if (!std::isfinite(v))
        return QStringLiteral("—");
    return QString::number(v, 'f', std::abs(v) < 1.0 ? 4 : 2);
}

inline double rearm_band(double level) {
    return std::isfinite(level) ? std::abs(level) * kRearmBandFrac : kNaN;
}

/// The price at which a tripped crossing re-arms.
inline double rearm_price(const Rule& r) {
    if (!r.is_price() || !std::isfinite(r.level))
        return kNaN;
    return r.kind == Kind::PriceAbove ? r.level - rearm_band(r.level) : r.level + rearm_band(r.level);
}

inline QString describe(const Rule& r) {
    switch (r.kind) {
        case Kind::PriceAbove:
        case Kind::PriceBelow:
            return QStringLiteral("Price crosses %1 %2 · %3")
                .arg(r.kind == Kind::PriceAbove ? QStringLiteral("above") : QStringLiteral("below"),
                     fmt_price(r.level),
                     r.session == Session::Extended ? QStringLiteral("incl. extended hours")
                                                    : QStringLiteral("regular session"));
        case Kind::DayMove:
            return QStringLiteral("Day move beyond ±%1% vs previous close").arg(QString::number(r.pct, 'g', 4));
        case Kind::InsiderCluster:
            return r.scope == Scope::OwnNames
                       ? QStringLiteral("Insider cluster buy · every watchlist & portfolio name")
                       : QStringLiteral("Insider cluster buy (%1+ insiders in %2 days)")
                             .arg(ownership::kClusterMinInsiders)
                             .arg(ownership::kClusterWindowDays);
        case Kind::StakeFiling:
            return r.scope == Scope::OwnNames ? QStringLiteral("5% stake filed (13D / 13G) · every watchlist & portfolio name")
                                              : QStringLiteral("5% stake filed (13D / 13G)");
        case Kind::FundMove:
            return r.scope == Scope::OwnNames ? QStringLiteral("Big 13F move · every watchlist & portfolio name")
                                              : QStringLiteral("Big 13F move by an active manager");
        case Kind::FundFiled:
            return QStringLiteral("13F filed by %1").arg(r.label.isEmpty() ? QStringLiteral("CIK ") + r.symbol : r.label);
    }
    return {};
}

/// The STATE column: armed / fired / paused, and what a waiting rule waits for.
inline QString state_label(const Rule& r, const RuleState& s) {
    if (r.paused)
        return QStringLiteral("paused");
    switch (r.kind) {
        case Kind::PriceAbove:
        case Kind::PriceBelow:
            switch (s.arm) {
                case Arm::Pending:
                    return QStringLiteral("armed · waiting for a price");
                case Arm::Armed:
                    return QStringLiteral("armed");
                case Arm::Tripped:
                    return QStringLiteral("%1 · re-arms %2 %3")
                        .arg(s.last_fired.isValid() ? QStringLiteral("fired") : QStringLiteral("already beyond"),
                             r.kind == Kind::PriceAbove ? QStringLiteral("below") : QStringLiteral("above"),
                             fmt_price(rearm_price(r)));
            }
            break;
        case Kind::DayMove:
            // Which session the last fire belonged to is not knowable here;
            // LAST FIRED says when. The rule re-arms on the next session's quote.
            return std::isfinite(s.fired_baseline) ? QStringLiteral("armed · once per session")
                                                   : QStringLiteral("armed");
        case Kind::InsiderCluster:
        case Kind::StakeFiling:
        case Kind::FundMove:
        case Kind::FundFiled:
            return QStringLiteral("armed · once per filing");
    }
    return {};
}

/// Whether an edit must restart a rule's state. A price rule whose level,
/// prints or symbol changed restarts (the old armed side means nothing); a
/// day-move rule keeps its session key unless its symbol changed, so editing
/// the threshold — or saving it unchanged — cannot fire it twice in a session.
inline bool edit_restarts(const Rule& was, const Rule& now) {
    if (was.kind != now.kind || was.symbol != now.symbol)
        return true;
    if (now.is_price())
        return was.level != now.level || was.session != now.session;
    return false;
}

// ── 1. Price crossings, with hysteresis ─────────────────────────────────────

struct Step {
    bool fired = false;
    bool changed = false; ///< state differs (persist it)
};

/// One usable price for a crossing rule. A non-finite or non-positive price is
/// no information: the state is left exactly as it was.
inline Step step_cross(const Rule& r, RuleState& s, double price) {
    Step out;
    if (!r.is_price() || !std::isfinite(r.level) || r.level <= 0 || !std::isfinite(price) || price <= 0)
        return out;
    const bool above = r.kind == Kind::PriceAbove;
    const bool beyond = above ? price >= r.level : price <= r.level;
    const double band = rearm_band(r.level);
    const bool back = above ? price <= r.level - band : price >= r.level + band;
    const Arm was = s.arm;
    switch (s.arm) {
        case Arm::Pending:
            // The first price decides the side. Starting beyond the level is
            // not a crossing — nothing was crossed while we were watching.
            s.arm = beyond ? Arm::Tripped : Arm::Armed;
            break;
        case Arm::Armed:
            if (beyond) {
                s.arm = Arm::Tripped;
                out.fired = true;
            }
            break;
        case Arm::Tripped:
            if (back)
                s.arm = Arm::Armed;
            break;
    }
    out.changed = s.arm != was || out.fired;
    return out;
}

/// Whether an extended-hours print may be used. The live stream's own rule
/// (LivePrice.h): a print carries Yahoo's change against Yahoo's previous
/// close, so price − change must BE the close the polled quote holds — the
/// regular price, which outside the session is that session's close. A print
/// paired with a different close (a session the quote has not reached yet)
/// waits for the next poll instead of being trusted.
inline bool extended_print_ok(double quote_price, double print_price, double print_change) {
    return services::tick_pairs_with(quote_price, print_price, print_change);
}

/// Whether @utc falls in the US regular session (09:30–16:00 New York time,
/// Monday–Friday). Exchange holidays are not modelled: on one, nothing trades,
/// so the quote does not move and treating it as regular hours is harmless.
inline bool us_regular_session(const QDateTime& utc) {
    static const QTimeZone ny("America/New_York");
    const QDateTime t = ny.isValid() ? utc.toTimeZone(ny) : utc.toUTC().addSecs(-5 * 3600);
    const int dow = t.date().dayOfWeek();
    if (dow > 5)
        return false;
    const int mins = t.time().hour() * 60 + t.time().minute();
    return mins >= 9 * 60 + 30 && mins < 16 * 60;
}

// ── 2. Day move, once per session ───────────────────────────────────────────

/// Two baselines name the same session when they are the same close, up to
/// the float noise of price − change (same tolerance as tick_pairs_with).
inline bool same_baseline(double a, double b) {
    if (!std::isfinite(a) || !std::isfinite(b) || a <= 0 || b <= 0)
        return false;
    return std::abs(a - b) <= 1e-5 * std::max(a, b);
}

/// @param change_pct  the quote's change vs previous close, in percent
/// @param baseline    the quote's previous close (price − change)
inline Step step_day_move(const Rule& r, RuleState& s, double change_pct, double baseline) {
    Step out;
    if (r.kind != Kind::DayMove || !std::isfinite(r.pct) || r.pct <= 0)
        return out;
    // A refused or partial quote carries NaN, never a flat 0%: no information.
    if (!std::isfinite(change_pct) || !std::isfinite(baseline) || baseline <= 0)
        return out;
    if (std::abs(change_pct) < r.pct)
        return out;
    if (same_baseline(s.fired_baseline, baseline))
        return out; // already announced for this session
    s.fired_baseline = baseline;
    out.fired = out.changed = true;
    return out;
}

// ── 3. Insider clusters, once per filing ────────────────────────────────────

/// One scorable open-market purchase from the local Form 4 store (10% owners
/// and 10b5-1 plan trades already excluded by the query).
struct InsiderBuy {
    QString symbol;
    QString accession;
    QString insider; ///< CIK when filed, else the name — one buyer, one key
    QDate filed;
    QDate traded;
    double value = kNaN;
};

struct ClusterEvent {
    QString symbol;
    int insiders = 0;
    QDate first_trade;
    QDate last_trade;
    QDate newest_filed;
    double value = kNaN; ///< sum of the reported values; NaN when none was
    QStringList new_accessions;
};

/// The Ownership definition, applied per symbol: buys sorted by trade date,
/// chained while consecutive trades are within kClusterWindowDays, a cluster
/// when the chain holds kClusterMinInsiders distinct buyers (as
/// ownership::find_cluster_buys). A cluster fires when it contains a FRESH
/// accession (filed within kInsiderFreshDays of @today) that @seen does not
/// hold; every accession of a fired cluster is then added to @seen.
inline QVector<ClusterEvent> step_insider(const QVector<InsiderBuy>& rows, const QSet<QString>& covered,
                                          QHash<QString, QDate>& seen, const QDate& today, bool* changed = nullptr) {
    QVector<ClusterEvent> out;
    QHash<QString, QVector<const InsiderBuy*>> by_symbol;
    for (const auto& b : rows) {
        if (b.symbol.isEmpty() || b.accession.isEmpty() || !b.traded.isValid() || !b.filed.isValid())
            continue;
        if (!covered.contains(b.symbol))
            continue;
        by_symbol[b.symbol].push_back(&b);
    }
    QStringList symbols = by_symbol.keys();
    symbols.sort(); // deterministic order of notifications
    for (const auto& sym : symbols) {
        auto buys = by_symbol.value(sym);
        std::stable_sort(buys.begin(), buys.end(), [](const InsiderBuy* a, const InsiderBuy* b) {
            return a->traded != b->traded ? a->traded < b->traded : a->accession < b->accession;
        });
        int i = 0;
        while (i < buys.size()) {
            int j = i;
            while (j + 1 < buys.size() && buys[j]->traded.daysTo(buys[j + 1]->traded) <= ownership::kClusterWindowDays)
                ++j;
            QSet<QString> people;
            for (int k = i; k <= j; ++k)
                people.insert(buys[k]->insider);
            if (people.size() >= ownership::kClusterMinInsiders) {
                ClusterEvent ev;
                ev.symbol = sym;
                ev.insiders = int(people.size());
                ev.first_trade = buys[i]->traded;
                ev.last_trade = buys[j]->traded;
                bool any_value = false;
                double total = 0.0;
                QSet<QString> accs;
                for (int k = i; k <= j; ++k) {
                    const auto* b = buys[k];
                    accs.insert(b->accession);
                    if (std::isfinite(b->value)) {
                        total += b->value;
                        any_value = true;
                    }
                    if (!ev.newest_filed.isValid() || b->filed > ev.newest_filed)
                        ev.newest_filed = b->filed;
                    const bool fresh = b->filed.daysTo(today) <= kInsiderFreshDays;
                    if (fresh && !seen.contains(b->accession) && !ev.new_accessions.contains(b->accession))
                        ev.new_accessions << b->accession;
                }
                ev.value = any_value ? total : kNaN;
                if (!ev.new_accessions.isEmpty()) {
                    // Remember the whole cluster, not just the new filings:
                    // a re-read of the same filings must stay silent.
                    for (int k = i; k <= j; ++k)
                        seen.insert(buys[k]->accession, buys[k]->filed);
                    ev.new_accessions.sort();
                    out.push_back(ev);
                    if (changed)
                        *changed = true;
                }
            }
            i = j + 1;
        }
    }
    return out;
}

// ── 3b. Stakes, fund moves, fund filings: once per filing ───────────────────

/// One filing-level event from a stake, 13F-move or fund-filing feed. `key`
/// identifies it for good (accession, or accession + CUSIP for a single
/// move in a book); `subject` is what a rule covers (a ticker, or a fund's
/// CIK); `filed` is the day it reached SEC.
struct FilingEvent {
    QString key;
    QString subject;
    QDate filed;
};

/// The events to announce now: covered, filed within kInsiderFreshDays of
/// @today (older is history however late the feed caught up), and not
/// announced before. Every returned key is added to @seen — the same set
/// the insider alert uses, with keys that cannot collide with an accession.
inline QVector<FilingEvent> step_filings(const QVector<FilingEvent>& rows, const QSet<QString>& covered,
                                         QHash<QString, QDate>& seen, const QDate& today, bool* changed = nullptr) {
    QVector<FilingEvent> out;
    for (const auto& e : rows) {
        if (e.key.isEmpty() || !e.filed.isValid() || !covered.contains(e.subject))
            continue;
        if (e.filed.daysTo(today) > kInsiderFreshDays || e.filed > today)
            continue;
        if (seen.contains(e.key))
            continue;
        seen.insert(e.key, e.filed);
        out.push_back(e);
        if (changed)
            *changed = true;
    }
    return out;
}

/// Whether one stake filing is worth an alert: an initial 13D or 13G always;
/// a 13D amendment when the stake moved by kStakeMovePts or more (or crossed
/// 10%, the line where short-swing rules start to bind), or when the earlier
/// percentage is unknown; a 13G amendment never — those are passive holders
/// restating a position.
inline bool stake_is_news(bool activist, bool amendment, double pct, double prev_pct) {
    if (!amendment)
        return true;
    if (!activist || !std::isfinite(pct))
        return false;
    // The previous filing is older than the store reaches (most amendments):
    // what moved cannot be told, so the amendment is announced with its
    // percentage rather than silently dropped.
    if (!std::isfinite(prev_pct))
        return true;
    const bool crossed_ten = (pct >= 10.0) != (prev_pct >= 10.0);
    return std::fabs(pct - prev_pct) >= kStakeMovePts || crossed_ten;
}

/// Forget announced accessions filed more than kInsiderSeenKeepDays ago.
inline bool prune_seen(QHash<QString, QDate>& seen, const QDate& today) {
    bool any = false;
    for (auto it = seen.begin(); it != seen.end();) {
        if (!it.value().isValid() || it.value().daysTo(today) > kInsiderSeenKeepDays) {
            it = seen.erase(it);
            any = true;
        } else {
            ++it;
        }
    }
    return any;
}

// ── 4. Persistence ──────────────────────────────────────────────────────────

inline QJsonObject to_json(const Rule& r) {
    QJsonObject o{{QStringLiteral("id"), r.id},
                  {QStringLiteral("kind"), kind_key(r.kind)},
                  {QStringLiteral("symbol"), r.symbol},
                  {QStringLiteral("paused"), r.paused}};
    if (r.created.isValid())
        o.insert(QStringLiteral("created"), r.created.toUTC().toString(Qt::ISODate));
    if (r.is_price()) {
        o.insert(QStringLiteral("level"), r.level);
        o.insert(QStringLiteral("session"),
                 r.session == Session::Extended ? QStringLiteral("extended") : QStringLiteral("regular"));
    } else if (r.kind == Kind::DayMove) {
        o.insert(QStringLiteral("pct"), r.pct);
    } else if (is_filing_kind(r.kind)) {
        o.insert(QStringLiteral("scope"), r.scope == Scope::OwnNames ? QStringLiteral("own") : QStringLiteral("symbol"));
        if (!r.label.isEmpty())
            o.insert(QStringLiteral("label"), r.label);
    }
    return o;
}

/// nullopt for an unknown kind or an invalid rule. A missing / null level or
/// threshold reads as NaN (invalid), never as 0.
inline std::optional<Rule> rule_from_json(const QJsonObject& o) {
    const auto kind = kind_from_key(o.value(QStringLiteral("kind")).toString());
    if (!kind)
        return std::nullopt;
    Rule r;
    r.kind = *kind;
    r.id = o.value(QStringLiteral("id")).toString();
    r.symbol = o.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
    r.paused = o.value(QStringLiteral("paused")).toBool(false);
    r.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    r.level = o.value(QStringLiteral("level")).toDouble(kNaN);
    r.pct = o.value(QStringLiteral("pct")).toDouble(kNaN);
    r.session = o.value(QStringLiteral("session")).toString() == QLatin1String("extended") ? Session::Extended
                                                                                            : Session::Regular;
    r.scope = o.value(QStringLiteral("scope")).toString() == QLatin1String("own") ? Scope::OwnNames : Scope::Symbol;
    r.label = o.value(QStringLiteral("label")).toString();
    if (r.id.isEmpty() || !r.valid())
        return std::nullopt;
    return r;
}

inline QJsonObject to_json(const RuleState& s) {
    QJsonObject o{{QStringLiteral("arm"), arm_key(s.arm)}};
    if (std::isfinite(s.fired_baseline))
        o.insert(QStringLiteral("fired_baseline"), s.fired_baseline);
    if (s.last_fired.isValid())
        o.insert(QStringLiteral("last_fired"), s.last_fired.toUTC().toString(Qt::ISODate));
    if (std::isfinite(s.last_value))
        o.insert(QStringLiteral("last_value"), s.last_value);
    return o;
}

inline RuleState state_from_json(const QJsonObject& o) {
    RuleState s;
    s.arm = arm_from_key(o.value(QStringLiteral("arm")).toString());
    s.fired_baseline = o.value(QStringLiteral("fired_baseline")).toDouble(kNaN);
    s.last_fired = QDateTime::fromString(o.value(QStringLiteral("last_fired")).toString(), Qt::ISODate);
    s.last_value = o.value(QStringLiteral("last_value")).toDouble(kNaN);
    return s;
}

/// Everything stored for one user.
struct RuleSet {
    QVector<Rule> rules;
    QHash<QString, RuleState> states;   ///< by rule id
    QHash<QString, QDate> insider_seen; ///< announced accession → filed date
    QJsonArray unknown;                 ///< stored rules this build cannot read, kept verbatim
};

inline QJsonArray rules_to_json(const RuleSet& set) {
    QJsonArray a;
    for (const auto& r : set.rules)
        a.append(to_json(r));
    for (const auto& u : set.unknown)
        a.append(u);
    return a;
}

inline QJsonObject states_to_json(const RuleSet& set) {
    QJsonObject rules;
    for (const auto& r : set.rules)
        if (set.states.contains(r.id))
            rules.insert(r.id, to_json(set.states.value(r.id)));
    QJsonObject seen;
    for (auto it = set.insider_seen.constBegin(); it != set.insider_seen.constEnd(); ++it)
        seen.insert(it.key(), it.value().toString(Qt::ISODate));
    return QJsonObject{{QStringLiteral("rules"), rules}, {QStringLiteral("insider_seen"), seen}};
}

/// Rebuild a RuleSet from the two stored documents. A rule this build cannot
/// read is kept in `unknown`; a duplicate id keeps the first.
inline RuleSet ruleset_from_json(const QJsonArray& rules, const QJsonObject& states) {
    RuleSet set;
    QSet<QString> ids;
    for (const auto& v : rules) {
        const QJsonObject o = v.toObject();
        if (o.isEmpty())
            continue;
        if (!kind_from_key(o.value(QStringLiteral("kind")).toString())) {
            set.unknown.append(o);
            continue;
        }
        const auto r = rule_from_json(o);
        if (!r || ids.contains(r->id))
            continue; // invalid (e.g. a level that was never stored) — dropped, never read as 0
        ids.insert(r->id);
        set.rules.push_back(*r);
    }
    const QJsonObject st = states.value(QStringLiteral("rules")).toObject();
    for (const auto& r : set.rules)
        if (st.contains(r.id))
            set.states.insert(r.id, state_from_json(st.value(r.id).toObject()));
    const QJsonObject seen = states.value(QStringLiteral("insider_seen")).toObject();
    for (auto it = seen.constBegin(); it != seen.constEnd(); ++it) {
        const QDate d = QDate::fromString(it.value().toString(), Qt::ISODate);
        if (d.isValid())
            set.insider_seen.insert(it.key(), d);
    }
    return set;
}

} // namespace finterm::alerts

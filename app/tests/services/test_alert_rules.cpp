// test_alert_rules.cpp — the pure half of user alerts (AlertRules.h).
//
// Pinned: a crossing fires once and re-arms only across the hysteresis band;
// a day move fires once per session (keyed by the quote's own baseline, not
// the clock); an insider cluster fires once per filing; absent data never
// fires; and rules + fired state survive a store round-trip unchanged.

#include <QTest>

#include "services/alerts/AlertRules.h"

#include <QTimeZone>

#include <cmath>

using namespace finterm::alerts;

namespace {
Rule price_rule(Kind k, double level, Session s = Session::Regular) {
    Rule r;
    r.id = QStringLiteral("r1");
    r.kind = k;
    r.symbol = QStringLiteral("AAPL");
    r.level = level;
    r.session = s;
    return r;
}
Rule move_rule(double pct) {
    Rule r;
    r.id = QStringLiteral("m1");
    r.kind = Kind::DayMove;
    r.symbol = QStringLiteral("AAPL");
    r.pct = pct;
    return r;
}
InsiderBuy buy(const QString& sym, const QString& acc, const QString& who, const QDate& filed, const QDate& traded,
               double value = 1000.0) {
    InsiderBuy b;
    b.symbol = sym;
    b.accession = acc;
    b.insider = who;
    b.filed = filed;
    b.traded = traded;
    b.value = value;
    return b;
}
} // namespace

class TestAlertRules : public QObject {
    Q_OBJECT
  private slots:
    // ── crossings ────────────────────────────────────────────────────────────
    void above_fires_once_then_rearms_across_the_band() {
        const Rule r = price_rule(Kind::PriceAbove, 100.0);
        RuleState s;
        QVERIFY(!step_cross(r, s, 99.0).fired);
        QCOMPARE(s.arm, Arm::Armed);
        QVERIFY(step_cross(r, s, 100.0).fired); // touching the level is reaching it
        QCOMPARE(s.arm, Arm::Tripped);
        QVERIFY(!step_cross(r, s, 101.0).fired);
        // Back under the level but inside the band (100 − 0.5): still tripped.
        QVERIFY(!step_cross(r, s, 99.6).fired);
        QCOMPARE(s.arm, Arm::Tripped);
        QVERIFY(!step_cross(r, s, 100.2).fired); // the wobble that must not re-fire
        QCOMPARE(s.arm, Arm::Tripped);
        // Across the band: re-armed, and the next crossing fires again.
        const Step back = step_cross(r, s, 99.49);
        QVERIFY(!back.fired);
        QVERIFY(back.changed);
        QCOMPARE(s.arm, Arm::Armed);
        QVERIFY(step_cross(r, s, 100.5).fired);
    }

    void below_is_the_mirror_image() {
        const Rule r = price_rule(Kind::PriceBelow, 50.0);
        RuleState s;
        step_cross(r, s, 51.0);
        QCOMPARE(s.arm, Arm::Armed);
        QVERIFY(step_cross(r, s, 49.9).fired);
        QVERIFY(!step_cross(r, s, 50.2).fired); // inside the 0.25 band
        QCOMPARE(s.arm, Arm::Tripped);
        step_cross(r, s, 50.26);
        QCOMPARE(s.arm, Arm::Armed);
        QVERIFY(step_cross(r, s, 50.0).fired);
    }

    void starting_beyond_the_level_is_not_a_crossing() {
        const Rule r = price_rule(Kind::PriceAbove, 100.0);
        RuleState s;
        const Step st = step_cross(r, s, 105.0);
        QVERIFY(!st.fired);
        QVERIFY(st.changed);
        QCOMPARE(s.arm, Arm::Tripped);
        QVERIFY(!s.last_fired.isValid());
        QVERIFY(state_label(r, s).startsWith(QStringLiteral("already beyond")));
    }

    void absent_price_changes_nothing() {
        const Rule r = price_rule(Kind::PriceAbove, 100.0);
        RuleState s;
        step_cross(r, s, 90.0);
        for (double bad : {kNaN, 0.0, -1.0, std::numeric_limits<double>::infinity()}) {
            const Step st = step_cross(r, s, bad);
            QVERIFY(!st.fired);
            QVERIFY(!st.changed);
            QCOMPARE(s.arm, Arm::Armed);
        }
        // A rule with no level never fires, whatever the price.
        Rule broken = r;
        broken.level = kNaN;
        RuleState s2;
        s2.arm = Arm::Armed;
        QVERIFY(!step_cross(broken, s2, 1e9).fired);
    }

    void rearm_price_is_the_band() {
        QCOMPARE(rearm_price(price_rule(Kind::PriceAbove, 190.0)), 190.0 - 0.95);
        QCOMPARE(rearm_price(price_rule(Kind::PriceBelow, 190.0)), 190.0 + 0.95);
    }

    // ── extended-hours self-check ────────────────────────────────────────────
    void extended_print_must_pair_with_the_quotes_close() {
        // Post-market: the polled quote's price is today's close, 200; a print
        // at 203 with Yahoo's change +3 is measured against that close.
        QVERIFY(extended_print_ok(200.0, 203.0, 3.0));
        // A print paired with another close (the poll has not caught up).
        QVERIFY(!extended_print_ok(200.0, 203.0, 1.0));
        QVERIFY(!extended_print_ok(200.0, 203.0, kNaN));
        QVERIFY(!extended_print_ok(kNaN, 203.0, 3.0));
    }

    // ── day move ─────────────────────────────────────────────────────────────
    void day_move_fires_once_per_session() {
        const Rule r = move_rule(5.0);
        RuleState s;
        QVERIFY(!step_day_move(r, s, 4.9, 100.0).fired);
        QVERIFY(step_day_move(r, s, 5.0, 100.0).fired);
        QVERIFY(!step_day_move(r, s, 7.0, 100.0).fired);
        // The baseline is price − change, recomputed per quote: float noise
        // must not read as a new session.
        QVERIFY(!step_day_move(r, s, 6.0, 100.0 + 1e-9).fired);
        // A 2 a.m. poll of the same closed session: same baseline, silent.
        QVERIFY(!step_day_move(r, s, 5.5, 100.0).fired);
        // Next session: a new previous close.
        QVERIFY(step_day_move(r, s, -6.0, 105.5).fired);
        QCOMPARE(s.fired_baseline, 105.5);
        QVERIFY(!step_day_move(r, s, -8.0, 105.5).fired);
    }

    void absent_change_or_baseline_never_fires_a_move() {
        const Rule r = move_rule(2.0);
        RuleState s;
        // A refused quote carries NaN: neither "no move" nor a move.
        QVERIFY(!step_day_move(r, s, kNaN, 100.0).fired);
        QVERIFY(!step_day_move(r, s, 9.0, kNaN).fired);         // no previous close
        QVERIFY(!step_day_move(r, s, 9.0, 0.0).fired);
        QVERIFY(!std::isfinite(s.fired_baseline));
        QVERIFY(step_day_move(r, s, -2.0, 100.0).fired); // a down move counts (±)
    }

    // ── insider clusters ─────────────────────────────────────────────────────
    void cluster_fires_once_per_filing() {
        const QDate today(2026, 11, 13);
        const QSet<QString> covered{QStringLiteral("WDGT")};
        QHash<QString, QDate> seen;
        QVector<InsiderBuy> rows{
            buy("WDGT", "a1", "ann", today.addDays(-3), today.addDays(-5)),
            buy("WDGT", "a2", "bob", today.addDays(-2), today.addDays(-4), 2500.0),
        };
        bool changed = false;
        auto ev = step_insider(rows, covered, seen, today, &changed);
        QCOMPARE(ev.size(), 1);
        QVERIFY(changed);
        QCOMPARE(ev[0].insiders, 2);
        QCOMPARE(ev[0].value, 3500.0);
        QCOMPARE(ev[0].newest_filed, today.addDays(-2));
        QCOMPARE(ev[0].new_accessions, (QStringList{"a1", "a2"}));
        // The same filings read again (next scan, next launch): silent.
        changed = false;
        QVERIFY(step_insider(rows, covered, seen, today, &changed).isEmpty());
        QVERIFY(!changed);
        // A third insider's filing joins the cluster: news, once.
        rows << buy("WDGT", "a3", "cy", today.addDays(-1), today.addDays(-2));
        ev = step_insider(rows, covered, seen, today);
        QCOMPARE(ev.size(), 1);
        QCOMPARE(ev[0].insiders, 3);
        QCOMPARE(ev[0].new_accessions, (QStringList{"a3"}));
        QVERIFY(step_insider(rows, covered, seen, today).isEmpty());
    }

    void one_buyer_is_not_a_cluster() {
        const QDate today(2026, 11, 13);
        QHash<QString, QDate> seen;
        const QVector<InsiderBuy> rows{
            buy("WDGT", "a1", "ann", today.addDays(-3), today.addDays(-5)),
            buy("WDGT", "a2", "ann", today.addDays(-2), today.addDays(-4)),
        };
        QVERIFY(step_insider(rows, {QStringLiteral("WDGT")}, seen, today).isEmpty());
        QVERIFY(seen.isEmpty());
    }

    void buys_more_than_the_window_apart_are_not_a_cluster() {
        const QDate today(2026, 11, 13);
        QHash<QString, QDate> seen;
        const QVector<InsiderBuy> rows{
            buy("WDGT", "a1", "ann", today.addDays(-1), today.addDays(-40)),
            buy("WDGT", "a2", "bob", today.addDays(-1), today.addDays(-40 + finterm::ownership::kClusterWindowDays + 1)),
        };
        QVERIFY(step_insider(rows, {QStringLiteral("WDGT")}, seen, today).isEmpty());
    }

    void stale_filings_never_fire() {
        const QDate today(2026, 11, 13);
        QHash<QString, QDate> seen;
        const QVector<InsiderBuy> rows{
            buy("WDGT", "a1", "ann", today.addDays(-kInsiderFreshDays - 1), today.addDays(-12)),
            buy("WDGT", "a2", "bob", today.addDays(-kInsiderFreshDays - 2), today.addDays(-12)),
        };
        // The store caught up late with a cluster two weeks old: history.
        QVERIFY(step_insider(rows, {QStringLiteral("WDGT")}, seen, today).isEmpty());
    }

    void uncovered_and_undated_rows_are_ignored() {
        const QDate today(2026, 11, 13);
        QHash<QString, QDate> seen;
        QVector<InsiderBuy> rows{
            buy("OTHR", "a1", "ann", today, today),
            buy("OTHR", "a2", "bob", today, today),
            buy("WDGT", "b1", "ann", today, QDate()),
            buy("WDGT", "b2", "bob", QDate(), today),
            buy("WDGT", "", "cy", today, today),
        };
        QVERIFY(step_insider(rows, {QStringLiteral("WDGT")}, seen, today).isEmpty());
    }

    void cluster_value_is_unknown_when_none_was_reported() {
        const QDate today(2026, 11, 13);
        QHash<QString, QDate> seen;
        const QVector<InsiderBuy> rows{
            buy("WDGT", "a1", "ann", today, today, kNaN),
            buy("WDGT", "a2", "bob", today, today, kNaN),
        };
        const auto ev = step_insider(rows, {QStringLiteral("WDGT")}, seen, today);
        QCOMPARE(ev.size(), 1);
        QVERIFY(!std::isfinite(ev[0].value)); // not $0
    }

    void prune_forgets_only_old_filings() {
        const QDate today(2026, 11, 13);
        QHash<QString, QDate> seen{{"old", today.addDays(-kInsiderSeenKeepDays - 1)},
                                   {"new", today.addDays(-kInsiderQueryDays)}};
        QVERIFY(prune_seen(seen, today));
        QVERIFY(!seen.contains("old"));
        QVERIFY(seen.contains("new"));
        QVERIFY(kInsiderSeenKeepDays > kInsiderQueryDays); // a remembered filing can never return as new
    }

    // ── persistence ──────────────────────────────────────────────────────────
    void ruleset_round_trips() {
        RuleSet set;
        Rule a = price_rule(Kind::PriceBelow, 0.4321, Session::Extended);
        a.id = "a";
        a.paused = true;
        a.created = QDateTime(QDate(2026, 11, 1), QTime(14, 0), QTimeZone::UTC);
        Rule m = move_rule(3.5);
        Rule i;
        i.id = "i";
        i.kind = Kind::InsiderCluster;
        i.scope = Scope::OwnNames;
        set.rules = {a, m, i};
        RuleState sa;
        sa.arm = Arm::Tripped;
        sa.last_fired = QDateTime(QDate(2026, 11, 12), QTime(15, 30), QTimeZone::UTC);
        sa.last_value = 0.43;
        RuleState sm;
        sm.fired_baseline = 187.25;
        set.states = {{"a", sa}, {"m1", sm}};
        set.insider_seen = {{"0001-26-000001", QDate(2026, 11, 10)}};
        set.unknown.append(QJsonObject{{"id", "e"}, {"kind", "earnings_reminder"}, {"symbol", "AAPL"}});

        const RuleSet back = ruleset_from_json(rules_to_json(set), states_to_json(set));
        QCOMPARE(back.rules.size(), 3);
        QCOMPARE(back.rules[0].kind, Kind::PriceBelow);
        QCOMPARE(back.rules[0].level, 0.4321);
        QCOMPARE(back.rules[0].session, Session::Extended);
        QVERIFY(back.rules[0].paused);
        QCOMPARE(back.rules[0].created, a.created);
        QCOMPARE(back.rules[1].pct, 3.5);
        QCOMPARE(back.rules[2].scope, Scope::OwnNames);
        QCOMPARE(back.states.value("a").arm, Arm::Tripped);
        QCOMPARE(back.states.value("a").last_fired, sa.last_fired);
        QCOMPARE(back.states.value("a").last_value, 0.43);
        QCOMPARE(back.states.value("m1").fired_baseline, 187.25);
        QVERIFY(!std::isfinite(back.states.value("m1").last_value));
        QCOMPARE(back.insider_seen.value("0001-26-000001"), QDate(2026, 11, 10));
        // A newer build's rule survives an older build's save.
        QCOMPARE(back.unknown.size(), 1);
        QCOMPARE(rules_to_json(back).size(), 4);
    }

    void a_missing_level_is_dropped_not_zero() {
        QJsonArray stored{
            QJsonObject{{"id", "x"}, {"kind", "price_above"}, {"symbol", "AAPL"}},                       // no level
            QJsonObject{{"id", "y"}, {"kind", "price_above"}, {"symbol", "AAPL"}, {"level", QJsonValue()}}, // null
            QJsonObject{{"id", "z"}, {"kind", "day_move"}, {"symbol", "AAPL"}, {"pct", 0}},
            QJsonObject{{"id", "w"}, {"kind", "price_above"}, {"symbol", ""}, {"level", 10}},
            QJsonObject{{"id", ""}, {"kind", "price_above"}, {"symbol", "AAPL"}, {"level", 10}},
            QJsonObject{{"id", "ok"}, {"kind", "price_above"}, {"symbol", "aapl"}, {"level", 10}},
            QJsonObject{{"id", "ok"}, {"kind", "price_below"}, {"symbol", "MSFT"}, {"level", 10}}, // duplicate id
        };
        const RuleSet set = ruleset_from_json(stored, {});
        QCOMPARE(set.rules.size(), 1);
        QCOMPARE(set.rules[0].symbol, QStringLiteral("AAPL"));
        QCOMPARE(set.rules[0].kind, Kind::PriceAbove);
        QVERIFY(set.unknown.isEmpty());
    }

    void a_fired_state_survives_a_restart() {
        // Fire, persist, reload: the reloaded rule must not fire again on the
        // same price (the restart guarantee).
        const Rule r = price_rule(Kind::PriceAbove, 100.0);
        RuleState s;
        step_cross(r, s, 99.0);
        QVERIFY(step_cross(r, s, 101.0).fired);
        RuleSet set;
        set.rules = {r};
        set.states.insert(r.id, s);
        RuleSet back = ruleset_from_json(rules_to_json(set), states_to_json(set));
        RuleState s2 = back.states.value(r.id);
        QVERIFY(!step_cross(back.rules[0], s2, 101.0).fired);

        const Rule m = move_rule(5.0);
        RuleState ms;
        QVERIFY(step_day_move(m, ms, 6.0, 100.0).fired);
        set.rules = {m};
        set.states = {{m.id, ms}};
        back = ruleset_from_json(rules_to_json(set), states_to_json(set));
        RuleState ms2 = back.states.value(m.id);
        QVERIFY(!step_day_move(back.rules[0], ms2, 6.0, 100.0).fired);
    }

    void editing_a_day_move_keeps_its_session_key() {
        const Rule m = move_rule(5.0);
        Rule m2 = m;
        m2.pct = 4.0;
        QVERIFY(!edit_restarts(m, m)); // an unchanged save
        QVERIFY(!edit_restarts(m, m2)); // a new threshold: same session key
        m2.symbol = "MSFT";
        QVERIFY(edit_restarts(m, m2));
        const Rule p = price_rule(Kind::PriceAbove, 100.0);
        Rule p2 = p;
        QVERIFY(!edit_restarts(p, p2));
        p2.level = 101.0;
        QVERIFY(edit_restarts(p, p2)); // the old armed side means nothing at a new level
        p2 = p;
        p2.session = Session::Extended;
        QVERIFY(edit_restarts(p, p2));
    }

    void regular_session_is_new_york_hours() {
        auto at = [](int y, int mo, int d, int h, int mi) {
            return QDateTime(QDate(y, mo, d), QTime(h, mi), QTimeZone("America/New_York")).toUTC();
        };
        QVERIFY(!us_regular_session(at(2026, 11, 13, 9, 29)));
        QVERIFY(us_regular_session(at(2026, 11, 13, 9, 30)));
        QVERIFY(us_regular_session(at(2026, 11, 13, 15, 59)));
        QVERIFY(!us_regular_session(at(2026, 11, 13, 16, 0)));
        QVERIFY(!us_regular_session(at(2026, 11, 14, 12, 0))); // Saturday
        QVERIFY(us_regular_session(at(2026, 7, 1, 10, 0)));    // summer time
    }

    void labels_say_what_the_rule_waits_for() {
        Rule r = price_rule(Kind::PriceAbove, 190.0);
        RuleState s;
        QCOMPARE(state_label(r, s), QStringLiteral("armed · waiting for a price"));
        step_cross(r, s, 180.0);
        QCOMPARE(state_label(r, s), QStringLiteral("armed"));
        step_cross(r, s, 191.0);
        s.last_fired = QDateTime::currentDateTimeUtc();
        QCOMPARE(state_label(r, s), QStringLiteral("fired · re-arms below 189.05"));
        r.paused = true;
        QCOMPARE(state_label(r, s), QStringLiteral("paused"));
        QVERIFY(describe(price_rule(Kind::PriceBelow, 0.5, Session::Extended)).contains("0.5000"));
        QVERIFY(describe(price_rule(Kind::PriceBelow, 0.5, Session::Extended)).contains("extended"));
    }
};

QTEST_GUILESS_MAIN(TestAlertRules)
#include "test_alert_rules.moc"

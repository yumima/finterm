// test_ownership_flags.cpp — the interpretive layer of Ownership.
//
// derive_header() and derive_flags() turn a register into figures and three
// findings, and a finding is much easier to believe than a number. The cases
// that matter most are the negative ones: a flag that fires when its input is
// missing is a fabricated claim about a real company, and a cluster that
// counts a 10% owner or a plan trade is the exact mistake the evidence says
// turns the one real insider signal into noise.

#include <QJsonArray>
#include <QTest>

#include "screens/ownership/OwnershipFlags.h"

using namespace fincept::ownership;

namespace {

const QDate kToday(2026, 9, 6);

OwnershipSnapshot blank() {
    OwnershipSnapshot s;
    s.symbol = QStringLiteral("EXC");
    return s;
}

InsiderTransaction buy(const QString& who, const QDate& when, double value = 100'000.0) {
    InsiderTransaction t;
    t.insider = who;
    t.date = when;
    t.code = QStringLiteral("P");
    t.open_market = true;
    t.acquired = true;
    t.shares = 1000.0;
    t.price = value / 1000.0;
    t.value = value;
    return t;
}

bool has(const QVector<Flag>& flags, FlagKind k) {
    for (const auto& f : flags)
        if (f.kind == k) return true;
    return false;
}

/// A daily close series with one price per calendar day.
QJsonArray closes(const QDate& from, int days, double start, double step) {
    QJsonArray a;
    for (int i = 0; i < days; ++i)
        a.append(QJsonArray{from.addDays(i).toString(Qt::ISODate), start + step * i});
    return a;
}

} // namespace

class TestOwnershipFlags : public QObject {
    Q_OBJECT

private slots:
    // ── nothing from nothing ────────────────────────────────────────────────

    void an_empty_snapshot_produces_no_flags_and_no_figures() {
        const auto s = blank();
        QVERIFY(derive_flags(s, kToday).isEmpty());
        const auto h = derive_header(s, kToday);
        QVERIFY(!h.inst_pct_out);
        QVERIFY(!h.si_pct_float);
        QVERIFY(!h.sirio);
        QVERIFY(!h.top10_pct_out);
        QCOMPARE(h.insider_buys, 0);
    }

    void half_reported_inputs_never_produce_a_figure() {
        // Institutional shares without a share count, a short position
        // without a float — each is a ratio with one side missing.
        OwnershipSnapshot s = blank();
        s.holders_ok = true;
        s.summary.total_shares = 5e8;
        auto h = derive_header(s, kToday);
        QVERIFY2(!h.inst_pct_out, "institutional % needs the vendor's share count");

        OwnershipSnapshot t = blank();
        ShortReading r;
        r.settlement = QDate(2026, 8, 14);
        r.shares_short = 1e7;
        t.short_history.rows.push_back(r);
        h = derive_header(t, kToday);
        QVERIFY2(!h.si_pct_float, "% of float needs the float");
        QVERIFY2(!h.sirio, "SI ÷ 13F needs the 13F shares");
        QVERIFY(derive_flags(t, kToday).isEmpty());
    }

    // ── institutional ownership: computed, both denominators, sane vendor ───

    void institutional_percentages_use_both_denominators() {
        OwnershipSnapshot s = blank();
        s.holders_ok = true;
        s.summary.total_shares = 620.0;
        s.vendor.shares_outstanding = 1000.0;
        s.vendor.float_shares = 960.0;
        s.vendor.held_pct_institutions = 0.664;
        const auto h = derive_header(s, kToday);
        QVERIFY(h.inst_pct_out);
        QCOMPARE(*h.inst_pct_out, 0.62);
        QVERIFY(h.inst_pct_float);
        QVERIFY(std::abs(*h.inst_pct_float - 620.0 / 960.0) < 1e-9);
        QVERIFY(h.vendor_inst_pct);
    }

    void an_impossible_vendor_percentage_is_dropped() {
        // Yahoo's figure is institutions ÷ float in practice and exceeds 1.0
        // on a closely held name. Shown, it destroys trust in every other number.
        OwnershipSnapshot s = blank();
        s.market_ok = true;
        s.vendor.held_pct_institutions = 1.18;
        s.vendor.held_pct_insiders = 0.02;
        const auto h = derive_header(s, kToday);
        QVERIFY(!h.vendor_inst_pct);
        QVERIFY(h.insiders_pct);
    }

    // ── concentration: of the company, not of the value; a level rule ───────

    void concentration_flag_needs_the_share_count_and_fires_at_forty_percent() {
        OwnershipSnapshot s = blank();
        s.holders_ok = true;
        s.summary.holder_count = 300;
        s.summary.top10_share = 0.70;      // of institutional value
        s.summary.total_shares = 600.0;
        // No share count yet: 70% of value is not 70% of the company.
        QVERIFY(!derive_header(s, kToday).top10_pct_out);
        QVERIFY(!has(derive_flags(s, kToday), FlagKind::Concentrated));

        s.vendor.shares_outstanding = 1000.0;   // 0.70 * 0.60 = 42% of the company
        const auto h = derive_header(s, kToday);
        QVERIFY(h.top10_pct_out);
        QVERIFY(std::abs(*h.top10_pct_out - 0.42) < 1e-9);
        const auto flags = derive_flags(s, kToday);
        QVERIFY(has(flags, FlagKind::Concentrated));
        for (const auto& f : flags)
            if (f.kind == FlagKind::Concentrated) {
                QVERIFY(f.detail.contains(QStringLiteral("42%")));
                QVERIFY(f.basis.contains(QStringLiteral("40%")));
                QVERIFY(f.basis.contains(QStringLiteral("not a forecast")));
            }
    }

    void a_mega_cap_dominated_by_index_books_is_not_concentrated() {
        // Apple: the ten largest hold 51% of 13F value, institutions own 62%
        // of the company — 32% of shares out, below the line. The old screen
        // printed 93% for this name from a truncated list.
        OwnershipSnapshot s = blank();
        s.holders_ok = true;
        s.summary.holder_count = 6004;
        s.summary.top10_share = 0.513;
        s.summary.total_shares = 9.0e9;
        s.vendor.shares_outstanding = 14.59e9;
        QVERIFY(!has(derive_flags(s, kToday), FlagKind::Concentrated));
    }

    // ── the short side: levels from FINRA, decile from the ranking ──────────

    void short_constrained_needs_both_levels_or_the_top_decile() {
        OwnershipSnapshot s = blank();
        s.vendor.float_shares = 1e8;
        ShortReading r;
        r.settlement = QDate(2026, 8, 14);
        r.shares_short = 1.5e7;   // 15% of float
        r.days_to_cover = 3.0;    // but liquid
        s.short_history.rows.push_back(r);
        QVERIFY2(!has(derive_flags(s, kToday), FlagKind::ShortConstrained),
                 "15% short with 3 days to cover is heavily shorted, not constrained");

        s.short_history.rows[0].days_to_cover = 6.0;
        auto flags = derive_flags(s, kToday);
        QVERIFY(has(flags, FlagKind::ShortConstrained));
        for (const auto& f : flags)
            if (f.kind == FlagKind::ShortConstrained)
                QCOMPARE(f.as_of, QDate(2026, 8, 14));

        // Alternatively: the ranking places SI ÷ 13F shares in the top decile.
        OwnershipSnapshot t = blank();
        t.holders_ok = true;
        t.summary.total_shares = 1e7;
        ShortReading r2;
        r2.settlement = QDate(2026, 8, 14);
        r2.shares_short = 4e6;
        t.short_history.rows.push_back(r2);
        t.sirio_percentile = 0.95;
        t.sirio_universe = 2800;
        QVERIFY(has(derive_flags(t, kToday), FlagKind::ShortConstrained));
        t.sirio_percentile = 0.80;
        QVERIFY(!has(derive_flags(t, kToday), FlagKind::ShortConstrained));
    }

    void the_newest_finra_reading_is_the_one_used() {
        OwnershipSnapshot s = blank();
        s.vendor.float_shares = 1e8;
        ShortReading newest, older;
        newest.settlement = QDate(2026, 8, 14);
        newest.shares_short = 2e6;
        newest.days_to_cover = 1.2;
        older.settlement = QDate(2026, 7, 31);
        older.shares_short = 3e7;
        older.days_to_cover = 12.0;
        s.short_history.rows = {newest, older};   // newest first, as the service stores it
        const auto h = derive_header(s, kToday);
        QCOMPARE(h.si_settlement, QDate(2026, 8, 14));
        QVERIFY(std::abs(*h.days_to_cover - 1.2) < 1e-9);
        QVERIFY(!has(derive_flags(s, kToday), FlagKind::ShortConstrained));
    }

    void vendor_short_figures_are_a_fallback_only() {
        OwnershipSnapshot s = blank();
        s.market_ok = true;
        s.vendor.short_pct_float = 0.12;
        s.vendor.short_ratio = 7.0;
        s.vendor.short_as_of = QDate(2026, 8, 14);
        auto h = derive_header(s, kToday);
        QVERIFY(h.si_pct_float);
        QCOMPARE(h.si_settlement, QDate(2026, 8, 14));
        QVERIFY(has(derive_flags(s, kToday), FlagKind::ShortConstrained));

        // The moment FINRA has a row, the vendor figure is not consulted.
        ShortReading r;
        r.settlement = QDate(2026, 8, 28);
        r.shares_short = 1e6;
        r.days_to_cover = 1.0;
        s.vendor.float_shares = 1e8;
        s.short_history.rows.push_back(r);
        h = derive_header(s, kToday);
        QCOMPARE(h.si_settlement, QDate(2026, 8, 28));
        QVERIFY(std::abs(*h.si_pct_float - 0.01) < 1e-9);
    }

    // ── insiders: clusters, and the two exclusions ──────────────────────────

    void two_distinct_buyers_inside_a_month_are_a_cluster() {
        OwnershipSnapshot s = blank();
        s.edgar_ok = true;
        s.transactions = {buy(QStringLiteral("A"), QDate(2026, 8, 3)),
                          buy(QStringLiteral("B"), QDate(2026, 8, 20))};
        const auto clusters = find_cluster_buys(s.transactions);
        QCOMPARE(clusters.size(), 1);
        QCOMPARE(clusters[0].insiders.size(), 2);
        QCOMPARE(clusters[0].start, QDate(2026, 8, 3));
        QCOMPARE(clusters[0].end, QDate(2026, 8, 20));
        QCOMPARE(clusters[0].total_value, 200'000.0);
        const auto flags = derive_flags(s, kToday);
        QVERIFY(has(flags, FlagKind::InsiderClusterBuy));
        QCOMPARE(flags.first().as_of, QDate(2026, 8, 20));
    }

    void the_same_person_buying_twice_is_not_a_cluster() {
        QVector<InsiderTransaction> tx = {buy(QStringLiteral("A"), QDate(2026, 8, 3)),
                                          buy(QStringLiteral("A"), QDate(2026, 8, 10))};
        QVERIFY(find_cluster_buys(tx).isEmpty());
    }

    void buys_more_than_a_month_apart_are_not_a_cluster() {
        QVector<InsiderTransaction> tx = {buy(QStringLiteral("A"), QDate(2026, 6, 1)),
                                          buy(QStringLiteral("B"), QDate(2026, 7, 15))};
        QVERIFY(find_cluster_buys(tx).isEmpty());
        // …but a chain of buys each within the window is one cluster.
        tx.push_back(buy(QStringLiteral("C"), QDate(2026, 6, 25)));
        const auto c = find_cluster_buys(tx);
        QCOMPARE(c.size(), 1);
        QCOMPARE(c[0].insiders.size(), 3);
    }

    void ten_percent_owners_and_plan_trades_never_count_toward_a_cluster() {
        auto owner = buy(QStringLiteral("HoldCo"), QDate(2026, 8, 5));
        owner.ten_percent_owner = true;
        auto plan = buy(QStringLiteral("CFO"), QDate(2026, 8, 6));
        plan.plan_10b5_1 = true;
        QVector<InsiderTransaction> tx = {owner, plan, buy(QStringLiteral("Director"), QDate(2026, 8, 7))};
        QVERIFY2(find_cluster_buys(tx).isEmpty(),
                 "one real buyer plus a 10% owner plus a plan trade is not two insiders");
        // The pre-2023 filing with no checkbox at all is unknown, and unknown
        // is not excluded — otherwise every historical cluster disappears.
        auto old = buy(QStringLiteral("Chair"), QDate(2026, 8, 8));
        old.plan_10b5_1 = std::nullopt;
        tx.push_back(old);
        QCOMPARE(find_cluster_buys(tx).size(), 1);
    }

    void sells_grants_and_derivatives_are_never_buys() {
        auto sell = buy(QStringLiteral("A"), QDate(2026, 8, 3));
        sell.acquired = false;
        auto grant = buy(QStringLiteral("B"), QDate(2026, 8, 4));
        grant.open_market = false;
        grant.code = QStringLiteral("A");
        auto option = buy(QStringLiteral("C"), QDate(2026, 8, 5));
        option.derivative = true;
        QVector<InsiderTransaction> tx = {sell, grant, option};
        QVERIFY(find_cluster_buys(tx).isEmpty());
        OwnershipSnapshot s = blank();
        s.edgar_ok = true;
        s.transactions = tx;
        const auto h = derive_header(s, kToday);
        QCOMPARE(h.insider_buys, 0);
        QCOMPARE(h.insider_sells, 1);
    }

    void header_counts_only_the_six_month_window_and_scorable_buys() {
        OwnershipSnapshot s = blank();
        s.edgar_ok = true;
        auto stale = buy(QStringLiteral("A"), kToday.addMonths(-8));
        auto fresh = buy(QStringLiteral("B"), kToday.addMonths(-1), 50'000.0);
        auto owner = buy(QStringLiteral("HoldCo"), kToday.addDays(-3), 9'000'000.0);
        owner.ten_percent_owner = true;
        s.transactions = {stale, fresh, owner};
        const auto h = derive_header(s, kToday);
        QCOMPARE(h.insider_buys, 1);
        QCOMPARE(h.insider_buy_value, 50'000.0);
    }

    // ── activists ───────────────────────────────────────────────────────────

    void activist_filings_are_counted_and_dated_passive_ones_are_not() {
        OwnershipSnapshot s = blank();
        BeneficialStake d, g;
        d.form = QStringLiteral("SC 13D");
        d.activist = true;
        d.filed_date = QDate(2026, 5, 2);
        g.form = QStringLiteral("SC 13G");
        g.filed_date = QDate(2026, 7, 9);
        s.stakes = {d, g};
        const auto h = derive_header(s, kToday);
        QCOMPARE(h.activist_filings, 1);
        QCOMPARE(h.latest_activist, QDate(2026, 5, 2));
    }

    // ── forward returns: after the fact, never before ───────────────────────

    void forward_returns_are_absent_until_the_window_has_elapsed() {
        const QDate trade(2026, 8, 20);
        const auto series = closes(QDate(2026, 8, 1), 60, 100.0, 1.0);   // +1/day
        // Today is 17 days after the trade: a week has elapsed, a month has not.
        const auto r = forward_returns(series, trade, kToday);
        QVERIFY(r.r1w);
        QVERIFY(std::abs(*r.r1w - 7.0 / 119.0) < 1e-9);   // 126/119 - 1
        QVERIFY2(!r.r1m, "the one-month window ends after today");
        QVERIFY(!r.r3m);
    }

    void forward_returns_use_the_last_close_on_or_before_each_mark() {
        const QDate trade(2026, 8, 22);   // a Saturday: no close that day
        QJsonArray series;
        series.append(QJsonArray{QStringLiteral("2026-08-21"), 50.0});
        series.append(QJsonArray{QStringLiteral("2026-08-28"), 55.0});
        series.append(QJsonArray{QStringLiteral("2026-09-01"), 60.0});
        const auto r = forward_returns(series, trade, kToday);
        QVERIFY(r.r1w);
        QVERIFY(std::abs(*r.r1w - 0.10) < 1e-9);   // 55 (28 Aug, on-or-before 29 Aug) over 50
    }

    void forward_returns_need_a_base_close() {
        const auto r = forward_returns(closes(QDate(2026, 9, 1), 5, 10.0, 0.0), QDate(2026, 8, 1), kToday);
        QVERIFY(!r.r1w);
        QVERIFY(!r.r1m);
    }
};

QTEST_APPLESS_MAIN(TestOwnershipFlags)
#include "test_ownership_flags.moc"

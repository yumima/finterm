// test_ownership_signals.cpp — the interpretive layer of the OWNERSHIP screen.
//
// derive_reads() is where a research tool is most tempted to lie: it turns a
// table into a sentence, and a sentence is much easier to believe than a
// number. The tests that matter most here are the negative ones — a read that
// appears when its input is missing is a fabricated claim about a real company.

#include <QJsonArray>
#include <QTest>

#include "screens/ownership/OwnershipSignals.h"

using namespace fincept::ownership;

namespace {

/// A snapshot with nothing reported. Every test starts here and adds only the
/// one field it is exercising, so any read that appears is provably caused by
/// that field.
OwnershipSnapshot blank() {
    OwnershipSnapshot s;
    s.symbol = QStringLiteral("EXC");
    return s;
}

bool has_headline(const QVector<Read>& reads, const QString& needle) {
    for (const auto& r : reads)
        if (r.headline.contains(needle, Qt::CaseInsensitive)) return true;
    return false;
}

Read find(const QVector<Read>& reads, const QString& needle) {
    for (const auto& r : reads)
        if (r.headline.contains(needle, Qt::CaseInsensitive)) return r;
    return {};
}

InstitutionalHolder holder(const QString& name, double value) {
    InstitutionalHolder h;
    h.holder = name;
    h.value = value;
    return h;
}

} // namespace

class TestOwnershipSignals : public QObject {
    Q_OBJECT

private slots:
    // ── the rule that matters most ───────────────────────────────────────────

    void an_empty_snapshot_produces_no_reads() {
        QVERIFY(derive_reads(blank()).isEmpty());
    }

    void missing_inputs_never_produce_a_read() {
        // Each of these is a partially-reported snapshot: the provider gave one
        // half of a ratio and not the other. Deriving anything here would mean
        // assuming the missing half.
        OwnershipSnapshot s = blank();
        s.shorts.held_pct_institutions = 0.95;   // no insider figure
        QVERIFY2(!has_headline(derive_reads(s), QStringLiteral("float")),
                 "closely-held read needs BOTH institutional and insider percentages");

        OwnershipSnapshot t = blank();
        t.shorts.shares_short = 5'000'000.0;     // no prior month to compare
        QVERIFY(!has_headline(derive_reads(t), QStringLiteral("Shorts")));

        OwnershipSnapshot u = blank();
        u.shorts.shares_short_prior = 4'000'000.0;  // no current
        QVERIFY(!has_headline(derive_reads(u), QStringLiteral("Shorts")));
    }

    void a_zero_prior_does_not_divide() {
        OwnershipSnapshot s = blank();
        s.shorts.shares_short = 1'000'000.0;
        s.shorts.shares_short_prior = 0.0;
        const auto reads = derive_reads(s); // must not produce inf/nan
        QVERIFY(!has_headline(reads, QStringLiteral("Shorts")));
    }

    // ── every read carries its number and its rule ───────────────────────────

    void every_read_states_a_number_and_a_threshold() {
        OwnershipSnapshot s = blank();
        s.shorts.held_pct_institutions = 0.72;
        s.shorts.held_pct_insiders = 0.15;
        s.shorts.short_ratio = 8.0;
        s.shorts.pct_float = 0.13;
        const auto reads = derive_reads(s);
        QVERIFY(!reads.isEmpty());
        for (const auto& r : reads) {
            QVERIFY2(!r.headline.isEmpty(), "read without a headline");
            QVERIFY2(!r.basis.isEmpty(), qPrintable(r.headline + ": no basis stated"));
            QVERIFY2(r.detail.contains(QRegularExpression(QStringLiteral("[0-9]"))),
                     qPrintable(r.headline + ": detail carries no number"));
        }
    }

    // ── float and register ───────────────────────────────────────────────────

    void a_closely_held_register_is_flagged() {
        OwnershipSnapshot s = blank();
        s.shorts.held_pct_institutions = 0.70;
        s.shorts.held_pct_insiders = 0.16;   // 86% combined, over the 85% rule
        const Read r = find(derive_reads(s), QStringLiteral("float"));
        QVERIFY(!r.headline.isEmpty());
        QCOMPARE(r.weight, Weight::Elevated);
        QVERIFY(r.detail.contains(QStringLiteral("86.0%")));
    }

    void incoherent_provider_percentages_produce_no_read() {
        // Yahoo's heldPercentInstitutions is institutions/float in practice, so
        // on a closely-held small cap it exceeds 1.0 on its own. Summing it
        // with the insider figure yields "118% of shares outstanding" — an
        // impossible number, and printing it as an Elevated finding would
        // discredit every other read on the screen.
        OwnershipSnapshot s = blank();
        s.shorts.held_pct_institutions = 1.12;
        s.shorts.held_pct_insiders = 0.06;
        const auto reads = derive_reads(s);
        QVERIFY2(!has_headline(reads, QStringLiteral("float")),
                 "a sum above 100% of shares outstanding must be refused");
        QVERIFY2(!has_headline(reads, QStringLiteral("Institutionally owned")),
                 "an institutional percentage above 100% is not a finding");
    }

    void a_normal_register_is_not_flagged_as_tight() {
        OwnershipSnapshot s = blank();
        s.shorts.held_pct_institutions = 0.55;
        s.shorts.held_pct_insiders = 0.02;
        QVERIFY(!has_headline(derive_reads(s), QStringLiteral("float")));
    }

    void concentration_needs_more_rows_than_it_measures() {
        // With exactly five reported holders, "top five / all reported" is 100%
        // by construction and measures nothing. The earlier version of this
        // test passed for that reason — it was pinning the bug, not the rule.
        OwnershipSnapshot s = blank();
        s.holders = {holder("A", 90.0), holder("B", 5.0), holder("C", 3.0),
                     holder("D", 2.0), holder("E", 1.0)};
        QVERIFY2(!has_headline(derive_reads(s), QStringLiteral("Concentrated")),
                 "five rows cannot evidence concentration among five rows");

        // Eight rows, genuinely top-heavy: the top five are 98 of 101.
        for (const auto& h : {holder("F", 1.0), holder("G", 1.0), holder("H", 1.0)})
            s.holders.push_back(h);
        QVERIFY(has_headline(derive_reads(s), QStringLiteral("Concentrated")));

        // Eight rows, evenly spread: top five are 5/8, still over the 50% rule
        // but the read must state the denominator it actually used.
        OwnershipSnapshot even = blank();
        for (int i = 0; i < 8; ++i)
            even.holders.push_back(holder(QString("H%1").arg(i), 10.0));
        const Read r = find(derive_reads(even), QStringLiteral("Concentrated"));
        QVERIFY(!r.headline.isEmpty());
        QVERIFY2(r.detail.contains(QStringLiteral("of 8 holders")),
                 qPrintable(r.detail));
        QVERIFY2(r.basis.contains(QStringLiteral("top-N list")),
                 "with only a vendor holder table the basis must admit the denominator "
                 "is truncated; the measured version says so instead");
    }

    void concentration_does_not_assume_the_rows_arrive_sorted() {
        // The provider returns an unordered top-N. Reading "the five largest"
        // off row position would measure five arbitrary holders.
        OwnershipSnapshot ascending = blank();
        const QVector<double> vals = {1.0, 1.0, 1.0, 2.0, 3.0, 5.0, 40.0, 47.0};
        for (int i = 0; i < vals.size(); ++i)
            ascending.holders.push_back(holder(QString("H%1").arg(i), vals[i]));
        const Read r = find(derive_reads(ascending), QStringLiteral("Concentrated"));
        QVERIFY2(!r.headline.isEmpty(),
                 "top five by VALUE is 47+40+5+3+2 = 97 of 100 — concentrated "
                 "regardless of the order the rows arrived in");
        QVERIFY(r.detail.contains(QStringLiteral("97.0%")));
    }

    void index_complex_weight_is_recognised_by_name() {
        QVERIFY(is_index_complex(QStringLiteral("Blackrock Inc.")));
        QVERIFY(is_index_complex(QStringLiteral("Vanguard Group Inc")));
        QVERIFY(is_index_complex(QStringLiteral("State Street Corporation")));
        QVERIFY(is_index_complex(QStringLiteral("Geode Capital Management, LLC")));
        // A stock-picking house must not be counted as index money.
        QVERIFY(!is_index_complex(QStringLiteral("Baupost Group LLC")));
        QVERIFY(!is_index_complex(QStringLiteral("Berkshire Hathaway Inc")));
    }

    void an_index_heavy_register_reads_as_flow_driven() {
        OwnershipSnapshot s = blank();
        s.holders = {holder("Blackrock Inc.", 40.0), holder("Vanguard Group Inc", 30.0),
                     holder("Baupost Group LLC", 20.0), holder("Some Fund LP", 10.0)};
        const Read r = find(derive_reads(s), QStringLiteral("index flow"));
        QVERIFY(!r.headline.isEmpty());
        QCOMPARE(r.lens, Lens::Flows);
        QVERIFY(r.detail.contains(QStringLiteral("70.0%")));
        // The honesty caveat must travel with it.
        QVERIFY2(r.basis.contains(QStringLiteral("active mandates")),
                 "the name-list fallback must admit those houses run active mandates too");
    }

    void index_weight_is_measured_from_book_breadth_when_available() {
        // The name list cannot see an index arm under an unfamiliar name, and
        // can mislabel a concentrated manager sharing a word with one. Breadth
        // is measurable from the filings: nobody holds a view on 40,000 names.
        QVERIFY(is_broad_book(49751));
        QVERIFY(is_broad_book(kBroadBookPositions));
        QVERIFY(!is_broad_book(kBroadBookPositions - 1));
        QVERIFY(!is_broad_book(29));   // Berkshire files about this many

        OwnershipSnapshot s = blank();
        auto pos = [](const QString& name, double value, int count) {
            ManagerPosition p;
            p.manager = name;
            p.value = value;
            p.position_count = count;
            return p;
        };
        // A firm the name list has never heard of, running a 12,000-name book.
        s.smart_money = {pos("Obscure Model Portfolios LLC", 70.0, 12000),
                         pos("Baupost Group LLC", 30.0, 40)};
        const Read r = find(derive_reads(s), QStringLiteral("index flow"));
        QVERIFY2(!r.headline.isEmpty(), "breadth must catch what the name list cannot");
        QVERIFY(r.detail.contains(QStringLiteral("70.0%")));
        QVERIFY2(r.basis.contains(QStringLiteral("not matched against a list")),
                 "the basis must say the signal is measured");
    }

    void option_only_holders_do_not_count_toward_index_weight() {
        // A put is a bearish position and is not a holding.
        OwnershipSnapshot s = blank();
        ManagerPosition broad;
        broad.manager = "Wide Index Book";
        broad.value = 90.0;
        broad.position_count = 9000;
        broad.is_derivative = true;      // options only
        broad.put_call = "PUT";
        ManagerPosition picker;
        picker.manager = "Concentrated LP";
        picker.value = 10.0;
        picker.position_count = 25;
        s.smart_money = {broad, picker};
        QVERIFY2(!has_headline(derive_reads(s), QStringLiteral("index flow")),
                 "an option line must not be counted as index-money weight");
    }

    void a_stock_picker_register_is_not_called_flow_driven() {
        OwnershipSnapshot s = blank();
        s.holders = {holder("Baupost Group LLC", 50.0), holder("Berkshire Hathaway Inc", 30.0),
                     holder("Some Fund LP", 20.0)};
        QVERIFY(!has_headline(derive_reads(s), QStringLiteral("index flow")));
    }

    // ── the short side ───────────────────────────────────────────────────────

    void days_to_cover_escalates_at_the_stated_levels() {
        OwnershipSnapshot low = blank();
        low.shorts.short_ratio = 3.0;
        QVERIFY(!has_headline(derive_reads(low), QStringLiteral("Crowded short")));

        OwnershipSnapshot mid = blank();
        mid.shorts.short_ratio = 6.0;
        QCOMPARE(find(derive_reads(mid), QStringLiteral("Crowded short")).weight, Weight::Notable);

        OwnershipSnapshot high = blank();
        high.shorts.short_ratio = 12.0;
        QCOMPARE(find(derive_reads(high), QStringLiteral("Crowded short")).weight, Weight::Elevated);
    }

    void a_short_interest_move_reports_its_direction() {
        OwnershipSnapshot up = blank();
        up.shorts.shares_short = 6'000'000.0;
        up.shorts.shares_short_prior = 4'000'000.0;   // +50%
        QVERIFY(has_headline(derive_reads(up), QStringLiteral("Shorts building")));

        OwnershipSnapshot down = blank();
        down.shorts.shares_short = 2'000'000.0;
        down.shorts.shares_short_prior = 4'000'000.0; // -50%
        QVERIFY(has_headline(derive_reads(down), QStringLiteral("Shorts covering")));

        OwnershipSnapshot flat = blank();
        flat.shorts.shares_short = 4'100'000.0;
        flat.shorts.shares_short_prior = 4'000'000.0; // +2.5%, under the rule
        QVERIFY(!has_headline(derive_reads(flat), QStringLiteral("Shorts")));
    }

    // ── insiders and activists ───────────────────────────────────────────────

    void a_cluster_buy_names_the_participants_and_excludes_grants() {
        OwnershipSnapshot s = blank();
        BuyCluster c;
        c.start = QDate(2026, 5, 1);
        c.end = QDate(2026, 5, 20);
        c.insiders = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
        c.total_value = 2'500'000.0;
        s.clusters.push_back(c);
        const Read r = find(derive_reads(s), QStringLiteral("cluster"));
        QCOMPARE(r.weight, Weight::Elevated);
        QVERIFY(r.detail.contains(QStringLiteral("3 insiders")));
        QVERIFY(r.basis.contains(QStringLiteral("code P")));
        QVERIFY2(r.basis.contains(QStringLiteral("Grants")),
                 "the basis must say grants are excluded — that is the whole distinction");
    }

    void unclassified_insiders_do_not_count_as_opportunistic() {
        OwnershipSnapshot s = blank();
        InsiderProfile a; a.insider = "A"; a.pattern = Pattern::Unclassified;
        InsiderProfile b; b.insider = "B"; b.pattern = Pattern::Routine;
        s.insiders = {a, b};
        QVERIFY(!has_headline(derive_reads(s), QStringLiteral("Opportunistic")));

        InsiderProfile c; c.insider = "C"; c.pattern = Pattern::Opportunistic;
        s.insiders.push_back(c);
        QVERIFY(has_headline(derive_reads(s), QStringLiteral("Opportunistic")));
    }

    void only_13d_counts_as_activist() {
        OwnershipSnapshot s = blank();
        BeneficialStake passive;
        passive.form = QStringLiteral("SC 13G");
        passive.activist = false;
        passive.filed_date = QDate(2026, 2, 10);
        s.stakes.push_back(passive);
        QVERIFY2(!has_headline(derive_reads(s), QStringLiteral("Activist")),
                 "13G is the passive schedule and must not read as activist intent");

        BeneficialStake activist;
        activist.form = QStringLiteral("SC 13D");
        activist.activist = true;
        activist.filed_date = QDate(2026, 6, 1);
        s.stakes.push_back(activist);
        const Read r = find(derive_reads(s), QStringLiteral("Activist"));
        QCOMPARE(r.weight, Weight::Elevated);
        QVERIFY(r.detail.contains(QStringLiteral("1 Jun 2026")));
    }

    // ── presentation contract ────────────────────────────────────────────────

    void institutional_ownership_is_computed_and_cross_checked() {
        OwnershipSnapshot s = blank();
        s.index_shares_held = 9'012'622'443.0;
        s.shorts.shares_outstanding = 14'594'180'000.0;
        s.holder_universe = 7850;
        s.index_quarter = QDate(2026, 3, 31);
        s.vendor_quarter = QDate(2026, 6, 30);
        s.shorts.held_pct_institutions = 0.6648;

        const Read r = find(derive_reads(s), QStringLiteral("Institutional ownership"));
        QVERIFY(!r.headline.isEmpty());
        QVERIFY2(r.detail.contains(QStringLiteral("61.8%")),
                 qPrintable(r.detail));   // computed, not the vendor's 66.5%
        QVERIFY2(r.detail.contains(QStringLiteral("7850")),
                 "the number of filings behind it is what makes it auditable");
        QVERIFY2(r.detail.contains(QStringLiteral("66.5%")),
                 "the vendor's number must be shown beside it, not hidden");
        QVERIFY2(r.basis.contains(QStringLiteral("trails the shallow one by a quarter")),
                 "a stale complete source must say so when a fresher one exists");
        QVERIFY2(r.basis.contains(QStringLiteral("floor")),
                 "sub-threshold filers do not appear, so it is a floor");
    }

    void institutional_ownership_needs_both_inputs() {
        OwnershipSnapshot a = blank();
        a.index_shares_held = 1'000'000.0;      // no shares outstanding
        QVERIFY(!has_headline(derive_reads(a), QStringLiteral("Institutional ownership")));

        OwnershipSnapshot b = blank();
        b.shorts.shares_outstanding = 1'000'000.0;  // nothing indexed
        QVERIFY(!has_headline(derive_reads(b), QStringLiteral("Institutional ownership")));
    }

    void reads_are_ordered_by_weight() {
        OwnershipSnapshot s = blank();
        s.shorts.held_pct_institutions = 0.75;      // Notable
        s.shorts.short_ratio = 12.0;                // Elevated
        InsiderProfile p; p.insider = "A"; p.pattern = Pattern::Opportunistic;
        s.insiders = {p};                           // Context
        const auto reads = derive_reads(s);
        QVERIFY(reads.size() >= 3);
        for (int i = 1; i < reads.size(); ++i)
            QVERIFY2(static_cast<int>(reads[i - 1].weight) >= static_cast<int>(reads[i].weight),
                     "reads must be ordered strongest first");
    }

    void the_two_lenses_partition_the_reads() {
        OwnershipSnapshot s = blank();
        s.shorts.held_pct_institutions = 0.75;
        s.shorts.short_ratio = 12.0;
        s.shorts.shares_short = 6'000'000.0;
        s.shorts.shares_short_prior = 4'000'000.0;
        const auto all = derive_reads(s);
        const auto stock = reads_for(all, Lens::Stock);
        const auto flows = reads_for(all, Lens::Flows);
        QCOMPARE(stock.size() + flows.size(), all.size());
        QVERIFY(!stock.isEmpty());
        QVERIFY(!flows.isEmpty());
    }

    // ── Price marks ─────────────────────────────────────────────────────────
    //
    // These back the 13F book's return columns. A missing mark must stay
    // missing: a mark silently read as zero renders a -100% return on a real
    // company's real position.

    /// A close series in the daemon's shape: [["YYYY-MM-DD", close], ...].
    static QJsonArray series(const QVector<QPair<const char*, double>>& rows) {
        QJsonArray a;
        for (const auto& r : rows)
            a.append(QJsonArray{QString::fromLatin1(r.first), r.second});
        return a;
    }

    // Quarter ends and 3/6-month anniversaries land on non-trading days often
    // enough that an exact-date lookup would drop the mark and the return with
    // it. The last trade at or before the date is the honest mark.
    void a_mark_lands_on_the_last_trade_before_the_date() {
        const auto s = series({{"2026-03-27", 100.0}, {"2026-03-30", 101.0},
                               {"2026-04-01", 109.0}});
        const auto px = close_on_or_before(s, QDate(2026, 3, 31));
        QVERIFY(px.has_value());
        QCOMPARE(*px, 101.0);   // the Monday close, never the April one
    }

    // The payload arrives ascending today, but the only other consumer of this
    // shape sorts before use, so ordering is not something to rely on.
    void an_unordered_series_still_yields_the_latest_mark() {
        const auto s = series({{"2026-03-30", 101.0}, {"2026-03-20", 90.0},
                               {"2026-03-27", 100.0}});
        const auto px = close_on_or_before(s, QDate(2026, 3, 31));
        QVERIFY(px.has_value());
        QCOMPARE(*px, 101.0);
    }

    void a_series_that_starts_too_late_yields_nothing() {
        const auto s = series({{"2026-05-01", 120.0}});
        QVERIFY(!close_on_or_before(s, QDate(2026, 3, 31)).has_value());
    }

    void a_malformed_row_is_skipped_not_read_as_zero() {
        QJsonArray s = series({{"2026-03-30", 101.0}});
        s.append(QJsonArray{QStringLiteral("not-a-date"), 5.0});
        s.append(QJsonArray{QStringLiteral("2026-03-31")});   // no close at all
        const auto px = close_on_or_before(s, QDate(2026, 3, 31));
        QVERIFY(px.has_value());
        QCOMPARE(*px, 101.0);
    }

    // The batched form exists only as a speed optimisation, so its answers must
    // be indistinguishable from the one-at-a-time lookup it replaced.
    void one_pass_agrees_with_the_single_date_lookup() {
        const auto s = series({{"2025-09-15", 80.0}, {"2026-02-27", 95.0},
                               {"2026-03-30", 101.0}, {"2026-05-29", 110.0},
                               {"2026-08-14", 130.0}});
        const QVector<QDate> marks{QDate(2026, 3, 31), QDate(2026, 8, 18),
                                   QDate(2026, 5, 31), QDate(2026, 2, 28),
                                   QDate(2020, 1, 1)};
        const auto batch = closes_on_or_before(s, marks);
        QCOMPARE(batch.size(), marks.size());
        for (int i = 0; i < marks.size(); ++i) {
            const auto one = close_on_or_before(s, marks[i]);
            QCOMPARE(batch[i].has_value(), one.has_value());
            if (one)
                QCOMPARE(*batch[i], *one);
        }
        QVERIFY(!batch.last().has_value());   // predates the series entirely
    }
};


QTEST_APPLESS_MAIN(TestOwnershipSignals)
#include "test_ownership_signals.moc"

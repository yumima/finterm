// test_ownership_signals.cpp — the interpretive layer of the OWNERSHIP screen.
//
// derive_reads() is where a research tool is most tempted to lie: it turns a
// table into a sentence, and a sentence is much easier to believe than a
// number. The tests that matter most here are the negative ones — a read that
// appears when its input is missing is a fabricated claim about a real company.

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
        QVERIFY2(r.detail.contains(QStringLiteral("of the 8 reported holders")),
                 qPrintable(r.detail));
        QVERIFY2(r.basis.contains(QStringLiteral("top-N list")),
                 "the basis must admit the denominator is a truncated list");
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
};

QTEST_APPLESS_MAIN(TestOwnershipSignals)
#include "test_ownership_signals.moc"

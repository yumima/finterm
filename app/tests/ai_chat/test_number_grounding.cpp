// test_number_grounding.cpp — a model may quote a figure, never originate one.
//
// Two directions are pinned, and they cost differently. A FALSE PASS lets an
// invented number reach the user as if it were data — the thing this exists to
// stop. A FALSE REMOVAL deletes a true figure the model copied faithfully,
// which makes every answer look broken and gets the guard switched off. Both
// are failures; the cases below hold the line on each.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "ai_chat/NumberGrounding.h"

using namespace finterm::ai_chat::grounding;

namespace {
const int kYear = 2026;

Result run(const QString& evidence, const QString& reply) {
    Evidence ev(kYear);
    ev.add(evidence);
    return enforce(reply, ev, kYear);
}
} // namespace

class TestNumberGrounding : public QObject {
    Q_OBJECT
  private slots:
    // ── Faithful quoting survives, however it is written ─────────────────────
    void quoted_as_given() {
        const auto r = run("price 150.25, change_pct -3.2, volume 1234567",
                           "It closed at $150.25, down 3.2%, on volume of 1,234,567.");
        QCOMPARE(r.removed, QStringList{});
        QCOMPARE(r.checked, 3);
    }
    void rounded_to_stated_digits() {
        const auto r = run("marketCap 1234567890 price 150.37 margin 0.1534",
                           "Market cap is $1.2B (1,234.6M), the price about $150 and the margin 15.3%.");
        QCOMPARE(r.removed, QStringList{});
    }
    void scale_words_and_filing_units() {
        // "in millions" tables: the evidence holds 4,512 for $4.5 billion.
        const auto r = run("Revenue (in millions): 4,512", "Revenue was $4.5 billion.");
        QCOMPARE(r.removed, QStringList{});
    }
    void rounding_is_not_licence() {
        // 19.2 is not "20%": a looser match would also pass an invented 20.
        const auto r = run("growth 19.2", "Growth was nearly 20%.");
        QCOMPARE(r.removed, QStringList{"20%"});
        QVERIFY(r.text.contains(kMarker));
        QVERIFY(!r.text.contains("20"));
    }

    // ── Invention is removed ─────────────────────────────────────────────────
    void invented_figures_removed() {
        const auto r = run("price 150.25", "It trades at $150.25; my target is $185.00, about 23% upside, "
                                           "with revenue of $4.2 billion expected.");
        QCOMPARE(r.removed, (QStringList{"$185.00", "23%", "$4.2 billion"}));
        QVERIFY(r.text.contains("$150.25"));
        QVERIFY(!r.text.contains("185"));
        QVERIFY(!r.text.contains("4.2"));
    }
    void no_evidence_means_no_figures() {
        const auto r = run("", "The stock usually returns 12.5% a year and trades near $48.");
        QCOMPARE(r.removed.size(), 2);
    }
    void future_year_is_a_claim() {
        const auto r = run("founded 1998", "Founded in 1998, it survived 2008 and expects profit by 2029.");
        QCOMPARE(r.removed, QStringList{"2029"});
    }

    // ── Shown arithmetic is verified, not trusted ────────────────────────────
    void derivation_with_operands_passes() {
        const auto r = run("open 120 close 150", "It rose from $120 to $150, a 25% gain of $30.");
        QCOMPARE(r.removed, QStringList{});
    }
    void wrong_arithmetic_is_removed() {
        const auto r = run("open 120 close 150", "It rose from $120 to $150, a 40% gain.");
        QCOMPARE(r.removed, QStringList{"40%"});
    }
    void derivation_needs_operands_in_the_sentence() {
        // 25% IS (150-120)/120, but the inputs are not shown beside it: with a
        // whole tool result as evidence, almost any number is the ratio of two
        // values somewhere, so an unshown derivation proves nothing.
        QCOMPARE(run("open 120 close 150", "Momentum is strong.\n\nThe gain was 25%.").removed, QStringList{"25%"});
        QCOMPARE(run("open 120 close 150", "It went from $120 to $150. The gain was 30%.").removed,
                 QStringList{"30%"});
    }
    void unverified_operands_derive_nothing() {
        // A list marker, "5 reasons" and a year were never checked; letting
        // them act as operands would make 40% (2/5) and $300 (150×2) "derived".
        const auto r = run("price 150", "2. There are 5 reasons it trades at $150, a 40% gain to $300 since 2019.");
        QCOMPARE(r.removed, (QStringList{"40%", "$300"}));
    }
    void user_arithmetic() {
        const auto r = run("what is 15% of 240?", "15% of 240 is 36.");
        QCOMPARE(r.removed, QStringList{});
    }

    // ── Not figures ──────────────────────────────────────────────────────────
    void names_identifiers_and_structure() {
        const auto r = run("", "1. Read the 10-K and the S-1.\n2. Compare Q3 with the S&P 500 and Russell 2000.\n"
                               "12. Check Form 4 and 13F filings under Rule 144.\nThere are 3 reasons, since 2019.");
        QCOMPARE(r.removed, QStringList{});
        QCOMPARE(r.checked, 0);
    }
    void code_is_left_alone() {
        const auto r = run("", "Use `df.rolling(20).mean()`:\n```python\nx = 42 * 1.5\n```\nDone.");
        QCOMPARE(r.removed, QStringList{});
    }
    void range_shares_its_unit() {
        // "5" would be an exempt small integer on its own; in "5-10%" it is a
        // percentage and must be grounded like one.
        const auto r = run("", "Expect a move of 5-10% next week.");
        QCOMPARE(r.removed.size(), 2);
    }
    void small_integer_with_a_unit_is_checked() {
        const auto r = run("", "Yield is 4% and it costs $7.");
        QCOMPARE(r.removed.size(), 2);
    }

    // ── Loopholes: a figure dressed up as something else is still a figure ───
    void lowercase_and_unknown_units_are_checked() {
        const auto r = run("", "Revenue of $900m, EBITDA $2.5bn, a 150USD price, 25bp wider and up 30pts.");
        QCOMPARE(r.removed.size(), 5);
        QCOMPARE(run("revenue 900000000", "Revenue of $900m.").removed, QStringList{});
    }
    void backticks_do_not_launder_a_figure() {
        const auto r = run("", "Target `$250`, EPS `3.41`.\n```\n| Year | Sales |\n| A | 9,400 |\n```\n");
        QCOMPARE(r.removed.size(), 3);
    }
    void an_index_name_is_not_an_index_level() {
        const auto r = run("", "The S&P 500 reaches S&P 7,500 and Dow 52,000; support level 182.50, window 30.");
        QCOMPARE(r.removed, (QStringList{"7,500", "52,000", "182.50", "30"}));
    }
    void a_date_does_not_ground_an_amount() {
        // Today's date and every bar timestamp are in the evidence; "15" as a
        // day of the month must not make "15%" or "$11M" traceable.
        const QString ev = "Today is 2026-11-15. Bars: 2026-11-13T16:00 close 142.37";
        QCOMPARE(run(ev, "Up 15% to $11M by 16:00 on 2026-11-13.").removed, (QStringList{"15%", "$11M"}));
        QCOMPARE(run(ev, "As of 2026-11-15 it closed at 142.37.").removed, QStringList{});
    }
    void a_stray_digit_does_not_ground_a_scaled_amount() {
        QCOMPARE(run("3 directors bought", "Revenue was $3 billion.").removed, QStringList{"$3 billion"});
    }
    void the_directive_carries_no_figures() {
        // It is part of every request's evidence; a digit in it would ground
        // that digit in every reply.
        QCOMPARE(extract(kDirective, kYear, true).size(), 0);
    }

    // ── JSON stays parseable ─────────────────────────────────────────────────
    void json_array_stays_parseable() {
        const auto r = run("", R"(["Compute the 50-day and 200-day averages", "Compare them"])");
        QVERIFY(r.json);
        QVERIFY(QJsonDocument::fromJson(r.text.toUtf8()).isArray());
        QCOMPARE(r.removed.size(), 2);
    }
    void json_numbers_become_null() {
        const auto r = run("price 150.25",
                           R"({"direction":"up","target_price":185.5,"last":150.25,"why":"From $150.25 to $185.50."})");
        const auto o = QJsonDocument::fromJson(r.text.toUtf8()).object();
        QVERIFY(!o.isEmpty());
        QVERIFY(o.value("target_price").isNull());
        QCOMPARE(o.value("last").toDouble(), 150.25);
        QCOMPARE(o.value("direction").toString(), QString("up"));
        QVERIFY(o.value("why").toString().contains(kMarker));
        QCOMPARE(r.removed.size(), 2);
        QVERIFY(r.json);
    }
    void clean_json_is_untouched() {
        const QString j = R"({ "b": 150.25,  "a": "closed at 150.25" })";
        QCOMPARE(run("150.25", j).text, j);
    }
};

QTEST_APPLESS_MAIN(TestNumberGrounding)
#include "test_number_grounding.moc"

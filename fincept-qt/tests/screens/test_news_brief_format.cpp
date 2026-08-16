// test_news_brief_format.cpp — splitting a brief into its two panes, and
// repairing the per-category half.
//
// Both live failures this covers were silent. A '•'-bulleted brief failed the
// marker-less fallback and rendered as one blob with an EMPTY "BY CATEGORY"
// pane; a merged "### DEFENSE, CRYPTO" heading showed five categories where the
// model had written six. Neither logged anything — the pane just had less in it
// than the model produced.

#include <QTest>

#include "screens/news/NewsBriefFormat.h"

using fincept::screens::brief::merge_duplicate_sections;
using fincept::screens::brief::split;

namespace {

/// Headings in render order, e.g. {"CRYPTO", "EARNINGS"}.
QStringList headings_of(const QString& detail) {
    QStringList out;
    for (const QString& line : detail.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1String("##")))
            out << t.mid(t.lastIndexOf(QLatin1Char('#')) + 1).trimmed();
    }
    return out;
}

} // namespace

class TestNewsBriefFormat : public QObject {
    Q_OBJECT

private slots:
    // ── splitting the two panes ──────────────────────────────────────────────

    void the_marker_splits_summary_from_detail() {
        const auto [summary, detail] = split(QStringLiteral(
            "**Overall read:** Risk-off into the close.\n"
            "<<<CATEGORIES>>>\n"
            "### CRYPTO\n"
            "- A phishing scam drained $550,000 from a Hyperliquid user.\n"));
        QCOMPARE(summary, QStringLiteral("**Overall read:** Risk-off into the close."));
        QCOMPARE(headings_of(detail), QStringList{QStringLiteral("CRYPTO")});
    }

    void bullet_briefs_still_split_without_the_marker() {
        // The '•' regression: the fallback tested tail lines against
        // QLatin1Char('•'), which truncated to a different character, so no
        // bullet ever matched, tail_is_sections failed and the whole brief
        // landed in the summary pane with nothing in BY CATEGORY.
        const auto [summary, detail] = split(QStringLiteral(
            "**Overall read:** The dollar firmed after the claims print.\n"
            "\n"
            "### ENERGY\n"
            "• A Libyan field outage removed two hundred thousand barrels from supply.\n"
            "### CRYPTO\n"
            "• A phishing scam drained $550,000 from a Hyperliquid user.\n"));
        QVERIFY(summary.startsWith(QStringLiteral("**Overall read:**")));
        QVERIFY(!summary.contains(QStringLiteral("###")));
        QCOMPARE(headings_of(detail),
                 (QStringList{QStringLiteral("ENERGY"), QStringLiteral("CRYPTO")}));
    }

    void prose_containing_a_hash_does_not_split_early() {
        // "###" inside the summary body must not be mistaken for the start of
        // the category half — the fallback only anchors on a heading whose
        // tail is nothing but headings and bullets.
        const auto [summary, detail] = split(QStringLiteral(
            "### Note\n"
            "The index closed flat, and desks are positioned for a soft payrolls print "
            "on Friday with breadth still unusually narrow.\n"));
        QVERIFY(detail.isEmpty());
        QVERIFY(summary.contains(QStringLiteral("payrolls")));
    }

    // ── repairing the per-category half ──────────────────────────────────────

    void a_repeated_heading_is_merged_once() {
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### ENERGY\n"
            "- A Libyan field outage removed supply from the market.\n"
            "### CRYPTO\n"
            "- A phishing scam drained $550,000 from a Hyperliquid user.\n"
            "### ENERGY\n"
            "- Gulf Coast refiners extended maintenance into the shoulder season.\n"));
        QCOMPARE(headings_of(out),
                 (QStringList{QStringLiteral("ENERGY"), QStringLiteral("CRYPTO")}));
        QVERIFY(out.contains(QStringLiteral("Libyan")));
        QVERIFY(out.contains(QStringLiteral("Gulf Coast")));
    }

    void a_merged_heading_becomes_two_sections() {
        // The observed failure, with bullets each side's keywords can claim.
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### DEFENSE, CRYPTO\n"
            "- Pentagon awarded a missile-defence contract to two suppliers.\n"
            "- A phishing scam drained bitcoin from a retail wallet.\n"));
        QCOMPARE(headings_of(out),
                 (QStringList{QStringLiteral("DEFENSE"), QStringLiteral("CRYPTO")}));
        // Each bullet under the name its own words support, not both under one.
        const int defense_at = out.indexOf(QStringLiteral("Pentagon"));
        const int crypto_at = out.indexOf(QStringLiteral("bitcoin"));
        QVERIFY(defense_at < out.indexOf(QStringLiteral("### CRYPTO")));
        QVERIFY(crypto_at > out.indexOf(QStringLiteral("### CRYPTO")));
    }

    void a_split_half_merges_with_its_own_standalone_section() {
        // The whole point of splitting. Before it, "DEFENSE, CRYPTO" and
        // "CRYPTO" were two unrelated keys, so the reader got a section named
        // after neither category plus a second one that should have been part
        // of it.
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### DEFENSE, CRYPTO\n"
            "- Troops were redeployed along the northern corridor.\n"
            "### CRYPTO\n"
            "- A phishing scam drained $550,000 from a Hyperliquid user.\n"));
        QCOMPARE(headings_of(out),
                 (QStringList{QStringLiteral("DEFENSE"), QStringLiteral("CRYPTO")}));
        QVERIFY(!out.contains(QStringLiteral("DEFENSE, CRYPTO")));
        QVERIFY(out.contains(QStringLiteral("Hyperliquid")));
        QVERIFY(out.contains(QStringLiteral("Troops")));
    }

    void a_split_that_would_lose_a_name_does_not_happen() {
        // The live case: "Sionic launches Instant Bank Pay" carries no DEFENSE
        // and no CRYPTO keyword, so nothing in the text says which half owns
        // it. Splitting would file it under DEFENSE and leave CRYPTO with
        // nothing — and a name with nothing can only be dropped or rendered as
        // an empty section, so the split would COST a category. That is the
        // failure the split exists to fix, so the heading is left as written.
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### DEFENSE, CRYPTO\n"
            "- Sionic launched Instant Bank Pay in the US through Microsoft's marketplace.\n"));
        QCOMPARE(headings_of(out), QStringList{QStringLiteral("DEFENSE, CRYPTO")});
        QVERIFY(out.contains(QStringLiteral("Sionic")));
    }

    void a_repeated_merged_heading_does_not_become_three_sections() {
        // The trap in deciding the split from single-name headings alone: the
        // first copy splits (both names claimed by its own bullets), the second
        // is vetoed because ITS body only claims DEFENSE — so DEFENSE and
        // CRYPTO each appear twice, once as a real section and once inside a
        // leftover literal "### DEFENSE, CRYPTO". That is worse than the merged
        // heading the split was repairing. The fixpoint sees that CRYPTO will
        // own a section and lets the second copy split too.
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### DEFENSE, CRYPTO\n"
            "- Pentagon awarded a missile contract to two suppliers.\n"
            "- Bitcoin ETFs saw a third day of outflows.\n"
            "### DEFENSE, CRYPTO\n"
            "- Troops moved along the northern corridor.\n"));
        QCOMPARE(headings_of(out),
                 (QStringList{QStringLiteral("DEFENSE"), QStringLiteral("CRYPTO")}));
        QVERIFY(!out.contains(QStringLiteral("DEFENSE, CRYPTO")));
        // The second copy's bullet lands with the rest of the defence news.
        QVERIFY(out.indexOf(QStringLiteral("Troops")) < out.indexOf(QStringLiteral("### CRYPTO")));
    }

    void a_heading_with_an_unknown_part_is_left_alone() {
        // "ENERGY & COMMODITIES" names one category and one thing that is not
        // one. Splitting would either invent a COMMODITIES section or silently
        // rename the heading to ENERGY; both lose what the model wrote.
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### ENERGY & COMMODITIES\n"
            "- Crude held its gains on the Libyan outage.\n"));
        QCOMPARE(headings_of(out), QStringList{QStringLiteral("ENERGY & COMMODITIES")});
    }

    void one_unknown_part_stops_a_three_way_split() {
        // Recognising only the parts it knows is worse than not splitting: this
        // would render ENERGY + CRYPTO with the word COMMODITIES deleted and
        // its bullet misfiled under whichever name came first.
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### ENERGY, COMMODITIES & CRYPTO\n"
            "- Crude held its gains on the Libyan outage.\n"
            "- Copper hit a fresh high on smelter outages.\n"
            "- Bitcoin held above its prior range.\n"));
        QCOMPARE(headings_of(out), QStringList{QStringLiteral("ENERGY, COMMODITIES & CRYPTO")});
        QVERIFY(out.contains(QStringLiteral("Copper")));
    }

    void a_decorated_heading_still_splits() {
        // Bold and a trailing colon are both common model formattings, and both
        // used to leave the emphasis stuck to a part ("**DEFENSE", "CRYPTO:")
        // so the lookup missed and the repair was skipped entirely.
        for (const QString& heading :
             {QStringLiteral("### **DEFENSE, CRYPTO**"), QStringLiteral("### DEFENSE, CRYPTO:")}) {
            const QString out = merge_duplicate_sections(
                heading + QStringLiteral("\n"
                                         "- Pentagon awarded a missile-defence contract.\n"
                                         "- A phishing scam drained bitcoin from a wallet.\n"));
            QCOMPARE(headings_of(out),
                     (QStringList{QStringLiteral("DEFENSE"), QStringLiteral("CRYPTO")}));
        }
    }

    void portfolio_is_a_heading_the_splitter_understands() {
        // PORTFOLIO has no keyword rule — no wording in a headline makes a
        // story "portfolio", only the reader's holdings do — but the prompt
        // offers it, so a merged "### PORTFOLIO, TECH" has to be splittable or
        // the one name the prompt adds is the one that can never be repaired.
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### PORTFOLIO, TECH\n"
            "- Your NVDA position is exposed to the export-control headline.\n"
            "- Semiconductor names gave back Monday's rally on no fresh news.\n"));
        QCOMPARE(headings_of(out),
                 (QStringList{QStringLiteral("PORTFOLIO"), QStringLiteral("TECH")}));
        QVERIFY(out.contains(QStringLiteral("NVDA")));
    }

    void identical_bullets_under_a_repeated_heading_are_dropped_once() {
        const QString out = merge_duplicate_sections(QStringLiteral(
            "### TECH\n"
            "- Semiconductor names gave back Monday's rally.\n"
            "### TECH\n"
            "- Semiconductor names gave back Monday's rally.\n"));
        QCOMPARE(out.count(QStringLiteral("Semiconductor")), 1);
    }

    void text_with_no_headings_is_returned_unchanged() {
        const QString plain = QStringLiteral("Nothing material in the headlines today.");
        QCOMPARE(merge_duplicate_sections(plain), plain);
        QCOMPARE(merge_duplicate_sections(QString()), QString());
    }

    // ── the vocabulary the split depends on ──────────────────────────────────

    void heading_separators_a_model_actually_uses_are_recognised() {
        using fincept::news::categories_in_heading;
        const QStringList both{QStringLiteral("DEFENSE"), QStringLiteral("CRYPTO")};
        QCOMPARE(categories_in_heading(QStringLiteral("DEFENSE, CRYPTO")), both);
        QCOMPARE(categories_in_heading(QStringLiteral("DEFENSE / CRYPTO")), both);
        QCOMPARE(categories_in_heading(QStringLiteral("DEFENSE & CRYPTO")), both);
        QCOMPARE(categories_in_heading(QStringLiteral("Defense and Crypto")), both);
        QCOMPARE(categories_in_heading(QStringLiteral("**DEFENSE, CRYPTO**")), both);
        QCOMPARE(categories_in_heading(QStringLiteral("DEFENSE, CRYPTO:")), both);
        // One name, or none, is not a merge.
        QCOMPARE(categories_in_heading(QStringLiteral("TECH")).size(), 1);
        QVERIFY(categories_in_heading(QStringLiteral("MARKET WRAP")).isEmpty());
    }

    void one_unrecognised_part_yields_nothing_at_all() {
        // All-or-nothing is the contract the caller relies on to leave a
        // heading alone. Returning the recognised subset would let the caller
        // split a heading whose other half it cannot name.
        using fincept::news::categories_in_heading;
        QVERIFY(categories_in_heading(QStringLiteral("ENERGY & COMMODITIES")).isEmpty());
        QVERIFY(categories_in_heading(QStringLiteral("ENERGY, COMMODITIES & CRYPTO")).isEmpty());
        QVERIFY(categories_in_heading(QStringLiteral("TOP STORIES")).isEmpty());
    }

    void stripping_decoration_must_not_glue_a_fragment_into_a_name() {
        // QString::remove(regex) strips every match, not just the edges, so a
        // \s in the decoration class would collapse "MARKET S" to "MARKETS" and
        // recognise a heading as a category it is not.
        using fincept::news::categories_in_heading;
        QVERIFY(categories_in_heading(QStringLiteral("MARKET S, TECH")).isEmpty());
        QVERIFY(categories_in_heading(QStringLiteral("CRY PTO, DEFENSE")).isEmpty());
    }

    void the_prompt_menu_and_the_classifier_offer_the_same_names() {
        // Two orders on purpose — keyword precedence decides classification,
        // editorial order decides what the model reaches for first — but they
        // must never hold different NAMES, or the prompt offers a category the
        // renderer cannot recognise in a heading.
        QStringList menu = fincept::news::prompt_menu();
        QStringList rules = fincept::news::category_names();
        QVERIFY2(menu != rules, "if these ever match, the editorial order was lost to a refactor");
        menu.sort();
        rules.sort();
        QCOMPARE(menu, rules);
        // PORTFOLIO is a heading but not a classification: no wording in a
        // headline can make a story belong to the reader's holdings.
        QVERIFY(fincept::news::heading_vocabulary().contains(QStringLiteral("PORTFOLIO")));
        QVERIFY(!rules.contains(QStringLiteral("PORTFOLIO")));
        QCOMPARE(fincept::news::classify(QStringLiteral("your nvda position is exposed")), QString());
    }

    void classification_order_survived_the_move_out_of_enrich_article() {
        // The table was an if/else-if chain inside enrich_article(); first
        // match still wins, and the order still decides the overlaps. Miners'
        // results are EARNINGS before they are CRYPTO.
        using fincept::news::classify;
        QCOMPARE(classify(QStringLiteral("bitcoin miner posts quarterly results")),
                 QStringLiteral("EARNINGS"));
        QCOMPARE(classify(QStringLiteral("ethereum staking hits a record")),
                 QStringLiteral("CRYPTO"));
        QCOMPARE(classify(QStringLiteral("federal reserve holds interest rate")),
                 QStringLiteral("ECONOMIC"));
        QCOMPARE(classify(QStringLiteral("mercedes-benz held its margins")), QString());
    }

    void the_allow_list_keeps_a_bullet_out_of_a_section_the_model_never_wrote() {
        // A DEFENSE/CRYPTO bullet mentioning Russia must not be pulled into
        // GEOPOLITICS, which is not one of the names on that heading.
        using fincept::news::classify;
        const QSet<QString> allowed{QStringLiteral("DEFENSE"), QStringLiteral("CRYPTO")};
        QCOMPARE(classify(QStringLiteral("russia moved troops to the border"), allowed),
                 QStringLiteral("DEFENSE"));
        QCOMPARE(classify(QStringLiteral("russia moved troops to the border")),
                 QStringLiteral("DEFENSE")); // DEFENSE precedes GEOPOLITICS anyway
        QCOMPARE(classify(QStringLiteral("russia widened its sanctions list"), allowed),
                 QString()); // GEOPOLITICS is not on offer, so nothing claims it
    }
};

QTEST_APPLESS_MAIN(TestNewsBriefFormat)
#include "test_news_brief_format.moc"

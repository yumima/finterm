// test_degeneracy.cpp — looks_degenerate() must catch both collapse modes
// without ever rejecting a good answer.
//
// The false-positive cases matter more than the true positives: rejecting a
// brief shows the user "AI brief unavailable" for a request that produced a
// perfectly good one, and that is silent — the good text is discarded before
// anything renders it.

#include <QTest>

#include "ai_chat/Degeneracy.h"

using fincept::ai_chat::looks_degenerate;

namespace {

// The observed live failure: 225 words, one full stop at the very end, not a
// single comma. Its vocabulary ratio is 0.89 — almost every word is new — so
// every repetition-based signal passes it through. Punctuation is the tell.
const char* kNounPhraseSalad =
    "Sionic launches Instant Bank Pay US through Microsoft Marketplace digital payment "
    "infrastructure advancement fintech sector evolution security concerns cryptocurrency "
    "regulation implications decentralized finance ecosystem expansion traditional banking "
    "systems integration seamless user experiences enhanced financial inclusion initiatives "
    "growing adoption blockchain technology applications expanding globally regulatory "
    "frameworks evolving industry standards established collaboration efforts strengthening "
    "partnerships strategic alliances formed cross-sector cooperation promoting innovation "
    "development sustainable growth opportunities emerging market penetration strategies "
    "optimization processes implemented efficiency gains achieved service reliability "
    "maintained customer satisfaction improved security protocols strengthened fraud "
    "prevention measures enhanced privacy protections ensured compliance regulations adhered "
    "ethical considerations addressed transparency practices demonstrated accountability "
    "commitments upheld trust built community engagement fostered user education initiatives "
    "conducted financial literacy programs expanded awareness campaigns launched responsible "
    "usage guidelines established best shared information disseminated publicly knowledge "
    "resources provided online platforms utilized for educational purposes training materials "
    "developed workshops organized seminars hosted expert panels convened thought leaders "
    "invited industry events sponsored research partnerships formed academic institutions "
    "engaged collaborative efforts sustained innovation momentum maintained competitive "
    "advantages secured market share grew revenue streams diversified customer bases expanded "
    "operational costs managed profit margins optimized risk exposures mitigated compliance "
    "burdens reduced regulatory liabilities minimized legal disputes avoided reputational "
    "risks managed crisis communications handled stakeholder relations strengthened community "
    "bonds nurtured ecosystem health promoted sustainability practices implemented "
    "environmental responsibilities fulfilled social impact goals achieved philanthropic "
    "commitments honored corporate citizenship embraced.";

// A real brief, the shape the prompt asks for.
const char* kGoodBrief =
    "**Overall read:** Risk-off, with a stronger dollar and softer China demand "
    "driving the tone.\n"
    "\n"
    "- Mercedes-Benz held Q2 margins despite softening China demand, a read-through for "
    "every European exporter with meaningful mainland exposure.\n"
    "- Sionic launched Instant Bank Pay in the US through Microsoft's marketplace, its "
    "first distribution deal outside the UK.\n"
    "- A Google ad phishing scam drained $550,000 from a Hyperliquid user, according to a "
    "security researcher.\n"
    "\n"
    "<<<CATEGORIES>>>\n"
    "### CRYPTO\n"
    "- A Google ad phishing scam drained $550,000 from a Hyperliquid user.\n"
    "### EARNINGS\n"
    "- Nothing material today.\n";

} // namespace

class TestDegeneracy : public QObject {
    Q_OBJECT

private slots:
    // ── the two collapse modes ───────────────────────────────────────────────

    void unpunctuated_noun_phrase_chain_is_degenerate() {
        QVERIFY(looks_degenerate(QString::fromLatin1(kNounPhraseSalad)));
    }

    void token_loop_is_degenerate() {
        // The original mode: a long line that repeats itself. Low vocabulary
        // ratio, so the existing signal catches it — locked in so the new
        // punctuation check cannot be written in a way that displaces it.
        QString loop;
        for (int i = 0; i < 200; ++i)
            loop += QStringLiteral("the market, the market, ");
        QVERIFY(looks_degenerate(loop));
    }

    // ── what must survive ────────────────────────────────────────────────────

    void a_real_brief_is_not_degenerate() {
        QVERIFY(!looks_degenerate(QString::fromLatin1(kGoodBrief)));
    }

    void empty_and_short_answers_are_not_degenerate() {
        QVERIFY(!looks_degenerate(QString()));
        QVERIFY(!looks_degenerate(QStringLiteral("Nothing material today.")));
    }

    void a_long_single_paragraph_analysis_survives() {
        // The equity-research analysis is legitimately one long paragraph with
        // no line breaks — the standing false-positive risk, because it clears
        // the 120-word line threshold and only its vocabulary and punctuation
        // keep it. Written out rather than generated in a loop: a loop produces
        // text that repeats by construction, which is the thing under test.
        const QString para = QStringLiteral(
            "Freight costs absorbed most of the operating leverage this quarter, and the "
            "bridge management laid out on the call puts roughly half of that back by the "
            "fourth quarter, assuming spot rates hold. Gross margin of 41.2% missed the "
            "Street by 60 basis points, though the shortfall sits almost entirely in the "
            "industrial segment, where a delayed line qualification in Malaysia pushed "
            "revenue into next period rather than losing it. Backlog grew 9% year over "
            "year and coverage for the next twelve months now stands near 78%, the highest "
            "since the 2022 restocking cycle. Buybacks resumed at a modest pace after the "
            "convertible was retired, leaving net leverage under 1.5 turns with no "
            "maturities before 2029. Guidance was trimmed at the top end only, which reads "
            "as conservatism around China rather than a change in demand; distributors "
            "there have been running lean inventories all year. Our concern remains "
            "pricing in the automotive book, where two competitors have added capacity and "
            "the renewal calendar is unusually heavy.");
        QVERIFY(para.split(QLatin1Char(' '), Qt::SkipEmptyParts).size()
                > fincept::ai_chat::kMaxSaneLineWords);
        QVERIFY(!looks_degenerate(para));
    }

    void a_long_sentence_with_commas_survives() {
        // Aimed directly at the new signal: comfortably past the run limit, but
        // punctuated throughout, so it is prose and must not be rejected.
        const QString s = QStringLiteral(
            "The session turned on the dollar, which firmed after the claims print, "
            "dragging on the metals complex, while crude held its gains on the Libyan "
            "outage, Treasuries sold off across the belly, European banks outperformed "
            "for a third day, semiconductors gave back Monday's rally, defensive sectors "
            "caught a modest bid late, and the volatility surface flattened into the "
            "close, leaving positioning roughly where it started the week, with breadth "
            "still narrow, the equal-weight index trailing the cap-weighted one by a wide "
            "margin, and no obvious catalyst on the calendar before Friday's payrolls "
            "print, which most desks now expect to land soft.");
        QVERIFY(s.split(QLatin1Char(' '), Qt::SkipEmptyParts).size()
                > fincept::ai_chat::kMaxUnpunctuatedRun);
        QVERIFY(!looks_degenerate(s));
    }

    void unpunctuated_bullets_on_separate_lines_survive() {
        // Bullet text often carries no full stop at all. Each bullet is its own
        // line, so a run can never span them however many there are — this
        // guards against tracking the run across the whole response instead of
        // resetting it per line, which would reject an ordinary breakdown.
        const QString bullets = QStringLiteral(
            "### MARKETS\n"
            "- Mercedes-Benz held second-quarter margins despite softening mainland demand\n"
            "- European exporters lagged as the euro firmed against a broadly stronger dollar\n"
            "### ENERGY\n"
            "- A Libyan field outage removed roughly two hundred thousand barrels from supply\n"
            "- Refiners across the Gulf Coast extended maintenance into the shoulder season\n"
            "### TECH\n"
            "- Sionic launched Instant Bank Pay in the US through Microsoft's marketplace\n"
            "- Semiconductor names gave back the previous session's rally on no fresh news\n"
            "### GEOPOLITICS\n"
            "- Brussels opened a subsidy probe into three mainland battery makers\n"
            "- Talks over the northern corridor stalled again without a published timetable\n"
            "### ECONOMIC\n"
            "- Weekly claims came in above every forecast in the survey range\n"
            "- Regional manufacturing surveys stayed below the expansion line for a fifth month\n");
        QVERIFY(bullets.split(QLatin1Char(' '), Qt::SkipEmptyParts).size()
                > fincept::ai_chat::kMaxUnpunctuatedRun);
        QVERIFY(!looks_degenerate(bullets));
    }

    void unpunctuated_bullets_on_ONE_line_survive() {
        // Same list with the newlines gone — a model that emits the breakdown
        // as a single • -separated line. No terminal punctuation anywhere, so
        // only the standalone-marker reset keeps this from reading as one
        // enormous sentence and being thrown away.
        const QString one_line = QStringLiteral(
            "• Mercedes-Benz held second-quarter margins despite softening mainland demand "
            "• European exporters lagged as the single currency firmed overnight "
            "• a Libyan field outage removed two hundred thousand barrels from supply "
            "• Gulf Coast refiners extended maintenance into the shoulder season "
            "• Sionic launched Instant Bank Pay through Microsoft's marketplace "
            "• semiconductor names gave back the previous session's rally "
            "• Brussels opened a subsidy probe into three battery makers "
            "• weekly claims arrived above every forecast in the survey range "
            "• regional manufacturing surveys stayed below the expansion line again "
            "• Treasuries sold off across the belly of the curve "
            "• defensive sectors caught a modest bid into the close "
            "• the volatility surface flattened for a third consecutive session");
        QVERIFY(one_line.split(QLatin1Char(' '), Qt::SkipEmptyParts).size()
                > fincept::ai_chat::kMaxUnpunctuatedRun);
        QVERIFY(!looks_degenerate(one_line));
    }

    void a_hyphenated_compound_does_not_rescue_a_collapse() {
        // The salad contains "cross-sector". If a hyphen anywhere in a word
        // reset the run, the live failure this whole check exists for would
        // slip through — so pin that it does not.
        const QString salad = QString::fromLatin1(kNounPhraseSalad);
        QVERIFY(salad.contains(QStringLiteral("cross-sector")));
        QVERIFY(looks_degenerate(salad));
    }
};

QTEST_APPLESS_MAIN(TestDegeneracy)
#include "test_degeneracy.moc"

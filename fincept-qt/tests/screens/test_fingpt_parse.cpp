// tests/screens/test_fingpt_parse.cpp
//
// The forecaster answer parser takes free text from whatever model the fingpt
// role is bound to and claims to have found FinGPT's three sections, a
// direction and a magnitude band. A wrong parse here paints a green "UP"
// header over an answer that said something else — so the cases below cover
// the canonical shape, the common drifts (missing colon, "and" for "&",
// reasoning traces), and the honest failure (sections_ok=false, direction 0,
// NaN bins) when the text does not carry the claim.

#include "screens/fingpt/FinGptParse.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace fincept::screens::fingpt;

namespace {
const QString kCanonical = QStringLiteral(
    "[Positive Developments]:\n"
    "1. Strong iPhone demand reported.\n"
    "2. Services revenue hit a record.\n\n"
    "[Potential Concerns]:\n"
    "1. Regulatory pressure in the EU.\n\n"
    "[Prediction & Analysis]\n"
    "Prediction: up by 2-3%\n"
    "Analysis: Momentum and the services beat outweigh the regulatory overhang.");
} // namespace

class TestFinGptParse : public QObject {
    Q_OBJECT

  private slots:

    void canonical_answer_parses_fully() {
        const ForecasterAnswer a = parse_forecaster_answer(kCanonical);
        QVERIFY(a.sections_ok);
        QVERIFY(a.positives.contains(QStringLiteral("Services revenue")));
        QVERIFY(!a.positives.contains(QStringLiteral("Regulatory")));
        QVERIFY(a.concerns.contains(QStringLiteral("Regulatory pressure")));
        QVERIFY(!a.concerns.contains(QStringLiteral("Prediction")));
        QCOMPARE(a.prediction, QStringLiteral("up by 2-3%"));
        QVERIFY(a.analysis.startsWith(QStringLiteral("Momentum")));
        QCOMPARE(a.direction, 1);
        QCOMPARE(a.bin_lo, 2.0);
        QCOMPARE(a.bin_hi, 3.0);
        QCOMPARE(describe_forecast(a), QStringLiteral("UP by 2–3%"));
    }

    void tolerant_of_marker_drift_and_think_traces() {
        // qwen-style trace + "and" instead of "&" + no colons.
        const QString drifted = QStringLiteral(
            "<think>let me reason about this</think>"
            "[Positive Developments]\nGood quarter.\n"
            "[Potential Concerns]\nChina exposure.\n"
            "[Prediction and Analysis]\n"
            "Prediction: down by more than 5%\n"
            "Analysis: Tariff risk dominates.");
        const ForecasterAnswer a = parse_forecaster_answer(drifted);
        QVERIFY(a.sections_ok);
        QVERIFY(!a.positives.contains(QStringLiteral("think")));
        QCOMPARE(a.direction, -1);
        QCOMPARE(a.bin_lo, 5.0);
        QVERIFY(std::isinf(a.bin_hi));
        QCOMPARE(describe_forecast(a), QStringLiteral("DOWN by more than 5%"));
    }

    void single_figure_and_earliest_direction_word_wins() {
        // A sentence naming both directions reads as what it LEADS with.
        const QString t = kCanonical.section(QStringLiteral("Prediction:"), 0, 0)
            + QStringLiteral("Prediction: up by 2% even if some sectors go down\nAnalysis: x");
        const ForecasterAnswer a = parse_forecaster_answer(t);
        QCOMPARE(a.direction, 1);
        QCOMPARE(a.bin_lo, 2.0);
        QCOMPARE(a.bin_hi, 2.0);
    }

    void a_decline_hedged_with_bullish_is_still_a_decline() {
        // Observed live (qwen3.5 on AAPL): the Prediction line led with
        // "Neutral-to-Moderate Decline" and ended with "...shifts back to
        // bullish on AI-driven device sales". The upstream-faithful
        // up-checked-first rule painted a green UP header over it.
        const QString t = kCanonical.section(QStringLiteral("Prediction:"), 0, 0)
            + QStringLiteral("Prediction: Neutral-to-Moderate Decline (Expected Range: $325 - "
                             "$340), a rebound becomes possible once sentiment shifts back to "
                             "bullish on AI-driven device sales\nAnalysis: x");
        const ForecasterAnswer a = parse_forecaster_answer(t);
        QCOMPARE(a.direction, -1);
        QVERIFY2(std::isnan(a.bin_lo), "a $-range is not a percent band");
    }

    void a_preamble_echoing_the_task_does_not_slice_the_sections() {
        // The closing instruction says "analyze the positive developments and
        // potential concerns", and models echo it as an opening sentence. An
        // unanchored marker match sliced the answer at that echo: positives
        // became the word "and" and concerns swallowed the real positives.
        const QString t = QStringLiteral(
            "Let's first analyze the positive developments and potential concerns for AAPL.\n\n")
            + kCanonical;
        const ForecasterAnswer a = parse_forecaster_answer(t);
        QVERIFY(a.sections_ok);
        QVERIFY(a.positives.contains(QStringLiteral("Services revenue")));
        QVERIFY(a.concerns.startsWith(QStringLiteral("1. Regulatory pressure")));
        QCOMPARE(a.direction, 1);
    }

    void markdown_decorated_markers_still_parse() {
        const QString t = QStringLiteral(
            "**[Positive Developments]:**\n1. a\n\n**[Potential Concerns]:**\n1. b\n\n"
            "**[Prediction & Analysis]**\nPrediction: down by 1-2%\nAnalysis: c");
        const ForecasterAnswer a = parse_forecaster_answer(t);
        QVERIFY(a.sections_ok);
        QCOMPARE(a.direction, -1);
        QCOMPARE(a.bin_lo, 1.0);
        QCOMPARE(a.bin_hi, 2.0);
    }

    void unstructured_text_fails_honestly() {
        const ForecasterAnswer a =
            parse_forecaster_answer(QStringLiteral("The stock looks fine, hard to say."));
        QVERIFY(!a.sections_ok);
        QCOMPARE(a.direction, 0);
        QVERIFY(std::isnan(a.bin_lo));
        QVERIFY(std::isnan(a.bin_hi));
        QVERIFY(describe_forecast(a).isEmpty());
    }

    void dangling_think_trace_is_stripped() {
        // A truncated stream can end inside the trace — nothing of it may
        // leak into what the user is shown.
        QCOMPARE(strip_think(QStringLiteral("before <think>cut off mid-")),
                 QStringLiteral("before "));
    }

    void chat_model_synonyms_carry_direction() {
        // Observed live: "**Modest Gains (2–5%) with Volatility Spikes**" —
        // no word from upstream's up/increase list, clearly an up call.
        const QString t = kCanonical.section(QStringLiteral("Prediction:"), 0, 0)
            + QStringLiteral("Prediction: Modest Gains (2-5%) with Volatility Spikes\nAnalysis: x");
        const ForecasterAnswer a = parse_forecaster_answer(t);
        QCOMPARE(a.direction, 1);
        QCOMPARE(a.bin_lo, 2.0);
        QCOMPARE(a.bin_hi, 5.0);
        QCOMPARE(describe_forecast(a), QStringLiteral("UP by 2–5%"));
    }

    void a_stray_percentage_is_not_the_predicted_move() {
        // "45%" here is implied volatility; claiming "UP by ~45%" for the
        // week would be a confidently wrong magnitude.
        const QString t = kCanonical.section(QStringLiteral("Prediction:"), 0, 0)
            + QStringLiteral("Prediction: up slightly, with implied volatility near 45%\nAnalysis: x");
        const ForecasterAnswer a = parse_forecaster_answer(t);
        QCOMPARE(a.direction, 1);
        QVERIFY(std::isnan(a.bin_lo));
        QCOMPARE(describe_forecast(a), QStringLiteral("UP"));
    }

    void appreciate_with_a_band_is_an_up_call() {
        const QString t = kCanonical.section(QStringLiteral("Prediction:"), 0, 0)
            + QStringLiteral("Prediction: likely to appreciate 2-3% over the week\nAnalysis: x");
        const ForecasterAnswer a = parse_forecaster_answer(t);
        QCOMPARE(a.direction, 1);
        QCOMPARE(a.bin_lo, 2.0);
        QCOMPARE(a.bin_hi, 3.0);
    }

    void direction_without_magnitude_is_still_a_direction() {
        const QString t = QStringLiteral(
            "[Positive Developments]:\n1. a\n[Potential Concerns]:\n1. b\n"
            "[Prediction & Analysis]\nPrediction: the stock will decline next week\nAnalysis: c");
        const ForecasterAnswer a = parse_forecaster_answer(t);
        QVERIFY(a.sections_ok);
        QCOMPARE(a.direction, -1);
        QVERIFY(std::isnan(a.bin_lo));
        QCOMPARE(describe_forecast(a), QStringLiteral("DOWN"));
    }
};

QTEST_MAIN(TestFinGptParse)
#include "test_fingpt_parse.moc"

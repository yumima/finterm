// tests/services/test_technical_rating.cpp
//
// The ER Technicals tab prints STRONG BUY … STRONG SELL over a stock, so the
// scorer behind it has to survive the cases that broke the previous one. Every
// test here is a regression against a verdict that shipped backwards:
//
//   - a stock trading above rising averages with expanding MACD came back
//     STRONG SELL, because only overbought oscillators were allowed to vote;
//   - a stock below its short averages came back STRONG BUY off oversold
//     oscillators alone;
//   - ADX voted bullish inside downtrends, being unsigned;
//   - a MACD line below its signal line scored bullish whenever both happened
//     to sit above zero;
//   - Aroon cast a bullish and a bearish at the same time;
//   - a one-month window rated confidently off four surviving indicators.

#include "services/equity/TechnicalRating.h"

#include <QtTest/QtTest>

using namespace fincept::services::equity;
using S = TechSignal;

namespace {

/// A clean, confirmed uptrend: price above every average, averages rising,
/// MACD above its signal and widening, ADX trending with DI+ on top. Its
/// oscillators are overbought — which is what an uptrend looks like.
RatingInput uptrend() {
    RatingInput in;
    in.bars = 250;
    in.close = 224.0;

    in.now.set("sma_10", 215.0);
    in.now.set("sma_20", 207.0);
    in.now.set("sma_50", 195.0);
    in.now.set("sma_100", 182.0);
    in.now.set("sma_200", 170.0);
    in.now.set("ema_12", 210.0);
    in.now.set("wma_9", 213.0);
    in.now.set("kama", 205.0);
    in.now.set("vwap", 206.0);
    in.now.set("macd", 3.1);
    in.now.set("macd_signal", 0.6);
    in.now.set("adx", 31.0);
    in.now.set("adx_pos", 32.0);
    in.now.set("adx_neg", 14.0);
    in.now.set("aroon_up", 100.0);
    in.now.set("aroon_down", 21.0);
    in.now.set("cci", 178.0);
    in.now.set("rsi", 72.0);
    in.now.set("stoch_k", 97.0);
    in.now.set("stoch_d", 91.0);
    in.now.set("williams_r", -3.0);
    in.now.set("mfi", 84.0);
    in.now.set("roc", 12.0);
    in.now.set("ao", 11.8);
    in.now.set("bb_pband", 1.01);
    in.now.set("atr", 7.6);
    in.now.set("bb_mavg", 207.0);
    in.now.set("bb_wband", 16.2);
    in.now.set("cmf", 0.24);
    in.now.set("obv", 665000000.0);
    in.now.set("adi", 546000000.0);

    // A bar earlier: every average lower, MACD histogram narrower.
    in.prev.set("sma_10", 213.0);
    in.prev.set("sma_20", 205.0);
    in.prev.set("sma_50", 193.0);
    in.prev.set("sma_100", 181.0);
    in.prev.set("sma_200", 169.0);
    in.prev.set("ema_12", 208.0);
    in.prev.set("wma_9", 211.0);
    in.prev.set("kama", 203.0);
    in.prev.set("vwap", 204.0);
    in.prev.set("macd", 2.6);
    in.prev.set("macd_signal", 0.5);
    in.prev.set("cci", 170.0);
    in.prev.set("rsi", 70.5);
    in.prev.set("stoch_k", 95.0);
    in.prev.set("stoch_d", 89.0);
    in.prev.set("williams_r", -5.0);
    in.prev.set("mfi", 82.0);
    in.prev.set("ao", 11.0);
    in.prev.set("bb_pband", 0.98);

    in.back.set("obv", 610000000.0);
    in.back.set("adi", 500000000.0);
    return in;
}

/// The mirror image: price under falling averages, MACD below its signal and
/// widening down, ADX trending with DI- on top, oscillators oversold.
RatingInput downtrend() {
    RatingInput in;
    in.bars = 250;
    in.close = 170.0;

    in.now.set("sma_10", 178.0);
    in.now.set("sma_20", 195.0);
    in.now.set("sma_50", 205.0);
    in.now.set("sma_100", 212.0);
    in.now.set("sma_200", 220.0);
    in.now.set("ema_12", 190.0);
    in.now.set("wma_9", 186.0);
    in.now.set("kama", 197.0);
    in.now.set("vwap", 194.0);
    in.now.set("macd", -3.1);
    in.now.set("macd_signal", -0.6);
    in.now.set("adx", 31.0);
    in.now.set("adx_pos", 14.0);
    in.now.set("adx_neg", 32.0);
    in.now.set("aroon_up", 7.0);
    in.now.set("aroon_down", 100.0);
    in.now.set("cci", -178.0);
    in.now.set("rsi", 24.0);
    in.now.set("stoch_k", 6.0);
    in.now.set("stoch_d", 11.0);
    in.now.set("williams_r", -96.0);
    in.now.set("mfi", 14.0);
    in.now.set("roc", -12.0);
    in.now.set("ao", -11.8);
    in.now.set("bb_pband", -0.02);
    in.now.set("atr", 7.6);
    in.now.set("bb_mavg", 195.0);
    in.now.set("bb_wband", 16.2);
    in.now.set("cmf", -0.24);
    in.now.set("obv", 400000000.0);
    in.now.set("adi", 300000000.0);

    in.prev.set("sma_10", 181.0);
    in.prev.set("sma_20", 197.0);
    in.prev.set("sma_50", 207.0);
    in.prev.set("sma_100", 213.0);
    in.prev.set("sma_200", 221.0);
    in.prev.set("ema_12", 192.0);
    in.prev.set("wma_9", 189.0);
    in.prev.set("kama", 199.0);
    in.prev.set("vwap", 196.0);
    in.prev.set("macd", -2.6);
    in.prev.set("macd_signal", -0.5);
    in.prev.set("cci", -170.0);
    in.prev.set("rsi", 26.0);
    in.prev.set("stoch_k", 9.0);
    in.prev.set("stoch_d", 13.0);
    in.prev.set("williams_r", -93.0);
    in.prev.set("mfi", 17.0);
    in.prev.set("ao", -11.0);
    in.prev.set("bb_pband", 0.02);

    in.back.set("obv", 470000000.0);
    in.back.set("adi", 360000000.0);
    return in;
}

/// Score a display list into TechIndicators the way parse_technicals does.
QVector<TechIndicator> build(const RatingInput& in, const QVector<QPair<QString, QString>>& cols) {
    QVector<TechIndicator> out;
    for (const auto& kv : cols) {
        if (!in.now.has(kv.second))
            continue;
        const auto v = technical_rating::score(kv.second, in);
        TechIndicator ti;
        ti.name = kv.second;
        ti.value = in.now.get(kv.second);
        ti.category = kv.first;
        ti.signal = v.signal;
        ti.votes = v.votes;
        ti.rating_bucket = v.bucket;
        out.append(ti);
    }
    return out;
}

/// The full display set, categorised exactly as the tab lays it out.
///
/// Keep in lock-step with kTrend/kMomentum/kVolatility/kVolume in
/// EquityResearchService.cpp (the canonical list) and with kCols in
/// tools/technicals_series/score_series.cpp — this copy had already drifted
/// once (missing sma_30 / ema_26 / ichimoku_base), which meant the tests
/// exercised a smaller indicator set than the panel ships.
QVector<TechIndicator> score_all(const RatingInput& in) {
    static const QVector<QPair<QString, QString>> cols = {
        {"trend", "sma_10"},     {"trend", "sma_20"},      {"trend", "sma_30"},
        {"trend", "sma_50"},     {"trend", "sma_100"},     {"trend", "sma_200"},
        {"trend", "ema_12"},     {"trend", "ema_26"},      {"trend", "wma_9"},
        {"trend", "ichimoku_base"},
        {"trend", "macd"},       {"trend", "macd_signal"}, {"trend", "cci"},
        {"trend", "adx"},        {"trend", "aroon_up"},    {"trend", "aroon_down"},
        {"momentum", "rsi"},     {"momentum", "stoch_k"},  {"momentum", "stoch_d"},
        {"momentum", "williams_r"}, {"momentum", "roc"},   {"momentum", "mfi"},
        {"momentum", "ao"},      {"momentum", "kama"},
        {"volatility", "atr"},   {"volatility", "bb_mavg"},{"volatility", "bb_pband"},
        {"volatility", "bb_wband"},
        {"volume", "obv"},       {"volume", "vwap"},       {"volume", "cmf"},
        {"volume", "adi"},
    };
    return build(in, cols);
}

/// Find one scored row by column name.
TechIndicator find(const QVector<TechIndicator>& v, const QString& name) {
    for (const auto& ti : v)
        if (ti.name == name)
            return ti;
    return {};
}

} // namespace

class TestTechnicalRating : public QObject {
    Q_OBJECT

  private slots:

    // ── The two verdicts that used to come out inverted ───────────────────────

    void uptrend_is_not_rated_a_sell() {
        const auto v = technical_rating::aggregate(score_all(uptrend()), uptrend());
        QVERIFY2(v.net > 0.0, qPrintable(QString("net=%1 basis=%2").arg(v.net).arg(v.basis)));
        QVERIFY(v.overall == S::Buy || v.overall == S::StrongBuy);
    }

    void downtrend_is_not_rated_a_buy() {
        const auto v = technical_rating::aggregate(score_all(downtrend()), downtrend());
        QVERIFY2(v.net < 0.0, qPrintable(QString("net=%1 basis=%2").arg(v.net).arg(v.basis)));
        QVERIFY(v.overall == S::Sell || v.overall == S::StrongSell);
    }

    /// The scorer must be sign-symmetric: mirrored inputs, mirrored score.
    void mirrored_inputs_give_mirrored_scores() {
        const auto up = technical_rating::aggregate(score_all(uptrend()), uptrend());
        const auto down = technical_rating::aggregate(score_all(downtrend()), downtrend());
        QVERIFY(qAbs(up.net + down.net) < 0.05);
    }

    // ── Per-indicator rules ──────────────────────────────────────────────────

    /// ADX is unsigned. Direction has to come from DI+/DI-, or a strong
    /// downtrend casts a bull vote purely for being strong.
    void adx_takes_its_direction_from_di() {
        RatingInput in = downtrend();
        QCOMPARE(technical_rating::score("adx", in).signal, S::StrongSell);

        // Same ADX strength, DI+ on top → the opposite call.
        in.now.set("adx_pos", 32.0);
        in.now.set("adx_neg", 14.0);
        QCOMPARE(technical_rating::score("adx", in).signal, S::StrongBuy);
    }

    /// Below 20 there is no trend to have a direction about.
    void adx_is_neutral_without_a_trend() {
        RatingInput in = uptrend();
        in.now.set("adx", 17.0);
        QCOMPARE(technical_rating::score("adx", in).signal, S::Neutral);
        QVERIFY(technical_rating::score("adx", in).votes);
    }

    /// MACD +0.9 under a signal line at +4.1 is a bearish cross. The old rule
    /// tested the MACD line against zero and called it bullish.
    void macd_scores_against_its_signal_not_zero() {
        RatingInput in;
        in.close = 313.0;
        in.now.set("macd", 0.88);
        in.now.set("macd_signal", 4.06);
        in.prev.set("macd", 1.2);
        in.prev.set("macd_signal", 4.0);
        const auto v = technical_rating::score("macd", in);
        QVERIFY(v.votes);
        QVERIFY(v.signal == S::Sell || v.signal == S::StrongSell);
    }

    /// Aroon Up and Aroon Down are one indicator. Scoring them independently
    /// let a single reading cast a bullish and a bearish in the same tally.
    void aroon_casts_exactly_one_vote() {
        RatingInput in;
        in.now.set("aroon_up", 100.0);
        in.now.set("aroon_down", 72.0);
        const auto up = technical_rating::score("aroon_up", in);
        const auto down = technical_rating::score("aroon_down", in);
        QVERIFY(up.votes);
        QVERIFY(!down.votes);
        QCOMPARE(up.signal, down.signal); // shown alike, counted once
    }

    /// Oversold and still falling is a falling knife, not a bullish.
    void oscillators_wait_for_the_turn() {
        RatingInput in;
        in.now.set("rsi", 22.0);
        in.prev.set("rsi", 27.0); // still dropping
        QCOMPARE(technical_rating::score("rsi", in).signal, S::Neutral);

        in.prev.set("rsi", 19.0); // turning back up
        QCOMPARE(technical_rating::score("rsi", in).signal, S::StrongBuy);
    }

    /// Overbought inside a confirmed uptrend is not a bearish signal.
    void mean_reversion_yields_to_a_confirmed_trend() {
        RatingInput in = uptrend();
        in.now.set("rsi", 78.0);
        in.prev.set("rsi", 80.0); // rolling over, would normally be StrongSell
        QCOMPARE(technical_rating::score("rsi", in).signal, S::Neutral);

        // Drop ADX below the confirmation threshold and the call comes back.
        in.now.set("adx", 15.0);
        QCOMPARE(technical_rating::score("rsi", in).signal, S::StrongSell);
    }

    /// Price above a rising average is bullish — the evidence the old scorer
    /// had no branch for at all.
    void moving_averages_vote_on_price() {
        RatingInput in;
        in.close = 110.0;
        in.now.set("sma_50", 100.0);
        in.prev.set("sma_50", 99.0);
        QCOMPARE(technical_rating::score("sma_50", in).signal, S::StrongBuy);

        in.close = 90.0;
        in.prev.set("sma_50", 101.0); // average now falling
        QCOMPARE(technical_rating::score("sma_50", in).signal, S::StrongSell);

        // Hugging the average is not a strong anything.
        in.close = 100.4;
        QCOMPARE(technical_rating::score("sma_50", in).signal, S::Buy);
    }

    // ── Non-voting rows ──────────────────────────────────────────────────────

    /// Readings with no direction of their own, and second lines of two-line
    /// indicators, are displayed but excluded — otherwise one indicator votes
    /// twice, or a Neutral nobody expressed drags the composite to the middle.
    void reference_rows_do_not_vote() {
        const auto scored = score_all(uptrend());
        for (const auto& col : {"atr", "bb_wband", "bb_mavg", "macd_signal", "stoch_d", "aroon_down"})
            QVERIFY2(!find(scored, col).votes, col);
        for (const auto& col : {"sma_20", "sma_200", "macd", "adx", "aroon_up", "rsi", "obv", "cmf"})
            QVERIFY2(find(scored, col).votes, col);
    }

    void counts_cover_voting_rows_only() {
        const auto scored = score_all(uptrend());
        const auto v = technical_rating::aggregate(scored, uptrend());
        int voting = 0;
        for (const auto& ti : scored)
            if (ti.votes)
                voting++;
        QCOMPARE(v.voting, voting);
        QCOMPARE(v.strong_buy + v.buy + v.neutral + v.sell + v.strong_sell, voting);
        QCOMPARE(v.displayed, static_cast<int>(scored.size()));
    }

    // ── Thin data ────────────────────────────────────────────────────────────

    /// A one-month window leaves the long averages, MACD, ADX and Aroon inside
    /// their warm-up. Rating confidently off the survivors is how a flat month
    /// came back STRONG BUY on four votes.
    void thin_history_refuses_to_rate() {
        RatingInput in;
        in.bars = 23;
        in.close = 313.0;
        in.now.set("sma_20", 300.0);
        in.prev.set("sma_20", 299.0);
        in.now.set("rsi", 55.0);
        in.now.set("stoch_k", 30.0);
        in.now.set("stoch_d", 28.0);
        in.now.set("williams_r", -70.0);
        in.now.set("mfi", 50.0);
        in.now.set("cmf", 0.25);

        const QVector<QPair<QString, QString>> cols = {
            {"trend", "sma_20"},        {"momentum", "rsi"},   {"momentum", "stoch_k"},
            {"momentum", "stoch_d"},    {"momentum", "williams_r"}, {"momentum", "mfi"},
            {"volume", "cmf"},
        };
        const auto v = technical_rating::aggregate(build(in, cols), in);
        QCOMPARE(v.overall, S::Neutral);
        QCOMPARE(v.net, 0.0);
        QVERIFY2(v.basis.contains("Not enough history"), qPrintable(v.basis));
    }

    /// The counting guard on its own is not enough, and this is the case that
    /// proves it. On a one-month daily window SMA 50/200, MACD, ADX and Aroon
    /// are all still warming up, but three near-identical short averages plus
    /// CCI clear "3 trend voters" — and ADX being absent silently disables the
    /// trend filter, so oversold oscillators vote Buy unopposed. A stock 6%
    /// below every average must not come back anything but unrated.
    void falling_knife_on_a_short_window_is_not_a_buy() {
        RatingInput in;
        in.bars = 21;
        in.close = 94.0;
        // Flat short averages, price well beneath them.
        for (const char* ma : {"sma_20", "ema_12", "wma_9", "kama", "vwap"}) {
            in.now.set(ma, 100.0);
            in.prev.set(ma, 100.0);
        }
        // Oscillators washed out and ticking back up — maximally bullish read.
        in.now.set("cci", -200.0);  in.prev.set("cci", -205.0);
        in.now.set("rsi", 18.0);    in.prev.set("rsi", 15.0);
        in.now.set("stoch_k", 4.0); in.now.set("stoch_d", 3.0);
        in.now.set("williams_r", -98.0); in.prev.set("williams_r", -99.0);
        in.now.set("mfi", 12.0);    in.prev.set("mfi", 10.0);
        in.now.set("bb_pband", -0.10); in.prev.set("bb_pband", -0.15);
        in.now.set("roc", -8.0);
        in.now.set("cmf", -0.02);
        in.now.set("obv", 100.0);   in.back.set("obv", 99.0);
        in.now.set("adi", 100.0);   in.back.set("adi", 99.0);

        QVERIFY(!technical_rating::has_sufficient_history(in));
        const auto v = technical_rating::aggregate(score_all(in), in);
        QCOMPARE(v.overall, S::Neutral);
        QCOMPARE(v.net, 0.0);
        QVERIFY2(v.basis.contains("Not enough history"), qPrintable(v.basis));
    }

    /// The structural indicators are what make the trend evidence real, and
    /// what arms the trend filter. Missing any one of them means no rating.
    void history_gate_needs_every_structural_indicator() {
        QVERIFY(technical_rating::has_sufficient_history(uptrend()));
        for (const char* col : {"sma_50", "sma_200", "atr", "macd", "macd_signal",
                                "adx", "adx_pos", "adx_neg"}) {
            RatingInput in;
            in.close = uptrend().close;
            // Rebuild the uptrend snapshot minus one structural column.
            for (const char* c : {"sma_50", "sma_200", "atr", "macd", "macd_signal",
                                  "adx", "adx_pos", "adx_neg"})
                if (qstrcmp(c, col) != 0)
                    in.now.set(c, uptrend().now.get(c));
            QVERIFY2(!technical_rating::has_sufficient_history(in), col);
        }
    }

    // ── Scoring buckets ──────────────────────────────────────────────────────

    /// The weighted bucket must follow what an indicator *does*, not the panel
    /// it is drawn under. CCI is displayed with the trend indicators but is a
    /// mean-reversion oscillator; letting the display grouping set the weight
    /// put a contrarian vote inside the heaviest bucket, where it cancelled the
    /// trend evidence it was meant to counterbalance. KAMA and VWAP leak the
    /// other way — both are moving averages shown under momentum and volume.
    void scoring_bucket_follows_behaviour_not_display() {
        const auto scored = score_all(uptrend());
        QCOMPARE(find(scored, "cci").category, QString("trend"));
        QCOMPARE(find(scored, "cci").rating_bucket, QString("momentum"));
        QCOMPARE(find(scored, "kama").category, QString("momentum"));
        QCOMPARE(find(scored, "kama").rating_bucket, QString("trend"));
        QCOMPARE(find(scored, "vwap").category, QString("volume"));
        QCOMPARE(find(scored, "vwap").rating_bucket, QString("volume"));
        QCOMPARE(find(scored, "bb_pband").category, QString("volatility"));
        QCOMPARE(find(scored, "bb_pband").rating_bucket, QString("momentum"));
    }

    /// A bucket holding one voter swings to a full ±1.00 on that indicator
    /// alone. There must be no such bucket — BB %B was the last one.
    void no_bucket_rests_on_a_single_voter() {
        QHash<QString, int> counts;
        for (const auto& ti : score_all(uptrend()))
            if (ti.votes)
                counts[ti.rating_bucket]++;
        QVERIFY(!counts.isEmpty());
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            QVERIFY2(it.value() >= 3, qPrintable(QString("%1 has %2").arg(it.key()).arg(it.value())));
    }

    /// No indicators at all must not produce a confident anything.
    void empty_input_is_neutral() {
        const auto v = technical_rating::aggregate({}, RatingInput{});
        QCOMPARE(v.overall, S::Neutral);
        QCOMPARE(v.net, 0.0);
        QCOMPARE(v.voting, 0);
    }

    // ── The composite ────────────────────────────────────────────────────────

    /// The gauge is drawn as 50 + 50*net, so net leaving [-1, 1] would push the
    /// bar off its own scale.
    void net_stays_in_range() {
        for (const auto& in : {uptrend(), downtrend()}) {
            const auto v = technical_rating::aggregate(score_all(in), in);
            QVERIFY(v.net >= -1.0 && v.net <= 1.0);
        }
    }

    /// The verdict and the net score must never point opposite ways — that
    /// mismatch is exactly what the old gauge showed next to its label.
    void verdict_agrees_with_the_score() {
        for (const auto& in : {uptrend(), downtrend()}) {
            const auto v = technical_rating::aggregate(score_all(in), in);
            if (v.overall == S::StrongBuy || v.overall == S::Buy)
                QVERIFY(v.net > 0.0);
            if (v.overall == S::StrongSell || v.overall == S::Sell)
                QVERIFY(v.net < 0.0);
        }
    }

    void basis_names_the_two_terms_behind_the_verdict() {
        const auto v = technical_rating::aggregate(score_all(uptrend()), uptrend());
        QVERIFY2(v.basis.contains("MA alignment"), qPrintable(v.basis));
        QVERIFY2(v.basis.contains("50-day"), qPrintable(v.basis));
    }

    /// The headline is trend structure, not a count of the indicator table.
    ///
    /// Averaging ~20 correlated indicator votes described the trend worse than
    /// two chosen readings do — 79.8% against 96.0% on held-out data — so the
    /// tally is now reported and no longer decides. This pins that: a stock
    /// whose averages are perfectly stacked and whose price sits well above the
    /// 50-day must read strongly bullish even though most of its oscillators,
    /// being overbought, have been demoted to Neutral and contribute nothing.
    void verdict_follows_structure_not_the_tally() {
        const auto in = uptrend();
        const auto scored = score_all(in);
        int neutral = 0;
        for (const auto& ti : scored)
            if (ti.votes && ti.signal == S::Neutral)
                neutral++;
        QVERIFY2(neutral >= 4, "fixture should have several demoted oscillators");

        const auto v = technical_rating::aggregate(scored, in);
        QCOMPARE(v.overall, S::StrongBuy);
        QVERIFY2(v.net > 0.9, qPrintable(QString::number(v.net)));
    }

    /// A perfectly inverted ladder is the mirror image, to the same magnitude.
    void inverted_ladder_is_strongly_bearish() {
        const auto in = downtrend();
        const auto v = technical_rating::aggregate(score_all(in), in);
        QCOMPARE(v.overall, S::StrongSell);
        QVERIFY2(v.net < -0.9, qPrintable(QString::number(v.net)));
    }

    /// Alignment alone is not enough: price hugging a stacked ladder is a
    /// weaker statement than price extended above it, and the score must say so.
    void alignment_without_extension_scores_lower() {
        auto in = uptrend();
        in.close = in.now.get("sma_50") + 0.05; // stacked, but sitting on the 50-day
        const auto v = technical_rating::aggregate(score_all(in), in);
        QVERIFY2(v.net > 0.0 && v.net < 0.7, qPrintable(QString::number(v.net)));
    }
};

QTEST_MAIN(TestTechnicalRating)
#include "test_technical_rating.moc"

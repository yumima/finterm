// tests/screens/test_fingpt_prompts.cpp
//
// The FinGPT tab is a port of someone else's pipeline, and a port drifts in
// exactly the places a reader cannot see: the week boundaries (bar dates, not
// instants), the news-to-week assignment (event instants on the ET calendar),
// the deterministic 5-item sample that replaced upstream's random.sample, and
// the NaN gates that keep absent vendor fields out of the model's prompt.
// These cases pin each of those.

#include "screens/fingpt/FinGptPrompts.h"

#include <QtTest/QtTest>

using namespace fincept::screens::fingpt;
using fincept::services::equity::Candle;
using fincept::services::equity::StockInfo;

namespace {

// A daily bar for `date`: BAR stamps are midnight in the exchange's zone;
// UTC midnight is inside bar_date()'s half-open validity range, so it decodes
// back to the same date on any machine.
Candle bar(const QDate& date, double close) {
    Candle c;
    c.timestamp = QDateTime(date, QTime(0, 0), QTimeZone::utc()).toSecsSinceEpoch();
    c.close = close;
    c.open = c.high = c.low = close;
    return c;
}

// Weekday-only candles from `first` for `n` sessions, closes 100, 101, ...
QVector<Candle> sessions(const QDate& first, int n) {
    QVector<Candle> out;
    QDate d = first;
    double px = 100.0;
    while (out.size() < n) {
        if (d.dayOfWeek() <= 5)
            out.append(bar(d, px++));
        d = d.addDays(1);
    }
    return out;
}

NewsItem item(const QDateTime& when_utc, const QString& headline) {
    NewsItem n;
    n.published_utc = when_utc;
    n.headline = headline;
    n.summary = QStringLiteral("summary of ") + headline;
    return n;
}

} // namespace

class TestFinGptPrompts : public QObject {
    Q_OBJECT

  private slots:

    // ── Week slicing (the port of get_stock_data) ────────────────────────────
    void weekly_anchors_are_first_session_on_or_after_each_step() {
        // curday Wed 2026-09-30, 2 weeks back → steps Wed 9/16 and Wed 9/23.
        const QDate curday(2026, 9, 30);
        const auto candles = sessions(QDate(2026, 9, 14), 13);  // Mon 9/14 … Wed 9/30
        const auto slices = make_week_slices(candles, curday, 2);
        QCOMPARE(slices.size(), 2);
        QCOMPARE(slices[0].start, QDate(2026, 9, 16));  // the step itself is a session
        QCOMPARE(slices[0].end, QDate(2026, 9, 23));
        QCOMPARE(slices[1].start, QDate(2026, 9, 23));
        QCOMPARE(slices[1].end, QDate(2026, 9, 30));    // final anchor = last candle
    }

    void a_step_landing_on_a_weekend_anchors_on_monday() {
        // curday Sat 2026-10-03: the 1-week step is Sat 9/26 → anchor Mon 9/28.
        const QDate curday(2026, 10, 3);
        const auto candles = sessions(QDate(2026, 9, 21), 10);  // Mon 9/21 … Fri 10/2
        const auto slices = make_week_slices(candles, curday, 1);
        QCOMPARE(slices.size(), 1);
        QCOMPARE(slices[0].start, QDate(2026, 9, 28));
        QCOMPARE(slices[0].end, QDate(2026, 10, 2));
    }

    void missing_history_fails_instead_of_inventing_a_window() {
        // History starts AFTER the oldest step: upstream would crash; the port
        // must return empty, and the caller says so. Never a fabricated week.
        const QDate curday(2026, 9, 30);
        const auto candles = sessions(QDate(2026, 9, 29), 2);
        QVERIFY(make_week_slices(candles, curday, 3).isEmpty());
        QVERIFY(make_week_slices({}, curday, 1).isEmpty());
    }

    // ── News bucketing (event instants → ET calendar → session windows) ──────
    void news_lands_on_its_et_calendar_day() {
        const QDate curday(2026, 9, 30);
        const auto candles = sessions(QDate(2026, 9, 14), 13);
        auto slices = make_week_slices(candles, curday, 2);
        QCOMPARE(slices.size(), 2);

        // 2026-09-24 01:30 UTC is still 2026-09-23 in New York — an EVENT
        // stamp decoded in UTC would file this one week late.
        const QDateTime late_evening(QDate(2026, 9, 24), QTime(1, 30), QTimeZone::utc());
        // 2026-09-24 13:00 UTC is 09:00 ET the same day → second week.
        const QDateTime next_morning(QDate(2026, 9, 24), QTime(13, 0), QTimeZone::utc());
        bucket_news(slices, {item(late_evening, "A"), item(next_morning, "B")}, curday);

        QCOMPARE(slices[0].news.size(), 1);  // "A" belongs to the week ENDING 9/23
        QCOMPARE(slices[0].news[0].headline, QStringLiteral("A"));
        QCOMPARE(slices[1].news.size(), 1);
        QCOMPARE(slices[1].news[0].headline, QStringLiteral("B"));
    }

    void todays_news_after_the_last_close_belongs_to_the_final_week() {
        const QDate curday(2026, 10, 3);  // Saturday; last session Fri 10/2
        const auto candles = sessions(QDate(2026, 9, 21), 10);
        auto slices = make_week_slices(candles, curday, 1);
        QCOMPARE(slices.size(), 1);
        const QDateTime saturday(QDate(2026, 10, 3), QTime(15, 0), QTimeZone::utc());
        bucket_news(slices, {item(saturday, "weekend piece")}, curday);
        QCOMPARE(slices[0].news.size(), 1);
    }

    // ── Deterministic sampling (replaces upstream's random.sample) ───────────
    void sample_is_deterministic_spanning_and_capped() {
        QCOMPARE(sample_indices(3, 5), (QVector<int>{0, 1, 2}));
        const auto s = sample_indices(20, 5);
        QCOMPARE(s.size(), 5);
        QCOMPARE(s.first(), 0);
        QCOMPARE(s.last(), 19);
        for (int i = 1; i < s.size(); ++i)
            QVERIFY(s[i] > s[i - 1]);
        QCOMPARE(sample_indices(20, 5), s);              // same inputs, same picks
        QCOMPARE(sample_indices(7, 1), (QVector<int>{6}));  // one slot → freshest
        QVERIFY(sample_indices(0, 5).isEmpty());
    }

    // ── Prompt text: structure kept, absent data kept out ────────────────────
    void week_block_keeps_fingpt_wording_and_caps_news() {
        WeekSlice w;
        w.start = QDate(2026, 9, 23);
        w.end = QDate(2026, 9, 30);
        w.start_close = 100.0;
        w.end_close = 104.5;
        for (int i = 0; i < 9; ++i)
            w.news.append(item(QDateTime(QDate(2026, 9, 24), QTime(12, 0), QTimeZone::utc()),
                               QStringLiteral("headline %1").arg(i)));
        const QString block = week_block(QStringLiteral("AAPL"), w);
        QVERIFY(block.startsWith(QStringLiteral(
            "From 2026-09-23 to 2026-09-30, AAPL's stock price increased from 100.00 to 104.50")));
        QCOMPARE(block.count(QStringLiteral("[Headline]:")), 5);
        QVERIFY(!block.contains(QStringLiteral("No relative news reported.")));

        w.news.clear();
        w.end_close = 95.0;
        const QString empty_block = week_block(QStringLiteral("AAPL"), w);
        QVERIFY(empty_block.contains(QStringLiteral("decreased from 100.00 to 95.00")));
        QVERIFY(empty_block.endsWith(QStringLiteral("No relative news reported.")));
    }

    void absent_vendor_fields_never_reach_the_prompt() {
        // Default StockInfo is all-NaN — the vendor-null case. Neither "nan"
        // nor a zero dressed up as data may appear.
        StockInfo info;
        info.symbol = QStringLiteral("XYZ");
        const QString intro = company_intro(QStringLiteral("XYZ"), info);
        QVERIFY(!intro.contains(QStringLiteral("nan"), Qt::CaseInsensitive));
        QVERIFY(!intro.contains(QStringLiteral("market capitalization")));
        QCOMPARE(basics_block(QStringLiteral("XYZ"), info),
                 QStringLiteral("[Basic Financials]:\n\nNo basic financial reported."));

        info.pe_ratio = 24.5;
        info.total_debt = 0.0;  // a REAL zero is data, not absence
        const QString basics = basics_block(QStringLiteral("XYZ"), info);
        QVERIFY(basics.contains(QStringLiteral("peTTM: 24.50")));
        QVERIFY(basics.contains(QStringLiteral("totalDebt: 0.00 USD")));
        QVERIFY(!basics.contains(QStringLiteral("roe")));
    }

    void closing_instruction_names_the_next_week() {
        const QString c = closing_instruction(QStringLiteral("MSFT"), QDate(2026, 10, 2));
        QVERIFY(c.contains(QStringLiteral("Based on all the information before 2026-10-02")));
        QVERIFY(c.contains(QStringLiteral("next week (2026-10-02 to 2026-10-09)")));
    }

    // ── Sentiment instruction frame and label handling ───────────────────────
    void sentiment_frame_and_templates_match_fingpt() {
        QCOMPARE(sentiment_instruction_canonical(),
                 QStringLiteral("What is the sentiment of this news? Please choose an answer "
                                "from {negative/neutral/positive}."));
        QCOMPARE(sentiment_frame(QStringLiteral("I"), QStringLiteral("text")),
                 QStringLiteral("Instruction: I\nInput: text\nAnswer: "));
        const QStringList votes = sentiment_vote_instructions();
        QCOMPARE(votes.size(), 5);
        for (const QString& v : votes) {
            QVERIFY(v.contains(QStringLiteral("news")));
            QVERIFY(v.endsWith(QStringLiteral("Options: positive, negative, neutral")));
        }
    }

    void labels_parse_by_substring_and_votes_resolve_ties_neutral() {
        QCOMPARE(parse_sentiment_label(QStringLiteral("  Positive.")), QStringLiteral("positive"));
        QCOMPARE(parse_sentiment_label(QStringLiteral("the tone is NEGATIVE overall")),
                 QStringLiteral("negative"));
        QCOMPARE(parse_sentiment_label(QStringLiteral("mixed / unclear")), QStringLiteral("neutral"));

        QCOMPARE(vote_sentiment({"positive", "positive", "negative", "neutral", "neutral"}),
                 QStringLiteral("positive"));
        QCOMPARE(vote_sentiment({"positive", "negative"}), QStringLiteral("neutral"));
        QCOMPARE(vote_sentiment({}), QStringLiteral("neutral"));
    }
};

QTEST_MAIN(TestFinGptPrompts)
#include "test_fingpt_prompts.moc"

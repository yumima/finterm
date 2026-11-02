// test_news_regions.cpp — what lands under the US / CHN / EU news pills.
//
// A region pill gathers every category for one region, matched by the feed's
// region tag or by the region being named in the text. The negative cases
// matter most: whole-word matching keeps "eu" out of "neutral", "xi" out of
// "taxi", the pronoun "us" out of the US pill, and "Latin America" out of it too.

#include <QTest>

#include "services/news/NewsFeedMerge.h"
#include "services/news/NewsRegions.h"

using fincept::news::in_region;
using fincept::news::merge_feed_batch;
using fincept::services::NewsArticle;

namespace {
NewsArticle art(const QString& id, const QString& link, const QString& region) {
    NewsArticle a;
    a.id = id;
    a.link = link;
    a.region = region;
    // Distinct headlines that differ in letters, not just digits, so the
    // headline / series dedupe never joins them ("world-1" vs "world-2").
    QString h = id;
    for (auto& c : h)
        if (c.isDigit())
            c = QChar('a' + c.digitValue());
    a.headline = QStringLiteral("Story ") + h;
    return a;
}

NewsArticle story(const QString& source, const QString& headline, qint64 ts, int tier = 4,
                  const QString& link = {}, QStringList tickers = {}) {
    NewsArticle a;
    a.source = source;
    a.headline = headline;
    a.sort_ts = ts;
    a.tier = tier;
    a.link = link.isEmpty() ? QStringLiteral("https://%1.test/%2").arg(source.toLower()).arg(ts) : link;
    a.tickers = tickers;
    a.region = QStringLiteral("GLOBAL");
    return a;
}
} // namespace
using fincept::news::is_region_code;

class TestNewsRegions : public QObject {
    Q_OBJECT
  private slots:
    void codes() {
        QVERIFY(is_region_code("US"));
        QVERIFY(is_region_code("CHN"));
        QVERIFY(is_region_code("EU"));
        QVERIFY(!is_region_code("ALL"));
        QVERIFY(!is_region_code("TECH"));
        QVERIFY(!in_region("ALL", "US", "U.S. stocks rally", {}));
    }

    void feed_tag_alone_is_enough() {
        QVERIFY(in_region("US", "US", "Quarterly results beat estimates", {}));
        QVERIFY(in_region("CHN", "CHINA", "Property developer misses bond payment", {}));
        QVERIFY(in_region("EU", "EU", "Parliament votes on budget", {}));
        QVERIFY(!in_region("EU", "UK", "Parliament votes on budget", {}));
        QVERIFY(!in_region("CHN", "ASIA", "Property developer misses bond payment", {}));
    }

    void us_from_text() {
        QVERIFY(in_region("US", "GLOBAL", "U.S. jobs report beats forecasts", {}));
        QVERIFY(in_region("US", "GLOBAL", "US inflation cools in October", {}));
        QVERIFY(in_region("US", "GLOBAL", "Fed's Powell signals pause", {}));
        QVERIFY(in_region("US", "GLOBAL", "Stocks slip as S&P 500 retreats", {}));
        QVERIFY(in_region("US", "GLOBAL", "Ministers meet", "Talks were held in Washington on Monday."));
        QVERIFY(in_region("US", "GLOBAL", "American chipmakers face new export rules", {}));
    }

    void us_negatives() {
        QVERIFY(!in_region("US", "GLOBAL", "Tell us what you think about the budget", {}));
        QVERIFY(!in_region("US", "GLOBAL", "JOIN US FOR THE LIVE BRIEFING", {}));
        QVERIFY(!in_region("US", "GLOBAL", "Latin America's lithium boom draws miners", {}));
        QVERIFY(!in_region("US", "GLOBAL", "Brazil leads South American growth", {}));
        QVERIFY(!in_region("US", "GLOBAL", "Museum opens new exhibition", {}));
        // "US$" is a currency, the way Asian outlets write every dollar amount.
        QVERIFY(!in_region("US", "GLOBAL", "Tencent raises US$5bn in bond sale", {}));
        QVERIFY(in_region("US", "GLOBAL", "US fines Tencent US$5bn", {}));
        // "fed" the verb is not the Fed; only the capitalised name counts.
        QVERIFY(!in_region("US", "GLOBAL", "Investors fed up with volatility", {}));
        QVERIFY(!in_region("US", "GLOBAL", "Fed up with rents, tenants march", {}));
        QVERIFY(in_region("US", "GLOBAL", "Fed holds rates steady", {}));
        QVERIFY(in_region("US", "GLOBAL", "Markets rally after Fed decision", {}));
        // Shouted runs carry no meaning in their capitals.
        QVERIFY(!in_region("US", "GLOBAL", "JOIN US LIVE: the Bank of England decision", {}));
        QVERIFY(!in_region("US", "GLOBAL", "TELL US WHAT YOU THINK: readers on housing", {}));
        QVERIFY(in_region("US", "GLOBAL", "US GDP grows 2.1% in third quarter", {}));
        // Adjacent exclusions are all removed, not just the first.
        QVERIFY(!in_region("US", "GLOBAL", "Latin America Latin America summit opens", {}));
    }

    void chn_from_text() {
        QVERIFY(in_region("CHN", "GLOBAL", "China's exports jump 8%", {}));
        QVERIFY(in_region("CHN", "GLOBAL", "PBOC cuts reserve requirement", {}));
        QVERIFY(in_region("CHN", "GLOBAL", "Huawei unveils new AI chip", {}));
        QVERIFY(in_region("CHN", "GLOBAL", "Hang Seng falls 2%", {}));
        QVERIFY(in_region("CHN", "GLOBAL", "Talks", "President Xi met the delegation in Beijing."));
        QVERIFY(!in_region("CHN", "GLOBAL", "Taxi drivers strike in Lagos", {}));
    }

    void eu_from_text() {
        QVERIFY(in_region("EU", "GLOBAL", "EU fines tech giant €2bn", {}));
        QVERIFY(in_region("EU", "GLOBAL", "ECB holds rates steady", {}));
        QVERIFY(in_region("EU", "GLOBAL", "Germany's factory orders slump", {}));
        QVERIFY(in_region("EU", "GLOBAL", "Eurozone inflation ticks up", {}));
        QVERIFY(in_region("EU", "GLOBAL", "Macron calls snap vote", {}));
        QVERIFY(!in_region("EU", "GLOBAL", "Analysts stay neutral on oil", {}));
        // Accented spellings fold to the ASCII keyword.
        QVERIFY(in_region("EU", "GLOBAL", "Orbán vetoes aid package", {}));
        QVERIFY(!in_region("EU", "GLOBAL", "Workers polish the new terminal floor", {}));
    }

    // Only the summary's lede counts: a region mentioned in passing deep in a
    // long summary is not what the story is about.
    void summary_lede_only() {
        const QString filler = QString(fincept::news::kSummaryLedeChars, QLatin1Char('x')) + QLatin1Char(' ');
        QVERIFY(in_region("CHN", "GLOBAL", "Ebola outbreak spreads", "Officials in Beijing warned on Friday."));
        QVERIFY(!in_region("CHN", "GLOBAL", "Ebola outbreak spreads",
                           filler + "It was compared to Covid, which first took hold in China."));
        QVERIFY(!in_region("US", "GLOBAL", "Ebola outbreak spreads", filler + "The US pledged aid."));
        // The cut lands on a word boundary: "Eurovision" straddling it must
        // not become the EU keyword "euro".
        const QString pad = QString(fincept::news::kSummaryLedeChars - 4, QLatin1Char('x')) + QLatin1Char(' ');
        QVERIFY(!in_region("EU", "GLOBAL", "Song contest", pad + "Eurovision fans gather."));
    }

    // One story syndicated into two feeds of the same publisher is one row,
    // and the region-desk copy's tag survives whichever copy arrived first.
    void merge_dedupes_by_link_and_keeps_specific_region() {
        QVector<NewsArticle> all;
        fincept::news::FeedMergeIndex idx;
        merge_feed_batch(all, idx, {art("world-1", "https://bbc.test/a", "GLOBAL"),
                                    art("world-2", "https://bbc.test/b", "GLOBAL"), art("nolink-1", "", "GLOBAL")});
        merge_feed_batch(all, idx, {art("china-1", " https://bbc.test/a ", "CHINA"), art("nolink-2", "", "CHINA")});
        QCOMPARE(all.size(), 4);
        QCOMPARE(all[0].id, QStringLiteral("world-1"));
        QCOMPARE(all[0].region, QStringLiteral("CHINA"));
        QCOMPARE(all[1].region, QStringLiteral("GLOBAL"));

        // A specific tag already kept is never overwritten, and GLOBAL never
        // downgrades one.
        merge_feed_batch(all, idx, {art("eu-1", "https://bbc.test/a", "EU"), art("w-3", "https://bbc.test/a", "GLOBAL")});
        QCOMPARE(all.size(), 4);
        QCOMPARE(all[0].region, QStringLiteral("CHINA"));

        // Not just GLOBAL: any non-pill tag (SCMP News is ASIA) yields to a
        // pill tag, and a non-pill tag never replaces another.
        merge_feed_batch(all, idx, {art("scmp-news", "https://scmp.test/x", "ASIA")});
        merge_feed_batch(all, idx, {art("bbc-eu", "https://scmp.test/x", "EUROPE")});
        QCOMPARE(all.last().region, QStringLiteral("ASIA"));
        merge_feed_batch(all, idx, {art("scmp-china", "https://scmp.test/x", "CHINA")});
        QCOMPARE(all.last().region, QStringLiteral("CHINA"));
    }

    // The same wire story under two outlets' links is one row: the better
    // tier wins, the tickers are joined, a pill tag survives.
    void merge_same_headline_other_link_is_one_row() {
        QVector<NewsArticle> all;
        fincept::news::FeedMergeIndex idx;
        auto inv = story("INVESTING.COM", "Stellantis chip shortage disrupts Fiat output in Italy, may last through December", 100, 3,
                         {}, {"STLA"});
        auto cna = story("CNA", "Stellantis chip shortage disrupts Fiat output in Italy — may last through December", 90, 2,
                         {}, {"F"});
        cna.region = QStringLiteral("EU");
        merge_feed_batch(all, idx, {inv});
        merge_feed_batch(all, idx, {cna});
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].source, QStringLiteral("CNA")); // tier 2 beats tier 3
        QCOMPARE(all[0].region, QStringLiteral("EU"));
        QCOMPARE(all[0].tickers, (QStringList{"STLA", "F"}));
        QCOMPARE(all[0].similar, 0); // a copy, not a sibling
        QCOMPARE(all[0].also_sources, QStringList{"INVESTING.COM"}); // clustering still counts 2 outlets

        // A later worse-tier repost (new link) never displaces the better copy.
        merge_feed_batch(all, idx, {story("INVESTING.COM", "Stellantis chip shortage disrupts Fiat output in Italy, may last through December", 400, 3)});
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].source, QStringLiteral("CNA"));

        // Same outlet, same title, new link minutes later: one row, at the
        // first report (a repost doesn't make it fresh), as urgent as its
        // loudest copy.
        auto repost = story("ABNORMAL RETURNS", "Top clicks this week on Abnormal Returns", 700);
        repost.priority = fincept::services::Priority::URGENT;
        merge_feed_batch(all, idx, {story("ABNORMAL RETURNS", "Top clicks this week on Abnormal Returns", 50)});
        merge_feed_batch(all, idx, {repost});
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[1].sort_ts, qint64(50));
        QVERIFY(all[1].priority == fincept::services::Priority::URGENT);

        // A same-outlet row out of the window doesn't hide another outlet's fresh row.
        {
            QVector<NewsArticle> rows;
            fincept::news::FeedMergeIndex ix;
            merge_feed_batch(rows, ix, {story("YAHOO", "Fed officials signal patience on further rate cuts", 100'000),
                                        story("REUTERS", "Fed officials signal patience on further rate cuts", 300'000, 1),
                                        story("YAHOO", "Fed officials signal patience on further rate cuts", 300'600)});
            QCOMPARE(rows.size(), 2);
            QCOMPARE(rows.last().source, QStringLiteral("REUTERS"));
            QCOMPARE(rows.last().also_sources, QStringList{"YAHOO"});
        }

        // A daily repost of one title (a live blog) is one row per day, and a
        // chain of reposts can't creep along the window (span, not last gap).
        merge_feed_batch(all, idx, {story("YAHOO", "Stock market today Dow S&P 500 Nasdaq", 1000),
                                    story("YAHOO", "Stock market today Dow S&P 500 Nasdaq", 1000 + 24 * 3600),
                                    story("YAHOO", "Stock market today Dow S&P 500 Nasdaq", 1000 + 10 * 3600),
                                    story("YAHOO", "Stock market today Dow S&P 500 Nasdaq", 1000 + 20 * 3600)});
        // 0h + 10h fold; 24h and 20h fold (20h would stretch the first row to 20h > 18h).
        QCOMPARE(all.size(), 4);
        // A dated copy is never displaced by an undated one, and a row keeps
        // its id whichever copy it shows.
        merge_feed_batch(all, idx, {story("AP", "Undated wire copy must not blank the time", 5000, 2, "https://ap.test/u")});
        const QString kept_id = all.last().id;
        auto undated = story("AP", "Undated wire copy must not blank the time", 0, 1, "https://ap.test/u");
        undated.id = QStringLiteral("other-id");
        merge_feed_batch(all, idx, {undated});
        QCOMPARE(all.size(), 5);
        QCOMPARE(all.last().sort_ts, qint64(5000));
        auto newer = story("AP", "Undated wire copy must not blank the time", 6000, 1, "https://ap.test/u2");
        newer.id = QStringLiteral("newer-id");
        merge_feed_batch(all, idx, {newer});
        QCOMPARE(all.last().sort_ts, qint64(6000)); // the better copy shows…
        QCOMPARE(all.last().id, kept_id);           // …under the row's original id

        // Undated items fold only by link.
        merge_feed_batch(all, idx, {story("RTE", "Irish budget passes final vote in the Dail", 0, 4, "https://rte.test/a"),
                                    story("RTE", "Irish budget passes final vote in the Dail", 0, 4, "https://rte.test/b")});
        QCOMPARE(all.size(), 7);

        // A short generic title from two outlets is two stories.
        merge_feed_batch(all, idx, {story("NYT", "Here's the latest", 10), story("CNN", "Here's the latest", 11)});
        QCOMPARE(all.size(), 9);
        // Accents and thousands separators don't split a story.
        merge_feed_batch(all, idx, {story("FT", "Société Générale cuts 2,000 jobs after weak quarter", 20, 2),
                                    story("REUTERS", "Societe Generale cuts 2000 jobs after weak quarter", 21, 1)});
        QCOMPARE(all.size(), 10);
        QCOMPARE(all.last().source, QStringLiteral("REUTERS"));
        QCOMPARE(all.last().also_sources, QStringList{"FT"});
    }

    // A templated series from one source folds into its newest item with a count;
    // numbers alone never join items across sources or days.
    void merge_templated_series_folds_with_count() {
        QVector<NewsArticle> all;
        fincept::news::FeedMergeIndex idx;
        const qint64 t0 = 1'780'000'000;
        merge_feed_batch(all, idx,
                         {story("SEEKING ALPHA", "iShares iBonds Dec 2033 Term Corporate ETF declares monthly distribution of $0.1043", t0),
                          story("SEEKING ALPHA", "iShares iBonds Dec 2055 Term Treasury ETF declares monthly distribution of $0.0959", t0 + 240),
                          story("SEEKING ALPHA", "iShares iBonds Dec 2054 Term Treasury ETF declares monthly distribution of $0.0930", t0 + 180),
                          story("SEEKING ALPHA", "iShares iBonds Dec 2045 Term Treasury ETF declares monthly distribution of $0.0814", t0 + 120),
                          story("SEEKING ALPHA", "iShares iBonds Dec 2034 Term Corporate ETF declares monthly distribution of $0.1050", t0 + 60)});
        QCOMPARE(all.size(), 2); // Treasury x3, Corporate x2
        QCOMPARE(all[0].headline.left(25), QStringLiteral("iShares iBonds Dec 2034 T"));
        QCOMPARE(all[0].similar, 1);
        QVERIFY(all[1].headline.contains("Dec 2055"));
        QCOMPARE(all[1].similar, 2);

        // Another source, or the same template two days later: separate rows.
        merge_feed_batch(all, idx,
                         {story("BENZINGA", "iShares iBonds Dec 2044 Term Treasury ETF declares monthly distribution of $0.0950", t0),
                          story("SEEKING ALPHA", "iShares iBonds Dec 2035 Term Corporate ETF declares monthly distribution of $0.1001",
                                t0 + 3 * 86400),
                          // a separate numeric event the next day is not a "series"
                          story("SEEKING ALPHA", "iShares iBonds Dec 2036 Term Corporate ETF declares monthly distribution of $0.1002",
                                t0 + 3 * 86400 + 60)});
        QCOMPARE(all.size(), 4); // 2035 and 2036 (a minute apart) fold together
        QCOMPARE(all.last().similar, 1);

        // A headline without numbers is never a "series" of a different one.
        merge_feed_batch(all, idx, {story("SEEKING ALPHA", "Oshkosh partners with Grid Aero", t0 + 300)});
        QCOMPARE(all.size(), 5);
        // CJK headlines have no spaces: each character counts as a word.
        QVERIFY(fincept::news::headline_keys(QStringLiteral("美联储维持利率不变")).words >= fincept::news::kMinCrossSourceWords);
        QCOMPARE(fincept::news::headline_keys("Oil at $81.50, up 2%").series, QStringLiteral("oil at # up #"));
        QCOMPARE(fincept::news::headline_keys("Oil at $81.50, up 2,000").story, QStringLiteral("oil at 81.50 up 2000"));
    }

    void one_story_two_regions() {
        const QString h = QStringLiteral("US raises tariffs on Chinese EVs");
        QVERIFY(in_region("US", "GLOBAL", h, {}));
        QVERIFY(in_region("CHN", "GLOBAL", h, {}));
        QVERIFY(!in_region("EU", "GLOBAL", h, {}));
    }
};

QTEST_GUILESS_MAIN(TestNewsRegions)
#include "test_news_regions.moc"

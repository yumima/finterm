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
    a.headline = id;
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
        QHash<QString, qsizetype> idx;
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

    void one_story_two_regions() {
        const QString h = QStringLiteral("US raises tariffs on Chinese EVs");
        QVERIFY(in_region("US", "GLOBAL", h, {}));
        QVERIFY(in_region("CHN", "GLOBAL", h, {}));
        QVERIFY(!in_region("EU", "GLOBAL", h, {}));
    }
};

QTEST_GUILESS_MAIN(TestNewsRegions)
#include "test_news_regions.moc"

// test_news_tagging.cpp — tickers and the MKT category are tagged on evidence.
//
// Two screenshot regressions (2026-11-13): headline tags like $COO, $CEO, $VP,
// $NSW, $UN, $EU, $AI, $US, $FAA, $EPS, $GAAP, $ETF (any all-caps word was a
// "ticker"), and sport / pop-music stories under MKT (a business feed's
// default category of MARKETS stood whenever no keyword matched).

#include "services/news/NewsCategories.h"
#include "services/news/NewsPromo.h"
#include "services/news/NewsTickers.h"

#include <QTest>

using finterm::news::extract_tickers;
using finterm::news::refine_category;

class TestNewsTagging : public QObject {
    Q_OBJECT
  private slots:
    void acronyms_titles_agencies_places_are_not_tickers() {
        const QStringList headlines = {
            "COO and CEO depart as VP takes over at regional lender",
            "MP says NSW budget faces UN scrutiny as EU weighs AI rules",
            "US and USA officials: FAA grounds fleet; EPS beat on GAAP basis",
            "ETF inflows hit record as IPO window reopens",
        };
        for (const auto& h : headlines)
            QVERIFY2(extract_tickers(h).isEmpty(), qPrintable(h + " -> " + extract_tickers(h).join(',')));
    }

    void stoplist_holds_even_with_a_dollar_sign() {
        QVERIFY(extract_tickers("Fund flows into $ETF products and $AI names").isEmpty());
        QVERIFY(extract_tickers("Costs $US100m, or $5bn in total").isEmpty());
    }

    void explicit_cashtags_are_tickers() {
        QCOMPARE(extract_tickers("Traders pile into $NVDA and $AMD ahead of results"), QStringList({"NVDA", "AMD"}));
        QCOMPARE(extract_tickers("Buffett adds to $BRK.B"), QStringList({"BRK.B"}));
    }

    void exchange_context_is_definitive() {
        // COO is a stoplisted title — but "(NYSE: COO)" is The Cooper Companies.
        QCOMPARE(extract_tickers("The Cooper Companies (NYSE: COO) reports Q3"), QStringList({"COO"}));
        QCOMPARE(extract_tickers("C3.ai, Inc. (NYSE:AI) announces"), QStringList({"AI"}));
        QCOMPARE(extract_tickers("Acme Corp (Nasdaq: ACME) prices offering"), QStringList({"ACME"}));
        // Lower-case after the exchange word is prose, not a symbol.
        QVERIFY(extract_tickers("listed on nasdaq: shares rose").isEmpty());
    }

    void bare_symbols_only_from_the_known_list() {
        QCOMPARE(extract_tickers("NVDA slips while TSLA gains"), QStringList({"NVDA", "TSLA"}));
        QVERIFY(extract_tickers("ACME and FOO rally").isEmpty());
    }

    void order_dedupe_and_cap() {
        QCOMPARE(extract_tickers("$AAPL (NASDAQ: AAPL) $MSFT"), QStringList({"AAPL", "MSFT"}));
        QCOMPARE(extract_tickers("$A $B $C $D $E $F $G").size(), 5);
    }

    void market_feed_default_needs_market_evidence() {
        const QString mk = QStringLiteral("MARKETS");
        // Sport and pop music from a business/latest feed: out of MKT.
        QCOMPARE(refine_category(mk, QStringLiteral("liverpool beat chelsea 2-1 in premier league thriller")),
                 QStringLiteral("GENERAL"));
        QCOMPARE(refine_category(mk, QStringLiteral("singer announces new album and world tour dates")),
                 QStringLiteral("GENERAL"));
        QCOMPARE(refine_category(mk, QStringLiteral("k-pop group tops charts with comeback single")),
                 QStringLiteral("GENERAL"));
        QCOMPARE(refine_category(mk, QStringLiteral("heavy rain expected across the region this weekend")),
                 QStringLiteral("GENERAL"));
        // Real market stories keep the default.
        QCOMPARE(refine_category(mk, QStringLiteral("european stocks slip as bond yields climb")), mk);
        QCOMPARE(refine_category(mk, QStringLiteral("dollar firms against the yen before payrolls")), mk);
        QCOMPARE(refine_category(mk, QStringLiteral("unilever to acquire beauty brand in $2bn deal")), mk);
        // Words with a business sense are not sport/entertainment markers.
        QCOMPARE(refine_category(mk, QStringLiteral("rivian price cut fails to match rival discounts")), mk);
        QCOMPARE(refine_category(mk, QStringLiteral("bond yields jump in a brief volatility episode")), mk);
        // Sport WITH strong market evidence is still a market story.
        QCOMPARE(refine_category(mk, QStringLiteral("manchester united shares fall after premier league exit")), mk);
    }

    void keyword_match_and_other_feed_defaults_are_unchanged() {
        QCOMPARE(refine_category(QStringLiteral("MARKETS"), QStringLiteral("bitcoin tops $100k")),
                 QStringLiteral("CRYPTO"));
        QCOMPARE(refine_category(QStringLiteral("GEOPOLITICS"), QStringLiteral("football final draws record crowd")),
                 QStringLiteral("GEOPOLITICS"));
        // Idempotent — the cache loader re-applies it.
        const QString t = QStringLiteral("singer announces new album");
        QCOMPARE(refine_category(refine_category(QStringLiteral("MARKETS"), t), t), QStringLiteral("GENERAL"));
    }

    void promo_spam_is_recognised() {
        using finterm::news::is_promo_spam;
        // Ranked #3 in INTEL › TOP STORIES on 2026-11-13.
        QVERIFY(is_promo_spam("Chatbooks Promo Code: 40% Off"));
        QVERIFY(is_promo_spam("Best Buy Coupons for November"));
        QVERIFY(is_promo_spam("The best Black Friday TV deals we've found"));
        QVERIFY(is_promo_spam("Save 25% off with this discount code"));
        // Market language that merely looks like it.
        QVERIFY(!is_promo_spam("Shares trade 40% off their highs after guidance cut"));
        QVERIFY(!is_promo_spam("Musk says it is the best deal for shareholders"));
        QVERIFY(!is_promo_spam("Retailers brace for a weak holiday sales season"));
    }
};

QTEST_APPLESS_MAIN(TestNewsTagging)
#include "test_news_tagging.moc"

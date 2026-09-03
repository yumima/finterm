// test_news_brief_selection.cpp — what the AI news brief is written from.
//
// Everything here is a sampling property, and sampling failures in this
// pipeline are invisible at the other end: the brief looks well-formed, reads
// fluently, and simply does not mention half the day. The two that shipped
// were "six outlets carrying one story consumed six of the 35 slots" and "a
// heavy MARKETS morning filled every slot, so the breakdown could not name
// CRYPTO even though the feed was full of it". Neither logged anything.
//
// The third property is consistency: two runs over the same feed must select
// the same stories in the same order, or the brief rewords itself for reasons
// the reader cannot see and has no way to distinguish from the news changing.

#include <QTest>

#include "services/news/NewsBriefPrompts.h"
#include "services/news/NewsBriefSelection.h"
#include "services/news/NewsClusterService.h"

using namespace fincept::news::brief_select;
using fincept::services::NewsArticle;
using fincept::services::NewsCluster;
using fincept::services::cluster_articles;

namespace {

constexpr int64_t kNow = 1'750'000'000;

NewsArticle article(const QString& headline, const QString& category, const QString& source,
                    int tier = 2, int64_t ts = kNow, const QStringList& tickers = {}) {
    NewsArticle a;
    a.headline = headline;
    a.summary = headline + QStringLiteral(" — wire copy follows.");
    a.link = QStringLiteral("https://example.test/") + source.toLower() + QStringLiteral("/")
             + QString::number(qHash(headline));
    a.source = source;
    a.category = category;
    a.tier = tier;
    a.sort_ts = ts;
    a.tickers = tickers;
    return a;
}

/// A cluster built by hand, for the ranking tests that should not also depend
/// on the tokenizer's idea of what is similar.
NewsCluster cluster(const QString& headline, const QString& category, int source_count, int tier,
                    int64_t ts, const QStringList& tickers = {}) {
    NewsCluster c;
    c.lead_article = article(headline, category, QStringLiteral("Wire"), tier, ts, tickers);
    c.articles = {c.lead_article};
    c.source_count = source_count;
    c.category = category;
    c.tier = tier;
    c.latest_sort_ts = ts;
    return c;
}

QStringList headlines_of(const QVector<Story>& stories) {
    QStringList out;
    for (const Story& s : stories)
        out << s.headline;
    return out;
}

QStringList categories_of(const QVector<Story>& stories) {
    QStringList out;
    for (const Story& s : stories) {
        if (!out.contains(s.category))
            out << s.category;
    }
    return out;
}

} // namespace

class TestNewsBriefSelection : public QObject {
    Q_OBJECT

  private slots:

    // ── One story, not one article per outlet ───────────────────────────────

    void outlets_carrying_one_story_cost_one_slot() {
        // The same story off six wires, plus five unrelated ones. Under the old
        // "newest N articles" sampling the six copies crowded out five other
        // stories and the model wrote the Fed decision six times.
        QVector<NewsArticle> pool;
        for (const QString& src : {"Reuters", "AP", "Bloomberg", "WSJ", "BBC", "CNBC"}) {
            pool << article(QStringLiteral("Federal Reserve holds interest rate steady at 4.25%"),
                            QStringLiteral("ECONOMIC"), src, 1);
        }
        pool << article(QStringLiteral("Bitcoin slips below $60,000 as ETF outflows continue"),
                        QStringLiteral("CRYPTO"), QStringLiteral("CoinDesk"), 2);
        pool << article(QStringLiteral("Shell announces North Sea platform decommissioning"),
                        QStringLiteral("ENERGY"), QStringLiteral("OilPrice"), 2);
        pool << article(QStringLiteral("Nvidia unveils next-generation datacentre accelerator"),
                        QStringLiteral("TECH"), QStringLiteral("Wired"), 2);

        const QVector<Story> chosen = select_stories(cluster_articles(pool), {});

        // Four distinct stories out of nine articles.
        QCOMPARE(chosen.size(), 4);
        int fed = 0;
        for (const Story& s : chosen) {
            if (s.headline.contains(QStringLiteral("Federal Reserve")))
                ++fed;
        }
        QCOMPARE(fed, 1);

        // And the six outlets became evidence rather than noise: the story
        // everyone carried leads.
        QVERIFY(chosen.first().headline.contains(QStringLiteral("Federal Reserve")));
        QCOMPARE(chosen.first().source_count, 6);
    }

    // ── Every category present gets a slot ──────────────────────────────────

    void a_dominant_category_cannot_starve_the_others() {
        // Thirty MARKETS stories, each stronger than the single CRYPTO one,
        // and a budget of five. Ranking alone fills all five with MARKETS and
        // the breakdown then has no crypto to write about — the exact "does
        // not cover the whole day" failure.
        QVector<NewsCluster> clusters;
        for (int i = 0; i < 30; ++i) {
            clusters << cluster(QStringLiteral("Markets story %1").arg(i),
                                QStringLiteral("MARKETS"), 5, 1, kNow);
        }
        clusters << cluster(QStringLiteral("A small crypto story"), QStringLiteral("CRYPTO"), 1, 4,
                            kNow - 3600);
        clusters << cluster(QStringLiteral("A small energy story"), QStringLiteral("ENERGY"), 1, 4,
                            kNow - 3600);

        Config cfg;
        cfg.max_stories = 5;
        const QVector<Story> chosen = select_stories(clusters, {}, cfg);

        QCOMPARE(chosen.size(), 5);
        const QStringList cats = categories_of(chosen);
        QVERIFY2(cats.contains(QStringLiteral("CRYPTO")), qPrintable(cats.join(',')));
        QVERIFY2(cats.contains(QStringLiteral("ENERGY")), qPrintable(cats.join(',')));
        QVERIFY(cats.contains(QStringLiteral("MARKETS")));
    }

    void the_guarantee_never_costs_more_slots_than_the_budget() {
        // One story in each of eight categories, budget of three. The
        // guarantee must yield to the budget rather than overrun it, and the
        // three it keeps must be the three strongest categories.
        QVector<NewsCluster> clusters;
        int strength = 8;
        for (const QString& cat : fincept::news::category_names())
            clusters << cluster(QStringLiteral("Story for ") + cat, cat, strength--, 1, kNow);

        Config cfg;
        cfg.max_stories = 3;
        const QVector<Story> chosen = select_stories(clusters, {}, cfg);

        QCOMPARE(chosen.size(), 3);
        QCOMPARE(categories_of(chosen).size(), 3);
        // EARNINGS was seeded with the widest coverage, so it must survive.
        QVERIFY(categories_of(chosen).contains(QStringLiteral("EARNINGS")));
    }

    void a_negligible_story_cannot_displace_a_real_one_to_fill_a_heading() {
        // Two wire-wide MARKETS stories and one stale single-blog DEFENSE
        // item, with room for two. The guarantee is allowed to displace a
        // stronger story to keep a category represented — but not with
        // something twenty times weaker than the day's lead, which buys a
        // DEFENSE heading at the cost of a story the reader actually needs.
        QVector<NewsCluster> clusters{
            cluster(QStringLiteral("Everyone carried this"), QStringLiteral("MARKETS"), 40, 1, kNow),
            cluster(QStringLiteral("And this"), QStringLiteral("MARKETS"), 30, 1, kNow),
            cluster(QStringLiteral("A stale nothing"), QStringLiteral("DEFENSE"), 1, 4,
                    kNow - 11 * 3600),
        };
        Config cfg;
        cfg.max_stories = 2;
        const QVector<Story> chosen = select_stories(clusters, {}, cfg);
        QCOMPARE(headlines_of(chosen),
                 (QStringList{QStringLiteral("Everyone carried this"), QStringLiteral("And this")}));

        // It is a floor on the GUARANTEE, not a filter on the brief: given
        // room, the straggler still comes in on rank.
        cfg.max_stories = 3;
        QVERIFY(categories_of(select_stories(clusters, {}, cfg))
                    .contains(QStringLiteral("DEFENSE")));
    }

    void categories_present_is_the_heading_list_the_stories_support() {
        QVector<NewsCluster> clusters{
            cluster(QStringLiteral("Big markets story"), QStringLiteral("MARKETS"), 6, 1, kNow),
            cluster(QStringLiteral("Small tech story"), QStringLiteral("TECH"), 1, 3, kNow),
        };
        const QVector<Story> chosen = select_stories(clusters, {});
        // Rank order, and nothing the selection cannot supply a bullet for.
        QCOMPARE(categories_present(chosen),
                 (QStringList{QStringLiteral("MARKETS"), QStringLiteral("TECH")}));

        Config narrow;
        narrow.max_categories = 1;
        QCOMPARE(categories_present(chosen, narrow), (QStringList{QStringLiteral("MARKETS")}));
    }

    // ── Ranking ─────────────────────────────────────────────────────────────

    void breadth_authority_and_recency_all_move_the_rank() {
        const auto score = [](int sources, int tier, int64_t age) {
            return score_cluster(cluster(QStringLiteral("x"), QStringLiteral("MARKETS"), sources,
                                         tier, kNow - age),
                                 kNow, false, {});
        };
        QVERIFY(score(6, 2, 0) > score(1, 2, 0));   // more outlets
        QVERIFY(score(2, 1, 0) > score(2, 4, 0));   // better outlet
        QVERIFY(score(2, 2, 0) > score(2, 2, 6 * 3600)); // fresher
        // A half-life, not a cliff: six hours costs half the score, not the story.
        QVERIFY(score(2, 2, 6 * 3600) > 0.0);
    }

    void a_holding_lifts_a_story_without_letting_it_lead() {
        Config cfg;
        const NewsCluster held = cluster(QStringLiteral("Apple ships a thing"),
                                         QStringLiteral("TECH"), 1, 2, kNow, {QStringLiteral("AAPL")});
        const NewsCluster huge = cluster(QStringLiteral("Fed cuts by 50bp"),
                                         QStringLiteral("ECONOMIC"), 12, 1, kNow);
        const QSet<QString> holdings{QStringLiteral("AAPL")};
        QVERIFY(score_cluster(held, kNow, true, cfg) > score_cluster(held, kNow, false, cfg));
        QVERIFY(score_cluster(huge, kNow, false, cfg) > score_cluster(held, kNow, true, cfg));

        const QVector<Story> chosen = select_stories({held, huge}, holdings, cfg);
        QCOMPARE(chosen.size(), 2);
        QCOMPARE(chosen.first().headline, QStringLiteral("Fed cuts by 50bp"));
        for (const Story& s : chosen)
            QCOMPARE(s.portfolio_hit, s.headline.startsWith(QStringLiteral("Apple")));
    }

    void the_exposure_named_is_the_holding_matched_not_the_lead_tags() {
        // The match may come from any article in the cluster, while `tickers`
        // is only the lead article's. Reporting the latter as the exposure
        // names symbols the reader does not hold and omits the one they do.
        NewsCluster c = cluster(QStringLiteral("Chip supply chain wobbles"), QStringLiteral("TECH"),
                                2, 2, kNow, {QStringLiteral("TSM"), QStringLiteral("ASML")});
        c.articles << article(QStringLiteral("Analysts cut Apple unit estimates"),
                              QStringLiteral("TECH"), QStringLiteral("CNBC"), 2, kNow,
                              {QStringLiteral("AAPL")});
        const QVector<Story> chosen = select_stories({c}, {QStringLiteral("AAPL")});
        QCOMPARE(chosen.size(), 1);
        QVERIFY(chosen.first().portfolio_hit);
        QCOMPARE(chosen.first().portfolio_tickers, QStringList{QStringLiteral("AAPL")});
    }

    // ── Consistency between runs ────────────────────────────────────────────

    void the_same_feed_selects_the_same_stories_in_the_same_order() {
        // Deliberately all-identical scores: same outlet count, tier and
        // timestamp. That is the case an ordering that stops at the score gets
        // wrong, and it is common — every single-source story filed in the
        // same minute lands here.
        QVector<NewsCluster> clusters;
        for (int i = 0; i < 12; ++i) {
            clusters << cluster(QStringLiteral("Identically weighted story %1").arg(i),
                                QStringLiteral("MARKETS"), 2, 2, kNow);
        }
        Config cfg;
        cfg.max_stories = 6;
        const QStringList first = headlines_of(select_stories(clusters, {}, cfg));
        for (int run = 0; run < 5; ++run)
            QCOMPARE(headlines_of(select_stories(clusters, {}, cfg)), first);

        // And feed order must not decide it either: the same stories arriving
        // in a different order are the same day's news.
        QVector<NewsCluster> shuffled = clusters;
        std::reverse(shuffled.begin(), shuffled.end());
        QCOMPARE(headlines_of(select_stories(shuffled, {}, cfg)), first);
    }

    void the_window_is_anchored_on_the_feed_not_on_the_clock() {
        // Same pool scored against a reference an hour later must not reorder.
        // Anchoring on wall-clock now would make every re-run a different
        // selection and defeat the content-addressed cache.
        QVector<NewsCluster> clusters{
            cluster(QStringLiteral("A"), QStringLiteral("MARKETS"), 3, 2, kNow - 3600),
            cluster(QStringLiteral("B"), QStringLiteral("TECH"), 3, 2, kNow - 7200),
        };
        const QStringList now_order = headlines_of(select_stories(clusters, {}));
        for (NewsCluster& c : clusters) {
            c.latest_sort_ts -= 86'400;
            c.lead_article.sort_ts -= 86'400;
        }
        QCOMPARE(headlines_of(select_stories(clusters, {})), now_order);
    }

    // ── The window ──────────────────────────────────────────────────────────

    void stories_older_than_the_window_drop_out() {
        QVector<NewsCluster> clusters{
            cluster(QStringLiteral("Today"), QStringLiteral("MARKETS"), 2, 2, kNow),
            cluster(QStringLiteral("Three days ago"), QStringLiteral("TECH"), 9, 1,
                    kNow - 3 * 86'400),
        };
        const QVector<Story> chosen = select_stories(clusters, {});
        QCOMPARE(headlines_of(chosen), QStringList{QStringLiteral("Today")});
    }

    void an_entirely_stale_pool_still_produces_a_brief() {
        // The window is relative to the pool, not to the clock, so a feed that
        // has published nothing for a month still briefs on what it has: the
        // newest cluster is zero seconds old by definition. This is what makes
        // the window safe to apply unconditionally — there is no "everything
        // aged out" state for it to produce.
        QVector<NewsCluster> clusters{
            cluster(QStringLiteral("Old news"), QStringLiteral("MARKETS"), 1, 4, kNow - 30 * 86'400),
            cluster(QStringLiteral("Older still"), QStringLiteral("TECH"), 1, 4, kNow - 31 * 86'400),
        };
        // ...and the window still does its job WITHIN that pool: the second
        // story is a further day back and drops out.
        QCOMPARE(headlines_of(select_stories(clusters, {})), QStringList{QStringLiteral("Old news")});
    }

    void an_empty_pool_selects_nothing_rather_than_crashing() {
        QVERIFY(select_stories({}, {}).isEmpty());
        QVERIFY(categories_present({}).isEmpty());
        QVERIFY(render_stories({}).isEmpty());
        QVERIFY(render_stories_by_category({}, {QStringLiteral("MARKETS")}).isEmpty());
    }

    // ── Enrichment budget ───────────────────────────────────────────────────

    void only_the_top_stories_are_flagged_for_body_extraction() {
        QVector<NewsCluster> clusters;
        for (int i = 0; i < 15; ++i) {
            clusters << cluster(QStringLiteral("Story %1").arg(i), QStringLiteral("MARKETS"),
                                20 - i, 2, kNow);
        }
        Config cfg;
        cfg.enrich_count = 3;
        const QVector<Story> chosen = select_stories(clusters, {}, cfg);
        QVERIFY(chosen.size() > 3);
        for (int i = 0; i < chosen.size(); ++i)
            QCOMPARE(chosen[i].enrich, i < 3);
    }

    // ── Prompt rendering ────────────────────────────────────────────────────

    void a_story_block_states_the_outlet_count() {
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Fed holds");
        stories[0].category = QStringLiteral("ECONOMIC");
        stories[0].source = QStringLiteral("Reuters");
        stories[0].source_count = 6;
        stories[0].summary = QStringLiteral("The committee left rates unchanged.");
        const QString block = render_stories(stories);
        // The one piece of importance evidence the model cannot infer from the
        // text, so it has to be said out loud.
        QVERIFY2(block.contains(QStringLiteral("6 outlets")), qPrintable(block));
        QVERIFY(block.contains(QStringLiteral("Fed holds")));
        QVERIFY(block.contains(QStringLiteral("committee left rates unchanged")));
    }

    void a_headline_containing_a_format_marker_cannot_hijack_the_block() {
        // Headlines are third-party text, passed through verbatim by
        // parse_rss_xml. With a chained .arg() the outlet count substitutes
        // into the FIRST remaining marker anywhere in the string — including
        // one the headline brought with it — so the count lands inside the
        // headline and the real markers survive as literal text.
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Vendor ships %1 and %2 in Q3");
        stories[0].category = QStringLiteral("TECH");
        stories[0].source = QStringLiteral("Reuters");
        stories[0].source_count = 4;
        const QString block = render_stories(stories);
        QVERIFY2(block.contains(QStringLiteral("Vendor ships %1 and %2 in Q3")), qPrintable(block));
        QVERIFY2(block.contains(QStringLiteral("4 outlets")), qPrintable(block));
        QVERIFY2(!block.contains(QStringLiteral("%3")), qPrintable(block));
        QVERIFY2(!block.contains(QStringLiteral("%4")), qPrintable(block));

        const QString grouped =
            render_stories_by_category(stories, {QStringLiteral("TECH")});
        QVERIFY2(grouped.contains(QStringLiteral("Vendor ships %1 and %2 in Q3")), qPrintable(grouped));
        QVERIFY2(grouped.contains(QStringLiteral("4 outlets")), qPrintable(grouped));
        QVERIFY2(!grouped.contains(QStringLiteral("%4")), qPrintable(grouped));
    }

    void a_single_outlet_is_not_pluralised() {
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Solo scoop");
        stories[0].source = QStringLiteral("Wire");
        stories[0].source_count = 1;
        QVERIFY(render_stories(stories).contains(QStringLiteral("1 outlet\n")));
    }

    void the_article_body_displaces_the_rss_blurb() {
        // Many feeds set <description> to the headline again; the body is
        // strictly better evidence wherever we managed to fetch one.
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Nvidia buys a company");
        stories[0].source = QStringLiteral("Reuters");
        stories[0].summary = QStringLiteral("RSS BLURB");
        stories[0].body = QStringLiteral("Nvidia agreed to pay $12.9bn in cash and stock.");
        const QString block = render_stories(stories);
        QVERIFY(block.contains(QStringLiteral("$12.9bn")));
        QVERIFY(!block.contains(QStringLiteral("RSS BLURB")));
    }

    void a_blurb_that_merely_repeats_the_headline_is_dropped() {
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Oil steadies near $70");
        stories[0].source = QStringLiteral("OilPrice");
        stories[0].summary = QStringLiteral("Oil steadies near $70");
        QCOMPARE(render_stories(stories).count(QStringLiteral("Oil steadies near $70")), 1);
    }

    void a_multi_story_rss_digest_is_cut_down_to_the_relevant_part() {
        // Verbatim shape of an OilPrice.com <description>: three unrelated
        // teasers in one blurb. Clipping the head of it attached Tanzania and
        // Kazakhstan to a Venezuela headline, and the model wrote a Tanzania
        // bullet into the ENERGY section — grounded, in the sense that the
        // block really did say so.
        QVector<Story> stories(1);
        stories[0].headline =
            QStringLiteral("Machado Backs U.S. Oil Partnership but Questions Venezuela Deal");
        stories[0].category = QStringLiteral("ENERGY");
        stories[0].source = QStringLiteral("OilPrice.com");
        stories[0].summary = QStringLiteral(
            "Kazakhstan and Uzbekistan are signaling\u2026 Tanzania has transformed its "
            "mining\u2026 Venezuelan opposition leader Mar\u00eda Corina Machado is backing a "
            "long-term U.S. oil partnership.");
        const QString block = render_stories(stories);
        QVERIFY2(block.contains(QStringLiteral("Machado is backing")), qPrintable(block));
        QVERIFY2(!block.contains(QStringLiteral("Tanzania")), qPrintable(block));
        QVERIFY2(!block.contains(QStringLiteral("Kazakhstan")), qPrintable(block));
    }

    void an_elaborating_blurb_that_shares_no_words_is_kept_whole() {
        // The filter needs positive evidence of which fragment is relevant.
        // With none, dropping fragments costs real detail to fix a problem
        // this blurb does not have.
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Fed holds rates");
        stories[0].source = QStringLiteral("Reuters");
        stories[0].summary = QStringLiteral(
            "The committee left policy unchanged\u2026 officials signalled patience.");
        const QString block = render_stories(stories);
        QVERIFY2(block.contains(QStringLiteral("committee left policy unchanged")), qPrintable(block));
        QVERIFY2(block.contains(QStringLiteral("signalled patience")), qPrintable(block));
    }

    void ordinary_prose_is_never_fragmented() {
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Nvidia buys Hugging Face");
        stories[0].source = QStringLiteral("Reuters");
        stories[0].summary =
            QStringLiteral("Nvidia agreed to pay $12.9bn. The deal closes next quarter.");
        const QString block = render_stories(stories);
        QVERIFY2(block.contains(QStringLiteral("$12.9bn")), qPrintable(block));
        QVERIFY2(block.contains(QStringLiteral("closes next quarter")), qPrintable(block));
    }

    void a_body_quoting_an_ellipsis_keeps_all_of_its_prose() {
        // One or two fragments is a quote, not a teaser rail. Filtering here
        // would cost a paragraph to fix a problem this body does not have.
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Machado backs oil deal");
        stories[0].source = QStringLiteral("OilPrice.com");
        stories[0].body = QStringLiteral(
            "She said the deal was \"unclear\u2026 and unauthorised\", adding that Venezuela's "
            "resources are not the interim government's to sell.");
        const QString block = render_stories(stories);
        QVERIFY2(block.contains(QStringLiteral("not the interim government")), qPrintable(block));
    }

    void a_body_that_is_really_a_teaser_rail_is_cut_down_too() {
        // Some article URLs render as listing pages, and the extractor returns
        // the rail around the story. "The body is the article by construction"
        // is exactly the assumption those pages break — this shape put a
        // Tanzania bullet in the ENERGY section of a live brief.
        QVector<Story> stories(1);
        stories[0].headline =
            QStringLiteral("Machado Backs U.S. Oil Partnership but Questions Venezuela Deal");
        stories[0].category = QStringLiteral("ENERGY");
        stories[0].source = QStringLiteral("OilPrice.com");
        stories[0].body = QStringLiteral(
            "Mine clearance has made Hormuz\u2026 UBS is urging investors to\u2026 Venezuelan "
            "opposition leader Mar\u00eda Corina Machado is backing a long-term U.S. role in the "
            "country's oil reserves.");
        const QString block = render_stories(stories);
        QVERIFY2(block.contains(QStringLiteral("Machado is backing")), qPrintable(block));
        QVERIFY2(!block.contains(QStringLiteral("Hormuz")), qPrintable(block));
        QVERIFY2(!block.contains(QStringLiteral("UBS")), qPrintable(block));
    }

    void the_breakdown_block_is_grouped_under_the_headings_it_asks_for() {
        QVector<Story> stories(3);
        stories[0].headline = QStringLiteral("Markets one");
        stories[0].category = QStringLiteral("MARKETS");
        stories[0].source = QStringLiteral("WSJ");
        stories[1].headline = QStringLiteral("Crypto one");
        stories[1].category = QStringLiteral("CRYPTO");
        stories[1].source = QStringLiteral("CoinDesk");
        stories[2].headline = QStringLiteral("Markets two");
        stories[2].category = QStringLiteral("MARKETS");
        stories[2].source = QStringLiteral("FT");

        const QString grouped = render_stories_by_category(
            stories, {QStringLiteral("MARKETS"), QStringLiteral("CRYPTO")});
        QVERIFY(grouped.indexOf(QStringLiteral("### MARKETS"))
                < grouped.indexOf(QStringLiteral("### CRYPTO")));
        // Both MARKETS stories under the one heading — the model is never
        // given a reason to write the heading twice.
        QCOMPARE(grouped.count(QStringLiteral("### MARKETS")), 1);
        QVERIFY(grouped.indexOf(QStringLiteral("Markets two"))
                < grouped.indexOf(QStringLiteral("### CRYPTO")));
    }

    void a_heading_with_no_stories_is_never_requested() {
        // An empty section reads as "we looked and there was nothing", which
        // is a claim the selection never made.
        QVector<Story> stories(1);
        stories[0].headline = QStringLiteral("Markets one");
        stories[0].category = QStringLiteral("MARKETS");
        stories[0].source = QStringLiteral("WSJ");
        const QString grouped = render_stories_by_category(
            stories, {QStringLiteral("MARKETS"), QStringLiteral("DEFENSE")});
        QVERIFY(grouped.contains(QStringLiteral("### MARKETS")));
        QVERIFY(!grouped.contains(QStringLiteral("### DEFENSE")));
    }

    // ── Clipping ────────────────────────────────────────────────────────────

    void clipping_prefers_a_sentence_boundary() {
        const QString text = QStringLiteral(
            "The committee left rates unchanged. Officials signalled patience through the "
            "autumn and declined to guide on December.");
        const QString clipped = clip_to_sentence(text, 60);
        QCOMPARE(clipped, QStringLiteral("The committee left rates unchanged."));
        QVERIFY(!clipped.endsWith(QChar(0x2026)));
    }

    void clipping_marks_a_hard_cut_so_the_model_can_see_it_is_a_fragment() {
        // No sentence end anywhere near the limit, so the cut is arbitrary —
        // and a fragment ending mid-clause is exactly what a model completes
        // by guessing.
        const QString text = QStringLiteral(
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa "
            "bbbbbbbbbbbbbbbbbbbbbbbb. cc");
        const QString clipped = clip_to_sentence(text, 40);
        QCOMPARE(clipped.size(), 41); // 40 chars plus the ellipsis
        QVERIFY(clipped.endsWith(QChar(0x2026)));
    }

    void short_text_is_returned_whole() {
        QCOMPARE(clip_to_sentence(QStringLiteral("Brief."), 100), QStringLiteral("Brief."));
        QCOMPARE(clip_to_sentence(QStringLiteral("  spaced   out  "), 100),
                 QStringLiteral("spaced out"));
    }

    // ── Classification ──────────────────────────────────────────────────────
    //
    // The headings the breakdown writes now come straight from these
    // assignments, so a substring accident is no longer a slightly-odd tag on
    // a feed row — it is a story printed under a heading that contradicts it.

    void short_keywords_match_whole_words_only() {
        using fincept::news::classify;
        // The shipped case: "eps" inside "Epstein" filed three of one day's
        // stories under EARNINGS, and the brief printed a congressional
        // subpoena story under a results heading.
        QCOMPARE(classify(QStringLiteral("billionaire sues congress over epstein subpoenas")),
                 QString());
        QCOMPARE(classify(QStringLiteral("q3 eps beat the consensus")), QStringLiteral("EARNINGS"));

        // Same trap, not yet observed but latent in the same table.
        QCOMPARE(classify(QStringLiteral("senator opens an inquiry")), QString());
        QCOMPARE(classify(QStringLiteral("nato ministers meet")), QStringLiteral("GEOPOLITICS"));
        QCOMPARE(classify(QStringLiteral("gdpr fines rise across the eu")), QString());
        QCOMPARE(classify(QStringLiteral("gdp grew 2% last quarter")), QStringLiteral("ECONOMIC"));
    }

    void bounded_keywords_still_match_through_punctuation() {
        using fincept::news::classify;
        // The boundary view normalises punctuation, so bounding these made
        // them match MORE of what they are for, not less: a bare "fed " missed
        // both of these outright.
        QCOMPARE(classify(QStringLiteral("fed's waller notes progress")), QStringLiteral("ECONOMIC"));
        QCOMPARE(classify(QStringLiteral("stocks rise as fed-hike bets ease")),
                 QStringLiteral("ECONOMIC"));
        // Start and end of string are word boundaries too.
        QCOMPARE(classify(QStringLiteral("gdp")), QStringLiteral("ECONOMIC"));
    }

    void unbounded_keywords_are_still_substrings_on_purpose() {
        using fincept::news::classify;
        // "tech" is meant to catch these; that is why the whole-word rule is
        // per-keyword rather than applied to everything short.
        QCOMPARE(classify(QStringLiteral("a fintech raises a round")), QStringLiteral("TECH"));
        QCOMPARE(classify(QStringLiteral("biotech results disappoint")), QStringLiteral("TECH"));
    }

    // ── The prompt contract ─────────────────────────────────────────────────
    //
    // The prompts decide what the brief says more than any other code here,
    // and each of these clauses is load-bearing for a failure that shipped.

    void the_breakdown_prompt_names_the_headings_it_wants() {
        const QStringList cats{QStringLiteral("MARKETS"), QStringLiteral("CRYPTO")};
        const QString p = fincept::news::brief_prompt::build_breakdown(
            QStringLiteral("### MARKETS\n- a\n\n### CRYPTO\n- b"), cats);
        // Naming them is what stops the model both skipping a category it has
        // stories for and inventing one it does not.
        QVERIFY(p.contains(QStringLiteral("MARKETS, CRYPTO")));
        QVERIFY(p.contains(QStringLiteral("NO heading that is not listed")));
        // The three structural failures the renderer had to repair after the fact.
        QVERIFY(p.contains(QStringLiteral("never write the same heading twice")));
        QVERIFY(p.contains(QStringLiteral("DEFENSE, CRYPTO")));
        QVERIFY(p.contains(QStringLiteral("untrusted data")));
    }

    void the_top_prompt_does_not_ask_for_category_headings() {
        // The two halves are separate requests joined on the marker. If the
        // top half writes its own "### " sections the reading pane shows the
        // categories twice.
        const QString p = fincept::news::brief_prompt::build_top(QStringLiteral("[1] ..."), {});
        QVERIFY(p.contains(QStringLiteral("no '### ' category headings")));
        QVERIFY(p.contains(QStringLiteral("Overall read")));
        QVERIFY(p.contains(QStringLiteral("Watch")));
        QVERIFY(p.contains(QStringLiteral("untrusted data")));
    }

    void the_portfolio_section_appears_only_when_there_are_holdings() {
        const QString without = fincept::news::brief_prompt::build_top(QStringLiteral("s"), {});
        QVERIFY(!without.contains(QStringLiteral("Your portfolio")));
        QVERIFY(!without.contains(QStringLiteral("<<<PORTFOLIO>>>")));
        const QString with = fincept::news::brief_prompt::build_top(
            QStringLiteral("s"), QStringLiteral("Holdings: AAPL"));
        QVERIFY(with.contains(QStringLiteral("Your portfolio")));
        QVERIFY(with.contains(QStringLiteral("Holdings: AAPL")));
    }

    void both_prompts_carry_the_grounding_rules() {
        // Article bodies make grounding MORE important, not less: with real
        // prose in the prompt the model has numbers to hand and is
        // correspondingly readier to pair one with the wrong company.
        for (const QString& p : {fincept::news::brief_prompt::build_top(QStringLiteral("s"), {}),
                                 fincept::news::brief_prompt::build_breakdown(
                                     QStringLiteral("s"), {QStringLiteral("MARKETS")})}) {
            QVERIFY(p.contains(QStringLiteral("GROUNDING")));
            QVERIFY(p.contains(QStringLiteral("never infer that one is listed")));
            QVERIFY(p.contains(QStringLiteral("never substitute a parent")));
        }
    }

    // ── Cache addressing ────────────────────────────────────────────────────

    void the_brief_key_follows_the_prompt_text() {
        const QString a = QStringLiteral("top half");
        const QString b = QStringLiteral("breakdown");
        QCOMPARE(brief_key(a, b), brief_key(a, b));
        QVERIFY(brief_key(a, b) != brief_key(a + QStringLiteral("!"), b));
        QVERIFY(brief_key(a, b) != brief_key(a, b + QStringLiteral("!")));
        // The two halves are separated before hashing, so moving text across
        // the boundary is a different key rather than the same one.
        QVERIFY(brief_key(QStringLiteral("ab"), QStringLiteral("c"))
                != brief_key(QStringLiteral("a"), QStringLiteral("bc")));
        QVERIFY(brief_key(a, b).startsWith(QStringLiteral("news:brief:")));
    }
};

QTEST_MAIN(TestNewsBriefSelection)
#include "test_news_brief_selection.moc"

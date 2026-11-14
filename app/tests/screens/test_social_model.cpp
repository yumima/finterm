// test_social_model.cpp — SOCIAL screen: per-network character counting,
// the pre-flight checklist that gates Publish, recorded post -> link, the
// enabled-networks switch and the hand-made launch posts (Reddit / HN).

#include "screens/social/SocialModel.h"

#include <QJsonDocument>
#include <QTest>

using namespace finterm::social;

namespace {
const QString kUrl = QStringLiteral("https://yumima.github.io/finterm/brief/2026-11-09/");

Brief good_brief() {
    Brief b;
    b.loaded = true;
    b.issue = QStringLiteral("2026-W46");
    b.url = kUrl;
    b.date = QStringLiteral("2026-11-13");
    b.count = 37;
    b.page_live = true;
    b.current_week = true;
    return b;
}

Channel x_channel(bool configured = true) {
    Channel c;
    c.network = QStringLiteral("x");
    c.configured = configured;
    c.enabled = true;
    c.account = QStringLiteral("posting_account");
    c.limit = 280;
    c.url_cost = 23;
    return c;
}

LaunchTarget target(const char* network, const char* community, const char* kind) {
    LaunchTarget t;
    t.id = QStringLiteral("t");
    t.network = network;
    t.community = community;
    t.kind = kind;
    return t;
}

QJsonObject json(const char* s) { return QJsonDocument::fromJson(QByteArray(s)).object(); }
} // namespace

class TestSocialModel : public QObject {
    Q_OBJECT
  private slots:
    // ── counting ────────────────────────────────────────────────────────────
    void url_counts_as_url_cost() {
        const QString t = QStringLiteral("Full list: ") + kUrl; // 11 + 51
        QCOMPARE(post_length(t, 23), 11 + 23);
        QCOMPARE(post_length(t, -1), 11 + int(kUrl.size())); // Bluesky: real length
    }

    void every_url_counts_and_trailing_punctuation_is_text() {
        const QString t = QStringLiteral("a http://x.co/abc. b https://example.com/a/b?c=1");
        // "a " + url(23) + ". b " + url(23)
        QCOMPARE(post_length(t, 23), 2 + 23 + 4 + 23);
    }

    void counts_characters_not_utf16_units() {
        // An astral emoji is two UTF-16 units but one character.
        const QString t = QString::fromUtf8("ok \xF0\x9F\x93\x88");
        QCOMPARE(t.size(), 5);
        QCOMPARE(post_length(t, 23), 4);
    }

    void emoji_sequences_count_once() {
        // Family (man ZWJ woman ZWJ girl): 5 code points, one grapheme.
        const QString family = QString::fromUtf8("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                                                 "\xF0\x9F\x91\xA7");
        QCOMPARE(family.toUcs4().size(), 5);
        QCOMPARE(post_length(family, -1), 1);       // Bluesky / Mastodon: graphemes
        QCOMPARE(post_length(family, 23, true), 2); // X: an emoji sequence weighs 2
        // e + combining acute: one grapheme, weight 1+1 on X (both in range).
        const QString e_acute = QString::fromUtf8("e\xCC\x81");
        QCOMPARE(post_length(e_acute, -1), 1);
        QCOMPARE(post_length(e_acute, 23, true), 2);
    }

    void never_more_than_python_len_on_generated_text() {
        // The backend fits generated texts with len() (code points).
        const QString t = QStringLiteral("Insider buying, 7 days to Nov 13: $FUN $15.1M (3 insiders). ") + kUrl;
        QCOMPARE(post_length(t, -1), int(t.toUcs4().size()));
    }

    void x_weights_wide_characters_double() {
        const QString t = QString::fromUtf8("ab\xE2\x80\xA6\xE4\xB8\xAD"); // "ab…中"
        QCOMPARE(post_length(t, 23, false), 4);
        QCOMPARE(post_length(t, 23, true), 1 + 1 + 2 + 2);
    }

    void counts_what_is_posted_not_surrounding_whitespace() {
        auto c = x_channel();
        QCOMPARE(channel_length(c, QStringLiteral("  hi\n\n")), 2);
    }

    // ── pre-flight ──────────────────────────────────────────────────────────
    void all_pass_for_a_good_post() {
        const auto c = x_channel();
        const auto p = preflight(good_brief(), &c, "x", QStringLiteral("Insider buying. ") + kUrl);
        QCOMPARE(p.checks.size(), 6);
        QVERIFY(p.all_ok());
    }

    void each_rule_blocks_on_its_own() {
        const auto c = x_channel();
        const QString ok_text = QStringLiteral("Insider buying. ") + kUrl;
        auto fails = [&](const Brief& b, const Channel* ch, const QString& text, int which) {
            const auto p = preflight(b, ch, "x", text);
            QVERIFY(!p.all_ok());
            QVERIFY2(!p.checks[which].ok, qPrintable(p.checks[which].label));
            for (int i = 0; i < p.checks.size(); ++i)
                if (i != which)
                    QVERIFY2(p.checks[i].ok, qPrintable(p.checks[i].label));
        };
        Brief b = good_brief();
        b.page_live = false;
        fails(b, &c, ok_text, 0);
        b = good_brief();
        b.current_week = false;
        fails(b, &c, ok_text, 1);
        b = good_brief();
        b.posted.insert("x", "1987654321");
        fails(b, &c, ok_text, 2);
        fails(good_brief(), &c, QStringLiteral("no link here"), 3);
        fails(good_brief(), &c, kUrl + QLatin1Char(' ') + QString(260, QLatin1Char('a')), 4); // 23+1+260 > 280
        const auto off = x_channel(false);
        fails(good_brief(), &off, ok_text, 5);
    }

    void empty_text_fails_limit_and_link() {
        const auto c = x_channel();
        const auto p = preflight(good_brief(), &c, "x", QStringLiteral("   "));
        QVERIFY(!p.all_ok());
        QVERIFY(!p.checks[3].ok);
        QVERIFY(!p.checks[4].ok);
        QCOMPARE(p.checks[4].detail, QStringLiteral("nothing to post"));
    }

    void posted_elsewhere_does_not_block() {
        const auto c = x_channel();
        Brief b = good_brief();
        b.posted.insert("bluesky", "at://did:plc:a/app.bsky.feed.post/3k");
        QVERIFY(preflight(b, &c, "x", kUrl).all_ok());
    }

    void exactly_at_limit_passes_one_over_fails() {
        const auto c = x_channel();
        const QString at = kUrl + QLatin1Char(' ') + QString(256, QLatin1Char('a')); // 23+1+256 = 280
        QVERIFY(preflight(good_brief(), &c, "x", at).all_ok());
        QVERIFY(!preflight(good_brief(), &c, "x", at + QLatin1Char('a')).all_ok());
    }

    void nothing_passes_without_data() {
        const auto c = x_channel();
        QVERIFY(!preflight(Brief{}, &c, "x", kUrl).all_ok());
        QVERIFY(!preflight(good_brief(), nullptr, "x", kUrl).all_ok());
        Brief stale = good_brief();
        stale.stale = true;
        const auto p = preflight(stale, &c, "x", kUrl);
        QVERIFY(!p.all_ok());
        QVERIFY(!p.checks[0].ok);
    }

    void manual_or_errored_channel_is_not_connected() {
        auto c = x_channel();
        c.error = QStringLiteral("keyring locked");
        QVERIFY(!preflight(good_brief(), &c, "x", kUrl).all_ok());
        c = x_channel();
        c.manual = true;
        QVERIFY(!preflight(good_brief(), &c, "x", kUrl).all_ok());
    }

    void a_network_not_enabled_never_passes() {
        auto c = x_channel();
        c.enabled = false;
        const auto p = preflight(good_brief(), &c, "x", kUrl);
        QVERIFY(!p.all_ok());
        QCOMPARE(p.checks[5].detail, QStringLiteral("not enabled yet — coming soon"));
    }

    void enabled_comes_from_the_backend_answer() {
        const auto ch = parse_channels(json("{\"channels\":["
            "{\"network\":\"x\",\"configured\":true,\"enabled\":true,\"limit\":280,\"url_cost\":23},"
            "{\"network\":\"bluesky\",\"configured\":true,\"enabled\":false,\"limit\":300},"
            "{\"network\":\"reddit\",\"manual\":true,\"configured\":true}]}"));
        QVERIFY(network_enabled(ch, "x"));
        QVERIFY(!network_enabled(ch, "bluesky"));
        QVERIFY(!network_enabled(ch, "reddit"));   // absent = off
        QVERIFY(!network_enabled(ch, "mastodon")); // not listed = off
        QVERIFY(!network_enabled({}, "x"));        // before the answer: nothing is usable
        // A configured Bluesky that isn't enabled must not pass pre-flight.
        Channel b = *find_channel(ch, "bluesky");
        QVERIFY(!preflight(good_brief(), &b, "bluesky", kUrl).all_ok());
        QCOMPARE(all_networks().size(), 5);
        QVERIFY(is_manual_network("hackernews") && !is_manual_network("x"));
    }

    // ── parsing ─────────────────────────────────────────────────────────────
    void parses_channels_with_null_url_cost() {
        const auto ch = parse_channels(json("{\"channels\":["
            "{\"network\":\"x\",\"configured\":true,\"account\":\"posting_account\",\"error\":\"\",\"limit\":280,\"url_cost\":23,\"manual\":false},"
            "{\"network\":\"bluesky\",\"configured\":false,\"account\":\"\",\"error\":\"\",\"limit\":300,\"url_cost\":null,\"manual\":false},"
            "{\"network\":\"reddit\",\"configured\":true,\"manual\":true,\"account\":\"\"}]}"));
        QCOMPARE(ch.size(), 3);
        QCOMPARE(ch[0].url_cost, 23);
        QCOMPARE(ch[1].url_cost, -1);
        QVERIFY(ch[2].manual);
        QCOMPARE(find_channel(ch, "bluesky")->limit, 300);
        QVERIFY(!find_channel(ch, "mastodon"));
        QCOMPARE(account_label("posting_account"), QStringLiteral("@posting_account"));
        QCOMPARE(account_label("@a"), QStringLiteral("@a"));
    }

    void parses_brief_and_header() {
        const auto b = parse_brief(json("{\"issue\":\"2026-W46\",\"url\":\"https://u/\",\"date\":\"2026-11-13\",\"count\":37,"
            "\"rows\":[{\"symbol\":\"FUN\"}],\"texts\":{\"x\":\"t\"},\"posted\":{\"x\":\"123\"},\"page_live\":true,\"current_week\":false}"));
        QVERIFY(b.loaded);
        QCOMPARE(b.count, 37);
        QCOMPARE(b.texts.value("x"), QStringLiteral("t"));
        QCOMPARE(b.posted.value("x"), QStringLiteral("123"));
        QVERIFY(!b.current_week);
        QCOMPARE(brief_header(b), QStringLiteral("Weekly brief · 2026-W46 · 37 companies · Fri 13 Nov 2026"));
        QVERIFY(!parse_brief(json("{\"error\":\"No published issue\"}")).loaded);
    }

    void trade_span_formats() {
        QCOMPARE(trade_span("2026-11-06", "2026-11-10"), QStringLiteral("6 Nov – 10 Nov"));
        QCOMPARE(trade_span("2026-11-10", "2026-11-10"), QStringLiteral("2026-11-10"));
        QCOMPARE(trade_span("", ""), QStringLiteral("—"));
    }

    // ── history ─────────────────────────────────────────────────────────────
    void ref_links() {
        QCOMPARE(ref_link("x", "1987654321"), QStringLiteral("https://x.com/i/status/1987654321"));
        QCOMPARE(ref_link("x", "already-posted (X rejected a duplicate)"), QString());
        QCOMPARE(ref_link("bluesky", "at://did:plc:abc/app.bsky.feed.post/3kxyz"),
                 QStringLiteral("https://bsky.app/profile/did:plc:abc/post/3kxyz"));
        QCOMPARE(ref_link("bluesky", "already-posted"), QString());
        QCOMPARE(ref_link("mastodon", "https://m.social/@a/1"), QStringLiteral("https://m.social/@a/1"));
        QCOMPARE(ref_link("mastodon", "1123"), QString());
    }

    void history_rows_in_display_order() {
        const auto rows = parse_history(json("{\"history\":["
            "{\"issue\":\"2026-W46\",\"posts\":{\"mastodon\":\"https://m/1\",\"x\":\"42\",\"bluesky\":\"at://d/app.bsky.feed.post/r\"}},"
            "{\"issue\":\"2026-W45\",\"posts\":{\"x\":\"already-posted (X rejected a duplicate)\"}}]}"));
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0].network, QStringLiteral("x"));
        QCOMPARE(rows[0].link, QStringLiteral("https://x.com/i/status/42"));
        QCOMPARE(rows[1].network, QStringLiteral("bluesky"));
        QCOMPARE(rows[2].network, QStringLiteral("mastodon"));
        QCOMPARE(rows[3].issue, QStringLiteral("2026-W45"));
        QVERIFY(rows[3].link.isEmpty());
    }

    void launch_rows_come_first_newest_first() {
        const auto rows = parse_history(json("{\"history\":[{\"issue\":\"2026-W46\",\"posts\":{\"x\":\"42\"}}],"
            "\"launch\":{"
            "\"reddit-algotrading\":{\"url\":\"https://www.reddit.com/r/algotrading/comments/a/\",\"at\":\"2026-11-10T15:00:00+00:00\","
            "\"network\":\"reddit\",\"community\":\"r/algotrading\"},"
            "\"hn-show\":{\"url\":\"https://news.ycombinator.com/item?id=1\",\"at\":\"2026-11-12T15:00:00+00:00\","
            "\"network\":\"hackernews\",\"community\":\"Show HN\"}}}"));
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].community, QStringLiteral("Show HN"));
        QCOMPARE(rows[0].issue, QStringLiteral("launch 2026-11-12"));
        QCOMPARE(rows[0].link, QStringLiteral("https://news.ycombinator.com/item?id=1"));
        QCOMPARE(rows[1].community, QStringLiteral("r/algotrading"));
        QCOMPARE(rows[2].issue, QStringLiteral("2026-W46"));
        QVERIFY(rows[2].community.isEmpty());
    }

    // ── launch posts ────────────────────────────────────────────────────────
    void parses_launch_set() {
        const auto s = parse_launch(json("{\"targets\":["
            "{\"id\":\"reddit-sideproject\",\"network\":\"reddit\",\"community\":\"r/SideProject\",\"kind\":\"link\","
            "\"title\":\"T\",\"url\":\"https://github.com/o/r\",\"first_comment\":\"C\",\"notes\":[\"n1\"]},"
            "{\"id\":\"hn-show\",\"network\":\"hackernews\",\"community\":\"Show HN\",\"kind\":\"link\",\"title\":\"S\"},"
            "{\"id\":\"bad\",\"network\":\"x\"}],"
            "\"rules\":[\"r1\"],\"skip\":[{\"community\":\"r/investing\",\"why\":\"bans it\"},{\"community\":\"r/quant\",\"why\":\"\"}],"
            "\"posted\":{\"hn-show\":{\"url\":\"https://news.ycombinator.com/item?id=9\",\"at\":\"2026-11-12T15:00:00+00:00\"}}}"));
        QVERIFY(s.loaded);
        QCOMPARE(s.targets.size(), 2); // a non-manual network is not a launch target
        QCOMPARE(s.for_network("reddit").size(), 1);
        QVERIFY(s.find("reddit-sideproject")->is_link());
        QCOMPARE(s.find("reddit-sideproject")->notes, QStringList{QStringLiteral("n1")});
        QCOMPARE(s.skip, (QStringList{QStringLiteral("r/investing — bans it"), QStringLiteral("r/quant")}));
        QVERIFY(s.posted.contains("hn-show"));
        QVERIFY(s.posted["hn-show"].at.isValid());
        QVERIFY(!parse_launch(json("{\"error\":\"x\"}")).loaded);
    }

    void submit_urls() {
        const auto text = target("reddit", "r/algotrading", "text");
        QCOMPARE(launch_submit_url(text, QStringLiteral("A & B? 100%"), {}),
                 QStringLiteral("https://old.reddit.com/r/algotrading/submit?selftext=true&title=A%20%26%20B%3F%20100%25"));
        const auto link = target("reddit", "r/SideProject", "link");
        QCOMPARE(launch_submit_url(link, QStringLiteral("finterm"), QStringLiteral("https://github.com/o/r")),
                 QStringLiteral("https://www.reddit.com/r/SideProject/submit?url=https%3A%2F%2Fgithub.com%2Fo%2Fr&title=finterm"));
        const auto hn = target("hackernews", "Show HN", "link");
        QCOMPARE(launch_submit_url(hn, QStringLiteral("Show HN: X – y"), QStringLiteral("https://github.com/o/r")),
                 QStringLiteral("https://news.ycombinator.com/submitlink?u=https%3A%2F%2Fgithub.com%2Fo%2Fr&t=Show%20HN%3A%20X%20%E2%80%93%20y"));
        QVERIFY(launch_submit_url(target("x", "", "text"), "t", {}).isEmpty());
        QCOMPARE(title_limit("hackernews"), 80);
        QCOMPARE(title_limit("reddit"), 300);
    }

    void post_url_must_be_on_the_targets_site_and_community() {
        const auto algo = target("reddit", "r/algotrading", "text");
        QVERIFY(validate_post_url(algo, "https://www.reddit.com/r/algotrading/comments/abc/x/").isEmpty());
        QVERIFY(validate_post_url(algo, " https://old.reddit.com/r/AlgoTrading/comments/abc/ ").isEmpty());
        QVERIFY(!validate_post_url(algo, "https://www.reddit.com/r/stocks/comments/abc/").isEmpty());
        QVERIFY(!validate_post_url(algo, "https://reddit.com.evil.example/r/algotrading/comments/a/").isEmpty());
        QVERIFY(!validate_post_url(algo, "https://evilreddit.com/r/algotrading/comments/a/").isEmpty());
        QVERIFY(!validate_post_url(algo, "http://www.reddit.com/r/algotrading/comments/abc/").isEmpty());
        QVERIFY(!validate_post_url(algo, "www.reddit.com/r/algotrading/comments/abc/").isEmpty());
        QVERIFY(!validate_post_url(algo, "").isEmpty());
        const auto hn = target("hackernews", "Show HN", "link");
        QVERIFY(validate_post_url(hn, "https://news.ycombinator.com/item?id=4242").isEmpty());
        QVERIFY(!validate_post_url(hn, "https://news.ycombinator.com/item?id=abc").isEmpty());
        QVERIFY(!validate_post_url(hn, "https://news.ycombinator.com/user?id=4242").isEmpty());
        QVERIFY(!validate_post_url(hn, "https://www.reddit.com/r/algotrading/comments/a/").isEmpty());
    }

    void pacing_is_one_community_per_day() {
        const QDateTime now = QDateTime::fromString("2026-11-13T12:00:00Z", Qt::ISODate);
        QMap<QString, LaunchRecord> posted;
        QVERIFY(pacing_hint(posted, "a", now).isEmpty());
        posted.insert("b", {"b", "reddit", "r/ValueInvesting", "u", now.addSecs(-5 * 3600)});
        const QString h = pacing_hint(posted, "a", now);
        QVERIFY2(h.startsWith(QStringLiteral("r/ValueInvesting was posted 5 h ago")), qPrintable(h));
        QVERIFY(pacing_hint(posted, "b", now).isEmpty()); // its own record is not "another"
        posted["b"].at = now.addSecs(-24 * 3600);         // exactly a day: free again
        QVERIFY(pacing_hint(posted, "a", now).isEmpty());
        posted["b"].at = now.addSecs(-(24 * 3600 - 1));
        QVERIFY(!pacing_hint(posted, "a", now).isEmpty());
        posted["b"].at = QDateTime();                     // unreadable time: no claim
        QVERIFY(pacing_hint(posted, "a", now).isEmpty());
    }

    void error_hints() {
        QCOMPARE(publish_error_hint("x", "HTTP 402: {\"title\":\"CreditsDepleted\"}"),
                 QStringLiteral("add credit to the X developer project"));
        QVERIFY(publish_error_hint("x", "HTTP 401 Unauthorized").startsWith("reconnect X"));
        QVERIFY(publish_error_hint("x", "That issue is not the one currently published").isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSocialModel)
#include "test_social_model.moc"

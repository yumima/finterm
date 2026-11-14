// test_social_model.cpp — SOCIAL screen: per-network character counting,
// the pre-flight checklist that gates Publish, and recorded post -> link.

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
    c.account = QStringLiteral("samloganxp");
    c.limit = 280;
    c.url_cost = 23;
    return c;
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

    // ── parsing ─────────────────────────────────────────────────────────────
    void parses_channels_with_null_url_cost() {
        const auto ch = parse_channels(json("{\"channels\":["
            "{\"network\":\"x\",\"configured\":true,\"account\":\"samloganxp\",\"error\":\"\",\"limit\":280,\"url_cost\":23,\"manual\":false},"
            "{\"network\":\"bluesky\",\"configured\":false,\"account\":\"\",\"error\":\"\",\"limit\":300,\"url_cost\":null,\"manual\":false},"
            "{\"network\":\"reddit\",\"configured\":true,\"manual\":true,\"account\":\"\"}]}"));
        QCOMPARE(ch.size(), 3);
        QCOMPARE(ch[0].url_cost, 23);
        QCOMPARE(ch[1].url_cost, -1);
        QVERIFY(ch[2].manual);
        QCOMPARE(find_channel(ch, "bluesky")->limit, 300);
        QVERIFY(!find_channel(ch, "mastodon"));
        QCOMPARE(account_label("samloganxp"), QStringLiteral("@samloganxp"));
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

    void error_hints() {
        QCOMPARE(publish_error_hint("x", "HTTP 402: {\"title\":\"CreditsDepleted\"}"),
                 QStringLiteral("add credit to the X developer project"));
        QVERIFY(publish_error_hint("x", "HTTP 401 Unauthorized").startsWith("reconnect X"));
        QVERIFY(publish_error_hint("x", "That issue is not the one currently published").isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSocialModel)
#include "test_social_model.moc"

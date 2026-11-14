#pragma once
// SocialModel.h — the pure half of the SOCIAL screen: the backend's answers
// (app/scripts/social_app.py) parsed into plain structs, the per-network
// character count, the pre-flight checklist that gates "Publish", and the
// link for a recorded post. Header-only and Qt-Core-only;
// tests/screens/test_social_model.cpp.
//
// The backend re-checks everything that matters before it posts (published
// issue, live page, link kept, not already posted); the checklist is the
// same rules shown up front, so the button is never enabled for a post the
// backend would refuse.

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextBoundaryFinder>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <algorithm>

namespace finterm::social {

/// The automated networks, in display order (the backend's NETWORKS order).
inline const QStringList& automated_networks() {
    static const QStringList n = {QStringLiteral("x"), QStringLiteral("bluesky"), QStringLiteral("mastodon")};
    return n;
}

/// Every network the screen lists: the automated ones, then the two posted
/// to by hand (launch posts, app/scripts/launch_posts.py).
inline const QStringList& all_networks() {
    static const QStringList n = {QStringLiteral("x"), QStringLiteral("bluesky"), QStringLiteral("mastodon"),
                                  QStringLiteral("reddit"), QStringLiteral("hackernews")};
    return n;
}

inline bool is_manual_network(const QString& network) {
    return network == QLatin1String("reddit") || network == QLatin1String("hackernews");
}

inline QString network_label(const QString& network) {
    if (network == QLatin1String("x"))
        return QStringLiteral("X");
    if (network == QLatin1String("bluesky"))
        return QStringLiteral("Bluesky");
    if (network == QLatin1String("mastodon"))
        return QStringLiteral("Mastodon");
    if (network == QLatin1String("reddit"))
        return QStringLiteral("Reddit");
    if (network == QLatin1String("hackernews"))
        return QStringLiteral("Hacker News");
    return network;
}

/// "@name" from a stored handle with or without its "@".
inline QString account_label(const QString& account) {
    const QString a = account.trimmed();
    if (a.isEmpty())
        return {};
    return a.startsWith(QLatin1Char('@')) ? a : QStringLiteral("@") + a;
}

// ── channels ────────────────────────────────────────────────────────────────

struct Channel {
    QString network;
    bool configured = false;
    bool manual = false;
    QString account;
    QString error;     ///< e.g. keyring unavailable — "could not look", not "not configured"
    /// The backend's switch (social_post.ENABLED_NETWORKS): a network not
    /// enabled is listed as "coming soon" and can't be selected or connected.
    bool enabled = false;
    int limit = 0;     ///< characters; 0 = unknown
    int url_cost = -1; ///< what any link counts as; -1 = its real length (JSON null)
};

inline QVector<Channel> parse_channels(const QJsonObject& root) {
    QVector<Channel> out;
    for (const auto& v : root.value(QLatin1String("channels")).toArray()) {
        const QJsonObject o = v.toObject();
        Channel c;
        c.network = o.value(QLatin1String("network")).toString();
        if (c.network.isEmpty())
            continue;
        c.configured = o.value(QLatin1String("configured")).toBool();
        c.manual = o.value(QLatin1String("manual")).toBool();
        c.account = o.value(QLatin1String("account")).toString();
        c.error = o.value(QLatin1String("error")).toString();
        c.enabled = o.value(QLatin1String("enabled")).toBool();
        c.limit = o.value(QLatin1String("limit")).toInt();
        const QJsonValue uc = o.value(QLatin1String("url_cost"));
        c.url_cost = uc.isDouble() ? uc.toInt() : -1;
        out.append(c);
    }
    return out;
}

inline const Channel* find_channel(const QVector<Channel>& channels, const QString& network) {
    for (const auto& c : channels)
        if (c.network == network)
            return &c;
    return nullptr;
}

/// Whether @p network may be used. Unknown until the channels answer says
/// so — the one switch lives in the backend, so the screen and the backend
/// can never disagree about it.
inline bool network_enabled(const QVector<Channel>& channels, const QString& network) {
    const Channel* c = find_channel(channels, network);
    return c && c->enabled;
}

// ── the published issue ─────────────────────────────────────────────────────

struct Brief {
    bool loaded = false; ///< a good answer has been seen
    bool stale = false;  ///< the latest refresh failed; fields are the last good answer
    QString issue;       ///< "2026-W46"
    QString url;         ///< the issue page every post must link to
    QString date;        ///< ISO day the issue was compiled
    int count = 0;
    QJsonArray rows;
    QMap<QString, QString> texts;  ///< network -> generated post
    QMap<QString, QString> posted; ///< network -> reference
    bool page_live = false;
    bool current_week = false;
    QString disclaimer;
};

inline Brief parse_brief(const QJsonObject& o) {
    Brief b;
    b.issue = o.value(QLatin1String("issue")).toString();
    b.url = o.value(QLatin1String("url")).toString();
    b.loaded = !b.issue.isEmpty() && !b.url.isEmpty();
    b.date = o.value(QLatin1String("date")).toString();
    b.rows = o.value(QLatin1String("rows")).toArray();
    b.count = o.value(QLatin1String("count")).toInt(int(b.rows.size()));
    const QJsonObject texts = o.value(QLatin1String("texts")).toObject();
    for (auto it = texts.begin(); it != texts.end(); ++it)
        b.texts.insert(it.key(), it.value().toString());
    const QJsonObject posted = o.value(QLatin1String("posted")).toObject();
    for (auto it = posted.begin(); it != posted.end(); ++it)
        b.posted.insert(it.key(), it.value().toString());
    b.page_live = o.value(QLatin1String("page_live")).toBool();
    b.current_week = o.value(QLatin1String("current_week")).toBool();
    b.disclaimer = o.value(QLatin1String("disclaimer")).toString();
    return b;
}

/// "Weekly brief · 2026-W46 · 37 companies · Fri 13 Nov 2026"
inline QString brief_header(const Brief& b) {
    QStringList parts{QStringLiteral("Weekly brief")};
    if (!b.loaded)
        return parts.first();
    parts << b.issue << QStringLiteral("%1 %2").arg(b.count).arg(b.count == 1 ? QStringLiteral("company")
                                                                                : QStringLiteral("companies"));
    const QDate d = QDate::fromString(b.date, Qt::ISODate);
    if (d.isValid())
        parts << QLocale(QLocale::English).toString(d, QStringLiteral("ddd d MMM yyyy"));
    else if (!b.date.isEmpty())
        parts << b.date;
    return parts.join(QStringLiteral(" · "));
}

/// A row's trade window as the Ownership › INSIDER BUYS table shows it: one
/// day as ISO, a span as "6 Nov – 10 Nov".
inline QString trade_span(const QString& first_iso, const QString& last_iso) {
    const QDate first = QDate::fromString(first_iso, Qt::ISODate);
    const QDate last = QDate::fromString(last_iso, Qt::ISODate);
    if (!last.isValid())
        return first.isValid() ? first.toString(Qt::ISODate) : QStringLiteral("—");
    if (!first.isValid() || first == last)
        return last.toString(Qt::ISODate);
    const QLocale en(QLocale::English);
    return QStringLiteral("%1 – %2").arg(en.toString(first, QStringLiteral("d MMM")),
                                         en.toString(last, QStringLiteral("d MMM")));
}

// ── character counting ──────────────────────────────────────────────────────

namespace detail {
/// X's weighted length (twitter-text v3): code points in these ranges count
/// 1, every other code point 2 (CJK, emoji, most symbols — "…" included).
inline int x_weight(char32_t cp) {
    if (cp <= 4351 || (cp >= 8192 && cp <= 8205) || (cp >= 8208 && cp <= 8223) || (cp >= 8242 && cp <= 8247))
        return 1;
    return 2;
}

/// Characters as the networks count them: user-perceived characters
/// (grapheme clusters — Bluesky's and Mastodon's unit), and on X each
/// cluster weighted: an emoji sequence (ZWJ family, flag, skin tone) counts
/// 2 however many code points it has, anything else the sum of its code
/// points' weights. Unweighted, never more than Python's len() (code
/// points), which the backend fits its generated texts to. On X the weights
/// can exceed len() ("…", CJK) — that is X's own rule, so a generated text
/// that reads over the limit here would be refused by X too.
inline int count_plain(QStringView s, bool x_weighted) {
    if (s.isEmpty())
        return 0;
    const QString str = s.toString();
    QTextBoundaryFinder bf(QTextBoundaryFinder::Grapheme, str);
    int n = 0;
    qsizetype start = 0;
    for (qsizetype end = bf.toNextBoundary(); end != -1; end = bf.toNextBoundary()) {
        if (!x_weighted) {
            ++n;
        } else {
            const auto cps = QStringView(str).mid(start, end - start).toUcs4();
            if (cps.size() > 1 && x_weight(cps.first()) == 2) {
                n += 2;
            } else {
                for (char32_t cp : cps)
                    n += x_weight(cp);
            }
        }
        start = end;
    }
    return n;
}
} // namespace detail

/// Links as the networks find them: http(s) up to whitespace, without the
/// sentence punctuation that usually follows one.
inline QRegularExpression url_pattern() {
    static const QRegularExpression re(QStringLiteral("https?://\\S+?(?=[.,;:!?)\\]'\"]*(\\s|$))"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

/// The length a network charges for @p text. Every http(s) link counts
/// @p url_cost characters (X and Mastodon: 23) or, when url_cost < 0, its
/// real length (Bluesky). Characters are grapheme clusters and, on X,
/// weighted as X weights them (detail::count_plain).
inline int post_length(const QString& text, int url_cost, bool x_weighted = false) {
    int n = 0;
    qsizetype pos = 0;
    auto it = url_pattern().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        n += detail::count_plain(QStringView(text).mid(pos, m.capturedStart() - pos), x_weighted);
        n += url_cost >= 0 ? url_cost : detail::count_plain(m.capturedView(), false);
        pos = m.capturedEnd();
    }
    n += detail::count_plain(QStringView(text).mid(pos), x_weighted);
    return n;
}

/// What is posted: the backend strips the text before it posts.
inline QString text_to_post(const QString& text) { return text.trimmed(); }

inline int channel_length(const Channel& c, const QString& text) {
    return post_length(text_to_post(text), c.url_cost, c.network == QLatin1String("x"));
}

// ── pre-flight ──────────────────────────────────────────────────────────────

struct Check {
    QString label;
    bool ok = false;
    QString detail; ///< why it failed, or the figure behind a pass
};

struct Preflight {
    QVector<Check> checks;
    bool all_ok() const {
        if (checks.isEmpty())
            return false;
        for (const auto& c : checks)
            if (!c.ok)
                return false;
        return true;
    }
};

/// Every rule the backend applies before posting, plus the two it can only
/// answer by failing (connected, within the limit). @p channel may be null
/// (channels not loaded yet).
inline Preflight preflight(const Brief& b, const Channel* channel, const QString& network, const QString& text) {
    Preflight p;
    const QString net = network_label(network);
    const QString body = text_to_post(text);
    const bool have = b.loaded && !b.stale;
    const QString not_loaded = b.loaded ? QStringLiteral("the last refresh failed — refresh first")
                                        : QStringLiteral("the published issue is not loaded");

    p.checks.append({QStringLiteral("Issue page is live"), have && b.page_live,
                     !have ? not_loaded
                           : b.page_live ? b.url : QStringLiteral("the page does not show this issue yet")});
    p.checks.append({QStringLiteral("Issue is this week's"), have && b.current_week,
                     !have ? not_loaded
                     : b.current_week ? b.issue
                                      : QStringLiteral("%1 is not the current week's issue").arg(b.issue)});
    const QString ref = b.posted.value(network);
    p.checks.append({QStringLiteral("Not yet posted to %1").arg(net), have && ref.isEmpty(),
                     !have ? not_loaded
                     : ref.isEmpty() ? QString()
                                     : QStringLiteral("already posted (%1)").arg(ref)});
    p.checks.append({QStringLiteral("Links to the issue page"), have && body.contains(b.url),
                     !have ? not_loaded
                     : body.contains(b.url) ? QString()
                                            : QStringLiteral("the text must keep %1").arg(b.url)});
    if (!channel || channel->limit <= 0) {
        p.checks.append({QStringLiteral("Within the character limit"), false,
                         QStringLiteral("%1's limit is not known yet").arg(net)});
    } else {
        const int len = channel_length(*channel, body);
        const bool ok = !body.isEmpty() && len <= channel->limit;
        p.checks.append({QStringLiteral("Within %1 characters").arg(channel->limit), ok,
                         body.isEmpty() ? QStringLiteral("nothing to post")
                                        : QStringLiteral("%1 / %2").arg(len).arg(channel->limit)});
    }
    const bool connected =
        channel && channel->enabled && channel->configured && !channel->manual && channel->error.isEmpty();
    p.checks.append({QStringLiteral("%1 connected").arg(net), connected,
                     !channel             ? QStringLiteral("channels not loaded yet")
                     : !channel->enabled  ? QStringLiteral("not enabled yet — coming soon")
                     : !channel->error.isEmpty() ? channel->error
                     : !connected         ? QStringLiteral("not connected")
                     : channel->account.isEmpty() ? QStringLiteral("connected")
                                                  : account_label(channel->account)});
    return p;
}

// ── history ─────────────────────────────────────────────────────────────────

/// A browser link for a recorded post, or empty when the reference is not
/// one (e.g. "already-posted (X rejected a duplicate)").
inline QString ref_link(const QString& network, const QString& ref) {
    const QString r = ref.trimmed();
    if (r.isEmpty())
        return {};
    if (network == QLatin1String("x")) {
        static const QRegularExpression digits(QStringLiteral("^\\d+$"));
        return digits.match(r).hasMatch() ? QStringLiteral("https://x.com/i/status/") + r : QString();
    }
    if (network == QLatin1String("bluesky")) {
        // at://<did>/app.bsky.feed.post/<rkey>
        static const QRegularExpression at(QStringLiteral("^at://([^/]+)/app\\.bsky\\.feed\\.post/([^/]+)$"));
        const auto m = at.match(r);
        return m.hasMatch() ? QStringLiteral("https://bsky.app/profile/%1/post/%2").arg(m.captured(1), m.captured(2))
                            : QString();
    }
    if (r.startsWith(QLatin1String("https://")) || r.startsWith(QLatin1String("http://")))
        return r; // Mastodon records the status URL
    return {};
}

struct HistoryRow {
    QString issue;
    QString network;
    QString ref;
    QString link;      ///< empty when the reference is not linkable
    QString community; ///< launch posts: "r/algotrading", "Show HN"
};

// ── launch posts (Reddit / Hacker News, by hand) ────────────────────────────

/// A hand-made launch post, as recorded by launch_mark.
struct LaunchRecord {
    QString target;
    QString network;
    QString community;
    QString url;
    QDateTime at; ///< invalid if the record's time was unreadable
};

inline QVector<LaunchRecord> parse_launch_records(const QJsonObject& launch) {
    QVector<LaunchRecord> out;
    for (auto it = launch.begin(); it != launch.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        LaunchRecord r;
        r.target = it.key();
        r.network = o.value(QLatin1String("network")).toString();
        r.community = o.value(QLatin1String("community")).toString();
        r.url = o.value(QLatin1String("url")).toString();
        r.at = QDateTime::fromString(o.value(QLatin1String("at")).toString(), Qt::ISODate);
        out.append(r);
    }
    // Newest first; an unreadable time last.
    std::stable_sort(out.begin(), out.end(), [](const LaunchRecord& a, const LaunchRecord& b) {
        if (a.at.isValid() != b.at.isValid())
            return a.at.isValid();
        return a.at > b.at;
    });
    return out;
}

/// One row per (issue, network), newest issue first as the backend sends
/// them; within an issue, the automated networks in display order, then any
/// other network alphabetically. Launch posts come first, newest first, as
/// "launch <date>".
inline QVector<HistoryRow> parse_history(const QJsonObject& root) {
    QVector<HistoryRow> out;
    for (const auto& r : parse_launch_records(root.value(QLatin1String("launch")).toObject())) {
        const QString when = r.at.isValid() ? r.at.toUTC().date().toString(Qt::ISODate) : QString();
        out.append({when.isEmpty() ? QStringLiteral("launch") : QStringLiteral("launch ") + when, r.network, r.url,
                    r.url.startsWith(QLatin1String("https://")) ? r.url : QString(),
                    r.community.isEmpty() ? r.target : r.community});
    }
    for (const auto& v : root.value(QLatin1String("history")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString issue = o.value(QLatin1String("issue")).toString();
        const QJsonObject posts = o.value(QLatin1String("posts")).toObject();
        QStringList order;
        for (const auto& n : automated_networks())
            if (posts.contains(n))
                order << n;
        for (const auto& k : posts.keys())
            if (!order.contains(k))
                order << k;
        for (const auto& n : order) {
            const QString ref = posts.value(n).toString();
            out.append({issue, n, ref, ref_link(n, ref), {}});
        }
    }
    return out;
}

struct LaunchTarget {
    QString id;
    QString network;   ///< "reddit" | "hackernews"
    QString community; ///< "r/algotrading", "Show HN"
    QString kind;      ///< "text" | "link"
    QString title;
    QString body;          ///< text posts
    QString url;           ///< link posts
    QString first_comment; ///< posted by hand right after, where the plan says so
    QStringList notes;     ///< the community's rules / how to post it
    bool needs_issue = false;  ///< the draft carries this week's table
    bool data_missing = false; ///< ...which could not be filled
    QString issue;             ///< the issue the table came from
    bool is_link() const { return kind == QLatin1String("link"); }
};

struct LaunchSet {
    bool loaded = false;
    QVector<LaunchTarget> targets;
    QStringList rules;
    QStringList skip; ///< "r/investing — bans self-promotion"
    QMap<QString, LaunchRecord> posted;
    QVector<const LaunchTarget*> for_network(const QString& network) const {
        QVector<const LaunchTarget*> out;
        for (const auto& t : targets)
            if (t.network == network)
                out.append(&t);
        return out;
    }
    const LaunchTarget* find(const QString& id) const {
        for (const auto& t : targets)
            if (t.id == id)
                return &t;
        return nullptr;
    }
};

inline LaunchSet parse_launch(const QJsonObject& o) {
    LaunchSet s;
    for (const auto& v : o.value(QLatin1String("targets")).toArray()) {
        const QJsonObject j = v.toObject();
        LaunchTarget t;
        t.id = j.value(QLatin1String("id")).toString();
        t.network = j.value(QLatin1String("network")).toString();
        if (t.id.isEmpty() || !is_manual_network(t.network))
            continue;
        t.community = j.value(QLatin1String("community")).toString();
        t.kind = j.value(QLatin1String("kind")).toString();
        t.title = j.value(QLatin1String("title")).toString();
        t.body = j.value(QLatin1String("body")).toString();
        t.url = j.value(QLatin1String("url")).toString();
        t.first_comment = j.value(QLatin1String("first_comment")).toString();
        for (const auto& n : j.value(QLatin1String("notes")).toArray())
            t.notes << n.toString();
        t.needs_issue = j.value(QLatin1String("needs_issue")).toBool();
        t.data_missing = j.value(QLatin1String("data_missing")).toBool();
        t.issue = j.value(QLatin1String("issue")).toString();
        s.targets.append(t);
    }
    for (const auto& r : o.value(QLatin1String("rules")).toArray())
        s.rules << r.toString();
    for (const auto& k : o.value(QLatin1String("skip")).toArray()) {
        const QJsonObject j = k.toObject();
        const QString why = j.value(QLatin1String("why")).toString();
        const QString community = j.value(QLatin1String("community")).toString();
        s.skip << (why.isEmpty() ? community : QStringLiteral("%1 — %2").arg(community, why));
    }
    for (const auto& r : parse_launch_records(o.value(QLatin1String("posted")).toObject()))
        s.posted.insert(r.target, r);
    s.loaded = !s.targets.isEmpty();
    return s;
}

/// The longest title each site takes (HN cuts at 80).
inline int title_limit(const QString& network) { return network == QLatin1String("hackernews") ? 80 : 300; }

inline QString reddit_sub(const QString& community) {
    return community.startsWith(QLatin1String("r/")) ? community.mid(2) : community;
}

/// The page where the reader submits @p t by hand, with the title (and a
/// link post's URL) filled in. A text post's body can't go in a URL — it
/// goes by clipboard.
inline QString launch_submit_url(const LaunchTarget& t, const QString& title, const QString& link) {
    auto enc = [](const QString& v) { return QString::fromLatin1(QUrl::toPercentEncoding(v)); };
    if (t.network == QLatin1String("hackernews"))
        return QStringLiteral("https://news.ycombinator.com/submitlink?u=%1&t=%2").arg(enc(link), enc(title));
    if (t.network != QLatin1String("reddit"))
        return {};
    const QString sub = enc(reddit_sub(t.community));
    if (t.is_link())
        return QStringLiteral("https://www.reddit.com/r/%1/submit?url=%2&title=%3").arg(sub, enc(link), enc(title));
    // Only old Reddit's form honours selftext=true (opens on the Text tab);
    // the new one may open on Link, with nowhere to paste the body.
    return QStringLiteral("https://old.reddit.com/r/%1/submit?selftext=true&title=%2").arg(sub, enc(title));
}

/// Empty if @p text is a post on @p t's community, else why not. The
/// backend (launch_posts.validate_post_url) applies the same rule.
inline QString validate_post_url(const LaunchTarget& t, const QString& text) {
    const QUrl u(text.trimmed(), QUrl::StrictMode);
    if (!u.isValid() || u.scheme() != QLatin1String("https") || u.host().isEmpty())
        return QStringLiteral("Paste the post's full https:// link.");
    const QString host = u.host().toLower();
    if (t.network == QLatin1String("reddit")) {
        static const QStringList hosts = {QStringLiteral("reddit.com"), QStringLiteral("www.reddit.com"),
                                          QStringLiteral("old.reddit.com"), QStringLiteral("new.reddit.com"),
                                          QStringLiteral("sh.reddit.com")};
        if (!hosts.contains(host))
            return QStringLiteral("That is not a reddit.com link.");
        const QString prefix = QStringLiteral("/r/%1/").arg(reddit_sub(t.community).toLower());
        if (!u.path().toLower().startsWith(prefix))
            return QStringLiteral("That link is not a post in %1.").arg(t.community);
        return {};
    }
    if (t.network == QLatin1String("hackernews")) {
        if (host != QLatin1String("news.ycombinator.com"))
            return QStringLiteral("That is not a news.ycombinator.com link.");
        static const QRegularExpression digits(QStringLiteral("^\\d+$"));
        if (u.path() != QLatin1String("/item") ||
            !digits.match(QUrlQuery(u).queryItemValue(QStringLiteral("id"))).hasMatch())
            return QStringLiteral("Paste the post's item link (news.ycombinator.com/item?id=…).");
        return {};
    }
    return QStringLiteral("unknown network");
}

/// "One sub per day": a hint when another launch post was recorded in the
/// 24 hours before @p now, else empty. @p target's own record is not
/// "another" (its re-use has its own warning).
inline QString pacing_hint(const QMap<QString, LaunchRecord>& posted, const QString& target, const QDateTime& now) {
    const LaunchRecord* latest = nullptr;
    for (const auto& r : posted) {
        if (r.target == target || !r.at.isValid())
            continue;
        const qint64 secs = r.at.secsTo(now);
        if (secs < 0 || secs >= 24 * 3600)
            continue;
        if (!latest || r.at > latest->at)
            latest = &r;
    }
    if (!latest)
        return {};
    const qint64 hours = latest->at.secsTo(now) / 3600;
    return QStringLiteral("%1 was posted %2 ago — one community per day; wait until %3.")
        .arg(latest->community.isEmpty() ? latest->target : latest->community,
             hours < 1 ? QStringLiteral("under an hour") : QStringLiteral("%1 h").arg(hours),
             latest->at.addSecs(24 * 3600).toLocalTime().toString(QStringLiteral("ddd HH:mm")));
}

/// A hint for a publish refusal the reader can act on, or empty.
inline QString publish_error_hint(const QString& network, const QString& error) {
    const QString e = error.toLower();
    if (network == QLatin1String("x") && (e.contains(QLatin1String("credit")) || e.contains(QLatin1String("http 402"))))
        return QStringLiteral("add credit to the X developer project");
    if (e.contains(QLatin1String("http 401")) || e.contains(QLatin1String("unauthorized")))
        return QStringLiteral("reconnect %1 — its sign-in was refused").arg(network_label(network));
    if (e.contains(QLatin1String("keyring")))
        return QStringLiteral("unlock the system keyring and try again");
    return {};
}

} // namespace finterm::social

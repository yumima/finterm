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
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextBoundaryFinder>
#include <QVector>

namespace finterm::social {

/// The automated networks, in display order (the backend's NETWORKS order).
inline const QStringList& automated_networks() {
    static const QStringList n = {QStringLiteral("x"), QStringLiteral("bluesky"), QStringLiteral("mastodon")};
    return n;
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

/// "@samloganxp" from a stored handle with or without its "@".
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
    const bool connected = channel && channel->configured && !channel->manual && channel->error.isEmpty();
    p.checks.append({QStringLiteral("%1 connected").arg(net), connected,
                     !channel             ? QStringLiteral("channels not loaded yet")
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
    QString link; ///< empty when the reference is not linkable
};

/// One row per (issue, network), newest issue first as the backend sends
/// them; within an issue, the automated networks in display order, then any
/// other network alphabetically.
inline QVector<HistoryRow> parse_history(const QJsonObject& root) {
    QVector<HistoryRow> out;
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
            out.append({issue, n, ref, ref_link(n, ref)});
        }
    }
    return out;
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

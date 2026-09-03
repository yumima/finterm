#pragma once
// Story selection for the AI news brief.
//
// The brief used to be built from "the newest 35 articles" — the head of a
// list sorted by recency across 55 feeds. Two things were wrong with that.
//
// It sampled by ARRIVAL, not by significance: the top of that list is whatever
// happened to publish in the last few minutes, so the categories a brief could
// name were decided by feed timing rather than by what the day contained.
//
// And it sampled ARTICLES, not STORIES: six outlets carrying the same Fed
// decision consumed six of the 35 slots and the model then wrote the same
// story six times, or picked one and dropped five other stories to do it.
//
// So selection happens over CLUSTERS (one cluster = one story, see
// NewsClusterService) and ranks them by how much the day's news actually
// weights them. Cluster size is the load-bearing signal and it is free: five
// independent outlets choosing to carry something IS the significance
// measurement, and no model call can improve on it.
//
// Header-only and free of the service layer's network stack, so the ranking
// and the per-category guarantee can be tested without standing up
// NewsService, LlmService, PythonRunner or CacheManager.

#include "services/news/NewsCategories.h"
#include "services/news/NewsClusterService.h"

#include <QCryptographicHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace fincept::news::brief_select {

using fincept::services::NewsCluster;

/// One story as the brief will see it — a cluster flattened to the fields the
/// prompt actually uses, plus the score that put it here.
struct Story {
    QString headline;
    QString summary;   // the lead article's RSS <description>
    QString link;      // lead article URL; the enrichment step reads this
    QString source;    // lead article's outlet
    QString category;  // the cluster's category, from the shared classifier
    QStringList tickers;
    QString body;      // article lead paragraphs, filled in by the enrich step
    int tier = 4;
    int source_count = 1;
    int64_t sort_ts = 0;
    double score = 0;
    /// The reader's holdings this story actually touches, in holdings order.
    /// Kept rather than recomputed by the caller, because the match may come
    /// from ANY article in the cluster while `tickers` above is only the lead
    /// article's — so a brief that reported `tickers` as the exposure would
    /// name symbols the reader does not hold and omit the one they do.
    QStringList portfolio_tickers;
    bool portfolio_hit = false;
    bool breaking = false;
    /// Selected for body extraction. Only the highest-scoring few are: a body
    /// costs a network fetch, and the accuracy it buys is concentrated in the
    /// stories the brief leads with.
    bool enrich = false;
};

struct Config {
    /// Stories handed to the model. Twenty distinct stories is already more
    /// than the output can name; the cap exists to bound prompt size, not to
    /// bound coverage — that is what the per-category guarantee is for.
    int max_stories = 20;
    /// Stories whose article body is fetched. Eight parallel extractions is
    /// roughly one batch of Python work; past that the marginal story is one
    /// the brief will not lead with anyway.
    int enrich_count = 8;
    /// Sections the breakdown may contain. Eight is every category in the
    /// vocabulary, i.e. "no cap" — kept as a knob because the reading pane's
    /// vertical space is a product decision, not a correctness one.
    int max_categories = 8;
    /// How far back a story may be and still count as today's news.
    int64_t window_sec = 12 * 60 * 60;
    /// Score halves every this many seconds of age.
    double recency_half_life_sec = 6 * 60 * 60;
    /// Multiplier for a story touching a holding. Deliberately below 2: a
    /// holding makes a story more relevant to this reader, it does not make it
    /// the day's biggest story, and the brief has a dedicated portfolio
    /// section for the cases where it is neither.
    double portfolio_boost = 1.6;
    double breaking_boost = 1.25;
    /// A category may claim its guaranteed slot only if its best story scores
    /// at least this fraction of the day's top story.
    ///
    /// Deliberately tiny, because it guards against one specific thing and
    /// must not become a second coverage cap. The guarantee is allowed to
    /// DISPLACE a stronger story to keep a category represented — that is the
    /// point of it — but not for a story twenty times weaker than the day's
    /// lead, which is a stale single-blog item on a morning the wires all
    /// carried something. Set this any higher and it starts silently dropping
    /// the quiet-but-real categories the guarantee exists to protect, which is
    /// the original failure wearing a different threshold.
    double min_guarantee_fraction = 0.05;
};

// ── Scoring components ──────────────────────────────────────────────────────

/// Outlet quality, 1.0 (wire) down to 0.25 (blog). Linear rather than 1/tier
/// so a tier-4 blog is discounted, not erased — some stories only ever appear
/// on one specialist site and the brief should still be able to see them.
inline double authority_of(int tier) {
    const int t = std::clamp(tier, 1, 4);
    return (5.0 - t) / 4.0;
}

/// How many outlets carried the story, compressed. log2 so the gap that
/// matters most is 1 -> 2 outlets (a story someone else thought worth
/// repeating) rather than 8 -> 16 (a story everyone has already told you).
inline double breadth_of(int source_count) {
    return 1.0 + std::log2(static_cast<double>(std::max(1, source_count)));
}

/// Exponential decay on age. A half-life beats a hard cutoff because the
/// window edge then costs a story rank rather than existence — the 12h-old
/// story that nothing has displaced still makes the brief.
inline double recency_of(int64_t age_sec, double half_life_sec) {
    if (half_life_sec <= 0)
        return 1.0;
    const double age = static_cast<double>(std::max<int64_t>(0, age_sec));
    return std::pow(0.5, age / half_life_sec);
}

/// Every held symbol that any article in the cluster tags, deduplicated and
/// in first-seen order. Empty when the story touches nothing the reader holds.
inline QStringList cluster_holdings(const NewsCluster& c, const QSet<QString>& held) {
    if (held.isEmpty())
        return {};
    QStringList out;
    for (const auto& a : c.articles) {
        for (const QString& t : a.tickers) {
            const QString up = t.trimmed().toUpper();
            if (held.contains(up) && !out.contains(up))
                out << up;
        }
    }
    return out;
}

inline double score_cluster(const NewsCluster& c, int64_t reference_ts, bool portfolio_hit,
                            const Config& cfg) {
    return breadth_of(c.source_count) * authority_of(c.tier)
           * recency_of(reference_ts - c.latest_sort_ts, cfg.recency_half_life_sec)
           * (portfolio_hit ? cfg.portfolio_boost : 1.0)
           * (c.is_breaking ? cfg.breaking_boost : 1.0);
}

// ── Selection ───────────────────────────────────────────────────────────────

/// Total order over stories: score first, then tiebreaks that are properties
/// of the CONTENT.
///
/// The tiebreaks are not decoration. Two runs over the same feed must produce
/// the same selection or the brief changes for no reason the reader can see,
/// and std::sort is free to reorder equal elements differently between calls.
/// Scores tie often — every single-source tier-2 story published in the same
/// minute scores identically — so an ordering that stops at the score is an
/// ordering that shuffles. Cluster ids cannot be used here: they are fresh
/// QUuids on every clustering pass.
inline bool story_precedes(const Story& a, const Story& b) {
    if (a.score != b.score)
        return a.score > b.score;
    if (a.sort_ts != b.sort_ts)
        return a.sort_ts > b.sort_ts;
    if (a.headline != b.headline)
        return a.headline < b.headline;
    return a.link < b.link;
}

/// Rank clusters and pick the stories the brief is written from.
///
/// Two passes, in this order:
///
///   1. GUARANTEE — the best-scoring story of every category present takes a
///      slot. This is what makes the breakdown cover the day rather than cover
///      the loudest category: without it, a morning of heavy MARKETS volume
///      fills all 20 slots and the model is asked to write a CRYPTO section
///      from a sample containing no crypto, which it correctly declines to do.
///      That is the "not covering the entire news of the day" failure, and it
///      is a sampling failure — no prompt can fix it.
///
///   2. FILL — the remaining slots go to the highest scorers overall.
///
/// The window is anchored on the newest article in the pool, not on
/// wall-clock now. Anchoring on now means the selection drifts every second
/// even when the feed has not changed, which defeats the content-addressed
/// cache and makes two briefs a minute apart gratuitously different.
inline QVector<Story> select_stories(const QVector<NewsCluster>& clusters,
                                     const QSet<QString>& portfolio_tickers,
                                     const Config& cfg = {}) {
    if (clusters.isEmpty())
        return {};

    int64_t newest = 0;
    for (const auto& c : clusters)
        newest = std::max(newest, c.latest_sort_ts);

    QVector<Story> pool;
    pool.reserve(clusters.size());
    for (const auto& c : clusters) {
        if (cfg.window_sec > 0 && (newest - c.latest_sort_ts) > cfg.window_sec)
            continue;
        const auto& lead = c.lead_article;
        Story s;
        s.headline = lead.headline;
        s.summary = lead.summary;
        s.link = lead.link;
        s.source = lead.source;
        // The cluster's category comes from its lead article, which
        // enrich_article() classified with the same table the brief renderer
        // uses. Falling back to the classifier here covers a lead article that
        // arrived with an empty category (a feed whose <category> we trust) so
        // it can still claim a section rather than landing in "".
        s.category = lead.category.trimmed().toUpper();
        if (s.category.isEmpty() || !news::heading_vocabulary().contains(s.category))
            s.category = news::classify((lead.headline + QLatin1Char(' ') + lead.summary).toLower());
        s.tickers = lead.tickers;
        s.tier = c.tier;
        s.source_count = c.source_count;
        s.sort_ts = c.latest_sort_ts;
        s.breaking = c.is_breaking;
        s.portfolio_tickers = cluster_holdings(c, portfolio_tickers);
        s.portfolio_hit = !s.portfolio_tickers.isEmpty();
        s.score = score_cluster(c, newest, s.portfolio_hit, cfg);
        pool.append(std::move(s));
    }

    // No "everything fell outside the window" guard, and none is needed: the
    // window is measured from the newest cluster IN THE POOL, so that cluster
    // is always zero seconds old and always survives. A pool of nothing but
    // month-old stories still briefs — the window ages stories relative to
    // each other, not against the clock. (A guard here would be dead code
    // that reads as though it were load-bearing.)
    std::sort(pool.begin(), pool.end(), story_precedes);

    const int budget = std::max(1, cfg.max_stories);
    QVector<Story> chosen;
    chosen.reserve(budget);
    QSet<int> taken;

    // Pass 1 — one slot per category, best first. Walking `pool` (already in
    // rank order) rather than iterating the vocabulary means the categories
    // themselves are offered in strength order, so if the budget is smaller
    // than the number of categories present it is the weakest category that
    // goes unrepresented rather than whichever one sorts last alphabetically.
    QSet<QString> category_seen;
    const double guarantee_floor = pool.first().score * cfg.min_guarantee_fraction;
    for (int i = 0; i < pool.size() && chosen.size() < budget; ++i) {
        const QString& cat = pool[i].category;
        if (cat.isEmpty() || category_seen.contains(cat))
            continue;
        if (category_seen.size() >= cfg.max_categories)
            break;
        // pool is in rank order, so the first story of an unseen category is
        // that category's best. Once it falls under the floor so does every
        // category still unseen, and pass 2 will pick up anything here that
        // earns its place on rank alone.
        if (pool[i].score < guarantee_floor)
            break;
        category_seen.insert(cat);
        taken.insert(i);
        chosen.append(pool[i]);
    }

    // Pass 2 — fill the rest on score alone.
    for (int i = 0; i < pool.size() && chosen.size() < budget; ++i) {
        if (taken.contains(i))
            continue;
        chosen.append(pool[i]);
    }

    // Present in rank order. Pass 1 appended by category, so without this the
    // top of the block is "best of each category" rather than "best", and the
    // model leads the brief with whatever category happened to sort first.
    std::sort(chosen.begin(), chosen.end(), story_precedes);

    for (int i = 0; i < chosen.size() && i < cfg.enrich_count; ++i)
        chosen[i].enrich = true;

    return chosen;
}

/// The categories the selection can actually support a section for, strongest
/// first, capped at cfg.max_categories.
///
/// Handed to the model as the exact list of headings to write. Asking for
/// "categories the headlines cover" made the model both invent sections it had
/// no stories for and skip ones it did; asking for a named list it can see the
/// stories for removes the judgement call entirely, and with it the merged and
/// duplicated headings the renderer had to repair after the fact.
inline QStringList categories_present(const QVector<Story>& stories, const Config& cfg = {}) {
    QStringList out;
    QSet<QString> seen;
    for (const Story& s : stories) { // already in rank order
        if (s.category.isEmpty() || seen.contains(s.category))
            continue;
        if (out.size() >= cfg.max_categories)
            break;
        seen.insert(s.category);
        out << s.category;
    }
    return out;
}

// ── Prompt rendering ────────────────────────────────────────────────────────

/// Trim `text` to at most `max_chars`, cutting at a sentence end where one is
/// near the limit so the model is never handed a fragment ending mid-clause —
/// a truncated sentence is exactly the kind of thing it completes by guessing.
inline QString clip_to_sentence(const QString& text, int max_chars) {
    const QString t = text.simplified();
    if (max_chars <= 0 || t.size() <= max_chars)
        return t;
    const QString head = t.left(max_chars);
    // Take the last sentence end in the second half of the budget. Below the
    // halfway mark the boundary is discarding more than it is worth, and the
    // ellipsis below already tells the model the text is a fragment. Searching
    // a narrower band than this was wrong in the other direction: a 35-char
    // sentence under a 60-char budget got thrown away for "…signalled pati…",
    // which is exactly the mid-clause fragment a model completes by guessing.
    const int floor_at = max_chars / 2;
    int cut = -1;
    for (const QChar stop : {QChar('.'), QChar('!'), QChar('?')})
        cut = std::max(cut, static_cast<int>(head.lastIndexOf(stop)));
    if (cut >= floor_at)
        return head.left(cut + 1).trimmed();
    return head.trimmed() + QStringLiteral("…");
}

/// Words distinctive enough to say two pieces of text are about one story.
/// Four characters and up, so "the", "has", "its" and the rest of the glue
/// cannot make unrelated fragments look related.
inline QSet<QString> distinctive_tokens(const QString& text) {
    static const QRegularExpression kNonWord(QStringLiteral("[^a-z0-9]+"));
    QSet<QString> out;
    for (const QString& w : text.toLower().split(kNonWord, Qt::SkipEmptyParts)) {
        if (w.size() > 3)
            out.insert(w);
    }
    return out;
}

/// The part of a blurb or body that is actually about `headline`.
///
/// TEASER RAILS. Two different sources produce them and both reach the prompt:
///
///   * Some feeds put a digest of several unrelated stories in <description>.
///   * Some article URLs render as listing pages, and the body extractor —
///     trafilatura with favor_recall — dutifully returns the rail of teasers
///     around the story. Every OilPrice.com link in the feed is one of these.
///
/// Either way the model is handed "Mine clearance has made Hormuz… UBS is
/// urging investors to… Venezuelan opposition leader María Corina Machado is
/// backing…" under a Venezuela headline, and it wrote a Tanzania bullet into
/// the ENERGY section of a live brief. Note what that is NOT: the prompt's
/// grounding rules were all satisfied, because the block really did say so.
/// A rule cannot save a brief from evidence that is about the wrong story;
/// only not putting it there can.
///
/// So this applies to the body as well as the blurb — "the body is the article
/// by construction" is exactly the assumption these pages break.
///
/// Two guards keep it from eating real prose:
///
///   * THREE or more fragments. A teaser rail has many; an article quoting an
///     ellipsis mid-sentence has one or two, and is left alone.
///   * Some fragment must POSITIVELY match the headline. A blurb that
///     elaborates without reusing any of the headline's words ("Fed holds
///     rates" / "The committee left policy unchanged") shares no tokens with
///     it, and dropping that would cost real detail to fix a problem it does
///     not have — with no evidence about which fragment is relevant, all of
///     them are kept.
inline QString detail_for_headline(const QString& headline, const QString& blurb) {
    static const QRegularExpression kEllipsis(
        QStringLiteral("\\s*(?:\\x{2026}|\\.\\.\\.)\\s*"));
    const QStringList parts = blurb.split(kEllipsis, Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return blurb;
    const QSet<QString> want = distinctive_tokens(headline);
    QStringList keep;
    for (const QString& part : parts) {
        if (distinctive_tokens(part).intersects(want))
            keep << part.trimmed();
    }
    return keep.isEmpty() ? blurb : keep.join(QStringLiteral(" "));
}

/// The story block for the top-stories prompt: rank order, with the article
/// lead for the enriched few.
///
/// Outlet count is stated because it is the one piece of importance evidence
/// the model cannot infer from the text, and saying it out loud is what lets
/// the brief lead with the right story instead of the first one.
inline QString render_stories(const QVector<Story>& stories, int body_chars = 700,
                              int summary_chars = 320) {
    QStringList out;
    out.reserve(stories.size());
    for (int i = 0; i < stories.size(); ++i) {
        const Story& s = stories[i];
        // ONE multi-arg call, and the integers pre-formatted.
        //
        // A chained .arg() substitutes into the lowest-numbered marker left
        // ANYWHERE in the string — including one that arrived inside a
        // substituted value. RSS titles reach here verbatim (parse_rss_xml
        // passes the first 200 characters straight through), so a headline
        // containing "%1" would swallow the outlet count and leave the
        // remaining markers as literal text in the model's prompt.
        QString block =
            QStringLiteral("[%1] %2 | %3 | %4 outlet%5")
                .arg(QString::number(i + 1),
                     s.category.isEmpty() ? QStringLiteral("GENERAL") : s.category, s.source,
                     QString::number(s.source_count),
                     s.source_count == 1 ? QString() : QStringLiteral("s"));
        if (!s.tickers.isEmpty())
            block += QStringLiteral(" | ") + s.tickers.join(QStringLiteral(", "));
        block += QLatin1Char('\n') + s.headline;
        // Body first when we have one — it is strictly better evidence than
        // the RSS blurb, which on many feeds is just the headline again.
        const bool have_body = !s.body.trimmed().isEmpty();
        const QString detail =
            clip_to_sentence(detail_for_headline(s.headline, have_body ? s.body : s.summary),
                             have_body ? body_chars : summary_chars);
        if (!detail.isEmpty() && detail.compare(s.headline.simplified(), Qt::CaseInsensitive) != 0)
            block += QLatin1Char('\n') + detail;
        out << block;
    }
    return out.join(QStringLiteral("\n\n"));
}

/// The story block for the breakdown prompt: pre-grouped under the headings
/// the model is being asked to write.
///
/// Grouping here rather than in the model is most of why the breakdown stopped
/// losing categories. The classification is already done — enrich_article()
/// filed every article when it arrived — so asking the model to redo it only
/// created ways for it to disagree with itself: a heading naming two
/// categories, the same heading written twice, a story filed under a section
/// the reader would not look for it in. It writes prose under fixed headings
/// now, and nothing about the structure is left to it.
inline QString render_stories_by_category(const QVector<Story>& stories,
                                          const QStringList& categories,
                                          int summary_chars = 240) {
    QStringList out;
    for (const QString& cat : categories) {
        QStringList lines;
        for (const Story& s : stories) {
            if (s.category != cat)
                continue;
            // One multi-arg call — see render_stories above for why a chain
            // is unsafe with a headline in it.
            QString line = QStringLiteral("- %1 (%2, %3 outlet%4)")
                               .arg(s.headline, s.source, QString::number(s.source_count),
                                    s.source_count == 1 ? QString() : QStringLiteral("s"));
            const QString detail = clip_to_sentence(
                detail_for_headline(s.headline,
                                    s.body.trimmed().isEmpty() ? s.summary : s.body),
                summary_chars);
            if (!detail.isEmpty() && detail.compare(s.headline.simplified(), Qt::CaseInsensitive) != 0)
                line += QStringLiteral("\n  ") + detail;
            lines << line;
        }
        if (lines.isEmpty())
            continue; // never ask for a section we cannot supply
        out << QStringLiteral("### ") + cat + QLatin1Char('\n') + lines.join(QLatin1Char('\n'));
    }
    return out.join(QStringLiteral("\n\n"));
}

/// Content address for a generated brief.
///
/// Hashes the exact prompt text the model was given, which is the only thing
/// that determines the output once temperature is pinned at 0. Everything the
/// old key had to enumerate by hand — headline set, portfolio, sample size,
/// prompt version — is in the prompt already, so nothing can change the brief
/// without changing the key, and a run whose selection is unchanged reuses the
/// previous brief even though the raw feed underneath it has moved on.
inline QString brief_key(const QString& prompt_a, const QString& prompt_b) {
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(prompt_a.toUtf8());
    h.addData(QByteArrayLiteral("\x1e"));
    h.addData(prompt_b.toUtf8());
    return QStringLiteral("news:brief:") + QString::fromLatin1(h.result().toHex());
}

} // namespace fincept::news::brief_select

#pragma once
// Merging one feed's batch into the running article list — one row per story.
//
// Three kinds of repeat reach the list, each matched differently:
//
// 1. The same item in several feeds of one publisher: a BBC China story is
//    also in BBC World and often BBC Business, each copy with its own article
//    id. Matched on the exact link (the sampled feeds repeat the URL, tracking
//    query included, so no normalisation is needed).
// 2. The same story under another link: a Reuters wire carried by both
//    Investing.com and CNA, or one outlet re-posting a headline with a new
//    URL. Matched on the headline, compared case-, accent- and
//    punctuation-blind (story key), within kStoryWindowSec. Across outlets the
//    headline must have at least kMinCrossSourceWords words — short generic
//    titles ("Here's the latest") from different outlets are different
//    stories. The outlets folded away are kept in `also_sources`, so the
//    clustering still counts every outlet that carried the story.
// 3. A templated series from one source: "iShares iBonds Dec 2055 Term
//    Treasury ETF declares monthly distribution of $0.0959", then the same for
//    Dec 2054, 2045, 2044… one per fund, minutes apart. Matched on the
//    headline with every number masked (series key), same source, all within
//    kSeriesWindowSec — short, so two separate numeric events a day apart
//    ("magnitude 6.1 quake…", "magnitude 7.0 quake…") stay two rows. They are
//    distinct items, so the kept row counts them in `similar` ("+N").
//
// Windows bound the row's whole span (oldest to newest item folded in), so a
// daily repeat can't chain for days. Items without a parsed date (sort_ts 0)
// fold only by link.
//
// Which copy is shown: a dated one; then for one story the better tier (lower
// number = wire/major), then the first report; for a series its newest item.
// The row keeps its id, the most urgent priority, the most severe threat and
// any credibility flag among its copies. Whichever is shown, a pill region
// (US / CHINA / EU, see is_pill_feed_region) wins over any other tag — if the BBC World (GLOBAL) or
// SCMP News (ASIA) copy were kept as-is, the story would lose the CHINA tag
// its China-desk copy carries, and with it its place under the CHN pill — and
// the copies' tickers are joined.

#include "services/news/NewsRegions.h"
#include "services/news/NewsService.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <algorithm>

namespace fincept::news {

// Words are letter/digit runs; each CJK character counts as one (no spaces).
inline constexpr int kMinCrossSourceWords = 5; // "Here's the latest" is 4 (here, s, the, latest)
// Short enough that a daily repost of one title (a live blog) stays one row
// per day; outlets carrying one wire story do so within hours.
inline constexpr int64_t kStoryWindowSec = 18 * 3600;
inline constexpr int64_t kSeriesWindowSec = 6 * 3600;

struct HeadlineKeys {
    QString story;  ///< lower-cased, accent-folded letters/digits; other runs -> one space
    QString series; ///< same with each number ("2,055", "0.0959") -> '#'
    int words = 0;
};

inline bool is_cjk(QChar c) {
    switch (c.script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Hangul:
        return true;
    default:
        return false;
    }
}

inline HeadlineKeys headline_keys(const QString& headline) {
    const QString h = detail::fold_accents(headline);
    HeadlineKeys k;
    k.story.reserve(h.size());
    k.series.reserve(h.size());
    bool gap = true; // at a word boundary
    const qsizetype n = h.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = h.at(i);
        if (!c.isLetterOrNumber()) {
            gap = true;
            continue;
        }
        if (gap) {
            if (!k.story.isEmpty()) {
                k.story += QLatin1Char(' ');
                k.series += QLatin1Char(' ');
            }
            ++k.words;
            gap = false;
        } else if (is_cjk(c)) {
            ++k.words; // a CJK headline has no spaces: one word per character
        }
        if (c.isDigit()) {
            // One number: digits with inner '.'/',' ("2,055", "0.0959").
            qsizetype j = i;
            while (j + 1 < n && (h.at(j + 1).isDigit() ||
                                 ((h.at(j + 1) == QLatin1Char('.') || h.at(j + 1) == QLatin1Char(',')) && j + 2 < n &&
                                  h.at(j + 2).isDigit())))
                ++j;
            for (qsizetype m = i; m <= j; ++m)
                if (h.at(m) != QLatin1Char(',')) // "2,055" and "2055" are one number
                    k.story += h.at(m);
            k.series += QLatin1Char('#');
            i = j;
        } else {
            const QString low = c.toLower();
            k.story += low;
            k.series += low;
        }
    }
    return k;
}

struct FeedMergeIndex {
    QHash<QString, qsizetype> by_link;         ///< exact link -> row
    QHash<QString, qsizetype> by_story;        ///< story key -> row (headlines long enough to match across outlets)
    QHash<QString, qsizetype> by_source_story; ///< source + '\n' + story key -> row
    QHash<QString, qsizetype> by_series;       ///< source + '\n' + series key -> row
    QVector<int64_t> row_min_ts, row_max_ts;   ///< per row: oldest / newest dated item folded in
};

namespace detail {

/// The copy a row shows. Always a dated one over an undated one. For copies
/// of one story: the better tier, then the FIRST report (a late repost must
/// not make an old story look fresh). For a series: its newest item.
inline bool better_copy(const services::NewsArticle& a, const services::NewsArticle& kept, bool series) {
    if ((a.sort_ts > 0) != (kept.sort_ts > 0))
        return a.sort_ts > 0;
    if (series)
        return a.sort_ts > kept.sort_ts;
    return a.tier < kept.tier || (a.tier == kept.tier && a.sort_ts < kept.sort_ts);
}

/// Would folding an item dated `ts` keep the row's span within `window`?
inline bool within(const FeedMergeIndex& idx, qsizetype row, int64_t ts, int64_t window) {
    const int64_t lo = idx.row_min_ts[row], hi = idx.row_max_ts[row];
    if (ts <= 0 || lo <= 0)
        return false; // undated: no headline folding
    return std::max(hi, ts) - std::min(lo, ts) <= window;
}

/// Folds `a` into the row at `row`, showing the better copy's content.
inline void fold_into(QVector<services::NewsArticle>& all, FeedMergeIndex& idx, qsizetype row,
                      const services::NewsArticle& a, int extra_similar) {
    auto& kept = all[row];
    QString region = kept.region;
    if (!is_pill_feed_region(region) && is_pill_feed_region(a.region))
        region = a.region;
    QStringList tickers = kept.tickers;
    for (const auto& t : a.tickers)
        if (!tickers.contains(t))
            tickers << t;
    QStringList outlets = QStringList{kept.source} + kept.also_sources;
    for (const auto& s : QStringList{a.source} + a.also_sources)
        if (!s.isEmpty() && !outlets.contains(s))
            outlets << s;
    const int similar = kept.similar + a.similar + extra_similar;
    // The row is as urgent, as threatening and as flagged as its loudest copy.
    const auto priority = std::min(kept.priority, a.priority); // enum: FLASH first
    const auto threat = a.threat.level < kept.threat.level ? a.threat : kept.threat; // CRITICAL first
    const auto flag = kept.source_flag != services::SourceFlag::NONE ? kept.source_flag : a.source_flag;
    if (better_copy(a, kept, extra_similar > 0)) {
        // The row keeps its id: selection, seen-state and saved stars follow
        // the row, not whichever copy it currently shows.
        const QString id = kept.id;
        kept = a;
        kept.id = id;
    }
    kept.region = region;
    kept.tickers = tickers;
    kept.similar = similar;
    kept.priority = priority;
    kept.threat = threat;
    kept.source_flag = flag;
    outlets.removeAll(kept.source);
    kept.also_sources = outlets;
    if (a.sort_ts > 0) {
        idx.row_min_ts[row] = idx.row_min_ts[row] > 0 ? std::min(idx.row_min_ts[row], a.sort_ts) : a.sort_ts;
        idx.row_max_ts[row] = std::max(idx.row_max_ts[row], a.sort_ts);
    }
}

} // namespace detail

inline void merge_feed_batch(QVector<services::NewsArticle>& all, FeedMergeIndex& idx,
                             const QVector<services::NewsArticle>& batch) {
    // Rows added outside this function (none today) get their own span.
    while (idx.row_min_ts.size() < all.size()) {
        const int64_t ts = all[idx.row_min_ts.size()].sort_ts;
        idx.row_min_ts.append(ts);
        idx.row_max_ts.append(ts);
    }
    for (const auto& a : batch) {
        const QString link = a.link.trimmed();
        const HeadlineKeys k = headline_keys(a.headline);
        const QString src_story = a.source + QLatin1Char('\n') + k.story;
        const QString src_series = a.source + QLatin1Char('\n') + k.series;
        const bool has_number = k.series != k.story;
        const bool cross_ok = k.words >= kMinCrossSourceWords;

        qsizetype row = -1;
        int extra_similar = 0;
        // 1. Same link: always the same item.
        if (!link.isEmpty())
            row = idx.by_link.value(link, -1);
        if (row < 0 && !k.story.isEmpty()) {
            // 2. Same headline — same outlet, or another outlet if long enough.
            // (A same-outlet row out of the window must not hide a fresh
            // row of the story from another outlet.)
            qsizetype r = idx.by_source_story.value(src_story, -1);
            if (r >= 0 && detail::within(idx, r, a.sort_ts, kStoryWindowSec))
                row = r;
            if (row < 0 && cross_ok) {
                r = idx.by_story.value(k.story, -1);
                if (r >= 0 && detail::within(idx, r, a.sort_ts, kStoryWindowSec))
                    row = r;
            }
            // 3. Same outlet, same headline but for its numbers, close in time.
            if (row < 0 && has_number) {
                r = idx.by_series.value(src_series, -1);
                // by_series is keyed by source, so this row carries this outlet.
                if (r >= 0 && detail::within(idx, r, a.sort_ts, kSeriesWindowSec)) {
                    row = r;
                    extra_similar = 1;
                }
            }
        }

        if (row < 0) {
            row = all.size();
            all.append(a);
            idx.row_min_ts.append(a.sort_ts);
            idx.row_max_ts.append(a.sort_ts);
        } else {
            detail::fold_into(all, idx, row, a, extra_similar);
        }
        // Keys point at the newest row they produced, so a stale row outside
        // the window never shadows a fresh one.
        if (!link.isEmpty())
            idx.by_link.insert(link, row);
        if (!k.story.isEmpty()) {
            idx.by_source_story.insert(src_story, row);
            if (cross_ok)
                idx.by_story.insert(k.story, row);
            if (has_number)
                idx.by_series.insert(src_series, row);
        }
    }
}

} // namespace fincept::news

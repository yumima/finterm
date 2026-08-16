#pragma once
// Shared formatting for AI news briefs (TL;DR and DIGEST).
//
// Both the reading pane and the INTEL drawer render the same model output and
// both need to split it, so this belongs to neither widget. It previously
// lived on NewsDetailPanel, which meant NewsSidePanel had to include a sibling
// widget's header — and drag its service dependencies along — to call one pure
// function.
//
// Header-only and free of Qt widget headers, so a test can exercise it without
// standing up either panel.

#include "services/news/NewsCategories.h"

#include <QHash>
#include <QLatin1StringView>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <algorithm>

namespace fincept::screens::brief {

/// Separates the top summary from the per-category detail in a model brief.
/// Both prompts are told to emit this on its own line. When it is absent — an
/// older cached brief, or a model that ignored the instruction — split() falls
/// back to the first "### NAME" section, because the category half identifies
/// itself and does not actually need the sentinel.
inline constexpr QLatin1StringView kCategoryMarker{"<<<CATEGORIES>>>"};

/// Collapses repeated "### NAME" sections in the per-category half of a brief,
/// and takes apart headings that name more than one category.
///
/// The model is asked for one section per category, but it routinely emits the
/// same heading two or three times — an observed brief had GEOPOLITICS, ENERGY
/// and ECONOMIC each listed twice. It also merges two categories into one
/// heading ("### DEFENSE, CRYPTO"), which costs the reader a whole section: the
/// breakdown showed five names where the model had covered six. A prompt cannot
/// guarantee either; the render can.
///
/// Bullets are merged under the FIRST occurrence of each heading, first-seen
/// order is preserved, and byte-identical bullets are dropped (the model
/// sometimes restates a line verbatim under its duplicate heading).
///
/// A merged heading is split into its named categories and each bullet is
/// classified among just those names by the same keyword table that classified
/// the headline it came from. Bullets no keyword claims go under the first
/// name — a guess, but a bounded one.
///
/// The split only happens when it LOSES NOTHING. A name that ends up with no
/// bullets could only be rendered as an empty section, which reads as "we
/// looked and there was nothing" and is a claim the model never made — so
/// rather than drop the name (which would make the split cost a category, the
/// very failure it exists to fix) the heading is left exactly as the model
/// wrote it. A name may go bullet-less only when it already has a section of
/// its own elsewhere in the brief, because then nothing has disappeared. That
/// case is the real prize: "### DEFENSE, CRYPTO" and a later "### CRYPTO" used
/// to be two unrelated keys.
///
/// Text before the first heading is passed through untouched, and input with
/// no headings at all is returned unchanged.
inline QString merge_duplicate_sections(const QString& detail) {
    if (detail.isEmpty())
        return detail;

    const QStringList lines = detail.split(QLatin1Char('\n'));

    // "## " through "#### ", but never a lone '#': "#1 story" is ordinary
    // bullet text and reading it as a heading would swallow the rest of the
    // section. Tolerating the deeper levels means a model that varies the
    // heading depth still gets deduplicated instead of silently splitting.
    auto is_heading = [](const QString& trimmed) {
        return trimmed.startsWith(QLatin1String("##")) && !trimmed.mid(2).trimmed().isEmpty();
    };
    auto heading_name = [](const QString& trimmed, int* hashes = nullptr) {
        QString name = trimmed;
        int n = 0;
        while (name.startsWith(QLatin1Char('#'))) {
            name.remove(0, 1);
            ++n;
        }
        if (hashes)
            *hashes = n;
        return name.trimmed();
    };

    // Which of `named` would receive at least one bullet if this heading split.
    // Reads the heading's own body without consuming it, so the decision can be
    // made before any filing starts. The first name absorbs bullets no keyword
    // claims, so it only counts when there is such a bullet to absorb.
    auto claims_of = [&](int heading_index, const QStringList& named) {
        const QSet<QString> allowed(named.cbegin(), named.cend());
        QSet<QString> claimed;
        for (int j = heading_index + 1; j < lines.size(); ++j) {
            const QString t = lines.at(j).trimmed();
            if (is_heading(t))
                break;
            if (t.isEmpty())
                continue;
            const QString c = news::classify(t.toLower(), allowed);
            claimed.insert(c.isEmpty() ? named.first() : c);
        }
        return claimed;
    };

    // Every name that ends up owning a section somewhere in this brief. A
    // merged heading may hand one of its own names no bullets only if the name
    // is in here, because then nothing has disappeared from the breakdown.
    //
    // Computed as a fixpoint, not a single pass. Whether a merged heading gives
    // its names sections depends on whether it splits, which depends on this
    // set — so seeding it from single-name headings alone gets it wrong: two
    // identical "### DEFENSE, CRYPTO" headings would see the first split and
    // the second vetoed, rendering DEFENSE and CRYPTO *and* a third literal
    // "### DEFENSE, CRYPTO", which is worse than the merged heading it was
    // trying to repair. The set only grows, so this settles; a brief has a
    // handful of headings and it settles in one or two rounds.
    QList<QPair<int, QStringList>> merged; // heading index -> its names
    QSet<QString> will_have_section;
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines.at(i).trimmed();
        if (!is_heading(t))
            continue;
        const QStringList named = news::categories_in_heading(heading_name(t));
        if (named.size() == 1)
            will_have_section.insert(named.first());
        else if (named.size() >= 2)
            merged.append({i, named});
    }
    auto would_split = [&](int heading_index, const QStringList& named) {
        const QSet<QString> claimed = claims_of(heading_index, named);
        for (const QString& n : named) {
            if (!claimed.contains(n) && !will_have_section.contains(n))
                return false;
        }
        return true;
    };
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [index, named] : merged) {
            if (!would_split(index, named))
                continue;
            for (const QString& n : claims_of(index, named)) {
                if (!will_have_section.contains(n)) {
                    will_have_section.insert(n);
                    grew = true;
                }
            }
        }
    }

    QString preamble;
    QStringList order;                 // heading keys, first-seen order
    QHash<QString, QString> display;   // key -> heading line as first written
    QHash<QString, QStringList> body;  // key -> accumulated body lines

    QString current;         // empty => still in the preamble
    QStringList split_names; // non-empty => `current` heading merged categories
    QSet<QString> split_set; // same names, for classify()'s allow-list
    QString split_hashes;    // the heading's own '#' run, reused for the parts

    // Registers a section on first use. A split part comes through here only
    // when a bullet actually lands in it; a part that gets none was already
    // cleared by the fixpoint above as owning a section elsewhere, and that
    // heading registers it.
    auto ensure_section = [&](const QString& key, const QString& heading_line) {
        if (display.contains(key))
            return;
        order.append(key);
        display.insert(key, heading_line);
        body.insert(key, {});
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        const QString trimmed = line.trimmed();
        if (is_heading(trimmed)) {
            int hashes = 0;
            const QString name = heading_name(trimmed, &hashes);
            split_hashes = QString(hashes, QLatin1Char('#'));

            // Two or more names means the model merged categories. Fewer covers
            // the ordinary "### TECH" and anything reaching outside the
            // vocabulary — categories_in_heading is all-or-nothing, so
            // "### ENERGY & COMMODITIES" yields nothing rather than silently
            // deleting the word COMMODITIES from the brief.
            const QStringList named = news::categories_in_heading(name);
            split_names.clear();
            split_set.clear();
            if (named.size() >= 2 && would_split(i, named)) {
                split_names = named;
                split_set = QSet<QString>(named.cbegin(), named.cend());
                current = named.first(); // fallback owner for the unclaimed
            } else {
                current = name.toUpper();
                ensure_section(current, trimmed);
            }
            continue;
        }
        if (current.isEmpty()) {
            if (!trimmed.isEmpty() || !preamble.isEmpty())
                preamble += line + QLatin1Char('\n');
            continue;
        }
        // Blank lines inside a section are dropped rather than preserved: the
        // body is a bullet list, and tightening it is the desired render. Only
        // the separation BETWEEN sections is re-established by the rebuild.
        if (trimmed.isEmpty())
            continue;

        QString key = current;
        if (!split_names.isEmpty()) {
            const QString claimed = news::classify(trimmed.toLower(), split_set);
            key = claimed.isEmpty() ? split_names.first() : claimed;
            // Only the split parts need registering here — an ordinary heading
            // registered itself above, and reaching this for one would mean
            // inventing a heading line for a section that already has one.
            ensure_section(key, split_hashes + QLatin1Char(' ') + key);
        }
        if (!body[key].contains(line))
            body[key].append(line);
    }

    if (order.isEmpty())
        return detail; // nothing heading-shaped; leave it alone

    QString out = preamble.trimmed();
    for (const QString& key : order) {
        if (!out.isEmpty())
            out += QLatin1String("\n\n");
        out += display.value(key);
        for (const QString& line : body.value(key))
            out += QLatin1Char('\n') + line;
    }
    return out;
}

/// Splits a model brief into {summary, per-category detail}.
///
/// Streaming-aware: a DIGEST arrives in chunks, so a chunk boundary can land
/// mid-marker and leave a dangling "<<<CATE" at the end of the text. Rendering
/// that fragment would show the user raw protocol for a frame, so a trailing
/// partial marker is trimmed off the summary.
inline QPair<QString, QString> split(const QString& text) {
    const int marker = text.indexOf(kCategoryMarker);
    if (marker >= 0) {
        return {text.left(marker).trimmed(),
                merge_duplicate_sections(text.mid(marker + kCategoryMarker.size()).trimmed())};
    }

    // No marker, but the category half is self-identifying: it is the run of
    // "### NAME" sections at the end. Split there instead of giving up.
    //
    // Depending on a model to echo an exact sentinel is the fragile part of
    // this design. qwen3.5 writes the category sections correctly and simply
    // omits the marker, and the all-or-nothing split then rendered the whole
    // brief as one blob in the summary pane with an empty category pane —
    // exactly the "single paragraph, two categories" regression. The structure
    // is recoverable without the sentinel, so recover it.
    //
    // Anchored on the FIRST heading that is followed only by heading/bullet/
    // blank lines, so a "### " inside the summary body cannot split it early.
    {
        const QStringList lines = text.split(QLatin1Char('\n'));
        int first_heading = -1;
        for (int i = 0; i < lines.size(); ++i) {
            const QString t = lines.at(i).trimmed();
            if (!t.startsWith(QLatin1String("###")) || t.mid(3).trimmed().isEmpty())
                continue;
            bool tail_is_sections = true;
            for (int j = i + 1; j < lines.size(); ++j) {
                const QString u = lines.at(j).trimmed();
                // U+2022 as a code point, not QLatin1Char('•'). Latin-1 is a
                // single byte and '•' is not in it: the multi-byte character
                // literal truncated to -94, so the test compared against '¢'
                // and no bullet ever matched. A brief written with • bullets
                // therefore failed tail_is_sections, the fallback gave up, and
                // the whole brief rendered as one blob with an empty BY
                // CATEGORY pane — the symptom this fallback exists to prevent.
                if (u.isEmpty() || u.startsWith(QLatin1Char('#')) || u.startsWith(QLatin1Char('-'))
                    || u.startsWith(QLatin1Char('*')) || u.startsWith(QChar(0x2022)))
                    continue;
                tail_is_sections = false;
                break;
            }
            if (tail_is_sections) {
                first_heading = i;
                break;
            }
        }
        if (first_heading > 0) {
            const QString head = lines.mid(0, first_heading).join(QLatin1Char('\n')).trimmed();
            const QString tail = lines.mid(first_heading).join(QLatin1Char('\n')).trimmed();
            if (!head.isEmpty() && !tail.isEmpty())
                return {head, merge_duplicate_sections(tail)};
        }
    }

    // No complete marker. If the text ends with a prefix of one, trim it.
    // Ordinary prose containing '<' (e.g. "yields fell <2%") is left alone,
    // because the run must match the marker from its very first character.
    //
    // Test each trailing run longest-first. Anchoring on lastIndexOf('<')
    // instead is wrong: a "<<" fragment ends at the second '<', so trimming
    // from there leaves the first one behind.
    //
    // Compared char by char against the Latin-1 marker — QLatin1StringView is
    // 8-bit so it cannot be viewed as a QStringView, and this keeps the check
    // allocation-free on a path that runs once per stream chunk.
    const qsizetype max_len =
        std::min<qsizetype>(kCategoryMarker.size() - 1, text.size());
    for (qsizetype len = max_len; len > 0; --len) {
        const QStringView tail = QStringView{text}.right(len);
        bool is_prefix = true;
        for (qsizetype i = 0; i < len; ++i) {
            if (tail[i] != QLatin1Char(kCategoryMarker[i])) {
                is_prefix = false;
                break;
            }
        }
        if (is_prefix)
            return {text.left(text.size() - len).trimmed(), QString()};
    }
    return {text, QString()};
}

} // namespace fincept::screens::brief

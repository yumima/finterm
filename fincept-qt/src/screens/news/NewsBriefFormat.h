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

#include <QLatin1StringView>
#include <QPair>
#include <QString>
#include <QStringView>

#include <algorithm>

namespace fincept::screens::brief {

/// Separates the top summary from the per-category detail in a model brief.
/// Both prompts are told to emit this on its own line. When it is absent —
/// an older cached brief, or a model that ignored the instruction — the whole
/// text is treated as the summary and the detail section stays empty.
inline constexpr QLatin1StringView kCategoryMarker{"<<<CATEGORIES>>>"};

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
                text.mid(marker + kCategoryMarker.size()).trimmed()};
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

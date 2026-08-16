#pragma once
// Canonical news category vocabulary.
//
// Three places need to agree on what the categories are and what words signal
// them: enrich_article() when it classifies an incoming headline, the brief
// prompt when it tells the model which names to draw from, and the brief
// renderer when it has to take a heading the model combined ("### DEFENSE,
// CRYPTO") apart again. They were three separate lists; when they drift, the
// model is asked for a category the classifier never assigns, or the renderer
// fails to recognise a heading as a category at all and leaves it merged.
//
// Header-only and Qt-Core-only, so the renderer and its test can use it
// without pulling in NewsService and its network stack.

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <iterator>

namespace fincept::news {

/// One category and the substrings that signal it in lowercased article text.
struct CategoryRule {
    const char* name;
    /// nullptr-terminated. The bound leaves a slot spare on purpose: filling
    /// every slot with a keyword would drop the terminator and classify()
    /// would run off the end. A static_assert below catches that at build
    /// time, but the headroom means an added keyword usually just fits.
    const char* keywords[10];
};

/// Classification order is significant: the FIRST rule whose keyword appears
/// wins, so the more specific categories come before the broader ones.
/// "Bitcoin miners' quarterly results" is EARNINGS, not CRYPTO, because a
/// results story is a results story whatever the sector.
///
/// This table is the extracted form of the if/else-if chain enrich_article()
/// used to carry inline — same order, same keywords, same first-match-wins
/// semantics — so moving it here changed no classification.
inline constexpr CategoryRule kCategoryRules[] = {
    {"EARNINGS", {"earnings", "quarterly results", "eps", "guidance", nullptr}},
    {"CRYPTO", {"crypto", "bitcoin", "ethereum", "blockchain", nullptr}},
    {"DEFENSE", {"missile", "troops", "pentagon", "military", nullptr}},
    {"ECONOMIC",
     {"fed ", "federal reserve", "inflation", "gdp", "interest rate", "central bank", nullptr}},
    {"MARKETS", {"s&p 500", "nasdaq", "dow jones", "stock market", nullptr}},
    {"ENERGY", {"energy", "crude", "opec", "natural gas", "oil price", nullptr}},
    {"TECH", {"tech", " ai ", "artificial intelligence", "semiconductor", "startup", nullptr}},
    {"GEOPOLITICS",
     {"nato", "ukraine", "russia", "china", "gaza", "sanctions", "geopolit", nullptr}},
};

/// classify() walks each keyword list until it hits the nullptr, so a list
/// with no terminator would read past the array. Checked here rather than
/// trusted to review.
constexpr bool all_rules_terminated() {
    for (const auto& r : kCategoryRules) {
        if (r.keywords[std::size(r.keywords) - 1] != nullptr)
            return false;
    }
    return true;
}
static_assert(all_rules_terminated(),
              "every CategoryRule keyword list must leave a trailing nullptr");

/// Every classifiable category name, in classification order.
///
/// The lists below are returned by const reference and built once. The brief
/// renderer calls into them for every heading of every stream chunk of a live
/// DIGEST, and there is no reason to rebuild a fixed nine-string list thousands
/// of times to render one drawer.
inline const QStringList& category_names() {
    static const QStringList kNames = [] {
        QStringList out;
        for (const auto& r : kCategoryRules)
            out << QString::fromLatin1(r.name);
        return out;
    }();
    return kNames;
}

/// A section the brief writes when the user holds positions. It has no keyword
/// rule because no wording in a headline makes a story "portfolio" — only the
/// reader's holdings do — but it is a heading the model writes, so heading
/// parsing has to know the word. Without it "### PORTFOLIO, TECH" is the one
/// merged heading the splitter can never take apart, for exactly the users the
/// section exists for.
inline constexpr QLatin1StringView kPortfolioCategory{"PORTFOLIO"};

/// Every name that may legitimately appear as a "### NAME" heading.
inline const QStringList& heading_vocabulary() {
    static const QStringList kVocab = [] {
        QStringList out = category_names();
        out << QString(kPortfolioCategory);
        return out;
    }();
    return kVocab;
}

/// The category menu offered to the model, in editorial order.
///
/// Deliberately NOT category_names(): that order is keyword precedence, chosen
/// so "bitcoin miner quarterly results" resolves to EARNINGS rather than
/// CRYPTO. The prompt asks for at most six headings out of eight and position
/// in a list measurably sways which the model reaches for, so offering them in
/// precedence order would quietly bias briefs toward EARNINGS and CRYPTO over
/// MARKETS and TECH. The two orders answer different questions and are kept
/// apart; a test pins that they hold the same names.
inline const QStringList& prompt_menu() {
    static const QStringList kMenu = {
        QStringLiteral("MARKETS"),  QStringLiteral("TECH"),     QStringLiteral("GEOPOLITICS"),
        QStringLiteral("ENERGY"),   QStringLiteral("ECONOMIC"), QStringLiteral("CRYPTO"),
        QStringLiteral("DEFENSE"),  QStringLiteral("EARNINGS")};
    return kMenu;
}

/// The category a piece of text belongs to, or an empty string if no keyword
/// matched. `lowered` must already be lowercased — callers classify the same
/// string they use for every other keyword test, and lowering it here would
/// mean doing that work twice per article.
///
/// `allowed`, when non-empty, restricts the answer to those categories. That is
/// what lets a combined heading be taken apart: a bullet under "DEFENSE,
/// CRYPTO" is asked which of those two it is, not which of all eight, so a
/// passing mention of Russia cannot drag it into GEOPOLITICS — a section the
/// model did not put it in.
inline QString classify(const QString& lowered, const QSet<QString>& allowed = {}) {
    for (const auto& r : kCategoryRules) {
        const QString name = QString::fromLatin1(r.name);
        if (!allowed.isEmpty() && !allowed.contains(name))
            continue;
        for (int i = 0; r.keywords[i] != nullptr; ++i) {
            if (lowered.contains(QLatin1String(r.keywords[i])))
                return name;
        }
    }
    return {};
}

/// The categories a heading names, in the order the heading writes them.
///
/// The model is told one heading names one category, but it still merges them
/// ("### DEFENSE, CRYPTO"), and a merged heading costs the reader a whole
/// section in the breakdown. Recognising the parts is the first half of putting
/// them back; splitting on the separators a model actually uses — comma, slash,
/// ampersand, the word "and" — rather than trying to enumerate them.
///
/// ALL-OR-NOTHING: returns the names only when every part is in the heading
/// vocabulary, and an empty list otherwise. Recognising just the parts it knows
/// would be worse than not splitting at all — "ENERGY, COMMODITIES & CRYPTO"
/// would render as ENERGY plus CRYPTO with the word COMMODITIES deleted and its
/// bullet misfiled, which is the very "a category vanishes from the breakdown"
/// failure this splitting exists to prevent.
///
/// Fewer than two names also means "leave it alone": one name is an ordinary
/// heading, zero means nothing recognisable.
inline QStringList categories_in_heading(const QString& heading) {
    static const QRegularExpression kSep(
        QStringLiteral("\\s*(?:[,/&+]|\\band\\b)\\s*"), QRegularExpression::CaseInsensitiveOption);
    // Models dress headings up: "### **DEFENSE, CRYPTO**" and "### DEFENSE,
    // CRYPTO:" are both common, and both leave the emphasis or the colon stuck
    // to a part ("**DEFENSE", "CRYPTO:") so the lookup misses and the repair is
    // skipped on the formatting the model most often produces.
    //
    // No \\s in the class: remove() strips every match, not just the edges, so
    // "MARKET S" would collapse to "MARKETS" and be recognised as a category it
    // is not. Whitespace around separators is already eaten by kSep, and the
    // outer edges are handled by trimmed().
    static const QRegularExpression kDecoration(QStringLiteral("[*_`:.]"));
    QStringList out;
    QSet<QString> seen;
    const QStringList& known = heading_vocabulary();
    const QStringList parts = heading.split(kSep, Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        QString up = part.trimmed().toUpper();
        up.remove(kDecoration);
        if (!known.contains(up))
            return {}; // one unknown part poisons the whole heading
        if (!seen.contains(up)) {
            seen.insert(up);
            out << up;
        }
    }
    return out;
}

} // namespace fincept::news

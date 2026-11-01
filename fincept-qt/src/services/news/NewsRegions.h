#pragma once
// Region tabs for the news screen: US, CHN, EU.
//
// A region tab covers every category (tech, economy, geopolitics, markets,
// …) for one part of the world. Region is a separate axis from category: a
// story about EU chip subsidies is TECH and EU, and the region pill has to
// find it whichever category the classifier gave it.
//
// The feed's own region tag isn't enough by itself. Most coverage of all
// three regions comes from GLOBAL feeds (BBC, AP, Bloomberg, Guardian World),
// and a China story carried by BBC World is still a China story. So an
// article is in a region when EITHER its feed is tagged with that region OR
// its headline/summary names the region (a country, its institutions, its
// cities, its leaders, its market benchmarks). An article can be in more than
// one region: "US tariffs on Chinese EVs" belongs in both US and CHN.
//
// All keywords match WHOLE WORDS against word_bounded_view(), for the reason
// NewsCategories.h gives: bare substrings misfire ("eu" is inside "euro" and
// "neutral", "xi" is inside "taxi"). Keywords are written without
// punctuation because the bounded view turns it into spaces: "U.S." becomes
// "u s" and "S&P 500" becomes "s p 500".
//
// Header-only and Qt-Core-only so the test can include it without pulling in
// NewsService and its network stack.

#include "services/news/NewsCategories.h"

#include <QLatin1StringView>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace fincept::news {

/// One region pill: the label the command bar shows and the filter keys on.
struct RegionPill {
    const char* code;
    const char* tooltip;
};

/// The region pills, in display order. Labels are fixed by the user: US, CHN, EU.
inline constexpr RegionPill kRegionPills[] = {
    {"US", "United States: all news (economy, tech, politics, markets…)"},
    {"CHN", "China: all news (economy, tech, politics, markets…)"},
    {"EU", "European Union & eurozone: all news (economy, tech, politics, markets…)"},
};

inline const QStringList& region_codes() {
    static const QStringList kCodes = [] {
        QStringList out;
        for (const auto& p : kRegionPills)
            out << QString::fromLatin1(p.code);
        return out;
    }();
    return kCodes;
}

inline bool is_region_code(const QString& code) {
    return region_codes().contains(code);
}

/// The feed region tags that put a whole feed under a pill (US / CHN / EU).
inline bool is_pill_feed_region(const QString& feed_region) {
    return feed_region == QLatin1StringView("US") || feed_region == QLatin1StringView("CHINA") ||
           feed_region == QLatin1StringView("EU");
}

namespace detail {

/// Turn a keyword list into " kw " needles, built once per region.
inline QStringList bounded_needles(std::initializer_list<const char*> words) {
    QStringList out;
    out.reserve(static_cast<qsizetype>(words.size()));
    for (const char* w : words)
        out << QLatin1Char(' ') + QString::fromLatin1(w) + QLatin1Char(' ');
    return out;
}

inline bool contains_any(const QString& bounded, const QStringList& needles) {
    for (const QString& n : needles) {
        if (bounded.contains(n))
            return true;
    }
    return false;
}

// "us" is also the pronoun, so lowercased text can't tell "US stocks" from
// "join us". Capital letters can: check for "US"/"USA" as an upper-case
// whole word in the ORIGINAL text. Skip any part written entirely in capitals
// ("TELL US WHAT YOU THINK") because there the capitals carry no meaning.
inline bool has_uppercase_us(const QString& original) {
    if (original.isEmpty())
        return false;
    bool has_lower = false;
    for (const QChar c : original) {
        if (c.isLower()) {
            has_lower = true;
            break;
        }
    }
    if (!has_lower)
        return false;
    // "US$5bn" is a currency, not the country: Hong Kong and Asian outlets
    // write every dollar amount that way, which would file all of their
    // business coverage under US.
    static const QRegularExpression kUsDollar(QStringLiteral("\\bUS\\$"));
    static const QRegularExpression kNonWord(QStringLiteral("[^A-Za-z0-9]+"));
    QString v = original;
    v.replace(kUsDollar, QStringLiteral(" "));
    v.replace(kNonWord, QStringLiteral(" "));
    v = QLatin1Char(' ') + v.trimmed() + QLatin1Char(' ');
    return v.contains(QLatin1StringView(" US ")) || v.contains(QLatin1StringView(" USA "));
}

inline const QStringList& us_needles() {
    static const QStringList k = bounded_needles({
        // the country (lowercased "U.S." / "U.S.A." both start with "u s")
        "u s", "usa", "united states", "america", "american", "americans",
        // government and politics
        "washington", "white house", "capitol hill", "pentagon", "state department", "trump", "biden",
        "republican", "republicans", "democrat", "democrats", "gop", "midterm", "midterms", "fbi", "cia", "fda", "irs",
        "medicare", "medicaid",
        // the central bank and the markets
        "federal reserve", "fed", "fomc", "powell", "wall street", "nasdaq", "nyse", "dow jones",
        "s p 500", "treasuries",
        // places
        "new york", "california", "texas", "florida", "silicon valley",
    });
    return k;
}

// "America" alone inside these phrases means another continent.
inline const QStringList& us_exclusions() {
    static const QStringList k = bounded_needles({
        "latin america", "latin american", "latin americans", "south america", "south american",
        "central america", "central american",
    });
    return k;
}

inline const QStringList& chn_needles() {
    static const QStringList k = bounded_needles({
        "china", "chinese", "sino", "prc", "beijing", "shanghai", "shenzhen", "guangzhou", "hong kong",
        "macau", "xinjiang", "tibet", "taiwan strait",
        // government and politics
        "xi jinping", "president xi", "li qiang", "ccp", "politburo", "national people s congress",
        // central bank, currency and markets
        "pboc", "yuan", "renminbi", "rmb", "hang seng", "csi 300", "shanghai composite",
        // the companies China coverage is mostly about
        "alibaba", "tencent", "huawei", "byd", "baidu", "xiaomi", "bytedance", "pinduoduo", "temu",
        "shein", "smic", "catl", "evergrande",
    });
    return k;
}

inline const QStringList& eu_needles() {
    static const QStringList k = bounded_needles({
        // the union and its institutions
        "eu", "european union", "european commission", "european parliament", "european council",
        "brussels", "strasbourg", "von der leyen", "kallas",
        // the eurozone, the ECB and the markets
        "eurozone", "euro zone", "euro area", "euro", "euros", "ecb", "european central bank", "lagarde",
        "stoxx", "euro stoxx", "dax", "cac 40", "ftse mib", "ibex 35", "bund", "bunds",
        // Europe broadly
        "europe", "european", "europeans",
        // member states. No "polish": it is also the verb.
        "austria", "austrian", "belgium", "belgian", "bulgaria", "bulgarian", "croatia", "croatian",
        "cyprus", "czech", "denmark", "danish", "estonia", "estonian", "finland", "finnish", "france",
        "french", "germany", "german", "germans", "greece", "greek", "hungary", "hungarian", "ireland",
        "irish", "italy", "italian", "latvia", "lithuania", "luxembourg", "malta", "netherlands", "dutch",
        "poland", "portugal", "portuguese", "romania", "romanian", "slovakia", "slovenia", "spain",
        "spanish", "sweden", "swedish",
        // capitals and financial centres
        "berlin", "paris", "rome", "madrid", "amsterdam", "frankfurt", "milan", "vienna", "warsaw",
        "dublin", "lisbon", "athens", "stockholm", "copenhagen", "helsinki", "prague", "budapest",
        // heads of government
        "macron", "merz", "meloni", "orban", "tusk",
    });
    return k;
}

} // namespace detail

/// How much of the summary is searched for region keywords. See in_region().
inline constexpr qsizetype kSummaryLedeChars = 200;

/// Whether an article belongs under a region pill.
///
/// `code` is one of region_codes(). `feed_region` is the article's feed tag
/// (RSSFeed::region: "US", "CHINA", "EU", "GLOBAL", …). Anything that isn't a
/// region code returns false.
inline bool in_region(const QString& code, const QString& feed_region, const QString& headline,
                      const QString& summary) {
    const QStringList* needles = nullptr;
    if (code == QLatin1StringView("US")) {
        if (feed_region == QLatin1StringView("US"))
            return true;
        if (detail::has_uppercase_us(headline))
            return true;
        needles = &detail::us_needles();
    } else if (code == QLatin1StringView("CHN")) {
        if (feed_region == QLatin1StringView("CHINA"))
            return true;
        needles = &detail::chn_needles();
    } else if (code == QLatin1StringView("EU")) {
        if (feed_region == QLatin1StringView("EU"))
            return true;
        needles = &detail::eu_needles();
    } else {
        return false;
    }

    // Only the summary's lede counts. Feeds like the Guardian's carry whole
    // opening paragraphs, and deep in those a region turns up in passing ("the
    // Covid pandemic, which first took hold in China") on stories that are not
    // about it. On a live sample, matching the full summary roughly doubled the
    // summary-only hits and about half of the extra ones were passing mentions
    // like that. The lede is where a story says where it is.
    // Cut at a word boundary: a fixed cut can leave a fragment that is itself a
    // keyword ("Eurovision" cut to "Euro", "Chinatown" to "China").
    QString lede = summary;
    if (lede.size() > kSummaryLedeChars) {
        const qsizetype space = lede.lastIndexOf(QLatin1Char(' '), kSummaryLedeChars);
        lede.truncate(space > 0 ? space : kSummaryLedeChars);
    }
    if (code == QLatin1StringView("US") && detail::has_uppercase_us(lede))
        return true;
    QString bounded = word_bounded_view((headline + QLatin1Char(' ') + lede).toLower());
    if (code == QLatin1StringView("US")) {
        for (const QString& ex : detail::us_exclusions())
            // Repeat until gone: adjacent copies ("latin america latin
            // america") share a space, so one replace() pass consumes it
            // and leaves the second copy behind.
            while (bounded.contains(ex))
                bounded.replace(ex, QStringLiteral(" "));
    }
    return detail::contains_any(bounded, *needles);
}

} // namespace fincept::news

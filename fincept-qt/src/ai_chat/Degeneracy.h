#pragma once
// Degeneracy.h — detect a model response that collapsed into repetition.
//
// Local models occasionally fall into a token loop and emit pages of a single
// pattern. An observed news brief ran to thousands of words of country names,
// then bird species, inside a "### TECH, SECURITY" section. The request
// succeeded, the transport was fine, and the panel rendered the whole thing:
// nothing between the model and the user was looking at the output.
//
// This is model-agnostic on purpose. It is not a qwen3.5 workaround — any
// local model can do this, the collapse is stochastic, and no sampling
// parameter reliably prevents it (measured: default and temp=0.3+rp=1.2 were
// clean while repeat_penalty=1.15 produced a 576-word line, same prompt and
// model). Capping output bounds the damage; this decides whether to show it.
//
// Deliberately conservative — a false positive throws away a good brief, so
// every signal must be extreme before a response is rejected:
//
//   * A very long line that is ALSO repetitive. Length alone is not evidence —
//     the equity-research analysis is legitimately one 141-word paragraph.
//   * Low vocabulary variety across a long response, which is what a repeating
//     loop produces and ordinary prose does not.
//   * A very long run of words with no punctuation at all — see below.

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace fincept::ai_chat {

/// A brief's longest legitimate line. Bullets run to ~40 words; 120 is far past
/// anything the prompt asks for and well clear of a verbose-but-valid model.
inline constexpr int kMaxSaneLineWords = 120;
/// Below this unique/total word ratio a long response is looping rather than
/// writing. Ordinary English prose sits around 0.45-0.65 at this length.
inline constexpr double kMinVocabRatio = 0.22;
/// Short responses are exempt: the ratio is meaningless on a few dozen words.
inline constexpr int kMinWordsToJudge = 250;
/// Longest run of words carrying no punctuation that can still be prose.
/// English breaks for a comma or a full stop every 15-25 words; 80 with no
/// break of any kind is several times past the longest sentence anyone writes.
inline constexpr int kMaxUnpunctuatedRun = 80;

/// True when `text` looks like a repetition collapse rather than an answer.
inline bool looks_degenerate(const QString& text) {
    if (text.isEmpty())
        return false;

    auto vocab_ratio = [](const QString& t) {
        const QStringList w = t.split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts);
        if (w.isEmpty())
            return 1.0;
        QSet<QString> uniq;
        for (const QString& x : w)
            uniq.insert(x.toLower());
        return double(uniq.size()) / double(w.size());
    };

    // A long line is only suspicious when it is ALSO repetitive. Length alone
    // is not evidence: the equity-research analysis is legitimately one
    // 141-word paragraph with no line breaks, and rejecting it would throw away
    // a perfectly good answer. Measured on real output — good prose scores
    // 0.78-0.88, the observed collapses scored 0.09-0.14.
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (line.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() > kMaxSaneLineWords
            && vocab_ratio(line) < kMinVocabRatio)
            return true;
    }

    // The other collapse mode, and the one the vocabulary test cannot see: the
    // model stops writing sentences and chains fresh noun phrases instead —
    // "…compliance regulations adhered ethical considerations addressed
    // transparency practices demonstrated accountability commitments upheld…".
    // An observed brief ran 225 words to a single full stop with no comma
    // anywhere, and scored 0.89 on vocabulary because almost every word was
    // new. Punctuation, not word reuse, is what separates it from prose.
    //
    // A word counts as breaking the run if it contains punctuation ANYWHERE,
    // not just at its end. That is the lenient reading on purpose: "U.S." and
    // "3.5%" reset a run they arguably shouldn't, which can only ever make this
    // miss a collapse — never reject a good answer, which is the failure that
    // costs the user something.
    //
    // A bare bullet or column marker resets it too, so a breakdown that arrives
    // as one line of "• item • item • item" with no full stops reads as the
    // list it is rather than as one enormous sentence. Only the STANDALONE
    // token counts: a hyphen inside a word is not a clause break, and treating
    // it as one would defeat the check on the very output that motivated it —
    // the observed salad contains "cross-sector".
    {
        static const QString kBreaks = QStringLiteral(".,;:!?…");
        static const QStringList kMarkers = {QStringLiteral("-"),   QStringLiteral("*"),
                                             QStringLiteral("|"),   QStringLiteral("•"),
                                             QStringLiteral("–"), QStringLiteral("—")};
        // Tabs separate columns, so treat them as word separators too — split on
        // ' ' alone leaves a tab-joined row as a single unsplittable "word".
        static const QRegularExpression kSpace(QStringLiteral("[ \\t]+"));
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            int run = 0; // a line break ends any run
            for (const QString& w : line.split(kSpace, Qt::SkipEmptyParts)) {
                if (kMarkers.contains(w)
                    || std::any_of(w.cbegin(), w.cend(),
                                   [](QChar c) { return kBreaks.contains(c); })) {
                    run = 0;
                    continue;
                }
                if (++run > kMaxUnpunctuatedRun)
                    return true;
            }
        }
    }

    const QStringList words = text.split(QRegularExpression(QStringLiteral("\\W+")),
                                         Qt::SkipEmptyParts);
    if (words.size() < kMinWordsToJudge)
        return false;
    return vocab_ratio(text) < kMinVocabRatio;
}

} // namespace fincept::ai_chat

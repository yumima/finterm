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
// both signals must be extreme before a response is rejected:
//
//   * A very long line that is ALSO repetitive. Length alone is not evidence —
//     the equity-research analysis is legitimately one 141-word paragraph.
//   * Low vocabulary variety across a long response, which is what a repeating
//     loop produces and ordinary prose does not.

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

namespace fincept::ai_chat {

/// A brief's longest legitimate line. Bullets run to ~40 words; 120 is far past
/// anything the prompt asks for and well clear of a verbose-but-valid model.
inline constexpr int kMaxSaneLineWords = 120;
/// Below this unique/total word ratio a long response is looping rather than
/// writing. Ordinary English prose sits around 0.45-0.65 at this length.
inline constexpr double kMinVocabRatio = 0.22;
/// Short responses are exempt: the ratio is meaningless on a few dozen words.
inline constexpr int kMinWordsToJudge = 250;

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

    const QStringList words = text.split(QRegularExpression(QStringLiteral("\\W+")),
                                         Qt::SkipEmptyParts);
    if (words.size() < kMinWordsToJudge)
        return false;
    return vocab_ratio(text) < kMinVocabRatio;
}

} // namespace fincept::ai_chat

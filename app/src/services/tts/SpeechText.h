#pragma once
// Markdown → the words a voice reads, keeping the pauses the layout implied.
//
// The brief is laid out for the eye: a "### DEFENSE" heading, then bullets.
// Flattened to plain text and joined into one run, nothing separated a heading
// from its first bullet, or one unpunctuated bullet from the next, so the voice
// read "...Fed holds rates defense France ordered..." in one breath. A reader
// pauses at every one of those line breaks; this puts the pause back as
// punctuation the engine already understands.
//
// Header-only and free of widget headers so a test can pin it.

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

#include <algorithm>

namespace finterm::services::speech {

/// A line the voice will read as its own sentence: one that doesn't already
/// end on a stop or a clause mark gets a full stop. "Top stories:" keeps its
/// colon — it introduces what follows. A closing quote or bracket after the
/// stop still counts as ended.
inline QString end_sentence(const QString& line) {
    QString t = line.trimmed();
    // A separator line ("—", "***", a stray quote) has nothing to say: drop it
    // rather than read a lone stop and its pause.
    if (std::none_of(t.cbegin(), t.cend(), [](QChar c) { return c.isLetterOrNumber(); }))
        return {};
    int i = t.size() - 1;
    while (i > 0 && QStringLiteral("\"')]”’").contains(t.at(i)))
        --i;
    if (QStringLiteral(".!?:;…").contains(t.at(i)))
        return t;
    // A trailing dash or comma is a cut-off, not an ending: replace it.
    while (!t.isEmpty() && QStringLiteral(",-–—").contains(t.back()))
        t.chop(1);
    return t.trimmed() + QLatin1Char('.');
}

/// A section heading as said aloud: "DEFENSE" → "Defense". The phonemizer
/// gives an all-caps word a flat, shouted reading and may spell a short one
/// out, so a heading word that is SAID as a word becomes title case: one of
/// five letters or more with a vowel, or a short word on the list ("TECH",
/// "OPEC", "OIL"). Anything else stays as written, because it is spelled out
/// when read: "AI", "FOMC", "NVDA", "US".
inline QString heading_words(const QString& heading) {
    static const QRegularExpression kWord(QStringLiteral("\\b[A-Z]{2,}\\b"));
    static const QRegularExpression kVowel(QStringLiteral("[AEIOUY]"));
    static const QSet<QString> kSpokenShort = {
        QStringLiteral("TECH"), QStringLiteral("OPEC"), QStringLiteral("NATO"), QStringLiteral("OIL"),
        QStringLiteral("GAS"),  QStringLiteral("WAR"),  QStringLiteral("TAX"),  QStringLiteral("JOBS"),
        QStringLiteral("GOLD"), QStringLiteral("BANK"), QStringLiteral("DEBT"), QStringLiteral("RATE"),
        QStringLiteral("RATES"), QStringLiteral("FOOD"), QStringLiteral("AUTO"), QStringLiteral("AND"),
        QStringLiteral("THE"),  QStringLiteral("OF"),   QStringLiteral("IN"),   QStringLiteral("ON"),
        QStringLiteral("CHIPS"), QStringLiteral("TRADE"), QStringLiteral("META"), QStringLiteral("NEWS")};
    QString out;
    qsizetype last = 0;
    for (auto it = kWord.globalMatch(heading); it.hasNext();) {
        const auto m = it.next();
        const QString w = m.captured();
        const bool spoken = kSpokenShort.contains(w) || (w.size() >= 5 && w.contains(kVowel));
        out += heading.mid(last, m.capturedStart() - last);
        out += spoken ? w.left(1) + w.mid(1).toLower() : w;
        last = m.capturedEnd();
    }
    out += heading.mid(last);
    return out.simplified();
}

/// Rendered markdown, one spoken sentence-or-more per line: headings in
/// speakable case, every heading / bullet / paragraph closed with a stop, and
/// the lines joined on '\n' so the engine can pause longer between them than
/// between two sentences of one paragraph. A blank line goes BEFORE each
/// heading: the topic changes there, and that is where a reader takes the
/// longest breath.
inline QString from_markdown(const QString& markdown) {
    QTextDocument doc;
    doc.setMarkdown(markdown);
    QString out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        QString t = b.text().simplified();
        // A table row is read as one line, its cells joined by commas — not
        // a sentence and a pause per cell.
        if (QTextTable* table = QTextCursor(b).currentTable()) {
            const int row = table->cellAt(b.position()).row();
            QStringList cells;
            for (;; b = b.next()) {
                const QString c = b.text().simplified();
                if (!c.isEmpty())
                    cells << c;
                const QTextBlock n = b.next();
                if (!n.isValid() || QTextCursor(n).currentTable() != table || table->cellAt(n.position()).row() != row)
                    break;
            }
            t = cells.join(QStringLiteral(", "));
        }
        if (t.isEmpty())
            continue;
        const bool heading = b.blockFormat().headingLevel() > 0;
        if (heading)
            t = heading_words(t);
        t = end_sentence(t);
        if (t.isEmpty())
            continue;
        if (!out.isEmpty())
            out += heading ? QStringLiteral("\n\n") : QStringLiteral("\n");
        out += t;
    }
    return out;
}

/// An article as read aloud: headline, summary, then each body paragraph,
/// one closed sentence per line so each gets its own pause.
inline QString from_paragraphs(const QString& headline, const QString& summary, const QString& body) {
    QStringList lines;
    for (const QString& para : QStringList{headline, summary} + body.split(QLatin1Char('\n'))) {
        const QString t = end_sentence(para.simplified());
        if (!t.isEmpty())
            lines << t;
    }
    return lines.join(QLatin1Char('\n'));
}

/// Dotted abbreviations as the voice should say them: "A.I." → "AI",
/// "U.S." → "US", "J.P. Morgan" → "JP Morgan".
///
/// Every splitter on the way to the voice — the one below and the engine's own
/// — ends a sentence at a full stop followed by a space, so "an A.I. chip" was
/// read as "an A." (pause) "I." (pause) "chip": two sentence breaks inside a
/// word. Written without the dots it is one token, which the phonemizer spells
/// out as letters, as it does any short all-caps word.
///
/// A run of single letters each followed by a dot is taken; "Inc." and "Ph.D."
/// are not (more than one letter between dots). Lower-case runs are raised
/// ("a.m." → "AM": as "am" it would be read as the word), except the two that
/// are words in disguise: "e.g." and "i.e.". An abbreviation that ends its
/// line keeps a stop, so the line still ends.
inline QString join_initialisms(const QString& text) {
    static const QRegularExpression kDotted(QStringLiteral("(?<![A-Za-z0-9.])((?:[A-Za-z]\\.){2,})(?![A-Za-z0-9])"));
    QString out;
    qsizetype last = 0;
    for (auto it = kDotted.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        QString letters = m.captured(1);
        letters.remove(QLatin1Char('.'));
        const QString lower = letters.toLower();
        QString said = lower == QLatin1String("eg")   ? QStringLiteral("for example")
                       : lower == QLatin1String("ie") ? QStringLiteral("that is")
                                                      : letters.toUpper();
        // At the end of its line (or of the text) the last dot was also the stop.
        qsizetype end = m.capturedEnd();
        qsizetype peek = end;
        while (peek < text.size() && (text[peek] == QLatin1Char(' ') || text[peek] == QLatin1Char('\t')))
            ++peek;
        if (peek >= text.size() || text[peek] == QLatin1Char('\n')) {
            said += QLatin1Char('.');
        } else if (peek > end) {
            // Mid-paragraph, "…in the U.S. The company said" ends a sentence
            // and "U.S. Treasury yields" does not; the text cannot say which.
            // The next word decides when it is one that opens sentences and
            // is not a name.
            static const QSet<QString> kOpeners = {
                QStringLiteral("The"),   QStringLiteral("A"),     QStringLiteral("An"),    QStringLiteral("It"),
                QStringLiteral("Its"),   QStringLiteral("He"),    QStringLiteral("She"),   QStringLiteral("They"),
                QStringLiteral("We"),    QStringLiteral("But"),   QStringLiteral("And"),   QStringLiteral("In"),
                QStringLiteral("On"),    QStringLiteral("This"),  QStringLiteral("That"),  QStringLiteral("These"),
                QStringLiteral("Those"), QStringLiteral("There"), QStringLiteral("However"), QStringLiteral("Meanwhile"),
                QStringLiteral("Still"), QStringLiteral("Yet"),   QStringLiteral("If"),    QStringLiteral("When"),
                QStringLiteral("While"), QStringLiteral("After"), QStringLiteral("Before"), QStringLiteral("As"),
                QStringLiteral("For"),   QStringLiteral("So"),    QStringLiteral("Analysts"), QStringLiteral("Investors"),
                QStringLiteral("Shares"), QStringLiteral("Stocks")};
            qsizetype word_end = peek;
            while (word_end < text.size() && text[word_end].isLetter())
                ++word_end;
            if (kOpeners.contains(text.mid(peek, word_end - peek)))
                said += QLatin1Char('.');
        }
        out += text.mid(last, m.capturedStart() - last) + said;
        last = end;
    }
    return out + text.mid(last);
}

/// Split text for chunked synthesis on sentence ends. The FIRST chunk is kept
/// short so audio starts within a couple of seconds; later chunks are larger
/// (fewer seams) and are synthesised while the previous one plays. No chunk
/// exceeds `cap`, the engine's input limit — so a long brief is read in full
/// rather than truncated (OpenAI rejects input past 4096 chars). Line breaks
/// survive into the chunks: '\n' a paragraph, '\n\n' a section.
inline QStringList split_for_speech(const QString& raw_text, int first_chunk, int second_chunk, int rest_chunk,
                                    int max_input) {
    // Before any splitting: a dot inside "A.I." must not be taken for a stop.
    const QString text = join_initialisms(raw_text);
    // Sizes ramp up: a short first chunk starts the audio fast, a medium second
    // one is synthesised while the first plays (so no gap after it), and the
    // rest are long to keep the seams few. The engine sets the ramp: a slower,
    // more natural voice needs a shorter first chunk.
    const int kFirst = first_chunk, kSecond = second_chunk, kRest = rest_chunk;
    int cap = max_input;
    // Chunk 0 closes at the first paragraph end only once it holds this much —
    // a one-line opener ("Markets rallied.") alone would play in a second and
    // leave silence while chunk 1 synthesises.
    constexpr int kMinFirst = 80;
    cap = cap > 0 ? cap : 4096;
    static const QRegularExpression kSentenceEnd(QStringLiteral("(?<=[.!?:;])\\s+"));
    // Pieces = sentences, each tagged with the break after it: a paragraph
    // (line) end, or a section end (a blank line follows — SpeechText.h puts
    // one before each heading).
    enum Break { kNoBreak, kParaBreak, kSectionBreak }; // not `None`: an X11 macro
    struct Piece {
        QString text;
        Break brk;
    };
    QList<Piece> pieces;
    for (const QString& para : text.split(QLatin1Char('\n'))) {
        if (para.trimmed().isEmpty()) {
            if (!pieces.isEmpty())
                pieces.last().brk = kSectionBreak;
            continue;
        }
        const QStringList sents = para.split(kSentenceEnd, Qt::SkipEmptyParts);
        bool added = false;
        for (int i = 0; i < sents.size(); ++i) {
            const QString t = sents[i].simplified();
            if (!t.isEmpty()) {
                pieces.append({t, kNoBreak});
                added = true;
            }
        }
        if (added)
            pieces.last().brk = kParaBreak;
    }
    QStringList chunks;
    QString cur;
    Break cur_brk = kNoBreak; // the break after the last piece in `cur`
    // The breaks stay in the text the engine gets — '\n' a paragraph, '\n\n'
    // a section — so it can pause longer there than between two sentences. A
    // chunk ending on one carries it at its end: the next chunk starts a new
    // heading or bullet, and the pause belongs between them.
    auto sep = [](Break b) {
        return b == kSectionBreak ? QStringLiteral("\n\n") : b == kParaBreak ? QStringLiteral("\n") : QStringLiteral(" ");
    };
    // Chunk 0 carries at most a paragraph break: whether a SECTION follows it
    // is decided by text after it, which a prewarm from the brief's top half
    // hasn't seen — and the warm clip only matches an identical chunk 0.
    auto flush = [&]() {
        const Break b = chunks.isEmpty() ? std::min(cur_brk, kParaBreak) : cur_brk;
        chunks << (b == kNoBreak ? cur : cur + sep(b));
        cur.clear();
    };
    // Room left for the break a flush may append ("\n\n"): cap stays a hard limit.
    constexpr int kTail = 2;
    for (int i = 0; i < pieces.size(); ++i) {
        const int target = std::min(chunks.isEmpty() ? kFirst : chunks.size() == 1 ? kSecond : kRest, cap);
        Piece pc = pieces[i];
        // A run-on piece longer than this chunk's target is cut on whitespace
        // and the rest goes back in line — so even a punctuation-free opening
        // yields a short chunk 0 and audio in ~2s.
        if (cur.isEmpty() && pc.text.size() + kTail > target) {
            const int room = std::max(1, target - kTail);
            int cut = pc.text.lastIndexOf(QLatin1Char(' '), room);
            if (cut <= 0)
                cut = room;
            pieces.insert(i + 1, {pc.text.mid(cut).trimmed(), pc.brk});
            pc = {pc.text.left(cut), kNoBreak};
        }
        if (!cur.isEmpty() && cur.size() + sep(cur_brk).size() + pc.text.size() + kTail > target) {
            flush();
            --i; // re-place this piece against the next chunk's target
            continue;
        }
        cur = cur.isEmpty() ? pc.text : cur + sep(cur_brk) + pc.text;
        cur_brk = pc.brk;
        // Chunk 0 ends at a paragraph end (or at kFirst), so it depends ONLY on
        // the brief's opening paragraphs: a prewarm from the top half (breakdown
        // still being written) speaks the same chunk 0 as the finished brief.
        if (chunks.isEmpty() && pc.brk != kNoBreak && cur.size() >= kMinFirst)
            flush();
    }
    if (!cur.isEmpty())
        chunks << cur; // the end of the reading: no pause owed after it
    return chunks;
}

} // namespace finterm::services::speech

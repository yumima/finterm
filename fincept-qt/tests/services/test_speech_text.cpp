// test_speech_text.cpp — the brief as the voice reads it.
//
// The live failure: a "### DEFENSE" heading and its first bullet were joined
// into one run, so the voice read "...Fed holds rates defense France ordered..."
// with no break anywhere. Headings and unpunctuated bullets must come out as
// sentences of their own, and the line breaks must survive chunking so the
// engine can pause on them.

#include <QTest>

#include "services/tts/SpeechText.h"

using namespace finterm::services::speech;

class TestSpeechText : public QObject {
    Q_OBJECT
  private slots:
    void heading_and_bullets_become_sentences() {
        const QString md = QStringLiteral("- **Overall read:** Risk-off as oil spikes\n"
                                          "- **Top stories:**\n"
                                          "  - Fed holds rates\n\n"
                                          "### DEFENSE\n"
                                          "- France ordered 40 Rafale jets.\n"
                                          "- Rheinmetall raised guidance\n\n"
                                          "### AI\n"
                                          "- Chips rallied.");
        QCOMPARE(from_markdown(md), QStringLiteral("Overall read: Risk-off as oil spikes.\n"
                                                   "Top stories:\n"
                                                   "Fed holds rates.\n"
                                                   "\n"
                                                   "Defense.\n"
                                                   "France ordered 40 Rafale jets.\n"
                                                   "Rheinmetall raised guidance.\n"
                                                   "\n"
                                                   "AI.\n"
                                                   "Chips rallied."));
    }

    void end_sentence_rules() {
        QCOMPARE(end_sentence(QStringLiteral("Fed holds")), QStringLiteral("Fed holds."));
        QCOMPARE(end_sentence(QStringLiteral("Top stories:")), QStringLiteral("Top stories:"));
        QCOMPARE(end_sentence(QStringLiteral("He said \"no.\"")), QStringLiteral("He said \"no.\""));
        QCOMPARE(end_sentence(QStringLiteral("Up again?")), QStringLiteral("Up again?"));
        QCOMPARE(end_sentence(QStringLiteral("Prices rose —")), QStringLiteral("Prices rose."));
        QCOMPARE(end_sentence(QStringLiteral("  ")), QString());
        // a separator line has nothing to say: no lone stop
        QCOMPARE(end_sentence(QStringLiteral("—")), QString());
        QCOMPARE(end_sentence(QStringLiteral("\"")), QString());
    }

    void heading_case() {
        QCOMPARE(heading_words(QStringLiteral("GEOPOLITICS")), QStringLiteral("Geopolitics"));
        QCOMPARE(heading_words(QStringLiteral("AI & EV")), QStringLiteral("AI & EV"));
        QCOMPARE(heading_words(QStringLiteral("US TECH")), QStringLiteral("US Tech"));
        // said as words → title case; spelled out → left alone
        QCOMPARE(heading_words(QStringLiteral("OPEC NATO FOMC NVDA OIL")), QStringLiteral("Opec Nato FOMC NVDA Oil"));
        QCOMPARE(heading_words(QStringLiteral("EARNINGS")), QStringLiteral("Earnings"));
    }

    void breaks_survive_chunking() {
        const QString text = QStringLiteral("Overall read: markets slipped as oil spiked on supply fears.\n"
                                            "Fed holds rates.\n\nDefense.\nFrance ordered jets.");
        // Chunk 0 closes at its first paragraph end past 80 chars; the breaks
        // inside it stay, and the one it ends on is carried at its end.
        QCOMPARE(split_for_speech(text, 4000, 4000, 4000, 4096),
                 (QStringList{QStringLiteral("Overall read: markets slipped as oil spiked on supply fears.\n"
                                             "Fed holds rates.\n\nDefense.\n"),
                              QStringLiteral("France ordered jets.")}));
        // A chunk ending on a paragraph says '\n', on a section '\n\n'; the
        // last chunk owes no pause.
        QCOMPARE(split_for_speech(text, 70, 20, 32, 4096),
                 (QStringList{QStringLiteral("Overall read: markets slipped as oil spiked on supply fears.\n"),
                              QStringLiteral("Fed holds rates.\n\n"),
                              QStringLiteral("Defense.\nFrance ordered jets.")}));
    }

    void cap_is_hard_with_multichar_breaks() {
        const QString text = QStringLiteral("aaaaaaaaa.\n\nbbbbbbbbbbbb.\n\nc.\n\ndddddddddddddddddddd eeeeeeeeeeeeeeeeeeee.");
        for (int cap : {12, 16, 24, 30}) {
            const QStringList chunks = split_for_speech(text, cap, cap, cap, cap);
            QVERIFY(!chunks.isEmpty());
            for (const QString& c : chunks)
                QVERIFY2(c.size() <= cap, qPrintable(QStringLiteral("cap %1: %2 chars").arg(cap).arg(c.size())));
        }
    }

    void prewarm_prefix_keeps_chunk0() {
        // The prewarm speaks the brief's top half before the breakdown exists;
        // the click must ask for the identical chunk 0 or the warm clip is lost.
        const QString top = QStringLiteral("- **Overall read:** Risk-off as oil spikes on supply fears across the Gulf.\n"
                                           "- **Top stories:** Nvidia beat estimates.");
        const QString full = top + QStringLiteral("\n\n### DEFENSE\n- France ordered jets.");
        QCOMPARE(split_for_speech(from_markdown(top), 130, 350, 700, 4096).first(),
                 split_for_speech(from_markdown(full), 130, 350, 700, 4096).first());
    }

    void table_row_is_one_line() {
        QCOMPARE(from_markdown(QStringLiteral("| Ticker | Move |\n|---|---|\n| NVDA | up 3% |\n")),
                 QStringLiteral("Ticker, Move.\nNVDA, up 3%."));
    }

    void article_paragraphs() {
        QCOMPARE(from_paragraphs(QStringLiteral("Fed holds rates"), QString(),
                                 QStringLiteral("Powell spoke.\n\n—\nMarkets rose")),
                 QStringLiteral("Fed holds rates.\nPowell spoke.\nMarkets rose."));
    }
};

QTEST_MAIN(TestSpeechText)
#include "test_speech_text.moc"

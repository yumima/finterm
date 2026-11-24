#include "services/stt/Pcm16kMono.h"

#include <QTest>

#include <cmath>
#include <vector>

using finterm::services::Pcm16kMono;

class TestPcm16kMono : public QObject {
    Q_OBJECT

    static QAudioFormat fmt(int rate, int channels, QAudioFormat::SampleFormat f) {
        QAudioFormat a;
        a.setSampleRate(rate);
        a.setChannelCount(channels);
        a.setSampleFormat(f);
        return a;
    }
    static QByteArray tone(const QAudioFormat& f, double hz, int ms) {
        QByteArray out;
        for (int i = 0; i < f.sampleRate() * ms / 1000; ++i) {
            const float v = float(0.5 * std::sin(2 * M_PI * hz * i / f.sampleRate()));
            for (int c = 0; c < f.channelCount(); ++c) {
                if (f.sampleFormat() == QAudioFormat::Float) {
                    out.append(reinterpret_cast<const char*>(&v), 4);
                } else {
                    const qint16 s = qint16(v * 32767);
                    out.append(reinterpret_cast<const char*>(&s), 2);
                }
            }
        }
        return out;
    }
    static std::vector<qint16> samples(const QByteArray& b) {
        const auto* p = reinterpret_cast<const qint16*>(b.constData());
        return {p, p + b.size() / 2};
    }
    static double rms(const std::vector<qint16>& v) {
        double s = 0;
        for (qint16 x : v)
            s += double(x) * x;
        return std::sqrt(s / double(v.size())) / 32767;
    }
    static int crossings(const std::vector<qint16>& v) {
        int n = 0;
        for (size_t i = 1; i < v.size(); ++i)
            n += (v[i - 1] < 0) != (v[i] < 0);
        return n;
    }

  private slots:
    void passes_its_own_format_through() {
        const QByteArray in = tone(Pcm16kMono::format(), 440, 50);
        QCOMPARE(Pcm16kMono(Pcm16kMono::format()).convert(in), in);
    }

    void keeps_length_pitch_and_level_data() {
        QTest::addColumn<int>("rate");
        QTest::addColumn<int>("channels");
        QTest::addColumn<bool>("is_float");
        QTest::newRow("48k stereo float") << 48000 << 2 << true;
        QTest::newRow("44.1k stereo") << 44100 << 2 << false;
        QTest::newRow("44.1k mono float") << 44100 << 1 << true;
        QTest::newRow("8k mono") << 8000 << 1 << false;
        QTest::newRow("16k stereo") << 16000 << 2 << false;
    }
    void keeps_length_pitch_and_level() {
        QFETCH(int, rate);
        QFETCH(int, channels);
        QFETCH(bool, is_float);
        const QAudioFormat f = fmt(rate, channels, is_float ? QAudioFormat::Float : QAudioFormat::Int16);
        const QByteArray in = tone(f, 440, 1000);
        const auto whole = samples(Pcm16kMono(f).convert(in));
        QVERIFY2(std::abs(int(whole.size()) - 16000) <= 2, qPrintable(QString::number(whole.size())));
        QVERIFY2(std::abs(crossings(whole) - 880) <= 3, qPrintable(QString::number(crossings(whole))));
        QVERIFY2(std::abs(rms(whole) - 0.3535) < 0.02, qPrintable(QString::number(rms(whole))));
        // In pieces that split frames: the same.
        Pcm16kMono c(f);
        QByteArray pieces;
        for (qsizetype i = 0; i < in.size(); i += 1001)
            pieces += c.convert(in.mid(i, 1001));
        QCOMPARE(samples(pieces), whole);
    }

    void what_is_too_high_is_weakened_not_folded_down() {
        const QAudioFormat f = fmt(48000, 1, QAudioFormat::Float);
        QVERIFY(rms(samples(Pcm16kMono(f).convert(tone(f, 15500, 500)))) < 0.05);
    }

    void no_format_no_samples() { QVERIFY(Pcm16kMono(QAudioFormat()).convert("abcd").isEmpty()); }
};

QTEST_MAIN(TestPcm16kMono)
#include "test_pcm_16k_mono.moc"

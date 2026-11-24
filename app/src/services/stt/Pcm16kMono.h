#pragma once
// Pcm16kMono.h — what a microphone gives, as what speech recognition takes:
// 16 kHz, one channel, 16-bit. A device that can do that is asked for it; this
// is for one that can't. Header-only and free of audio devices, for its test.

#include <QAudioFormat>
#include <QByteArray>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace finterm::services {

class Pcm16kMono {
  public:
    static constexpr int kRate = 16000;

    explicit Pcm16kMono(const QAudioFormat& from) : from_(from) {}

    static QAudioFormat format() {
        QAudioFormat f;
        f.setSampleRate(kRate);
        f.setChannelCount(1);
        f.setSampleFormat(QAudioFormat::Int16);
        return f;
    }

    /// The converted samples of `in`, which may end inside a frame. State is
    /// carried from call to call, so that chunks join without a seam.
    QByteArray convert(const QByteArray& in) {
        const int frame = from_.bytesPerFrame();
        if (frame <= 0 || from_.sampleRate() <= 0)
            return {};
        if (from_ == format() && rest_.isEmpty() && in.size() % frame == 0)
            return in;
        rest_.append(in);
        const qsizetype frames = rest_.size() / frame;
        QByteArray out;
        out.reserve(frames * kRate / from_.sampleRate() * 2 + 4);
        const double step = double(from_.sampleRate()) / kRate;
        for (qsizetype i = 0; i < frames; ++i) {
            const float s = mono(rest_.constData() + i * frame);
            // Down: the mean of the samples an output one covers (a plain pick
            // would fold what is above 8 kHz into the speech). Up: in between.
            if (step > 1.0) {
                sum_ += s;
                ++count_;
                due_ += 1.0;
                if (due_ >= step) {
                    put(out, sum_ / count_);
                    sum_ = 0;
                    count_ = 0;
                    due_ -= step;
                }
            } else {
                for (; due_ < 1.0; due_ += step)
                    put(out, float(last_ + (s - last_) * due_));
                due_ -= 1.0;
                last_ = s;
            }
        }
        rest_.remove(0, frames * frame);
        return out;
    }

  private:
    float mono(const char* p) const {
        const int channels = std::max(1, from_.channelCount());
        const int bytes = from_.bytesPerSample();
        float sum = 0;
        for (int c = 0; c < channels; ++c)
            sum += from_.normalizedSampleValue(p + c * bytes);
        return sum / float(channels);
    }
    static void put(QByteArray& out, float v) {
        const qint16 s = qToLittleEndian(qint16(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
        out.append(reinterpret_cast<const char*>(&s), 2);
    }

    QAudioFormat from_;
    QByteArray rest_; // the part of a frame that has come
    double due_ = 0;
    float sum_ = 0, last_ = 0;
    int count_ = 0;
};

} // namespace finterm::services

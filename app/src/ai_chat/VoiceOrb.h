#pragma once
// VoiceOrb.h — the live-conversation orb (Gemini Live / ChatGPT voice style).
//
// One painted circle that says what the conversation is doing without words:
//   Idle       a still, dim ring
//   Listening  breathing slowly, in the accent colour — "your turn"
//   Thinking   a ring of dots chasing round — "working on it"
//   Speaking   rippling outward rings — "talking"; a click interrupts
// Header-only; animation by a 30 fps timer that runs only while visible.

#include "ui/theme/Theme.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

namespace finterm::ai_chat {

class VoiceOrb : public QWidget {
  public:
    enum class State { Idle, Listening, Thinking, Speaking };

    explicit VoiceOrb(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(160, 160);
        setCursor(Qt::PointingHandCursor);
        timer_.setInterval(33);
        QObject::connect(&timer_, &QTimer::timeout, this, [this]() {
            t_ += 0.033;
            update();
        });
    }

    void set_state(State s) {
        if (s == state_)
            return;
        state_ = s;
        sync_timer();
        update();
    }
    State state() const { return state_; }

    /// Called on a click (the host decides: interrupt, or start listening).
    std::function<void()> on_click;

  protected:
    // Painting is the CPU cost: animate only while visible AND in a moving
    // state — Idle is a still circle.
    void sync_timer() {
        if (isVisible() && state_ != State::Idle)
            timer_.start();
        else
            timer_.stop();
    }
    void showEvent(QShowEvent*) override { sync_timer(); }
    void hideEvent(QHideEvent*) override { timer_.stop(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && on_click)
            on_click();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c = rect().center();
        const qreal base = std::min(width(), height()) * 0.26;
        const QColor accent(ui::colors::AMBER());
        const QColor dim(ui::colors::TEXT_TERTIARY());

        auto disc = [&](qreal r, QColor col, qreal alpha) {
            col.setAlphaF(std::clamp(alpha, 0.0, 1.0));
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawEllipse(c, r, r);
        };
        switch (state_) {
            case State::Idle:
                disc(base, dim, 0.25);
                break;
            case State::Listening: {
                const qreal k = 0.5 + 0.5 * std::sin(t_ * 2.4); // slow breath
                disc(base * (1.25 + 0.15 * k), accent, 0.12 + 0.10 * k);
                disc(base * (1.0 + 0.06 * k), accent, 0.85);
                break;
            }
            case State::Thinking: {
                disc(base * 0.9, accent, 0.35);
                const int n = 10;
                for (int i = 0; i < n; ++i) {
                    const qreal a = 2 * 3.14159265358979 * i / n + t_ * 3.0;
                    const QPointF d(std::cos(a) * base * 1.35, std::sin(a) * base * 1.35);
                    QColor col = accent;
                    col.setAlphaF(0.25 + 0.75 * (i + 1) / qreal(n));
                    p.setBrush(col);
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(c + d, base * 0.09, base * 0.09);
                }
                break;
            }
            case State::Speaking: {
                for (int i = 0; i < 3; ++i) {
                    const qreal ph = std::fmod(t_ * 0.9 + i / 3.0, 1.0); // 0..1 outward
                    disc(base * (1.0 + 0.9 * ph), accent, 0.35 * (1.0 - ph));
                }
                const qreal k = 0.5 + 0.5 * std::sin(t_ * 9.0);
                disc(base * (1.0 + 0.05 * k), accent, 0.95);
                break;
            }
        }
    }

  private:
    State state_ = State::Idle;
    QTimer timer_;
    qreal t_ = 0;
};

} // namespace finterm::ai_chat

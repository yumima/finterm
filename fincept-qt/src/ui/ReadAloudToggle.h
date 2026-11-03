#pragma once
// ReadAloudToggle.h — one QPushButton bound to one read-aloud session on the
// shared TtsService.
//
// TtsService is a singleton with ONE utterance at a time, and more than one
// button can start it (the news article LISTEN and the TL;DR speaker). Each
// needs the same protocol, which used to be hand-copied per button:
//   - idle → PREP… (synthesis launched) → STOP (audio playing) → idle;
//   - only react to TtsService signals while THIS button owns the session —
//     another button's start fires state_changed(false) through the service's
//     stop(), which must reset this one, not be mistaken for its own;
//   - a refused start flashes NO VOICE instead of stranding on PREP…;
//   - an error resets to idle and puts the reason in the tooltip until the
//     next successful start restores the normal tooltip.
//
// ChatPane's 🔊 deliberately does NOT use this: it is part of hands-free voice
// mode, which auto-speaks replies, resumes the mic when playback ends, and
// mirrors ANY playback on the service — a different contract from a button
// that owns its own reading.

#include "services/tts/TtsService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QTimer>

#include <functional>

namespace fincept::ui {

/// The read-aloud glyphs, drawn rather than shipped as assets so they take the
/// current theme accent: a speaker with sound waves (play), the same speaker
/// dimmed and silent (preparing), and a cross (stop).
enum class ReadAloudGlyph { Speaker, Preparing, Cross };
inline QIcon read_aloud_icon(ReadAloudGlyph g, int px = 16) {
    const qreal dpr = 2.0;  // crisp on HiDPI; Qt scales down on 1x
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c = QColor(colors::AMBER);
    if (g == ReadAloudGlyph::Preparing)
        c.setAlphaF(0.45);
    const qreal s = px / 16.0;
    if (g == ReadAloudGlyph::Cross) {
        p.setPen(QPen(c, 2.0 * s, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4 * s, 4 * s), QPointF(12 * s, 12 * s));
        p.drawLine(QPointF(12 * s, 4 * s), QPointF(4 * s, 12 * s));
        return QIcon(pm);
    }
    // Speaker: a box and a cone.
    QPainterPath body;
    body.moveTo(1.5 * s, 6 * s);
    body.lineTo(4.5 * s, 6 * s);
    body.lineTo(8.5 * s, 2.5 * s);
    body.lineTo(8.5 * s, 13.5 * s);
    body.lineTo(4.5 * s, 10 * s);
    body.lineTo(1.5 * s, 10 * s);
    body.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(body);
    if (g == ReadAloudGlyph::Speaker) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c, 1.4 * s, Qt::SolidLine, Qt::RoundCap));
        // Two waves; Qt angles are in 1/16 degree, centred on 3 o'clock.
        p.drawArc(QRectF(7 * s, 5 * s, 5 * s, 6 * s), -55 * 16, 110 * 16);
        p.drawArc(QRectF(6.5 * s, 2.5 * s, 8.5 * s, 11 * s), -55 * 16, 110 * 16);
    }
    return QIcon(pm);
}

class ReadAloudToggle : public QObject {
  public:
    /// Text labels, or — with `icons` set — the drawn glyphs, the strings
    /// then serving as the button's accessible name per state.
    struct Labels {
        QString idle;
        QString stop;
        QString prep = QStringLiteral("PREP…");
        bool icons = false;
    };

    /// Owned by `button` (parented to it), so it dies with the button.
    ReadAloudToggle(QPushButton* button, Labels labels, QString tooltip)
        : QObject(button), btn_(button), labels_(std::move(labels)), tooltip_(std::move(tooltip)) {
        show(State::Idle);
        btn_->setToolTip(tooltip_);
        auto& tts = services::TtsService::instance();
        connect(&tts, &services::TtsService::state_changed, this, [this](bool speaking) {
            if (!active_)
                return;
            if (speaking) {
                show(State::Stop);
            } else {
                active_ = false;
                show(State::Idle);
            }
        });
        // The glyphs are painted in the accent colour: repaint on a theme switch.
        if (labels_.icons)
            connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
                    [this](const ThemeTokens&) { show(state_); });
        connect(&tts, &services::TtsService::error, this, [this](const QString& msg) {
            if (!active_)
                return;
            active_ = false;
            show(State::Idle);
            btn_->setToolTip(QStringLiteral("Last attempt failed: %1").arg(msg));
        });
    }

    /// The click handler. Stops this toggle's reading if it owns one;
    /// otherwise calls `launch`, which returns TtsService::speak*()'s result.
    void toggle(const std::function<bool()>& launch) {
        if (active_) {
            stop();
            return;
        }
        if (launch()) {
            active_ = true;
            show(State::Prep);
            btn_->setToolTip(tooltip_);
            return;
        }
        // Refused start (no voice anywhere / switched off). An icon has no room
        // for words, so it says so in the tooltip; a text button flashes it.
        if (labels_.icons) {
            btn_->setToolTip(QStringLiteral("No voice available — set one up in Settings → Voice"));
            return;
        }
        btn_->setText(QStringLiteral("NO VOICE"));
        QTimer::singleShot(1500, this, [this]() {
            if (!active_)
                show(State::Idle);
        });
    }

    /// Stop playback if — and only if — this toggle owns it. Idempotent.
    void stop() {
        if (!active_)
            return;
        active_ = false;  // first: TtsService::stop() emits state_changed(false)
        services::TtsService::instance().stop();
        show(State::Idle);
    }

    bool active() const { return active_; }

  private:
    enum class State { Idle, Prep, Stop };
    void show(State st) {
        state_ = st;
        if (labels_.icons) {
            btn_->setText({});
            btn_->setIcon(read_aloud_icon(st == State::Idle   ? ReadAloudGlyph::Speaker
                                          : st == State::Prep ? ReadAloudGlyph::Preparing
                                                              : ReadAloudGlyph::Cross));
            // The state word goes to the accessible name, which an icon lacks.
            btn_->setAccessibleName(st == State::Idle ? labels_.idle : st == State::Prep ? labels_.prep : labels_.stop);
            return;
        }
        btn_->setText(st == State::Idle ? labels_.idle : st == State::Prep ? labels_.prep : labels_.stop);
    }

    QPushButton* btn_;
    Labels labels_;
    QString tooltip_;
    bool active_ = false;
    State state_ = State::Idle;
};

} // namespace fincept::ui

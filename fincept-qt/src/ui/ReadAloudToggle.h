#pragma once
// ReadAloudToggle.h — one QPushButton bound to one read-aloud session on the
// shared TtsService.
//
// TtsService is a singleton with ONE utterance at a time, and more than one
// button can start it (the news article LISTEN and the TL;DR speaker). Each
// needs the same protocol, which used to be hand-copied per button:
//   - idle → PREP… (synthesis launched) → STOP (audio playing) → idle;
//   - follow only THIS button's reading: TtsService tags every edge with a
//     session id, and the toggle records the id its launch created, so another
//     button's reading (or the stop() that precedes it) is never mistaken for
//     its own;
//   - a refused start says why (the service's own reason) instead of
//     stranding on PREP…;
//   - an error resets to idle and puts the reason in the tooltip until the
//     next successful start restores the normal tooltip;
//   - right-click opens the voice menu (the engine's voices, current one
//     checked). Picking one re-reads what was playing in the new voice, or
//     says a short line in it so the choice can be heard.
//
// ChatPane's 🔊 deliberately does NOT use this: it is part of hands-free voice
// mode, which auto-speaks replies, resumes the mic when playback ends, and
// mirrors ANY playback on the service — a different contract from a button
// that owns its own reading.

#include "services/tts/TtsService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QTimer>

#include <functional>

namespace fincept::ui {

/// The read-aloud glyphs, drawn rather than shipped as assets so they take the
/// current theme accent: a speaker with sound waves (play), the same speaker
/// dimmed and silent (preparing), and the speaker struck through (playing —
/// click to silence). Not a bare cross: next to a pane, a cross reads as
/// "close this".
enum class ReadAloudGlyph { Speaker, Preparing, Mute };
inline QIcon read_aloud_icon(ReadAloudGlyph g, int px = 16) {
    const qreal dpr = 2.0; // crisp on HiDPI; Qt scales down on 1x
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c = QColor(colors::AMBER);
    if (g == ReadAloudGlyph::Preparing)
        c.setAlphaF(0.45);
    const qreal s = px / 16.0;
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
    if (g != ReadAloudGlyph::Preparing) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c, 1.4 * s, Qt::SolidLine, Qt::RoundCap));
        // Two waves; Qt angles are in 1/16 degree, centred on 3 o'clock.
        p.drawArc(QRectF(7 * s, 5 * s, 5 * s, 6 * s), -55 * 16, 110 * 16);
        p.drawArc(QRectF(6.5 * s, 2.5 * s, 8.5 * s, 11 * s), -55 * 16, 110 * 16);
    }
    if (g == ReadAloudGlyph::Mute) {
        // The strike: first cut a transparent channel through the speaker so
        // the line stands apart from it on any background, then draw it.
        const QPointF a(2 * s, 1.5 * s), b(14.5 * s, 14.5 * s);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.setPen(QPen(Qt::transparent, 4.4 * s, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(c, 1.7 * s, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
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
        tooltip_ += QStringLiteral("\nRight-click to choose the voice");
        btn_->setToolTip(tooltip_);
        auto& tts = services::TtsService::instance();
        btn_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn_, &QPushButton::customContextMenuRequested, this,
                [this](const QPoint& pos) { show_voice_menu(btn_->mapToGlobal(pos)); });
        tts.refresh_engine_voices(); // so the first right-click has the list
        connect(&tts, &services::TtsService::session_state, this, [this](int session, bool speaking) {
            if (!active_ || session != session_)
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
        connect(&tts, &services::TtsService::session_error, this, [this](int session, const QString& msg) {
            // A synchronous launch failure arrives while launch() is still on
            // the stack, before this toggle knows the new id: keep the reason.
            if (launching_) {
                launch_error_ = msg;
                return;
            }
            if (!active_ || session != session_)
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
        last_launch_ = launch;
        start(launch);
    }

    /// Stop playback if — and only if — this toggle owns it. Idempotent.
    void stop() {
        if (!active_)
            return;
        active_ = false; // first: TtsService::stop() emits state_changed(false)
        services::TtsService::instance().stop();
        show(State::Idle);
    }

    bool active() const { return active_; }

  private:
    void start(const std::function<bool()>& launch) {
        launching_ = true;
        launch_error_.clear();
        const bool ok = launch();
        launching_ = false;
        if (ok) {
            active_ = true;
            session_ = services::TtsService::instance().session();
            show(State::Prep);
            btn_->setToolTip(tooltip_);
            return;
        }
        // Refused start. Say why — the service's own reason when it gave one
        // (Piper failed to start, voice file vanished, read-aloud switched off).
        btn_->setToolTip(launch_error_.isEmpty() ? QStringLiteral("No voice available — set one up in Settings → Voice")
                                                 : QStringLiteral("Could not start: %1").arg(launch_error_));
        if (labels_.icons)
            return; // no room for words on an icon; the tooltip carries them
        btn_->setText(QStringLiteral("NO VOICE"));
        QTimer::singleShot(1500, this, [this]() {
            if (!active_)
                show(State::Idle);
        });
    }

    void show_voice_menu(const QPoint& global_pos) {
        auto& tts = services::TtsService::instance();
        tts.refresh_engine_voices(); // reuses a fresh list; else updates for next time
        const auto voices = tts.engine_voices();
        const QString current = tts.engine_voice_key();
        // On the heap and non-blocking (popup, not exec): the panel can delete
        // the button while the menu is open, and a stack QMenu parented to it
        // would then be deleted twice, with this toggle gone on return.
        auto* menu = new QMenu(btn_);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        // A disabled row, not addSection(): the app style draws section titles
        // as a bare separator, which would leave the menu unlabelled.
        menu->addAction(QStringLiteral("READ-ALOUD VOICE"))->setEnabled(false);
        menu->addSeparator();
        if (voices.isEmpty()) {
            menu->addAction(current.isEmpty() ? QStringLiteral("No engine voice — using the on-device voice")
                            : tts.engine_voices_unlisted()
                                ? QStringLiteral("This engine can't list voices — update hearth")
                                : QStringLiteral("Loading voices…"))
                ->setEnabled(false);
        }
        for (const auto& v : voices) {
            QAction* a = menu->addAction(v.label);
            a->setCheckable(true);
            a->setChecked(v.key() == current);
            const QString key = v.key();
            connect(a, &QAction::triggered, this, [this, key, current]() { pick_voice(key, current); });
        }
        menu->popup(global_pos);
    }

    void pick_voice(const QString& key, const QString& previous) {
        if (key == previous)
            return;
        const bool was_reading = active_;
        stop();
        services::TtsService::instance().set_engine_voice(key);
        // Hear the choice: what was being read starts again in the new voice;
        // otherwise a short line.
        if (was_reading && last_launch_) {
            start(last_launch_);
            return;
        }
        start([]() {
            return services::TtsService::instance().speak_via_engine(
                QStringLiteral("Hi. This is the voice that will read your news."));
        });
    }

    enum class State { Idle, Prep, Stop };
    void show(State st) {
        state_ = st;
        if (labels_.icons) {
            btn_->setText({});
            btn_->setIcon(read_aloud_icon(st == State::Idle   ? ReadAloudGlyph::Speaker
                                          : st == State::Prep ? ReadAloudGlyph::Preparing
                                                              : ReadAloudGlyph::Mute));
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
    int session_ = -1;       ///< TtsService::session() of this toggle's reading
    bool launching_ = false; ///< inside launch(): errors belong to the launch
    QString launch_error_;
    std::function<bool()> last_launch_; ///< the last click's reading, re-run on a voice change
    State state_ = State::Idle;
};

} // namespace fincept::ui

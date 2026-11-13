#pragma once
// ReadAloudToggle.h — one QPushButton bound to one read-aloud session on the
// shared TtsService.
//
// TtsService is a singleton with ONE utterance at a time, and more than one
// button can start it (the news article LISTEN and the TL;DR speaker). Each
// needs the same protocol, which used to be hand-copied per button:
//   - a media control, not an on/off switch: idle → PREP… (synthesis
//     launched) → PAUSE (audio playing); a click there PAUSES and the next
//     RESUMES from the same word — the old click-to-stop threw the place away
//     and the next click started over. A ■ stop button sits beside it, enabled
//     only while a reading exists (playing, paused or preparing); after a
//     stop, or once the reading ends, the next click starts from the top.
//     Nothing moves under the cursor: the stop button is always laid out
//     (greyed when idle) and the main button is as wide as its widest label —
//     a stop button that appeared on start shifted PAUSE away from the
//     pointer, so the very next click missed it;
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
//     says a short line in it so the choice can be heard. While a reading
//     exists the menu also offers "Restart from the beginning" and "Stop".
//
// ChatPane's 🔊 deliberately does NOT use this: it is part of hands-free voice
// mode, which auto-speaks replies, resumes the mic when playback ends, and
// mirrors ANY playback on the service — a different contract from a button
// that owns its own reading.

#include "services/tts/TtsService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QBoxLayout>
#include <QEvent>
#include <QFontMetrics>
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

#include <algorithm>
#include <functional>

namespace finterm::ui {

/// The read-aloud glyphs, drawn rather than shipped as assets so they take the
/// current theme accent: a speaker with sound waves (start reading), the same
/// speaker dimmed and silent (preparing), pause bars (playing — click to
/// pause), a play triangle (paused — click to resume) and a square (stop).
enum class ReadAloudGlyph { Speaker, Preparing, Pause, Play, Stop };
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
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    switch (g) {
    case ReadAloudGlyph::Pause:
        p.drawRoundedRect(QRectF(3.5 * s, 2.5 * s, 3.2 * s, 11 * s), 0.8 * s, 0.8 * s);
        p.drawRoundedRect(QRectF(9.3 * s, 2.5 * s, 3.2 * s, 11 * s), 0.8 * s, 0.8 * s);
        return QIcon(pm);
    case ReadAloudGlyph::Play: {
        QPainterPath tri;
        tri.moveTo(4 * s, 2.5 * s);
        tri.lineTo(13.5 * s, 8 * s);
        tri.lineTo(4 * s, 13.5 * s);
        tri.closeSubpath();
        p.drawPath(tri);
        return QIcon(pm);
    }
    case ReadAloudGlyph::Stop:
        p.drawRoundedRect(QRectF(3.5 * s, 3.5 * s, 9 * s, 9 * s), 1 * s, 1 * s);
        return QIcon(pm);
    case ReadAloudGlyph::Speaker:
    case ReadAloudGlyph::Preparing:
        break;
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
    /// then serving as the buttons' accessible names per state.
    struct Labels {
        QString idle;   ///< start reading
        QString pause;  ///< playing: click to pause
        QString resume; ///< paused: click to carry on
        QString stop;   ///< the companion stop button
        QString prep = QStringLiteral("PREP…");
        bool icons = false;
    };

    /// Owned by `button` (parented to it), so it dies with the button. Puts a
    /// stop button right after `button` in whatever layout holds it, styled
    /// like it, visible whenever `button` is and enabled while a reading exists.
    ReadAloudToggle(QPushButton* button, Labels labels, QString tooltip)
        : QObject(button), btn_(button), labels_(std::move(labels)), tooltip_(std::move(tooltip)) {
        tooltip_ += QStringLiteral("\nRight-click to choose the voice");
        make_stop_button();
        show(State::Idle);
        btn_->setToolTip(tooltip_);
        btn_->installEventFilter(this); // the stop button follows its visibility
        auto& tts = services::TtsService::instance();
        btn_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn_, &QPushButton::customContextMenuRequested, this,
                [this](const QPoint& pos) { show_menu(btn_->mapToGlobal(pos)); });
        tts.refresh_engine_voices(); // so the first right-click has the list
        connect(&tts, &services::TtsService::session_state, this, [this](int session, bool speaking) {
            if (!active_ || session != session_)
                return;
            if (speaking) {
                speaking_ = true;
                if (state_ != State::Paused)
                    show(State::Playing);
            } else {
                active_ = false;
                show(State::Idle);
            }
        });
        // Paused or resumed from anywhere (another control, a future shortcut):
        // the button shows what a click will do next.
        connect(&tts, &services::TtsService::session_paused, this, [this](int session, bool paused) {
            if (!active_ || session != session_)
                return;
            show(paused ? State::Paused : speaking_ ? State::Playing : State::Prep);
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

    ~ReadAloudToggle() override {
        // The stop button lives in the button's parent, not under this object.
        if (stop_btn_)
            stop_btn_->deleteLater();
    }

    /// The click handler: start → pause → resume → pause… Starts a reading
    /// (calling `launch`, which returns TtsService::speak*()'s result) only
    /// when this toggle owns none; otherwise pauses or resumes the one it has.
    void toggle(const std::function<bool()>& launch) {
        if (active_) {
            // The service's state decides, not this button's picture of it;
            // and a call that changes nothing still repaints, so the button
            // can never stick on a word that no longer applies.
            auto& tts = services::TtsService::instance();
            const bool changed = tts.is_paused() ? tts.resume() : tts.pause();
            if (!changed)
                show(tts.is_paused() ? State::Paused : speaking_ ? State::Playing : State::Prep);
            return; // otherwise session_paused() repaints
        }
        last_launch_ = launch;
        start(launch);
    }

    /// Stop playback if — and only if — this toggle owns it. Idempotent. The
    /// next click starts from the top.
    void stop() {
        if (!active_)
            return;
        active_ = false; // first: TtsService::stop() emits state_changed(false)
        services::TtsService::instance().stop();
        show(State::Idle);
    }

    bool active() const { return active_; }

  protected:
    bool eventFilter(QObject* watched, QEvent* e) override {
        // *ToParent: sent on every setVisible(), even while an ancestor is
        // hidden (plain Show/Hide aren't, which stranded a lone ■).
        if (watched == btn_ && (e->type() == QEvent::ShowToParent || e->type() == QEvent::HideToParent))
            sync_stop_button();
        return QObject::eventFilter(watched, e);
    }

  private:
    /// The layout that holds `w` (searched through nested layouts) and its index.
    static QBoxLayout* layout_holding(QLayout* root, QWidget* w, int* index) {
        if (!root)
            return nullptr;
        for (int i = 0; i < root->count(); ++i) {
            QLayoutItem* it = root->itemAt(i);
            if (it->widget() == w) {
                *index = i;
                return qobject_cast<QBoxLayout*>(root);
            }
            if (QBoxLayout* found = layout_holding(it->layout(), w, index))
                return found;
        }
        return nullptr;
    }

    void make_stop_button() {
        QWidget* parent = btn_->parentWidget();
        int index = -1;
        QBoxLayout* lay = parent ? layout_holding(parent->layout(), btn_, &index) : nullptr;
        if (!lay)
            return; // no layout to sit in: Stop stays in the right-click menu
        stop_btn_ = new QPushButton(parent);
        stop_btn_->setObjectName(btn_->objectName()); // same QSS look
        stop_btn_->setCursor(Qt::PointingHandCursor);
        stop_btn_->setFocusPolicy(btn_->focusPolicy());
        if (labels_.icons) {
            stop_btn_->setFixedSize(btn_->minimumSize().isValid() && btn_->minimumWidth() > 0 ? btn_->minimumSize()
                                                                                                : btn_->sizeHint());
            stop_btn_->setIconSize(btn_->iconSize());
            stop_btn_->setIcon(read_aloud_icon(ReadAloudGlyph::Stop));
            stop_btn_->setAccessibleName(labels_.stop);
        } else {
            stop_btn_->setText(labels_.stop);
            if (btn_->maximumHeight() < QWIDGETSIZE_MAX)
                stop_btn_->setFixedHeight(btn_->maximumHeight());
        }
        stop_btn_->setToolTip(QStringLiteral("Stop reading — the next play starts from the beginning"));
        stop_btn_->setEnabled(false);
        stop_btn_->setVisible(!btn_->isHidden());
        lay->insertWidget(index + 1, stop_btn_);
        if (!labels_.icons) {
            // As wide as the widest state word, so PAUSE → RESUME doesn't
            // resize the button (and shove its neighbours) mid-click.
            btn_->ensurePolished(); // measure in the QSS font, not the default one
            const QFontMetrics fm(btn_->font());
            int w = 0;
            for (const QString& t : {labels_.idle, labels_.pause, labels_.resume, labels_.prep,
                                     QStringLiteral("NO VOICE")})
                w = std::max(w, fm.horizontalAdvance(t));
            btn_->setMinimumWidth(std::max(btn_->minimumWidth(), w + 24));
        }
        connect(stop_btn_, &QPushButton::clicked, this, [this]() { stop(); });
    }

    void sync_stop_button() {
        if (!stop_btn_)
            return;
        stop_btn_->setVisible(!btn_->isHidden());
        stop_btn_->setEnabled(active_);
    }

    void start(const std::function<bool()>& launch) {
        launching_ = true;
        launch_error_.clear();
        const bool ok = launch();
        launching_ = false;
        if (ok) {
            active_ = true;
            speaking_ = false;
            session_ = services::TtsService::instance().session();
            show(State::Prep);
            return;
        }
        // Refused start. Say why — the service's own reason when it gave one
        // (Piper failed to start, voice file vanished, read-aloud switched off).
        show(State::Idle);
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

    void show_menu(const QPoint& global_pos) {
        auto& tts = services::TtsService::instance();
        tts.refresh_engine_voices(); // reuses a fresh list; else updates for next time
        const auto voices = tts.engine_voices();
        const QString current = tts.engine_voice_key();
        // On the heap and non-blocking (popup, not exec): the panel can delete
        // the button while the menu is open, and a stack QMenu parented to it
        // would then be deleted twice, with this toggle gone on return.
        auto* menu = new QMenu(btn_);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        if (active_) {
            // The deliberate ways out of "resume": start over, or stop.
            connect(menu->addAction(QStringLiteral("Restart from the beginning")), &QAction::triggered, this,
                    [this]() { restart(); });
            connect(menu->addAction(QStringLiteral("Stop")), &QAction::triggered, this, [this]() { stop(); });
            menu->addSeparator();
        }
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

    void restart() {
        if (!last_launch_)
            return;
        stop();
        start(last_launch_);
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
        // The preview becomes "what this button reads", so Restart replays it.
        last_launch_ = []() {
            return services::TtsService::instance().speak_via_engine(
                QStringLiteral("Hi. This is the voice that will read your news."));
        };
        start(last_launch_);
    }

    enum class State { Idle, Prep, Playing, Paused };
    void show(State st) {
        state_ = st;
        sync_stop_button();
        const QString& name = st == State::Idle      ? labels_.idle
                              : st == State::Prep    ? labels_.prep
                              : st == State::Playing ? labels_.pause
                                                     : labels_.resume;
        // What a click does NOW. Idle keeps whatever start() put there (the
        // normal tooltip, or the last failure's reason).
        if (st == State::Prep)
            btn_->setToolTip(QStringLiteral("Preparing the voice — click to pause, ■ to stop"));
        else if (st == State::Playing)
            btn_->setToolTip(QStringLiteral("Pause — click again to resume from here\nRight-click to restart or "
                                            "choose the voice"));
        else if (st == State::Paused)
            btn_->setToolTip(QStringLiteral("Resume from where it paused\nRight-click to restart or choose the voice"));
        else if (state_tooltip_owned_)
            btn_->setToolTip(tooltip_);
        state_tooltip_owned_ = st != State::Idle;
        if (labels_.icons) {
            btn_->setText({});
            btn_->setIcon(read_aloud_icon(st == State::Idle      ? ReadAloudGlyph::Speaker
                                          : st == State::Prep    ? ReadAloudGlyph::Preparing
                                          : st == State::Playing ? ReadAloudGlyph::Pause
                                                                 : ReadAloudGlyph::Play));
            // The state word goes to the accessible name, which an icon lacks.
            btn_->setAccessibleName(name);
            return;
        }
        btn_->setText(name);
    }

    QPushButton* btn_;
    QPointer<QPushButton> stop_btn_; ///< beside btn_, visible while a reading exists
    Labels labels_;
    QString tooltip_;
    bool active_ = false;
    bool speaking_ = false;  ///< this reading's audio has started (resume → Playing, not Prep)
    int session_ = -1;       ///< TtsService::session() of this toggle's reading
    bool launching_ = false; ///< inside launch(): errors belong to the launch
    QString launch_error_;
    std::function<bool()> last_launch_; ///< the last click's reading, re-run on restart / a voice change
    State state_ = State::Idle;
    bool state_tooltip_owned_ = false; ///< btn_'s tooltip is a state hint, to restore on idle
};

} // namespace finterm::ui

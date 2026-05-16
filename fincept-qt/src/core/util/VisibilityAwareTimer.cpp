#include "core/util/VisibilityAwareTimer.h"

#include <QEvent>
#include <QWidget>

namespace fincept::util {

VisibilityAwareTimer::VisibilityAwareTimer(QWidget* watched)
    : QObject(watched), watched_(watched) {
    // Forward the QTimer's timeout to our public signal. No queued
    // connection needed — both objects live on the watched widget's
    // (typically the GUI) thread.
    QObject::connect(&timer_, &QTimer::timeout, this, &VisibilityAwareTimer::timeout);
    if (watched_)
        watched_->installEventFilter(this);
}

void VisibilityAwareTimer::setInterval(int msec) {
    timer_.setInterval(msec);
}

void VisibilityAwareTimer::setSingleShot(bool single) {
    timer_.setSingleShot(single);
}

void VisibilityAwareTimer::start() {
    wanted_active_ = true;
    // Only start ticking if the widget is actually visible. isVisible()
    // returns false when any ancestor is hidden — covers dock-tab switches,
    // hidden screens, and minimised top-level windows — so we don't need a
    // separate window-state check.
    if (watched_ && watched_->isVisible())
        timer_.start();
}

void VisibilityAwareTimer::start(int msec) {
    timer_.setInterval(msec);
    start();
}

void VisibilityAwareTimer::stop() {
    wanted_active_ = false;
    timer_.stop();
}

bool VisibilityAwareTimer::eventFilter(QObject* obj, QEvent* event) {
    if (obj == watched_) {
        const auto t = event->type();
        if (t == QEvent::Hide) {
            // Pause but preserve wanted_active_ so the next Show re-arms.
            timer_.stop();
        } else if (t == QEvent::Show) {
            if (wanted_active_ && !timer_.isActive())
                timer_.start();
        }
    }
    return QObject::eventFilter(obj, event);
}

} // namespace fincept::util

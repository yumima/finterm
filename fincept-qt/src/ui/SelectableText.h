#pragma once
// SelectableText.h — make displayed text selectable and copyable app-wide.
//
// Qt labels are read-only AND unselectable by default, so everything finterm
// renders through a QLabel — the news TL;DR, article bodies, AI analysis,
// figures in panels — could be read but not copied. For a terminal whose whole
// job is producing text a user acts on, that is the wrong default.
//
// Doing this per widget is not viable: there are ~2500 QLabel instances and any
// new one would default back to unselectable, so the fix would rot immediately.
// A single application-level event filter catches every label — existing and
// future — at Polish time, which is after construction and before first paint.
//
// What is deliberately skipped:
//   * Labels that already declare an interaction mode. Some are rich-text with
//     links (LinksAccessibleByMouse) and stealing their mouse handling would
//     break the link.
//   * Labels wearing a PointingHandCursor. That is finterm's convention for a
//     label acting as a button; TextSelectableByMouse consumes the press, so a
//     drag-select would come at the cost of the click.
// Both keep the change additive: nothing that already worked stops working.

#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QObject>

namespace fincept::ui {

class SelectableTextFilter : public QObject {
  public:
    explicit SelectableTextFilter(QObject* parent = nullptr) : QObject(parent) {}

    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() != QEvent::Polish)
            return QObject::eventFilter(obj, ev);
        auto* label = qobject_cast<QLabel*>(obj);
        if (!label)
            return QObject::eventFilter(obj, ev);

        // Already configured by its owner — leave it alone.
        if (label->textInteractionFlags() != Qt::NoTextInteraction)
            return QObject::eventFilter(obj, ev);
        // Click-as-button convention: keep the click.
        if (label->cursor().shape() == Qt::PointingHandCursor)
            return QObject::eventFilter(obj, ev);

        label->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                       Qt::TextSelectableByKeyboard);
        return QObject::eventFilter(obj, ev);
    }
};

/// Install on the QApplication once, before the main window is built.
inline void install_selectable_text(QApplication& app) {
    app.installEventFilter(new SelectableTextFilter(&app));
}

} // namespace fincept::ui

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
//   * Labels already made selectable by their owner — nothing to add.
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

        // Click-as-button convention: keep the click.
        if (label->cursor().shape() == Qt::PointingHandCursor)
            return QObject::eventFilter(obj, ev);

        const Qt::TextInteractionFlags have = label->textInteractionFlags();
        // Nothing to do if the owner already made it selectable.
        if (have & Qt::TextSelectableByMouse)
            return QObject::eventFilter(obj, ev);

        // ADD the selection flags rather than testing for "unconfigured" and
        // replacing them. A QLabel's default is Qt::LinksAccessibleByMouse, NOT
        // NoTextInteraction — an earlier version of this filter treated any
        // non-NoTextInteraction value as "the owner configured this" and so
        // skipped every label in the app, doing nothing at all.
        //
        // OR-ing also preserves whatever the owner did set: a rich-text label
        // with links keeps LinksAccessibleByMouse and gains selection, instead
        // of trading one for the other.
        label->setTextInteractionFlags(have | Qt::TextSelectableByMouse |
                                       Qt::TextSelectableByKeyboard);
        return QObject::eventFilter(obj, ev);
    }
};

/// Install on the QApplication once, before the main window is built.
inline void install_selectable_text(QApplication& app) {
    app.installEventFilter(new SelectableTextFilter(&app));
}

} // namespace fincept::ui

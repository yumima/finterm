#pragma once
// RememberedSplitter.h — a splitter that keeps the sizes the user dragged it
// to, across sessions. Call once, after the splitter's children are added.
//
// Saved in QSettings under "splitters/<key>" when the handle is released (not
// on every pixel of the drag). A saved state that no longer fits — a child
// was added or removed since — is ignored by QSplitter::restoreState, and the
// splitter keeps its built-in proportions.

#include <QSettings>
#include <QSplitter>
#include <QString>
#include <QTimer>

namespace finterm::ui {

inline void remember_splitter(QSplitter* split, const QString& key) {
    if (!split || key.isEmpty())
        return;
    const QString setting = QStringLiteral("splitters/") + key;
    const QByteArray saved = QSettings().value(setting).toByteArray();
    if (!saved.isEmpty())
        split->restoreState(saved);
    auto* save = new QTimer(split);
    save->setSingleShot(true);
    save->setInterval(400); // splitterMoved fires per pixel of a drag
    QObject::connect(save, &QTimer::timeout, split,
                     [split, setting]() { QSettings().setValue(setting, split->saveState()); });
    QObject::connect(split, &QSplitter::splitterMoved, save, [save](int, int) { save->start(); });
}

} // namespace finterm::ui

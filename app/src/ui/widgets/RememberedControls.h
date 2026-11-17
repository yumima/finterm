#pragma once
// RememberedControls.h — filter controls that keep the user's last choice
// across sessions, in QSettings under "controls/<key>".
//
// Call right after building the control and BEFORE the panel's first load:
// the saved choice is put back with signals blocked, so it does not trigger a
// load of its own — the panel's first load simply reads it. A saved choice the
// control no longer offers (an option was removed) is ignored.

#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QString>

namespace finterm::ui {

namespace remembered_detail {
inline QString setting(const QString& key) {
    return QStringLiteral("controls/") + key;
}
} // namespace remembered_detail

/// A combo box, remembered by its item's data (not its position, so adding an
/// option later does not shift every saved choice onto its neighbour).
inline void remember_choice(QComboBox* box, const QString& key) {
    if (!box || key.isEmpty())
        return;
    const QString s = remembered_detail::setting(key);
    const QVariant saved = QSettings().value(s);
    if (saved.isValid()) {
        for (int i = 0; i < box->count(); ++i)
            if (box->itemData(i).toString() == saved.toString()) {
                QSignalBlocker block(box);
                box->setCurrentIndex(i);
                break;
            }
    }
    QObject::connect(box, &QComboBox::currentIndexChanged, box,
                     [box, s](int) { QSettings().setValue(s, box->currentData().toString()); });
}

inline void remember_choice(QSpinBox* spin, const QString& key) {
    if (!spin || key.isEmpty())
        return;
    const QString s = remembered_detail::setting(key);
    bool ok = false;
    const int v = QSettings().value(s).toInt(&ok);
    if (ok && v >= spin->minimum() && v <= spin->maximum()) {
        QSignalBlocker block(spin);
        spin->setValue(v);
    }
    QObject::connect(spin, &QSpinBox::valueChanged, spin, [s](int value) { QSettings().setValue(s, value); });
}

inline void remember_choice(QCheckBox* check, const QString& key) {
    if (!check || key.isEmpty())
        return;
    const QString s = remembered_detail::setting(key);
    const QVariant saved = QSettings().value(s);
    if (saved.isValid()) {
        QSignalBlocker block(check);
        check->setChecked(saved.toBool());
    }
    QObject::connect(check, &QCheckBox::toggled, check, [s](bool on) { QSettings().setValue(s, on); });
}

} // namespace finterm::ui

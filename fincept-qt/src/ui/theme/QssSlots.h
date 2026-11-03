#pragma once
// QssSlots.h — fill a stylesheet template's %N slots by number.
//
// A positional .arg() chain fills the LOWEST placeholder still in the string,
// so a slot the template doesn't use shifts every later value one place down
// with nothing but a runtime "Argument missing" warning: the global QSS once
// coloured #positiveText with icon_dim and #warningText with the positive
// green that way. Here slot N always takes value N, inserted values are never
// rescanned (a value may contain `%23` etc.), and a slot without a value or a
// value without a slot is reported instead of silently shifting.

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace fincept::ui {

struct QssSlotFill {
    QString text;
    QList<int> missing; ///< slot numbers in the template with no value (left as written)
    QList<int> unused;  ///< value numbers (1-based) no slot refers to

    bool exact() const { return missing.isEmpty() && unused.isEmpty(); }
};

/// `%` followed by ASCII 1–9 starts a slot; a second ASCII digit always
/// belongs to it (%1..%99). `%0…`, `%` before anything else, and text inside
/// inserted values are left alone.
inline QssSlotFill fill_qss_slots(const QString& tmpl, const QStringList& values) {
    auto ascii_digit = [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9') ? c.unicode() - '0' : -1; };
    QssSlotFill r;
    r.text.reserve(tmpl.size() + values.size() * 8);
    QList<bool> used(values.size(), false);
    const qsizetype n_chars = tmpl.size();
    qsizetype run = 0; // start of the literal text not yet copied
    for (qsizetype i = 0; i + 1 < n_chars; ++i) {
        if (tmpl.at(i) != QLatin1Char('%'))
            continue;
        const int d1 = ascii_digit(tmpl.at(i + 1));
        if (d1 < 1)
            continue;
        int slot = d1;
        qsizetype len = 2; // '%' + one digit
        if (i + 2 < n_chars) {
            const int d2 = ascii_digit(tmpl.at(i + 2));
            if (d2 >= 0) {
                slot = d1 * 10 + d2;
                len = 3;
            }
        }
        if (slot > values.size()) {
            if (!r.missing.contains(slot))
                r.missing.append(slot);
        } else {
            r.text += QStringView(tmpl).mid(run, i - run);
            r.text += values.at(slot - 1);
            used[slot - 1] = true;
            run = i + len;
        }
        i += len - 1;
    }
    r.text += QStringView(tmpl).mid(run);
    for (int k = 0; k < used.size(); ++k)
        if (!used[k])
            r.unused.append(k + 1);
    return r;
}

/// Warns (and asserts in debug builds) when the template and values don't
/// match one-to-one. `what` names the stylesheet in the message.
inline void report_qss_fill(const char* what, const QssSlotFill& fill) {
    if (fill.exact())
        return;
    QStringList m, u;
    for (int n : fill.missing)
        m << QString::number(n);
    for (int n : fill.unused)
        u << QString::number(n);
    qWarning("QSS template %s: slots without a value [%s], values without a slot [%s]", what, qPrintable(m.join(',')),
             qPrintable(u.join(',')));
    Q_ASSERT_X(false, what, "QSS template slots and values don't match");
}

inline QString fill_qss_checked(const char* what, const QString& tmpl, const QStringList& values) {
    QssSlotFill fill = fill_qss_slots(tmpl, values);
    report_qss_fill(what, fill);
    return std::move(fill.text);
}

} // namespace fincept::ui

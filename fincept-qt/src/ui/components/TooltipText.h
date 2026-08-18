#pragma once
#include <QString>
#include <QStringList>

namespace fincept::ui {

/// Wrap tooltip prose to a readable column.
///
/// Qt lays a plain-text tooltip out on one line per paragraph, so a sentence
/// explaining a finding renders as a single strip most of the screen wide —
/// which is the one shape prose cannot be read in. Rich text would wrap, but
/// only at a width Qt picks, and these tooltips carry numbers and rule text
/// that must not be re-flowed differently on every machine.
///
/// So the break points are chosen here: hard-wrapped at a fixed column, on word
/// boundaries, with existing newlines preserved as paragraph breaks. The result
/// is a block roughly as wide as a column of body text and as tall as it needs
/// to be.
inline QString tooltip_wrap(const QString& text, int cols = 62) {
    QStringList out;
    // Existing newlines are the author's paragraph breaks and are kept; only
    // the runs between them are re-flowed.
    for (const QString& para : text.split(QLatin1Char('\n'))) {
        if (para.isEmpty()) {
            out << QString();
            continue;
        }
        QString line;
        for (const QString& word : para.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (!line.isEmpty() && line.size() + 1 + word.size() > cols) {
                out << line;
                line.clear();
            }
            line += line.isEmpty() ? word : QLatin1Char(' ') + word;
        }
        if (!line.isEmpty())
            out << line;
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace fincept::ui

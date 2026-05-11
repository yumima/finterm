// src/ui/components/LayoutHelpers.cpp
#include "ui/components/LayoutHelpers.h"

#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLatin1String>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTimer>
#include <algorithm>

namespace fincept::ui {

void equalize_and_distribute(const QList<QPushButton*>& peers,
                             const QFont&               font,
                             int                        hpad) {
    if (peers.isEmpty()) return;

    const QFontMetrics fm(font);
    int longest = 0;
    for (QPushButton* b : peers) {
        if (!b) continue;
        longest = std::max(longest, fm.horizontalAdvance(b->text()));
    }
    const int min_w = longest + hpad;

    for (QPushButton* b : peers) {
        if (!b) continue;
        b->setMinimumWidth(min_w);
        // Preserve the existing vertical policy — pills are typically Fixed
        // height. We only override horizontal to Expanding so peers grow
        // together to fill the available row.
        QSizePolicy sp = b->sizePolicy();
        sp.setHorizontalPolicy(QSizePolicy::Expanding);
        b->setSizePolicy(sp);
    }
}

void ensure_header_fits(QTableWidget* table, int hpad) {
    if (!table) return;

    // Defer to the next event-loop iteration so this runs AFTER the caller's
    // own per-column `resizeSection` / `setSectionResizeMode` calls in the
    // same construction block. Otherwise a caller's `resizeSection(3, 70)`
    // after this helper would overwrite our widening for a narrow Fixed
    // column whose header doesn't fit. QPointer guards against the table
    // being destroyed before the timer fires.
    QPointer<QTableWidget> guard(table);
    QTimer::singleShot(0, table, [guard, hpad]() {
        if (!guard) return;
        QHeaderView* h = guard->horizontalHeader();
        if (!h) return;

        QFont hdr_font = h->font();
        hdr_font.setBold(true);
        const QFontMetrics fm(hdr_font);

        // Per-column: bump the current section width up to fit the column's
        // OWN header text. Each column is sized to its own header — narrow
        // columns (`#`) stay narrow, long ones (`PORTFOLIO (EST)`) get the
        // room they need. `setMinimumSectionSize` is a global minimum across
        // the table, so we deliberately don't touch it (would force the `#`
        // column to the widest header's width — the anti-pattern we avoid).
        //
        // Stretch columns: `resizeSection` is a no-op (the header dictates
        // the width from available space), so skip them. The Stretch column
        // is almost always the widest content column (name/asset), so the
        // table's own width carries the header.
        const int cols = guard->columnCount();
        for (int c = 0; c < cols; ++c) {
            if (h->sectionResizeMode(c) == QHeaderView::Stretch) continue;
            QTableWidgetItem* item = guard->horizontalHeaderItem(c);
            const QString text = item ? item->text() : QString();
            if (text.isEmpty()) continue;
            const int needed = fm.horizontalAdvance(text) + hpad;
            if (h->sectionSize(c) < needed)
                h->resizeSection(c, needed);
        }
    });
}

} // namespace fincept::ui

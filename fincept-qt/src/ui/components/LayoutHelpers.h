// src/ui/components/LayoutHelpers.h
#pragma once

#include <QList>

class QFont;
class QPushButton;
class QTableWidget;
class QWidget;

namespace fincept::ui {

/// Make a row of peer widgets (pills, tabs, chips) all the same width and
/// distribute them across the available horizontal space:
///
///   * Computes the longest label width with the supplied font's metrics,
///     reading each button's CURRENT text() (call after the buttons have been
///     constructed with their final labels).
///   * Sets `setMinimumWidth(longest + hpad)` on every peer so no label can be
///     truncated below its natural text width.
///   * Sets size policy to `(Expanding, Fixed-height-preserved)` so the
///     widgets grow together to fill the row.
///
/// Use for: pill filters in panels (RankingsPanel), tab-button rows, chip
/// strips where every item is a labeled peer. Do NOT use for table column
/// headers (use ensure_header_fits) or heterogeneous form rows.
void equalize_and_distribute(const QList<QPushButton*>& peers,
                             const QFont&               font,
                             int                        hpad = 24);

/// Ensure every visible column header on a QTableWidget has a width at least
/// as wide as its header text + padding.
///
/// Per-column behavior, applied after the next event-loop tick so the caller's
/// own `resizeSection`/`setSectionResizeMode` calls in the same construction
/// block have settled first:
///   * Interactive / Fixed columns: bump `sectionSize` up if currently smaller
///     than the header's natural width. Caller's chosen width is preserved
///     when already adequate.
///   * Stretch columns: leave the section width alone (it's dictated by the
///     header), and only raise the column's min via `setMinimumSectionSize`
///     IF the header requires it — but we deliberately avoid touching the
///     GLOBAL min here, so a Stretch column whose header truncates relies on
///     the table's overall width being sufficient. Most stretch columns hold
///     the widest content (name / asset), so this is rarely an issue.
///
/// Call after `setHorizontalHeaderLabels()`. Safe to call before or after the
/// per-column `resizeSection`/`setSectionResizeMode` calls thanks to the
/// deferred execution.
void ensure_header_fits(QTableWidget* table, int hpad = 18);

} // namespace fincept::ui

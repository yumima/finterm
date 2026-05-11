// src/ui/components/SectionHeader.h
#pragma once

class QLabel;
class QString;
class QWidget;

namespace fincept::ui {

/// Canonical section-header bar used at the top of each panel's content
/// region — e.g. "RANKINGS", "LEADERBOARD", "COMMITTEE INTELLIGENCE". Returns
/// a QLabel with a unified style so any panel divider looks the same:
///
///   * background: BG_RAISED
///   * color:      TEXT_SECONDARY
///   * font:       12px, weight 700, letter-spacing 1.5px
///   * padding:    6px 10px
///   * bottom rule:1px BORDER_MED
///
/// Height is computed via QFontMetrics off an explicit 12px font (NOT
/// QLabel::font(), which doesn't reflect the stylesheet until painted) and
/// pinned via setFixedHeight, so the bar lines up across panels regardless
/// of system DPI or the parent's default font size.
///
/// Use for: top-of-panel section dividers, splitter-pane sub-section
/// dividers (e.g. "COMMITTEES" inside CommitteePanel's left pane). Do NOT
/// use for table headers (those are styled via QHeaderView), pill-bar
/// labels, card titles inside info panes, or welcome banners.
QLabel* make_section_header(const QString& text, QWidget* parent = nullptr);

/// Return the same style as a QSS string, for callers that already own a
/// QLabel and want to re-style it (e.g. on theme refresh).
QString section_header_qss();

} // namespace fincept::ui

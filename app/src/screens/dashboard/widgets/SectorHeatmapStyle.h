#pragma once
// SectorHeatmapStyle.h — tile fill and label colours for the sector heatmap.
// Header-only so the contrast guarantee is testable without the widget
// (tests/ui/test_palette_contrast.cpp).

#include "ui/theme/Contrast.h"
#include "ui/theme/Theme.h"

#include <QColor>
#include <QString>

#include <algorithm>
#include <cmath>

namespace finterm::screens::widgets::heatmap {

/// Tile fill: the sign colour at an alpha that grows with |move|, as #AARRGGBB.
inline QString cell_bg(double change_pct) {
    // Unknown change (NaN, no previous close) → neutral, untinted cell.
    const bool known = std::isfinite(change_pct);
    const int intensity = known ? static_cast<int>(std::min(std::abs(change_pct) * 60.0, 200.0)) : 0;
    QColor tint(!known              ? ui::colors::TEXT_TERTIARY()
                : change_pct >= 0 ? ui::colors::POSITIVE()
                                  : ui::colors::NEGATIVE());
    tint.setAlpha(40 + intensity);
    return tint.name(QColor::HexArgb);
}

struct LabelColors {
    QString name;
    QString chg;
};

/// Label colours chosen against the fill the eye actually sees: the tint
/// composited over the widget surface. At a large move the tint reaches ~94%
/// opacity of the same red/green the change text uses, so colouring the text
/// by sign alone drew red on red. The sector name needs 4.5:1. The change keeps
/// its sign colour while that clears 3:1 (the floor the palette itself holds
/// RED/GREEN to — tests/ui/test_palette_contrast.cpp; even untinted, RED is
/// only ~3.7:1 on the surface), otherwise it falls back to the theme's primary
/// text, then black or white. A function of (bg, sign) only, so callers may
/// cache it with bg.
inline LabelColors label_colors(const QString& bg, double change_pct) {
    const QColor fill = ui::contrast::over(QColor(bg), QColor(ui::colors::BG_SURFACE()));
    const QColor primary(ui::colors::TEXT_PRIMARY());
    const QColor name = ui::contrast::readable_on(fill, primary, {});
    const QColor chg = ui::contrast::readable_on(fill, QColor(ui::change_color(change_pct)), {primary}, 3.0);
    return {name.name(), chg.name()};
}

} // namespace finterm::screens::widgets::heatmap

#pragma once
// Contrast.h — pick a text colour that stays readable on a computed fill.
//
// Any widget that tints its own background by a value (heatmap tiles, P&L
// cells) and ALSO colours the text by the same value ends up with red-on-red at
// the extreme it most needs to show. These helpers composite the translucent
// fill over the surface it is actually drawn on and choose the text colour by
// WCAG contrast, so the semantic colour is kept while it is legible and dropped
// for a neutral the moment it is not.

#include <QColor>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace finterm::ui::contrast {

/// WCAG 2.x relative luminance of an opaque colour.
inline double luminance(const QColor& c) {
    auto chan = [](double v) {
        v /= 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * chan(c.red()) + 0.7152 * chan(c.green()) + 0.0722 * chan(c.blue());
}

/// WCAG contrast ratio, 1.0 .. 21.0.
inline double ratio(const QColor& a, const QColor& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// `top` (with its alpha) painted over the opaque `under` — what the eye sees.
inline QColor over(const QColor& top, const QColor& under) {
    const double a = top.alphaF();
    auto mix = [a](int t, int u) { return static_cast<int>(std::lround(t * a + u * (1.0 - a))); };
    return QColor(mix(top.red(), under.red()), mix(top.green(), under.green()), mix(top.blue(), under.blue()));
}

/// `preferred` when it clears `floor` against `fill`; otherwise whichever of
/// `fallbacks` (then pure black / white, which always reach >= 4.58:1 against
/// one of them) has the highest contrast.
inline QColor readable_on(const QColor& fill, const QColor& preferred, std::initializer_list<QColor> fallbacks,
                          double floor = 4.5) {
    if (ratio(preferred, fill) >= floor)
        return preferred;
    QColor best = preferred;
    double best_r = ratio(preferred, fill);
    for (const QColor& c : fallbacks) {
        const double r = ratio(c, fill);
        if (r >= floor)
            return c; // first acceptable fallback keeps the theme's own neutral
        if (r > best_r) {
            best = c;
            best_r = r;
        }
    }
    for (const QColor& c : {QColor(Qt::white), QColor(Qt::black)}) {
        const double r = ratio(c, fill);
        if (r > best_r) {
            best = c;
            best_r = r;
        }
    }
    return best;
}

} // namespace finterm::ui::contrast

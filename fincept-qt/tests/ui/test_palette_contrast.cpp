// test_ownership_layout.cpp — the OWNERSHIP screen must lay out, not just compile.
//
// Every layout defect in this screen so far was found by a user sending a
// screenshot: text at 1.91:1 contrast, two identical-looking inputs side by
// side, a button row squeezed to a truncated placeholder. None of them were
// compile errors and none were caught by a unit test, because "does it read
// well" is not something the type system can answer.
//
// Contrast is the part that CAN be asserted without standing up the screen: the
// palette is data, and a token below the floor is unreadable everywhere it is
// used. Layout itself is checked by rendering the screen offscreen and looking
// at the picture — that needs the whole app linked, so it stays a diagnostic
// rather than a test.

#include <QTest>

#include "ui/theme/Theme.h"

#include <cmath>

namespace {

/// WCAG relative luminance.
double luminance(const QColor& c) {
    auto chan = [](double v) {
        v /= 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * chan(c.red()) + 0.7152 * chan(c.green()) + 0.0722 * chan(c.blue());
}

double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a), lb = luminance(b);
    const double hi = std::max(la, lb), lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

} // namespace

class TestPaletteContrast : public QObject {
    Q_OBJECT

private slots:
    void the_palette_clears_the_contrast_floor() {
        // The regression that shipped: text_dim was 1.91:1 and text_tertiary
        // 2.53:1 against the ground, both below WCAG's 3:1 floor for ANY text,
        // and both were used for the lines explaining where a number came from.
        const QColor bg(fincept::ui::colors::BG_BASE());
        struct { const char* name; QColor c; double floor; } tokens[] = {
            {"TEXT_PRIMARY", QColor(fincept::ui::colors::TEXT_PRIMARY()), 4.5},
            {"TEXT_SECONDARY", QColor(fincept::ui::colors::TEXT_SECONDARY()), 4.5},
            {"TEXT_TERTIARY", QColor(fincept::ui::colors::TEXT_TERTIARY()), 3.0},
            {"TEXT_DIM", QColor(fincept::ui::colors::TEXT_DIM()), 3.0},
        };
        for (const auto& t : tokens) {
            const double r = contrast(t.c, bg);
            QVERIFY2(r >= t.floor,
                     qPrintable(QStringLiteral("%1 is %2:1 against the ground, below its %3:1 "
                                               "floor — that is unreadable, not dim")
                                    .arg(t.name).arg(r, 0, 'f', 2).arg(t.floor, 0, 'f', 1)));
        }
        // The accents used for semantic state have to be legible too.
        for (const char* n : {"GREEN", "RED", "AMBER", "CYAN"}) {
            const QColor c(n == QLatin1String("GREEN")   ? fincept::ui::colors::GREEN()
                           : n == QLatin1String("RED")   ? fincept::ui::colors::RED()
                           : n == QLatin1String("AMBER") ? fincept::ui::colors::AMBER()
                                                         : fincept::ui::colors::CYAN());
            QVERIFY2(contrast(c, bg) >= 3.0,
                     qPrintable(QStringLiteral("%1 is %2:1").arg(n).arg(contrast(c, bg))));
        }
    }

};

QTEST_APPLESS_MAIN(TestPaletteContrast)
#include "test_palette_contrast.moc"

#include "ui/theme/Theme.h"

#include "ui/theme/ThemeManager.h"

#include <QApplication>

#include <cmath>

namespace fincept::ui {

static bool s_rtl = false;

bool is_rtl() {
    return s_rtl;
}

void set_rtl(bool rtl) {
    s_rtl = rtl;
    qApp->setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
    // Re-apply current theme so RTL direction change is reflected
    ThemeManager::instance().apply_theme(ThemeManager::instance().current_theme_name());
}

QString change_color(double value) {
    // Unknown change (NaN — no previous close) is neither a gain nor a loss.
    if (!std::isfinite(value))
        return QString(ThemeManager::instance().tokens().text_secondary);
    return value >= 0 ? QString(ThemeManager::instance().tokens().positive)
                      : QString(ThemeManager::instance().tokens().negative);
}

void apply_global_stylesheet() {
    ThemeManager::instance().apply_theme("Obsidian");
}

} // namespace fincept::ui

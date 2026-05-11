// src/ui/components/SectionHeader.cpp
#include "ui/components/SectionHeader.h"
#include "ui/theme/Theme.h"

#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <algorithm>

namespace fincept::ui {

QString section_header_qss() {
    return QString(
        "QLabel { background:%1; color:%2; font-size:12px; font-weight:700;"
        " letter-spacing:1.5px; padding:6px 10px; border-bottom:1px solid %3; }")
        .arg(ui::colors::BG_RAISED(),
             ui::colors::TEXT_SECONDARY(),
             ui::colors::BORDER_MED());
}

QLabel* make_section_header(const QString& text, QWidget* parent) {
    auto* lbl = new QLabel(text, parent);
    lbl->setStyleSheet(section_header_qss());

    // Compute height from an explicit 12px font; QLabel::font() doesn't yet
    // reflect the stylesheet here, so we'd misread the height otherwise.
    QFont f(QStringLiteral("sans-serif"), -1);
    f.setPixelSize(12);
    const int h = QFontMetrics(f).height() + 12;  // 6px padding top + bottom
    lbl->setFixedHeight(std::max(h, 26));
    return lbl;
}

} // namespace fincept::ui

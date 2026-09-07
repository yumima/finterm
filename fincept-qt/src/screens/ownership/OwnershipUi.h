#pragma once
// Small shared pieces for the Ownership widgets: a section title, a data
// table with the terminal's conventions, a coloured cell, and the formatting
// rules every panel follows so the same number reads the same everywhere.

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QWidget>

#include <optional>

namespace fincept::screens::ownership_ui {

namespace fmt = fincept::ui::formatting;

/// The amber caps label every section starts with, with an optional right
/// side for the section's own "as of" line — the date is part of the title
/// because it is part of the number.
struct SectionTitle {
    QWidget* widget = nullptr;
    QLabel*  left = nullptr;
    QLabel*  right = nullptr;
};

inline SectionTitle section_title(const QString& text) {
    SectionTitle t;
    t.widget = new QWidget;
    auto* h = new QHBoxLayout(t.widget);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    t.left = new QLabel(text);
    t.left->setStyleSheet(QString("color:%1;font-weight:700;font-size:12px;letter-spacing:1px;")
                              .arg(ui::colors::AMBER()));
    t.right = new QLabel;
    t.right->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    t.right->setWordWrap(true);
    h->addWidget(t.left);
    h->addWidget(t.right, 1);
    return t;
}

inline QTableWidget* make_table(const QStringList& headers) {
    auto* t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->setSortingEnabled(false);
    // Interactive + stretch-last: the reader can size columns, and nothing
    // re-measures on every layout pass. Callers size once after filling.
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setSectionsMovable(true);
    // Content-width columns with empty space to the right, rather than a
    // last column stretched across half the pane with its header adrift.
    t->horizontalHeader()->setStretchLastSection(false);
    t->setWordWrap(false);
    return t;
}

inline QTableWidgetItem* cell(const QString& text, const QString& colour = {},
                              Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(align);
    if (!colour.isEmpty())
        it->setForeground(QColor(colour));
    return it;
}

inline QTableWidgetItem* num_cell(const QString& text, const QString& colour = {}) {
    return cell(text, colour, Qt::AlignRight | Qt::AlignVCenter);
}

/// Compact, not exact: a position value is read for its magnitude.
inline QString compact_or_dash(const std::optional<double>& v) {
    return v ? fmt::format_compact(*v) : fmt::placeholder();
}

inline QString pct_or_dash(const std::optional<double>& fraction, int dp = 1, bool sign = false) {
    return fraction ? fmt::format_percent(*fraction * 100.0, dp, sign) : fmt::placeholder();
}

inline QString date_or_dash(const QDate& d, const char* f = "yyyy-MM-dd") {
    return d.isValid() ? d.toString(QLatin1String(f)) : fmt::placeholder();
}

/// Green up, red down, the primary text colour for nothing.
inline QString signed_colour(const std::optional<double>& v) {
    if (!v)
        return {};
    return *v >= 0 ? ui::colors::GREEN() : ui::colors::RED();
}

/// The stylesheet every ownership widget applies: the tables are the screen,
/// and Qt's default palette renders them light-on-light against the
/// terminal's dark ground.
inline QString table_stylesheet() {
    return QString("QWidget{background:%1;color:%2;}"
                   "QTableWidget{background:%1;gridline-color:%3;"
                   "alternate-background-color:%6;color:%2;border:none;}"
                   // No `color` on ::item: a stylesheet colour there overrides
                   // every per-cell foreground, and the green/red/amber cells
                   // are half of what these tables say.
                   "QTableWidget::item{padding:1px 4px;}"
                   "QHeaderView::section{background:%4;color:%5;padding:4px;border:0;}"
                   "QToolTip{color:%2;background:%4;border:1px solid %3;}")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
             ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE());
}

/// A dim, wrapping note under a table: coverage, exclusions, what was left out.
inline QLabel* note_label() {
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

/// A thin rule between sections.
inline QFrame* rule() {
    auto* f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setStyleSheet(QString("color:%1;background:%1;max-height:1px;").arg(ui::colors::BORDER_DIM()));
    return f;
}

} // namespace fincept::screens::ownership_ui

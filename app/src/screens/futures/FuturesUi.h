#pragma once
// FuturesUi.h — the futures widgets' shared formatting, tables and style
// (moved from the old FUTURES screen so every futures widget reads alike).

#include "ui/formatting/NumberFormat.h"
#include "ui/tables/RowMarkDelegate.h"
#include "ui/theme/Theme.h"

#include <QColor>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QString>
#include <QTableWidget>

#include <cmath>

namespace finterm::screens::futures_ui {

inline QString num(double v, int dp) {
    return std::isfinite(v) ? QLocale(QLocale::English).toString(v, 'f', dp) : ui::formatting::placeholder();
}

inline QString signed_num(double v, int dp) {
    if (!std::isfinite(v))
        return ui::formatting::placeholder();
    return (v > 0 ? QStringLiteral("+") : v < 0 ? QStringLiteral("−") : QString()) +
           QLocale(QLocale::English).toString(std::abs(v), 'f', dp);
}

inline QString whole(double v) {
    return std::isfinite(v) ? QLocale(QLocale::English).toString(static_cast<qlonglong>(std::llround(v)))
                            : ui::formatting::placeholder();
}

inline QString dollars(double v) {
    if (!std::isfinite(v))
        return ui::formatting::placeholder();
    return (v < 0 ? QStringLiteral("−$") : QStringLiteral("$")) +
           QLocale(QLocale::English).toString(std::abs(v), 'f', std::abs(v) >= 1000 ? 0 : 2);
}

inline QColor tone(double v) {
    return QColor(!std::isfinite(v) || v == 0 ? ui::colors::TEXT_SECONDARY()
                  : v > 0                     ? ui::colors::POSITIVE()
                                              : ui::colors::NEGATIVE());
}

inline QTableWidgetItem* cell(QTableWidget* t, int row, int col) {
    QTableWidgetItem* it = t->item(row, col);
    if (!it) {
        it = new QTableWidgetItem;
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        t->setItem(row, col, it);
    }
    return it;
}

inline void put(QTableWidget* t, int row, int col, const QString& text, const QColor& color, bool right = true) {
    QTableWidgetItem* it = cell(t, row, col);
    if (it->text() != text)
        it->setText(text);
    it->setForeground(color);
    it->setTextAlignment((right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
}

inline QTableWidget* make_table(const QStringList& headers, QWidget* parent) {
    auto* t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(22);
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setFocusPolicy(Qt::NoFocus);
    t->setCursor(Qt::PointingHandCursor);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->setItemDelegate(new ui::RowMarkDelegate(t)); // the selected market / month: an amber band
    return t;
}

/// "CLZ26.NYM" → "CL"; "" for anything that is not a dated contract.
inline QString root_of_contract(const QString& contract_symbol) {
    if (!contract_symbol.contains(QLatin1Char('.')))
        return {};
    const QString code = contract_symbol.section(QLatin1Char('.'), 0, 0);
    return code.size() > 3 ? code.left(code.size() - 3) : QString();
}

/// The futures widgets' look (the old FUTURES screen's).
inline QString stylesheet() {
    using namespace ui::colors;
    const QString family = QString(ui::fonts::DATA_FAMILY);
    const int px = ui::fonts::font_px(-2);
    return QStringLiteral(
               "QLabel{background:transparent;border:none;font-family:'%1';font-size:%2px;color:%3;}"
               "QLabel#futStripName{color:%4;font-weight:700;font-size:%5px;}"
               "QLabel#futStripPrice{color:%4;font-weight:700;font-size:%5px;}"
               "QLabel#futStripChange{font-weight:700;font-size:%5px;}"
               "QLabel#futStripFacts{color:%6;}"
               "QLabel#futNote{color:%6;padding:4px 8px;}"
               "QLabel#futTerms{color:%3;padding:10px;}"
               "QLabel#futTicketContract{color:%4;font-weight:700;}"
               "QPushButton#futSeg{background:transparent;color:%6;border:none;padding:3px 6px;font-family:'%1';"
               "font-size:%2px;font-weight:700;}"
               "QPushButton#futSeg:hover{color:%4;}"
               "QPushButton#futSeg:checked{color:%7;border-bottom:2px solid %7;}")
               .arg(family)
               .arg(px)
               .arg(TEXT_SECONDARY(), TEXT_PRIMARY())
               .arg(ui::fonts::font_px(1))
               .arg(TEXT_TERTIARY(), AMBER()) +
           QStringLiteral(
               "QTableWidget{background:%1;alternate-background-color:%2;border:none;font-family:'%3';font-size:%4px;"
               "selection-background-color:%5;}"
               "QHeaderView::section{background:%1;color:%6;border:none;border-bottom:1px solid %7;padding:3px 5px;"
               "font-family:'%3';font-size:%4px;font-weight:700;}"
               "QTableWidget::item{padding:0 5px;}"
               "QTabWidget::pane{border:0;background:%1;}"
               "QTabBar::tab{background:%8;color:%6;padding:4px 12px;border:0;border-bottom:2px solid transparent;"
               "font-family:'%3';font-size:%4px;font-weight:700;letter-spacing:1px;}"
               "QTabBar::tab:selected{color:%9;border-bottom:2px solid %9;}"
               "QSpinBox,QLineEdit{background:%8;color:%10;border:1px solid %7;padding:3px 6px;font-family:'%3';"
               "font-size:%4px;}")
               .arg(BG_BASE(), ROW_ALT(), family)
               .arg(px)
               .arg(BG_HOVER(), TEXT_TERTIARY(), BORDER_DIM(), BG_RAISED(), AMBER(), TEXT_PRIMARY()) +
           QStringLiteral(
               "QPushButton#futBuy,QPushButton#futSell,QPushButton#futPlace,QPushButton#futClose{background:%1;"
               "color:%2;border:1px solid %3;padding:4px 12px;font-family:'%4';font-size:%5px;font-weight:700;}"
               "QPushButton#futBuy:checked{background:%6;color:%7;border-color:%7;}"
               "QPushButton#futSell:checked{background:%8;color:%9;border-color:%9;}"
               "QPushButton#futPlace{color:%10;border-color:%10;}"
               "QPushButton#futPlace:disabled{color:%2;border-color:%3;}"
               "QPushButton#futClose{padding:0 8px;}")
               .arg(BG_RAISED(), TEXT_TERTIARY(), BORDER_MED(), family)
               .arg(px)
               .arg(POSITIVE_BG(), POSITIVE(), NEGATIVE_BG(), NEGATIVE(), AMBER());
}

} // namespace finterm::screens::futures_ui

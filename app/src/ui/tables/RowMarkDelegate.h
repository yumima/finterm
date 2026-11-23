#pragma once
// RowMarkDelegate.h — how a table marks its selected row: the row a click
// picked, which on a canvas is the one the linked chart draws. An amber band
// (tint, top and bottom rules, a bar at the left edge) over the row's own
// colours — Qt's selection look replaced the row's red / green with one
// highlight colour and, on these dark tables, read as a barely-there grey.

#include "ui/theme/Theme.h"

#include <QPainter>
#include <QStyledItemDelegate>

namespace finterm::ui {

class RowMarkDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        paint_plain(p, opt, idx);
        if (opt.state & QStyle::State_Selected)
            paint_mark(p, opt.rect, idx.column() == 0);
    }

    /// The cell as if unselected: the row keeps its own colours.
    void paint_plain(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
        QStyleOptionViewItem o(opt);
        o.state &= ~QStyle::State_Selected;
        QStyledItemDelegate::paint(p, o, idx);
    }

    static void paint_mark(QPainter* p, const QRect& r, bool first_column) {
        QColor tint(colors::AMBER());
        tint.setAlpha(48);
        p->fillRect(r, tint);
        QColor rule(colors::AMBER());
        rule.setAlpha(200);
        p->fillRect(QRect(r.left(), r.top(), r.width(), 1), rule);
        p->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), rule);
        if (first_column)
            p->fillRect(QRect(r.left(), r.top(), 3, r.height()), QColor(colors::AMBER()));
    }

};

} // namespace finterm::ui

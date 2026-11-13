#include "ui/tables/TickFlashDelegate.h"

#include "ui/theme/Theme.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QEvent>
#include <QPainter>
#include <QRegion>
#include <QTimer>

namespace fincept::ui {

TickFlashDelegate* TickFlashDelegate::install(QAbstractItemView* view, int key_column) {
    if (!view)
        return nullptr;
    if (auto* cur = view->itemDelegate()) {
        if (auto* mine = qobject_cast<TickFlashDelegate*>(cur))
            return mine;
        if (cur->metaObject() != &QStyledItemDelegate::staticMetaObject)
            return nullptr; // someone else's painting — leave it alone
    }
    auto* d = new TickFlashDelegate(view, key_column);
    view->setItemDelegate(d);
    view->installEventFilter(d);
    return d;
}

void TickFlashDelegate::set_key_column(int column) {
    if (column == key_column_)
        return;
    key_column_ = column;
    cells_.clear();
    flashing_.clear();
}

bool TickFlashDelegate::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == view_) {
        if (ev->type() == QEvent::Hide) {
            cells_.clear(); // values change unseen while hidden; the next paint is a baseline
            flashing_.clear();
        }
        // NOT the base filter: QStyledItemDelegate::eventFilter treats the object
        // as an item EDITOR (Enter/Esc/FocusOut → commitData/closeEditor).
        return false;
    }
    return QStyledItemDelegate::eventFilter(obj, ev);
}

TickFlashDelegate::TickFlashDelegate(QAbstractItemView* view, int key_column)
    : QStyledItemDelegate(view), view_(view), key_column_(key_column) {
    anim_ = new QTimer(this);
    anim_->setInterval(40); // ~25 fps fade; stops itself once every flash is over
    connect(anim_, &QTimer::timeout, this, [this]() {
        if (flashing_.isEmpty() || !view_) {
            anim_->stop();
            return;
        }
        // Each entry is repainted until its fade is over, then dropped HERE —
        // not only by its own paint: a row removed, re-keyed or scrolled away
        // mid-flash is never painted again and would keep this timer alive.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QRegion dirty;
        for (auto it = flashing_.begin(); it != flashing_.end();) {
            dirty += it->rect;
            it = (now - it->start_ms > kFlashMs + 100) ? flashing_.erase(it) : std::next(it);
        }
        view_->viewport()->update(dirty);
    });
}

bool TickFlashDelegate::parse_number(const QString& s, double* out) {
    // Keep sign, digits and the decimal point; drop $, %, commas, spaces.
    // A LEADING down arrow is the sign ("▼ 1.25" = -1.25: MarketPanel shows the
    // change as arrow + magnitude) — dropping it would flash a deepening fall
    // green. Anywhere else an arrow is decoration, not a sign.
    // A trailing K/M/B/T scales the value (below).
    QString t;
    t.reserve(s.size());
    bool any_digit = false;
    const QString lead = s.trimmed();
    const bool down_arrow = !lead.isEmpty() && (lead.front() == QChar(0x25BC) || lead.front() == QChar(0x25BE) ||
                                                lead.front() == QChar(0x2193)); // ▼ ▾ ↓
    for (const QChar c : s) {
        if (c.isDigit()) {
            t += c;
            any_digit = true;
        } else if (c == QLatin1Char('.') || c == QLatin1Char('-') || c == QChar(0x2212)) {
            t += (c == QChar(0x2212)) ? QLatin1Char('-') : c;
        }
    }
    if (!any_digit)
        return false;
    bool ok = false;
    double v = t.toDouble(&ok);
    if (!ok)
        return false;
    if (down_arrow && v > 0)
        v = -v;
    // Unit suffix after the number ("999.5K" → "1.0M" is UP, not 999.5 → 1.0).
    for (int i = s.size() - 1; i >= 0; --i) {
        const QChar c = s.at(i).toUpper();
        if (c.isDigit())
            break;
        if (c == QLatin1Char('K') || c == QLatin1Char('M') || c == QLatin1Char('B') || c == QLatin1Char('T')) {
            v *= c == QLatin1Char('K') ? 1e3 : c == QLatin1Char('M') ? 1e6 : c == QLatin1Char('B') ? 1e9 : 1e12;
            break;
        }
    }
    *out = v;
    return true;
}

void TickFlashDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    QStyledItemDelegate::paint(p, opt, idx);
    if (key_column_ < 0 || idx.column() == key_column_)
        return;
    const QString key_text = idx.siblingAtColumn(key_column_).data(Qt::DisplayRole).toString();
    if (key_text.isEmpty())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (cells_.size() > 4096) {
        cells_.clear(); // bounded: a view cycling through symbols must not grow forever
        flashing_.clear();
    }
    const QString key = key_text + QChar(0x1f) + QString::number(idx.column());
    Cell& c = cells_[key];

    double v = 0;
    const bool has = parse_number(idx.data(Qt::DisplayRole).toString(), &v);
    if (has && c.has_value && v != c.value) {
        c.dir = v > c.value ? 1 : -1;
        c.flash_ms = now;
        if (!anim_->isActive())
            anim_->start();
    }
    c.value = v;
    c.has_value = has;

    if (c.flash_ms == 0)
        return;
    const qint64 age = now - c.flash_ms;
    if (age >= kFlashMs) {
        c.flash_ms = 0;
        flashing_.remove(key); // this paint drew it without the overlay
        return;
    }
    flashing_.insert(key, Flash{opt.rect, c.flash_ms}); // current position (rows can move between paints)
    QColor col(c.dir > 0 ? colors::POSITIVE() : colors::NEGATIVE());
    col.setAlpha(static_cast<int>(110.0 * (1.0 - double(age) / kFlashMs)));
    p->fillRect(opt.rect, col);
}

} // namespace fincept::ui

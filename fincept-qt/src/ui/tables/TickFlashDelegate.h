#pragma once
// TickFlashDelegate.h — Bloomberg-style tick flash for any QAbstractItemView.
//
// A cell whose number changes between two paints gets a green (up) or red
// (down) overlay that fades out over kFlashMs. Detection is by PAINTED TEXT,
// keyed by (row key, column) — the row key is the text of `key_column` (the
// symbol) — so it works for views that rebuild every item on each update
// (most of ours) as well as ones that edit cells in place, and survives
// re-sorting. Nothing flashes on a first paint: what a view remembers is
// dropped when it is hidden, so a screen coming back into view is not a tick.
//
// install() is a no-op on a view that already has a custom delegate: wrapping
// someone else's painting is not worth the surprise.

#include <QHash>
#include <QPointer>
#include <QStyledItemDelegate>

class QAbstractItemView;
class QTimer;

namespace fincept::ui {

class TickFlashDelegate : public QStyledItemDelegate {
    Q_OBJECT
  public:
    /// Attach to `view` unless it already has a non-default delegate.
    static TickFlashDelegate* install(QAbstractItemView* view, int key_column = 0);

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;

    /// The column whose text identifies a row (the symbol). Changing it
    /// forgets every remembered value. Negative = no key (nothing flashes).
    void set_key_column(int column);

    /// Short and faint on purpose: a cue that a number moved, not a light
    /// show — a stronger, longer flash read as annoying on a busy table.
    static constexpr int kFlashMs = 450;
    static constexpr int kFlashAlpha = 45; ///< peak overlay opacity (of 255)

    /// The SIGNED number a display string shows ("+1.25%", "$1,234.50",
    /// "▼ 0.35" = -0.35, "1.2M"), or false when it shows none.
    static bool parse_number(const QString& s, double* out);

  protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

  private:
    TickFlashDelegate(QAbstractItemView* view, int key_column);

    struct Cell {
        double value = 0;
        bool has_value = false;
        qint64 flash_ms = 0; ///< 0 = not flashing
        int dir = 0;         ///< +1 up, -1 down
    };
    mutable QHash<QString, Cell> cells_;
    /// Cells mid-flash → where they were last painted. The fade repaints only
    /// these rects (a full-viewport repaint at 25 fps of a styled table was
    /// most of the live dashboard's CPU).
    struct Flash {
        QRect rect;
        qint64 start_ms = 0;
    };
    mutable QHash<QString, Flash> flashing_;
    QPointer<QAbstractItemView> view_;
    QTimer* anim_ = nullptr; ///< repaints flashing_ while any flash is fading
    int key_column_ = 0;
};

} // namespace fincept::ui

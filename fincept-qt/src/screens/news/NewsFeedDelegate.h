#pragma once
#include <QFont>
#include <QFontMetrics>
#include <QStyledItemDelegate>

namespace fincept::screens {

/// Paints news rows directly with QPainter — zero widget allocation per item.
/// Handles WIRE (dense 26px rows) and CLUSTERS (multi-line cards) modes.
class NewsFeedDelegate : public QStyledItemDelegate {
    Q_OBJECT
  public:
    explicit NewsFeedDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Source-column width is user-adjustable. NewsFeedPanel installs a
    // drag handle on the list viewport that calls set_source_col_width().
    void set_source_col_width(int px);
    int  source_col_width() const { return source_col_width_; }

    static constexpr int kSourceColMin = 60;
    static constexpr int kSourceColMax = 220;
    static constexpr int kSourceColDefault = 110;

    // Distance from rect.left() at which the source name starts, in px.
    // Sum of the fixed-width gutters before source in paint_wire_row:
    //   rect.left()+2 + new-indicator-dot(6) + monitor-stripe(5)
    //   + time-cell(40) + priority-dot(8) + tier-badge(12) = 73
    // Exposed so NewsFeedPanel can locate the source/headline boundary
    // for its drag-resize hot-zone.
    static constexpr int kPreSourceX = 73;

  private:
    void paint_wire_row(QPainter* painter, const QRect& rect, const QModelIndex& index, bool selected,
                        bool hovered) const;
    void paint_cluster_card(QPainter* painter, const QRect& rect, const QModelIndex& index, bool selected,
                            bool hovered) const;

    QFont data_font_;
    QFont bold_font_;
    QFont tiny_font_;
    mutable QFontMetrics data_fm_;
    mutable QFontMetrics bold_fm_;
    mutable QFontMetrics tiny_fm_;

    int source_col_width_ = kSourceColDefault;
};

} // namespace fincept::screens

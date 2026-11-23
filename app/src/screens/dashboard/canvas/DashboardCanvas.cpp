#include "screens/dashboard/canvas/DashboardCanvas.h"

#include <QHash>
#include <QSet>

#include "core/logging/Logger.h"
#include "screens/dashboard/canvas/DashboardTemplates.h"
#include "screens/dashboard/canvas/SelectionLink.h"
#include "screens/dashboard/canvas/WidgetRegistry.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QUuid>

#include <algorithm>

namespace finterm::screens {

DashboardCanvas::DashboardCanvas(QWidget* parent) : QWidget(parent) {
    tokens_ = ui::ThemeManager::instance().tokens();
    // Set both autoFillBackground (Qt paints bg_base before paintEvent) AND
    // a stylesheet so the background survives hide/show cycles from tab switching.
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    apply_bg();
    setMinimumHeight(200);

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this](const ui::ThemeTokens& t) {
        tokens_ = t;
        apply_bg();
        update();
    });

    placeholder_ = new PlaceholderOverlay(this);

    scroll_timer_ = new QTimer(this);
    scroll_timer_->setInterval(40);
    connect(scroll_timer_, &QTimer::timeout, this, &DashboardCanvas::on_scroll_tick);

    // Debounce resize events so ADS splitter drags don't trigger repeated
    // column recalculations and layout compaction on every pixel of movement.
    resize_timer_ = new QTimer(this);
    resize_timer_->setSingleShot(true);
    resize_timer_->setInterval(150);
    connect(resize_timer_, &QTimer::timeout, this, [this]() {
        const int w = pending_resize_w_;
        if (w <= 0)
            return;

        // Restore to canonical columns if width now permits it — user's layout
        // should snap back when they expand the panel again.
        const int target_cols = (w >= 1000)  ? canonical_cols_
                                : (w >= 600) ? std::min(canonical_cols_, 9)
                                             : std::min(canonical_cols_, 6);

        if (target_cols != layout_.cols) {
            // Always derive from the DESIGNED arrangement (the one made at the
            // canonical column count): squeezing to 9 and widening back to 12
            // used to keep the squeezed positions forever.
            QHash<QString, QJsonObject> configs;
            for (const auto& it : layout_.items)
                configs.insert(it.instance_id, it.config);
            QVector<GridItem> base = designed_items_.isEmpty() ? layout_.items : designed_items_;
            for (auto& it : base)
                if (configs.contains(it.instance_id))
                    it.config = configs.value(it.instance_id);
            layout_.cols = target_cols;
            layout_.items = target_cols == canonical_cols_ ? base : refit_to_cols(base, layout_.cols);
        }

        if (!dragging_tile_ && !resizing_tile_)
            reflow_tiles();
        update_canvas_height();
    });
}

DashboardCanvas::~DashboardCanvas() {
    for (auto* t : tiles_)
        t->deleteLater();
    tiles_.clear();
}

// ── Layout management ─────────────────────────────────────────────────────────

void DashboardCanvas::load_layout(const GridLayout& layout) {
    for (auto* t : tiles_)
        t->deleteLater();
    tiles_.clear();
    layout_ = layout;

    // Record the canonical column count from the saved layout — this is what
    // the user designed for. Responsive shrink is allowed on narrow viewports
    // but we restore to this when the panel expands back to full width.
    canonical_cols_ = layout_.cols > 0 ? layout_.cols : 12;

    // Apply responsive columns for current width without clamping canonical_cols_.
    // A canvas not laid out yet (a screen built in the background has its
    // default ~100 px child size) keeps the designed columns: refitting to 6
    // there painted a 6-column dashboard that jumped to 12 on first show.
    if (width() >= 200)
        layout_.cols = responsive_cols(width());

    for (auto& item : layout_.items) {
        const WidgetMeta* meta = WidgetRegistry::instance().find(item.id);
        if (!meta) {
            LOG_WARN("Canvas", QString("Unknown widget type: %1").arg(item.id));
            continue;
        }
        // The registry — not the saved blob — is the source of truth for the
        // minimums. They're persisted alongside position, so a layout saved
        // under an older build would otherwise pin its tiles to that build's
        // floor forever and the user could never shrink them.
        item.cell.min_w = meta->min_w;
        item.cell.min_h = meta->min_h;
        // Growing w to reach a raised min_w can push the tile past the right
        // edge on a responsive (6/9-column) grid — clamp both back in.
        item.cell.w = std::min(std::max(item.cell.w, meta->min_w), layout_.cols);
        item.cell.x = std::max(0, std::min(item.cell.x, layout_.cols - item.cell.w));
        item.cell.h = std::max(item.cell.h, meta->min_h);
    }
    // A fit-to-view canvas keeps the rows it was saved with; a layout from
    // before rows were saved: the rows its tiles use (after the minimums
    // above), and an empty page the blank page's twelve.
    if (fit_exact_ && layout_.rows <= 0)
        layout_.rows = layout_.items.isEmpty() ? 12 : max_row(layout_.items);
    // A new arrangement: the page's selection starts again from its chart.
    if (auto* link = findChild<SelectionLink*>(QString(), Qt::FindDirectChildrenOnly))
        link->reset();

    // Tiles are built from a copy: a widget shown here can already talk back
    // (a chart selecting its symbol, a futures widget saving the contract it
    // follows → config_changed → layout_.items written while shared), and a
    // reference into layout_.items held across that pointed at freed memory.
    const QVector<GridItem> to_build = layout_.items;
    for (const auto& item : to_build) {
        const WidgetMeta* meta = WidgetRegistry::instance().find(item.id);
        if (!meta)
            continue;
        auto* widget = meta->factory(item.config);
        auto* tile = new WidgetTile(item.instance_id, widget, this);
        connect_tile(tile);
        tiles_.append(tile);
        tile->show();
    }
    // The design is the loaded layout with the same registry corrections
    // (minimums; unknown widget types dropped), at the canonical columns.
    designed_items_.clear();
    for (auto item : layout.items) {
        const WidgetMeta* meta = WidgetRegistry::instance().find(item.id);
        if (!meta)
            continue;
        item.cell.min_w = meta->min_w;
        item.cell.min_h = meta->min_h;
        item.cell.w = std::min(std::max(item.cell.w, meta->min_w), canonical_cols_);
        item.cell.x = std::max(0, std::min(item.cell.x, canonical_cols_ - item.cell.w));
        item.cell.h = std::max(item.cell.h, meta->min_h);
        designed_items_.append(item);
    }
    // Loaded into fewer columns than it was designed for: the per-tile clamps
    // above can land tiles on each other — separate them.
    if (layout_.cols != canonical_cols_)
        layout_.items = refit_to_cols(layout_.items, layout_.cols);

    reflow_tiles();
    update_canvas_height();
    emit widget_count_changed(tiles_.size());
    LOG_INFO("Canvas", QString("Loaded layout with %1 tiles, %2 cols").arg(tiles_.size()).arg(layout_.cols));
}

void DashboardCanvas::apply_template(const QString& template_id) {
    const auto tmpls = all_dashboard_templates();
    for (const auto& t : tmpls) {
        if (t.id == template_id) {
            GridLayout layout;
            layout.cols = 12; // templates are designed for 12 columns
            canonical_cols_ = 12;
            layout.row_h = layout_.row_h;
            layout.margin = layout_.margin;
            for (auto item : t.items) {
                item.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                // Clamp item to current column count
                item.cell.x = std::min(item.cell.x, layout.cols - item.cell.w);
                if (item.cell.x < 0) {
                    item.cell.x = 0;
                    item.cell.w = std::min(item.cell.w, layout.cols);
                }
                layout.items.append(item);
            }
            layout.items = compact_vertical(layout.items);
            load_layout(layout);
            notify_layout_changed();
            return;
        }
    }
    LOG_WARN("Canvas", QString("Template not found: %1").arg(template_id));
}

void DashboardCanvas::add_widget(const QString& widget_type_id) {
    const WidgetMeta* meta = WidgetRegistry::instance().find(widget_type_id);
    if (!meta)
        return;

    GridItem item;
    item.id = widget_type_id;
    item.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int w = std::min(meta->default_w, layout_.cols);
    int h = meta->default_h;
    item.cell = find_first_fit(layout_.items, w, h, layout_.cols);
    item.cell.min_w = meta->min_w;
    item.cell.min_h = meta->min_h;

    layout_.items.append(item);
    layout_.items = compact_vertical(layout_.items);

    auto* widget = meta->factory(item.config);
    auto* tile = new WidgetTile(item.instance_id, widget, this);
    connect_tile(tile);
    tiles_.append(tile);
    tile->show();

    reflow_tiles();
    update_canvas_height();
    emit widget_count_changed(tiles_.size());
    notify_layout_changed();
}

void DashboardCanvas::remove_widget(const QString& instance_id) {
    auto it = std::find_if(layout_.items.begin(), layout_.items.end(),
                           [&](const GridItem& i) { return i.instance_id == instance_id; });
    if (it != layout_.items.end())
        layout_.items.erase(it);

    auto* tile = tile_for_id(instance_id);
    if (tile) {
        tiles_.removeOne(tile);
        tile->deleteLater();
    }

    layout_.items = compact_vertical(layout_.items);
    reflow_tiles(true);
    update_canvas_height();
    emit widget_count_changed(tiles_.size());
    notify_layout_changed();
}

void DashboardCanvas::set_row_height(int px) {
    layout_.row_h = px;
    reflow_tiles(true);
    update_canvas_height();
    notify_layout_changed();
}

void DashboardCanvas::fit_rows_to_viewport(int vp_h) {
    // A fit-to-view canvas divides the view into a FIXED number of rows (the
    // page's, saved): sizing it by the tiles (the old way) re-sized every tile
    // whenever one grew or shrank. Tiles running past the last row — a widget
    // added to a full page — are shown by fitting more rows for as long as
    // they are there; the page's own rows do not change.
    const int rows = fit_exact_ ? std::max(layout_.rows, max_row(layout_.items)) : max_row(layout_.items);
    if (rows <= 0 || vp_h <= 0)
        return;

    // Ideal row_h so all tile rows fill vp_h without a scrollbar:
    //   vp_h = margin + rows*(row_h+margin)  →  row_h = (vp_h - margin*(rows+1)) / rows
    const int ideal = (vp_h - layout_.margin * (rows + 1)) / rows;

    // Only expand — never shrink below the current row_h (user may have manually
    // made tiles taller) and never below 36px (tiles become unreadable).
    // Cap at 240px to avoid absurdly tall tiles on very large monitors.
    // A Markets screen is read at a glance: it fits the view exactly, shrinking
    // too (down to 30px rows, the title bar plus a line).
    const int new_rh = fit_exact_ ? qBound(30, ideal, 240) : qBound(qMax(36, layout_.row_h), ideal, 240);
    if (new_rh == layout_.row_h)
        return;

    layout_.row_h = new_rh;
    reflow_tiles(false); // no animation for viewport-driven reflow
    update_canvas_height();
    notify_layout_changed();
}

// ── Drag handling (swap on collision) ─────────────────────────────────────────

void DashboardCanvas::on_drag_started(WidgetTile* tile, QPoint canvas_pos) {
    dragging_tile_ = tile;
    drag_offset_ = canvas_pos - tile->pos();
    pre_drag_layout_ = layout_;

    auto* item = item_for_id(tile->instance_id());
    if (item)
        ghost_cell_ = item->cell;

    tile->set_dragging(true);
    tile->raise();
    update_placeholder(ghost_cell_);
    scroll_timer_->start();
}

void DashboardCanvas::on_drag_moved(WidgetTile* tile, QPoint canvas_pos) {
    last_drag_canvas_pos_ = canvas_pos;
    if (!dragging_tile_ || dragging_tile_ != tile)
        return;

    // Move tile freely
    QPoint wanted = canvas_pos - drag_offset_;
    tile->move(wanted);

    // Compute grid target
    QPoint center(wanted.x() + tile->width() / 2, wanted.y() + tile->height() / 2);
    GridCell new_ghost = rect_to_grid(
        QRect(center.x() - tile->width() / 2, center.y() - tile->height() / 2, tile->width(), tile->height()),
        layout_.cols, width(), layout_.row_h, layout_.margin, ghost_cell_.min_w, ghost_cell_.min_h);

    // Keep original size
    auto* orig = item_for_id(tile->instance_id());
    new_ghost.x = std::max(0, std::min(new_ghost.x, layout_.cols - (orig ? orig->cell.w : new_ghost.w)));
    new_ghost.y = std::max(0, new_ghost.y);
    new_ghost.w = orig ? orig->cell.w : new_ghost.w;
    new_ghost.h = orig ? orig->cell.h : new_ghost.h;
    new_ghost.min_w = orig ? orig->cell.min_w : 2;
    new_ghost.min_h = orig ? orig->cell.min_h : 1;

    if (new_ghost == ghost_cell_)
        return;
    const GridCell prev_ghost = ghost_cell_;
    ghost_cell_ = new_ghost;

    auto resolved = dragged_items(tile->instance_id(), ghost_cell_);
    if (!resolved) { // it would push a tile off the page: the tile stays at the last place that fit
        ghost_cell_ = prev_ghost;
        return;
    }

    // Reflow non-dragging tiles
    for (const auto& ri : *resolved) {
        if (ri.instance_id == tile->instance_id())
            continue;
        auto* t = tile_for_id(ri.instance_id);
        if (t)
            t->setGeometry(grid_to_rect(ri.cell, layout_.cols, width(), layout_.row_h, layout_.margin));
    }

    update_placeholder(ghost_cell_);
}

void DashboardCanvas::on_drag_released(WidgetTile* tile, QPoint /*canvas_pos*/) {
    if (!dragging_tile_ || dragging_tile_ != tile)
        return;
    scroll_timer_->stop();

    // ghost_cell_ is the last place on_drag_moved accepted: the same layout.
    if (auto resolved = dragged_items(tile->instance_id(), ghost_cell_))
        layout_.items = *resolved;
    else
        layout_.items = pre_drag_layout_.items;

    dragging_tile_ = nullptr;
    tile->set_dragging(false);
    hide_placeholder();
    reflow_tiles(true);
    update_canvas_height();
    notify_layout_changed(/*user_edit=*/true);
}

// ── Resize handling (push-down, reflows ALL tiles) ────────────────────────────

void DashboardCanvas::on_resize_started(WidgetTile* tile, QPoint /*canvas_pos*/) {
    resizing_tile_ = tile;
    pre_resize_layout_ = layout_;

    auto* item = item_for_id(tile->instance_id());
    if (item)
        resize_origin_cell_ = item->cell;

    // Initialize ghost to current cell so first move doesn't snap
    ghost_cell_ = resize_origin_cell_;

    tile->set_resizing(true);
    update_placeholder(resize_origin_cell_);
}

void DashboardCanvas::on_resize_moved(WidgetTile* tile, QPoint canvas_pos) {
    if (!resizing_tile_ || resizing_tile_ != tile)
        return;

    int col_w = (width() - layout_.margin * (layout_.cols + 1)) / layout_.cols;
    if (col_w <= 0)
        return;

    // canvas_pos is now computed from global screen coordinates in WidgetTile,
    // so it's stable even if the tile gets repositioned by setGeometry.
    // Compute tile origin from the fixed origin cell (doesn't change during resize).
    int tile_px_x = layout_.margin + resize_origin_cell_.x * (col_w + layout_.margin);
    int tile_px_y = layout_.margin + resize_origin_cell_.y * (layout_.row_h + layout_.margin);

    int new_px_w = canvas_pos.x() - tile_px_x;
    int new_px_h = canvas_pos.y() - tile_px_y;

    // Convert pixel delta to grid columns/rows.
    // Use truncation (not rounding) so the cell only grows when the mouse
    // has clearly crossed the midpoint of the next column/row. This prevents
    // the widget from shrinking on the initial click.
    int new_w = std::max(1, (new_px_w + col_w + layout_.margin / 2) / (col_w + layout_.margin));
    int new_h = std::max(1, (new_px_h + layout_.row_h + layout_.margin / 2) / (layout_.row_h + layout_.margin));

    new_w = std::max(resize_origin_cell_.min_w, std::min(new_w, layout_.cols - resize_origin_cell_.x));
    new_h = std::max(resize_origin_cell_.min_h, new_h);

    GridCell target = resize_origin_cell_;
    target.w = new_w;
    target.h = new_h;

    // Early-out: skip expensive collision resolution if nothing changed
    if (target == ghost_cell_)
        return;

    auto resized = resized_items(tile->instance_id(), target);
    if (!resized)
        return; // no room left on the page: the tile stays at the last size that fit
    ghost_cell_ = target;
    const auto& working = *resized;

    // Reflow non-resizing tiles only.
    // The resizing tile stays where it is — we only update its geometry
    // on release, so it can't shift the coordinate basis mid-drag.
    for (const auto& wi : working) {
        if (wi.instance_id == tile->instance_id())
            continue;
        auto* t = tile_for_id(wi.instance_id);
        if (t)
            t->setGeometry(grid_to_rect(wi.cell, layout_.cols, width(), layout_.row_h, layout_.margin));
    }

    // But do resize the tile itself to reflect the new size (anchored at origin)
    QRect resizing_rect = grid_to_rect(target, layout_.cols, width(), layout_.row_h, layout_.margin);
    tile->setGeometry(resizing_rect);

    update_placeholder(target);
    update_canvas_height();
}

void DashboardCanvas::on_resize_released(WidgetTile* tile) {
    if (!resizing_tile_ || resizing_tile_ != tile)
        return;

    // ghost_cell_ is the last size that fit (resized_items accepted it).
    if (auto resized = resized_items(tile->instance_id(), ghost_cell_))
        layout_.items = *resized;

    resizing_tile_ = nullptr;
    tile->set_resizing(false);
    hide_placeholder();
    reflow_tiles(true);
    update_canvas_height();
    notify_layout_changed(/*user_edit=*/true);
}

std::optional<QVector<GridItem>> DashboardCanvas::dragged_items(const QString& id, const GridCell& ghost) const {
    GridItem moving;
    moving.instance_id = id;
    moving.cell = ghost;
    auto items = resolve_collisions_with_swap(pre_drag_layout_.items, moving);
    for (auto& it : items)
        if (it.instance_id == id)
            it.cell = ghost;
    items = compact_vertical(items);
    if (!fit_exact_)
        return items;
    // The rows on screen when the drag began (a page holding an added widget
    // below its own rows shows those too).
    return keep_within_rows(items, id, std::max(layout_.rows, max_row(pre_drag_layout_.items)));
}

std::optional<QVector<GridItem>> DashboardCanvas::resized_items(const QString& id, const GridCell& target) const {
    auto working = pre_resize_layout_.items;
    for (auto& wi : working)
        if (wi.instance_id == id)
            wi.cell = target;
    GridItem moving;
    moving.instance_id = id;
    moving.cell = target;
    working = compact_vertical(resolve_collisions(working, moving));
    if (!fit_exact_)
        return working;
    // The page keeps its rows: growing a tile takes room from the tiles it
    // pushes, never from the rest of the page.
    return keep_within_rows(working, id, std::max(layout_.rows, max_row(pre_resize_layout_.items)));
}

// ── Close / scroll ────────────────────────────────────────────────────────────

void DashboardCanvas::on_tile_close(WidgetTile* tile) {
    remove_widget(tile->instance_id());
}

void DashboardCanvas::on_scroll_tick() {
    auto* scroll = qobject_cast<QScrollArea*>(parent() ? parent()->parent() : nullptr);
    if (!scroll)
        return;
    auto* bar = scroll->verticalScrollBar();
    if (!bar)
        return;

    int edge = last_drag_canvas_pos_.y() - bar->value();
    int h = scroll->viewport()->height();
    if (edge > h - 40)
        bar->setValue(bar->value() + 10);
    else if (edge < 40)
        bar->setValue(bar->value() - 10);
}

// ── Qt events ─────────────────────────────────────────────────────────────────

void DashboardCanvas::apply_bg() {
    // Set both stylesheet AND QPalette so Qt always knows the correct background,
    // even during hide/show cycles from tab switching in ADS docking.
    QString bg = QString(tokens_.bg_base);
    // Runs on every show: re-setting an identical stylesheet re-polishes every
    // tile under the canvas (see DashboardScreen::refresh_theme). The palette
    // and stylesheet persist across hide/show, so only a changed theme needs
    // re-applying.
    if (bg == applied_bg_)
        return;
    applied_bg_ = bg;
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString("background:%1;").arg(bg));
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(bg));
    pal.setColor(QPalette::Base, QColor(bg));
    pal.setColor(QPalette::Button, QColor(bg));
    setPalette(pal);
}

void DashboardCanvas::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // Re-apply background and force immediate repaint when tab is re-selected.
    // Without this, the canvas shows Qt's default grey for one frame.
    apply_bg();
    update();
}

void DashboardCanvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);

    // Immediately reflow tiles to the new pixel width without changing column
    // count. This keeps widget positions visually correct as the panel is dragged.
    // Column recalculation (and potentially layout compaction) is deferred to
    // resize_timer_ so transient ADS splitter movements don't destroy the layout.
    if (!dragging_tile_ && !resizing_tile_)
        reflow_tiles();

    update_canvas_height();

    // Kick the debounce timer — column recalc fires after 150ms of stable width.
    pending_resize_w_ = width();
    resize_timer_->start();
}

void DashboardCanvas::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    // Fill only the exposed region (clip rect) instead of blitting a full-height pixmap
    QRect clip = event->rect();
    p.fillRect(clip, QColor(tokens_.bg_base));

    if (layout_.cols <= 0 || width() <= 0)
        return;

    // Use border_dim at low opacity for grid lines
    QColor grid_line(tokens_.border_dim);
    grid_line.setAlpha(40);
    p.setPen(QPen(grid_line, 1));

    const int total_margin = layout_.margin * (layout_.cols + 1);
    const int col_w = (width() - total_margin) / layout_.cols;

    // Only draw grid lines that intersect the clip rect
    for (int c = 0; c <= layout_.cols; ++c) {
        int x = layout_.margin + c * (col_w + layout_.margin);
        if (x >= clip.left() - 1 && x <= clip.right() + 1)
            p.drawLine(x, clip.top(), x, clip.bottom());
    }
    int row_step = layout_.row_h + layout_.margin;
    if (row_step > 0) {
        // Start from the first row visible in the clip rect
        int first_row = std::max(0, (clip.top() - layout_.margin) / row_step) & ~1; // align to even
        for (int row = first_row; row * row_step < clip.bottom(); row += 2) {
            int y = layout_.margin + row * row_step;
            if (y >= clip.top() - 1 && y <= clip.bottom() + 1)
                p.drawLine(clip.left(), y, clip.right(), y);
        }
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void DashboardCanvas::reflow_tiles(bool animate) {
    if (width() <= 0)
        return;

    for (const auto& item : layout_.items) {
        auto* tile = tile_for_id(item.instance_id);
        if (!tile)
            continue;
        QRect target = grid_to_rect(item.cell, layout_.cols, width(), layout_.row_h, layout_.margin);

        if (animate && tile->geometry() != target) {
            // Stop any in-flight animation on this tile to prevent fighting
            auto* existing = tile->findChild<QPropertyAnimation*>(QString(), Qt::FindDirectChildrenOnly);
            if (existing)
                existing->stop();

            auto* anim = new QPropertyAnimation(tile, "geometry", tile);
            anim->setDuration(200);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->setEndValue(target);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            tile->setGeometry(target);
        }
    }
}

void DashboardCanvas::update_canvas_height() {
    int rows = max_row(layout_.items);
    if (fit_exact_)
        rows = std::max(rows, layout_.rows); // the empty rows below are the page's too
    int h = layout_.margin + rows * (layout_.row_h + layout_.margin);
    int min_h = std::max(h, 400);
    setMinimumHeight(min_h);
    // Do NOT call setFixedHeight — that locks the canvas to exactly the content
    // height and prevents dragging tiles below the last row or resizing them
    // taller than the current window. Instead, give 3 extra empty rows below
    // the content so tiles can be dragged/resized into that space. The
    // QScrollArea (widgetResizable=false) shows a scrollbar when the canvas
    // exceeds the viewport, so extra canvas space is fully accessible.
    const int extra = 3 * (layout_.row_h + layout_.margin);
    resize(width(), min_h + extra);
}

void DashboardCanvas::update_placeholder(const GridCell& cell) {
    if (width() <= 0)
        return;
    QRect r = grid_to_rect(cell, layout_.cols, width(), layout_.row_h, layout_.margin);
    placeholder_->setGeometry(r);
    placeholder_->raise();
    if (dragging_tile_)
        dragging_tile_->raise();
    placeholder_->show();
}

void DashboardCanvas::hide_placeholder() {
    placeholder_->hide();
}

void DashboardCanvas::connect_tile(WidgetTile* tile) {
    connect(tile, &WidgetTile::drag_started, this, &DashboardCanvas::on_drag_started);
    connect(tile, &WidgetTile::drag_moved, this, &DashboardCanvas::on_drag_moved);
    connect(tile, &WidgetTile::drag_released, this, &DashboardCanvas::on_drag_released);
    connect(tile, &WidgetTile::resize_started, this, &DashboardCanvas::on_resize_started);
    connect(tile, &WidgetTile::resize_moved, this, &DashboardCanvas::on_resize_moved);
    connect(tile, &WidgetTile::resize_released, this, &DashboardCanvas::on_resize_released);
    connect(tile, &WidgetTile::close_requested, this, &DashboardCanvas::on_tile_close);

    // Per-instance widget config changes (gear-icon dialog) — persist into GridItem
    // and emit layout_changed so the DashboardScreen writes it through to sqlite.
    if (auto* content = tile->content_widget()) {
        const QString instance_id = tile->instance_id();
        connect(content, &widgets::BaseWidget::config_changed, this, [this, instance_id](const QJsonObject& cfg) {
            if (auto* item = item_for_id(instance_id)) {
                item->config = cfg;
                notify_layout_changed();
            }
        });
    }
}

WidgetTile* DashboardCanvas::tile_for_id(const QString& instance_id) const {
    for (auto* t : tiles_)
        if (t->instance_id() == instance_id)
            return t;
    return nullptr;
}

GridItem* DashboardCanvas::item_for_id(const QString& instance_id) {
    for (auto& i : layout_.items)
        if (i.instance_id == instance_id)
            return &i;
    return nullptr;
}

GridLayout DashboardCanvas::current_layout() const {
    GridLayout l = layout_;
    if (layout_.cols != canonical_cols_ && !designed_items_.isEmpty()) {
        // Same tiles, designed positions; configs are the live ones.
        QHash<QString, QJsonObject> configs;
        for (const auto& it : layout_.items)
            configs.insert(it.instance_id, it.config);
        l.items = designed_items_;
        for (auto& it : l.items)
            if (configs.contains(it.instance_id))
                it.config = configs.value(it.instance_id);
        l.cols = canonical_cols_;
    }
    return l;
}

void DashboardCanvas::notify_layout_changed(bool user_edit) {
    if (layout_.cols == canonical_cols_) {
        designed_items_ = layout_.items;
    } else if (user_edit) {
        // A drag or resize made while squeezed is kept: mapped back into the
        // canonical grid proportionally (x and w scale by canonical/current)
        // and separated, so it survives widening — without making the
        // squeezed column count the design's (that stuck at 9 for good).
        QVector<GridItem> mapped = layout_.items;
        const double k = double(canonical_cols_) / std::max(1, layout_.cols);
        for (auto& it : mapped) {
            it.cell.w = std::max(it.cell.min_w, int(std::lround(it.cell.w * k)));
            it.cell.x = int(std::lround(it.cell.x * k));
        }
        designed_items_ = refit_to_cols(mapped, canonical_cols_);
    } else {
        // Squeezed: positions are a temporary refit, but membership is real —
        // a tile added or closed now is added to / closed in the design too.
        QSet<QString> live;
        for (const auto& it : layout_.items)
            live.insert(it.instance_id);
        QSet<QString> designed;
        QVector<GridItem> kept;
        for (const auto& it : designed_items_)
            if (live.contains(it.instance_id)) {
                kept.append(it);
                designed.insert(it.instance_id);
            }
        for (const auto& it : layout_.items)
            if (!designed.contains(it.instance_id)) {
                GridItem add = it;
                add.cell = find_first_fit(kept, it.cell.w, it.cell.h, canonical_cols_);
                add.cell.min_w = it.cell.min_w;
                add.cell.min_h = it.cell.min_h;
                kept.append(add);
            }
        designed_items_ = kept;
    }
    emit layout_changed(layout_);
}

} // namespace finterm::screens

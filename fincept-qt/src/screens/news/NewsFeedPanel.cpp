#include "screens/news/NewsFeedPanel.h"

#include "core/logging/Logger.h"
#include <QApplication>
#include <QDateTime>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>

#if defined(Q_OS_WIN)
#    include <windows.h>
#endif

namespace fincept::screens {

namespace {

// Filter proxy that shows only source rows whose row index matches a given
// parity (0 = even, 1 = odd). Used to fan out the news feed across two
// columns on wide viewports while keeping a single source model.
class ParityProxy : public QSortFilterProxyModel {
  public:
    explicit ParityProxy(int parity, QObject* parent = nullptr)
        : QSortFilterProxyModel(parent), parity_(parity) {}

  protected:
    bool filterAcceptsRow(int source_row, const QModelIndex&) const override {
        return (source_row % 2) == parity_;
    }

  public:
    // Source row inserts can change which rows match the parity (a row
    // inserted at position 0 shifts everyone down). Refresh on every
    // structural change so the two columns stay in lockstep.
    void connect_source(QAbstractItemModel* src) {
        connect(src, &QAbstractItemModel::rowsInserted, this, [this] { invalidateFilter(); });
        connect(src, &QAbstractItemModel::rowsRemoved,  this, [this] { invalidateFilter(); });
        connect(src, &QAbstractItemModel::modelReset,   this, [this] { invalidateFilter(); });
        connect(src, &QAbstractItemModel::layoutChanged, this, [this] { invalidateFilter(); });
    }

  private:
    int parity_;
};

} // namespace

NewsFeedPanel::NewsFeedPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("newsFeedPanel");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Breaking banner (hidden by default)
    build_breaking_banner();
    root->addWidget(banner_widget_);

    // Use a QStackedWidget so skeleton and list can swap cleanly
    auto* stack = new QStackedWidget(this);

    // Source model + shared delegate. Two QListViews each wrap a parity
    // proxy so wide viewports get a 2-column feed without splitting the
    // underlying data.
    model_ = new NewsFeedModel(this);
    delegate_ = new NewsFeedDelegate(this);

    auto* pl = new ParityProxy(0, this);
    pl->setSourceModel(model_);
    pl->connect_source(model_);
    proxy_left_ = pl;

    auto* pr = new ParityProxy(1, this);
    pr->setSourceModel(model_);
    pr->connect_source(model_);
    proxy_right_ = pr;

    auto build_view = [&](QSortFilterProxyModel* proxy, const char* name) {
        auto* v = new QListView;
        v->setObjectName(name);
        v->setModel(proxy);
        v->setItemDelegate(delegate_);
        v->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        v->setSelectionMode(QAbstractItemView::NoSelection);
        v->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        v->setMouseTracking(true);
        v->setFrameShape(QFrame::NoFrame);
        v->setUniformItemSizes(true);
        v->viewport()->installEventFilter(this);
        v->viewport()->setMouseTracking(true);
        return v;
    };
    list_view_       = build_view(proxy_left_,  "newsFeedList");
    list_view_right_ = build_view(proxy_right_, "newsFeedListRight");

    // Restore saved source-column width.
    {
        const int saved = QSettings()
                              .value(QStringLiteral("news/source_col_width"),
                                     NewsFeedDelegate::kSourceColDefault)
                              .toInt();
        delegate_->set_source_col_width(saved);
    }

    feed_splitter_ = new QSplitter(Qt::Horizontal, stack);
    feed_splitter_->setObjectName("newsFeedSplitter");
    feed_splitter_->setChildrenCollapsible(false);
    feed_splitter_->setHandleWidth(1);
    feed_splitter_->addWidget(list_view_);
    feed_splitter_->addWidget(list_view_right_);
    feed_splitter_->setSizes({500, 500});
    list_view_right_->hide();  // start narrow; update_two_column_layout() toggles on resize

    // Skeleton loading widget
    build_skeleton();

    // Empty-state widget — shown when fetch yields zero articles
    empty_state_ = new QWidget(stack);
    empty_state_->setObjectName("newsEmptyState");
    {
        auto* layout = new QVBoxLayout(empty_state_);
        layout->setContentsMargins(24, 24, 24, 24);
        layout->setSpacing(8);
        layout->addStretch();
        auto* title = new QLabel(QStringLiteral("No articles available"), empty_state_);
        title->setObjectName("newsEmptyStateTitle");
        title->setAlignment(Qt::AlignCenter);
        auto* hint = new QLabel(
            QStringLiteral("Check your network connection and click Refresh to retry."),
            empty_state_);
        hint->setObjectName("newsEmptyStateHint");
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        layout->addWidget(title);
        layout->addWidget(hint);
        layout->addStretch();
    }

    stack->addWidget(feed_splitter_);    // index 0 = feed (left + optional right)
    stack->addWidget(skeleton_overlay_); // index 1 = skeleton
    stack->addWidget(empty_state_);      // index 2 = empty state
    stack->setCurrentIndex(0);
    stack_ = stack;

    root->addWidget(stack, 1);

    // Wire clicks from both columns through on_item_clicked. Each handler
    // converts the proxy index back to the source row before reading data.
    connect(list_view_,       &QListView::clicked, this, &NewsFeedPanel::on_item_clicked);
    connect(list_view_right_, &QListView::clicked, this, &NewsFeedPanel::on_item_clicked);

    // Scroll-to-bottom detection — fire from either column, whichever the
    // user is browsing. near_bottom() is idempotent for the consumer.
    connect(list_view_->verticalScrollBar(),       &QScrollBar::valueChanged,
            this, &NewsFeedPanel::check_scroll_position);
    connect(list_view_right_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &NewsFeedPanel::check_scroll_position);

    // Wait until after the panel is mounted to compute the layout — we need
    // a real viewport width before we can decide whether to show the right
    // column.
    QTimer::singleShot(0, this, [this]() { update_two_column_layout(); });

    // Banner dismiss timer
    banner_dismiss_timer_ = new QTimer(this);
    banner_dismiss_timer_->setSingleShot(true);
    connect(banner_dismiss_timer_, &QTimer::timeout, this, &NewsFeedPanel::clear_breaking);
}

void NewsFeedPanel::build_breaking_banner() {
    banner_widget_ = new QWidget(this);
    banner_widget_->setObjectName("newsBreakingBanner");
    banner_widget_->setFixedHeight(28);
    banner_widget_->hide();

    auto* layout = new QHBoxLayout(banner_widget_);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    banner_tag_ = new QLabel("FLASH", banner_widget_);
    banner_tag_->setObjectName("newsBreakingTag");
    banner_tag_->setFixedWidth(48);
    banner_tag_->setAlignment(Qt::AlignCenter);

    banner_headline_ = new QLabel(banner_widget_);
    banner_headline_->setObjectName("newsBreakingHeadline");

    banner_source_ = new QLabel(banner_widget_);
    banner_source_->setObjectName("newsBreakingSource");
    banner_source_->setFixedWidth(80);
    banner_source_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* dismiss_btn = new QPushButton("x", banner_widget_);
    dismiss_btn->setObjectName("newsBreakingDismiss");
    dismiss_btn->setFixedSize(20, 20);
    connect(dismiss_btn, &QPushButton::clicked, this, &NewsFeedPanel::clear_breaking);

    layout->addWidget(banner_tag_);
    layout->addWidget(banner_headline_, 1);
    layout->addWidget(banner_source_);
    layout->addWidget(dismiss_btn);
}

void NewsFeedPanel::show_breaking(const QVector<services::NewsCluster>& breaking_clusters) {
    if (breaking_clusters.isEmpty())
        return;

    const auto& cluster = breaking_clusters.first();
    const auto& lead = cluster.lead_article;
    int64_t now = QDateTime::currentSecsSinceEpoch();

    // Global cooldown
    if (now < global_cooldown_until_)
        return;

    // Per-event deduplication
    QString key = lead.headline.left(50).toLower();
    if (is_banner_duplicate(key))
        return;

    // Show banner
    QString tag = lead.priority == services::Priority::FLASH ? "FLASH" : "BREAKING";
    banner_tag_->setText(tag);
    banner_headline_->setText(lead.headline);
    banner_source_->setText(lead.source.toUpper());
    banner_widget_->show();

    // Record for dedup
    recent_banners_.append({key, now});
    global_cooldown_until_ = now + BANNER_GLOBAL_COOLDOWN_SEC;

    // Cleanup old entries
    QVector<BreakingEntry> fresh;
    for (const auto& entry : recent_banners_) {
        if (now - entry.shown_at < BANNER_DEDUP_WINDOW_SEC)
            fresh.append(entry);
    }
    recent_banners_ = fresh;

    // Auto-dismiss: 60s for FLASH, 30s for BREAKING
    int dismiss_ms = (lead.priority == services::Priority::FLASH) ? 60000 : 30000;
    banner_dismiss_timer_->start(dismiss_ms);

    // Sound notification with 5-min cooldown
    if (now - last_sound_at_ >= SOUND_COOLDOWN_SEC) {
        last_sound_at_ = now;
#if defined(Q_OS_WIN)
        MessageBeep(MB_ICONEXCLAMATION);
#else
        QApplication::beep();
#endif
    }

    LOG_INFO("NewsFeedPanel", QString("Breaking banner: %1").arg(lead.headline.left(60)));
}

void NewsFeedPanel::clear_breaking() {
    banner_widget_->hide();
    banner_dismiss_timer_->stop();
}

bool NewsFeedPanel::is_banner_duplicate(const QString& headline) const {
    int64_t now = QDateTime::currentSecsSinceEpoch();
    for (const auto& entry : recent_banners_) {
        if (now - entry.shown_at < BANNER_DEDUP_WINDOW_SEC && entry.headline_key == headline)
            return true;
    }
    return false;
}

void NewsFeedPanel::build_skeleton() {
    skeleton_overlay_ = new QWidget(this);
    skeleton_overlay_->setObjectName("newsSkeletonOverlay");
    // No hide() — managed by QStackedWidget

    auto* skel_layout = new QVBoxLayout(skeleton_overlay_);
    skel_layout->setContentsMargins(0, 0, 0, 0);
    skel_layout->setSpacing(1);

    for (int i = 0; i < 20; ++i) {
        auto* row = new QWidget(skeleton_overlay_);
        row->setFixedHeight(26);
        row->setObjectName("newsSkeletonRow");
        skel_layout->addWidget(row);
    }
    skel_layout->addStretch();

    skeleton_anim_timer_ = new QTimer(this);
    skeleton_anim_timer_->setInterval(500);
    connect(skeleton_anim_timer_, &QTimer::timeout, this, [this]() {
        skeleton_phase_ = (skeleton_phase_ + 1) % 2;
        if (skeleton_overlay_->isVisible()) {
            // Toggle between two opacity levels via property
            QString opacity = skeleton_phase_ == 0 ? "0.04" : "0.08";
            skeleton_overlay_->setStyleSheet(
                QString("QWidget#newsSkeletonRow { background: rgba(255,255,255,%1); }").arg(opacity));
        }
    });
}

void NewsFeedPanel::remove_skeleton() {
    stack_->setCurrentWidget(feed_splitter_);
    skeleton_anim_timer_->stop();
}

void NewsFeedPanel::set_loading(bool loading) {
    is_loading_ = loading;
    if (loading) {
        stack_->setCurrentWidget(skeleton_overlay_);
        skeleton_anim_timer_->start();
    } else {
        remove_skeleton();
    }
}

void NewsFeedPanel::set_empty_state(bool empty) {
    if (is_loading_)
        return; // skeleton wins until loading completes
    if (empty) {
        stack_->setCurrentWidget(empty_state_);
    } else if (stack_->currentWidget() == empty_state_) {
        stack_->setCurrentWidget(feed_splitter_);
    }
}

void NewsFeedPanel::scroll_to(const QString& article_id) {
    auto src_idx = model_->index_for_article(article_id);
    if (!src_idx.isValid()) return;
    // Even source rows live in the left proxy, odd ones on the right.
    auto* proxy = (src_idx.row() % 2 == 0) ? proxy_left_ : proxy_right_;
    auto* view  = (src_idx.row() % 2 == 0) ? list_view_  : list_view_right_;
    auto proxy_idx = proxy->mapFromSource(src_idx);
    if (proxy_idx.isValid())
        view->scrollTo(proxy_idx, QAbstractItemView::EnsureVisible);
}

void NewsFeedPanel::set_selected(const QString& article_id) {
    model_->set_selected_id(article_id);
    scroll_to(article_id);
}

void NewsFeedPanel::select_next() {
    // Keyboard navigation moves through the source model directly so it
    // sweeps both columns in chronological order rather than skipping rows.
    auto current_proxy = list_view_->currentIndex();
    int current_src = source_row_for(current_proxy);
    int next_row = (current_src >= 0) ? current_src + 1 : 0;
    if (next_row < model_->rowCount()) {
        auto src_idx = model_->index(next_row, 0);
        // bring focus to the matching column for arrow-key continuity
        auto* view  = (next_row % 2 == 0) ? list_view_  : list_view_right_;
        auto* proxy = (next_row % 2 == 0) ? proxy_left_ : proxy_right_;
        last_active_view_ = view;
        view->setCurrentIndex(proxy->mapFromSource(src_idx));
        on_item_clicked(proxy->mapFromSource(src_idx));
    }
}

void NewsFeedPanel::select_previous() {
    auto current_proxy = list_view_->currentIndex();
    int current_src = source_row_for(current_proxy);
    int prev_row = (current_src > 0) ? current_src - 1 : 0;
    if (prev_row >= 0 && prev_row < model_->rowCount()) {
        auto src_idx = model_->index(prev_row, 0);
        auto* view  = (prev_row % 2 == 0) ? list_view_  : list_view_right_;
        auto* proxy = (prev_row % 2 == 0) ? proxy_left_ : proxy_right_;
        last_active_view_ = view;
        view->setCurrentIndex(proxy->mapFromSource(src_idx));
        on_item_clicked(proxy->mapFromSource(src_idx));
    }
}

void NewsFeedPanel::on_item_clicked(const QModelIndex& proxy_index) {
    const int src_row = source_row_for(proxy_index);
    if (src_row < 0)
        return;

    // Track which column the user just touched so current_article() knows
    // which currentIndex to prefer when both columns hold a row.
    if (proxy_index.model() == proxy_left_)  last_active_view_ = list_view_;
    if (proxy_index.model() == proxy_right_) last_active_view_ = list_view_right_;

    auto article = model_->article_at(src_row);
    model_->set_selected_id(article.id);
    model_->mark_seen(article.id);

    emit article_clicked(article);

    if (model_->view_mode() == "CLUSTERS") {
        auto cluster = model_->cluster_at(src_row);
        emit cluster_clicked(cluster);
    }
}

int NewsFeedPanel::source_row_for(const QModelIndex& proxy_index) const {
    if (!proxy_index.isValid())
        return -1;
    if (proxy_index.model() == proxy_left_)
        return proxy_left_->mapToSource(proxy_index).row();
    if (proxy_index.model() == proxy_right_)
        return proxy_right_->mapToSource(proxy_index).row();
    return proxy_index.row(); // already a source index
}

services::NewsArticle NewsFeedPanel::current_article() const {
    // Both columns can carry a non-empty currentIndex from earlier keyboard
    // navigation. We tracked the most recently-touched view in
    // last_active_view_ (updated by on_item_clicked and select_*); prefer
    // that one. Fall back to whichever has any valid index.
    auto from = [this](QListView* v) {
        if (!v) return -1;
        return source_row_for(v->currentIndex());
    };
    int src_row = -1;
    if (last_active_view_)
        src_row = from(last_active_view_);
    if (src_row < 0) src_row = from(list_view_);
    if (src_row < 0) src_row = from(list_view_right_);
    if (src_row < 0) return {};
    return model_->article_at(src_row);
}

void NewsFeedPanel::mark_visible_seen(QSet<QString>& out_new) {
    auto walk = [&](QListView* v, QSortFilterProxyModel* proxy) {
        if (!v || !v->isVisible() || !proxy) return;
        const QRect vp = v->viewport()->rect();
        const int n = proxy->rowCount();
        for (int i = 0; i < n; ++i) {
            const auto pidx = proxy->index(i, 0);
            if (v->visualRect(pidx).intersects(vp)) {
                const int src_row = proxy->mapToSource(pidx).row();
                const QString id = model_->article_at(src_row).id;
                if (!id.isEmpty()) {
                    model_->mark_seen(id);
                    out_new.insert(id);
                }
            }
        }
    };
    walk(list_view_,       proxy_left_);
    walk(list_view_right_, proxy_right_);
}

void NewsFeedPanel::resizeEvent(QResizeEvent* ev) {
    QWidget::resizeEvent(ev);
    update_two_column_layout();
}

void NewsFeedPanel::update_two_column_layout() {
    if (!list_view_right_) return;
    const bool wide = width() >= kWideViewportThreshold;
    const bool was_wide = list_view_right_->isVisible();
    list_view_right_->setVisible(wide);
    // Only seed the split when transitioning narrow→wide. On a straight
    // resize we let QSplitter scale proportionally so the user's own
    // drag-adjusted split isn't wiped out. setSizes must always pass one
    // entry per child — QSplitter zero-fills missing entries, which would
    // collapse the middle widget if we only gave it two sizes after the
    // insertWidget(1, middle) shifted the right list to index 2.
    if (wide && !was_wide) {
        const bool has_middle = middle_widget_ != nullptr;
        if (has_middle && middle_widget_->isVisible()) {
            const int detail_w = 420;
            const int side = (width() - detail_w) / 2;
            feed_splitter_->setSizes({side, detail_w, side});
        } else if (has_middle) {
            const int half = width() / 2;
            feed_splitter_->setSizes({half, 0, width() - half});
        } else {
            const int half = width() / 2;
            feed_splitter_->setSizes({half, width() - half});
        }
    }
}

void NewsFeedPanel::set_middle_widget(QWidget* widget) {
    if (middle_widget_ == widget)
        return;
    if (middle_widget_) {
        // Detach the prior widget. The caller retains the pointer; the
        // widget's Qt parent is set to null here, so the caller MUST keep
        // a reference (or reparent immediately) or the widget will leak.
        middle_widget_->setParent(nullptr);
        middle_widget_ = nullptr;
    }
    if (widget) {
        // Insert between left list (index 0) and right list (index 1).
        feed_splitter_->insertWidget(1, widget);
        middle_widget_ = widget;
        // Re-seed sizes so the new pane gets a sensible width. If we're in
        // narrow mode the right column stays hidden; the detail pane takes
        // the right slot anyway because the splitter's hidden child shrinks
        // to zero width.
        const bool wide = width() >= kWideViewportThreshold;
        const int detail_w = std::min(420, std::max(280, width() / 3));
        if (wide) {
            const int side = (width() - detail_w) / 2;
            feed_splitter_->setSizes({side, detail_w, side});
        } else {
            // Narrow: split between left list and middle widget (right list
            // is hidden by update_two_column_layout).
            feed_splitter_->setSizes({width() - detail_w, detail_w, 0});
        }
    }
}

bool NewsFeedPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (!delegate_) return QWidget::eventFilter(obj, ev);

    // Drag handle works in either column's viewport.
    QWidget* viewport = nullptr;
    if (list_view_       && obj == list_view_->viewport())       viewport = list_view_->viewport();
    if (list_view_right_ && obj == list_view_right_->viewport()) viewport = list_view_right_->viewport();
    if (!viewport) return QWidget::eventFilter(obj, ev);

    // Source/headline boundary lives at kPreSourceX + source_col_width()
    // from the viewport left edge (mirrors paint_wire_row's x-advance).
    const int boundary_x = NewsFeedDelegate::kPreSourceX + delegate_->source_col_width();
    auto near_boundary = [&](int x) {
        return std::abs(x - boundary_x) <= kSourceColDragHotzone;
    };

    auto repaint_both = [this] {
        if (list_view_)       list_view_->viewport()->update();
        if (list_view_right_) list_view_right_->viewport()->update();
    };

    if (ev->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(ev);
        const int x = me->pos().x();
        if (dragging_source_col_) {
            const int delta = x - drag_start_x_;
            delegate_->set_source_col_width(drag_start_width_ + delta);
            repaint_both();
            return true;
        }
        viewport->setCursor(near_boundary(x) ? Qt::SplitHCursor : Qt::ArrowCursor);
        return false;
    }
    if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton && near_boundary(me->pos().x())) {
            dragging_source_col_ = true;
            drag_start_x_        = me->pos().x();
            drag_start_width_    = delegate_->source_col_width();
            viewport->setCursor(Qt::SplitHCursor);
            return true; // swallow so the list doesn't select
        }
        return false;
    }
    if (ev->type() == QEvent::MouseButtonRelease) {
        if (dragging_source_col_) {
            dragging_source_col_ = false;
            QSettings().setValue(QStringLiteral("news/source_col_width"),
                                 delegate_->source_col_width());
            return true;
        }
        return false;
    }
    if (ev->type() == QEvent::Leave) {
        if (!dragging_source_col_)
            viewport->setCursor(Qt::ArrowCursor);
        return false;
    }
    return QWidget::eventFilter(obj, ev);
}

void NewsFeedPanel::check_scroll_position() {
    auto check = [this](QListView* v) {
        if (!v || !v->isVisible()) return;
        auto* sb = v->verticalScrollBar();
        if (!sb) return;
        int remaining = sb->maximum() - sb->value();
        if (remaining < 200 && sb->maximum() > 0)
            emit near_bottom();
    };
    check(list_view_);
    check(list_view_right_);
}

} // namespace fincept::screens

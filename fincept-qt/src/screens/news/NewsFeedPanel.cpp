#include "screens/news/NewsFeedPanel.h"

#include "core/logging/Logger.h"
#include <QApplication>
#include <QDateTime>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>

#include <cstdlib>

#if defined(Q_OS_WIN)
#    include <windows.h>
#endif

namespace fincept::screens {

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

    // Source model + delegate. One headline list, bound straight to the
    // model — the feed used to fan articles across two columns via parity
    // proxies, which meant chronological order read down-then-across and a
    // story's neighbours were never adjacent.
    model_ = new NewsFeedModel(this);
    delegate_ = new NewsFeedDelegate(this);

    list_view_ = new QListView;
    list_view_->setObjectName("newsFeedList");
    list_view_->setModel(model_);
    list_view_->setItemDelegate(delegate_);
    list_view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_view_->setSelectionMode(QAbstractItemView::NoSelection);
    list_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_view_->setMouseTracking(true);
    list_view_->setFrameShape(QFrame::NoFrame);
    list_view_->setUniformItemSizes(true);
    list_view_->viewport()->installEventFilter(this);
    list_view_->viewport()->setMouseTracking(true);

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
    // A QSplitter handle's grab area is exactly handleWidth. This was 1px —
    // and painted in the background colour — so the divider between the
    // headline list and the reading pane was invisible and, in practice,
    // impossible to hit with a mouse. kSplitHandleW gives a real target; the
    // stylesheet insets the painted strip with a margin so it still *reads*
    // as a hairline divider while staying draggable in both directions.
    feed_splitter_->setHandleWidth(kSplitHandleW);
    feed_splitter_->addWidget(list_view_);

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
        empty_state_title_ = new QLabel(QStringLiteral("No articles available"), empty_state_);
        empty_state_title_->setObjectName("newsEmptyStateTitle");
        empty_state_title_->setAlignment(Qt::AlignCenter);
        empty_state_hint_ = new QLabel(
            QStringLiteral("Check your network connection and click Refresh to retry."),
            empty_state_);
        empty_state_hint_->setObjectName("newsEmptyStateHint");
        empty_state_hint_->setAlignment(Qt::AlignCenter);
        empty_state_hint_->setWordWrap(true);
        layout->addWidget(empty_state_title_);
        layout->addWidget(empty_state_hint_);
        layout->addStretch();
    }

    stack->addWidget(feed_splitter_);    // index 0 = feed (headline list + detail)
    stack->addWidget(skeleton_overlay_); // index 1 = skeleton
    stack->addWidget(empty_state_);      // index 2 = empty state
    stack->setCurrentIndex(0);
    stack_ = stack;

    root->addWidget(stack, 1);

    connect(list_view_, &QListView::clicked, this, &NewsFeedPanel::on_item_clicked);

    connect(list_view_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &NewsFeedPanel::check_scroll_position);

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

void NewsFeedPanel::set_empty_state_message(const QString& title, const QString& hint) {
    if (empty_state_title_) {
        empty_state_title_->setText(title.isEmpty()
            ? QStringLiteral("No articles available")
            : title);
    }
    if (empty_state_hint_) {
        empty_state_hint_->setText(hint.isEmpty()
            ? QStringLiteral("Check your network connection and click Refresh to retry.")
            : hint);
    }
}

void NewsFeedPanel::scroll_to(const QString& article_id) {
    auto idx = model_->index_for_article(article_id);
    if (idx.isValid())
        list_view_->scrollTo(idx, QAbstractItemView::EnsureVisible);
}

void NewsFeedPanel::set_selected(const QString& article_id) {
    model_->set_selected_id(article_id);
    scroll_to(article_id);
}

void NewsFeedPanel::select_next() {
    const int current = list_view_->currentIndex().row();
    const int next_row = (current >= 0) ? current + 1 : 0;
    if (next_row < model_->rowCount()) {
        const auto idx = model_->index(next_row, 0);
        list_view_->setCurrentIndex(idx);
        on_item_clicked(idx);
    }
}

void NewsFeedPanel::select_previous() {
    const int current = list_view_->currentIndex().row();
    const int prev_row = (current > 0) ? current - 1 : 0;
    if (prev_row < model_->rowCount()) {
        const auto idx = model_->index(prev_row, 0);
        list_view_->setCurrentIndex(idx);
        on_item_clicked(idx);
    }
}

void NewsFeedPanel::on_item_clicked(const QModelIndex& index) {
    const int row = index.row();
    if (!index.isValid() || row < 0)
        return;

    auto article = model_->article_at(row);
    model_->set_selected_id(article.id);
    model_->mark_seen(article.id);

    emit article_clicked(article);

    if (model_->view_mode() == "CLUSTERS") {
        auto cluster = model_->cluster_at(row);
        emit cluster_clicked(cluster);
    }
}

services::NewsArticle NewsFeedPanel::current_article() const {
    const auto idx = list_view_->currentIndex();
    if (!idx.isValid())
        return {};
    return model_->article_at(idx.row());
}

void NewsFeedPanel::mark_visible_seen(QSet<QString>& out_new) {
    if (!list_view_ || !list_view_->isVisible())
        return;
    const QRect vp = list_view_->viewport()->rect();
    const int n = model_->rowCount();
    for (int i = 0; i < n; ++i) {
        const auto idx = model_->index(i, 0);
        if (!list_view_->visualRect(idx).intersects(vp))
            continue;
        const QString id = model_->article_at(i).id;
        if (!id.isEmpty()) {
            model_->mark_seen(id);
            out_new.insert(id);
        }
    }
}

void NewsFeedPanel::set_detail_widget(QWidget* widget) {
    if (detail_widget_ == widget)
        return;
    if (detail_widget_) {
        detail_widget_->setParent(nullptr);
        detail_widget_ = nullptr;
    }
    if (!widget)
        return;

    feed_splitter_->addWidget(widget);
    detail_widget_ = widget;
    // Actual sizes are seeded in showEvent(), once there is a real width.
}

void NewsFeedPanel::showEvent(QShowEvent* ev) {
    QWidget::showEvent(ev);
    if (split_seeded_ || !detail_widget_ || width() <= 0)
        return;
    split_seeded_ = true;
    // Even split: with the INTEL drawer taking a fifth of the content area,
    // headline list and reading pane are equal halves of what remains, giving
    // an intel : headlines : article ratio of 1 : 2 : 2. A pure ratio,
    // deliberately: a
    // pixel floor on the reading pane would go negative on the left side if
    // this first show reports a small width, and the split would never be
    // re-seeded to recover. Seeded exactly once — from here the handle
    // belongs to the user; we never write sizes again and never intercept
    // splitterMoved.
    const int detail_w = width() / 2;
    feed_splitter_->setSizes({width() - detail_w, detail_w});
}

bool NewsFeedPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (!delegate_) return QWidget::eventFilter(obj, ev);

    if (!list_view_ || obj != list_view_->viewport())
        return QWidget::eventFilter(obj, ev);
    QWidget* viewport = list_view_->viewport();

    // Source/headline boundary lives at kPreSourceX + source_col_width()
    // from the viewport left edge (mirrors paint_wire_row's x-advance).
    const int boundary_x = NewsFeedDelegate::kPreSourceX + delegate_->source_col_width();
    auto near_boundary = [&](int x) {
        return std::abs(x - boundary_x) <= kSourceColDragHotzone;
    };

    if (ev->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(ev);
        const int x = me->pos().x();
        if (dragging_source_col_) {
            const int delta = x - drag_start_x_;
            delegate_->set_source_col_width(drag_start_width_ + delta);
            viewport->update();
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
    if (!list_view_ || !list_view_->isVisible())
        return;
    auto* sb = list_view_->verticalScrollBar();
    if (!sb)
        return;
    const int remaining = sb->maximum() - sb->value();
    if (remaining < 200 && sb->maximum() > 0)
        emit near_bottom();
}

} // namespace fincept::screens

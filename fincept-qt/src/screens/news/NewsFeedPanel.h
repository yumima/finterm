#pragma once
#include "screens/news/NewsFeedDelegate.h"
#include "screens/news/NewsFeedModel.h"
#include "services/news/NewsClusterService.h"
#include "services/news/NewsService.h"

#include <QLabel>
#include <QListView>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace fincept::screens {

class NewsFeedPanel : public QWidget {
    Q_OBJECT
  public:
    explicit NewsFeedPanel(QWidget* parent = nullptr);

    NewsFeedModel* model() { return model_; }
    QListView* list_view() { return list_view_; }

    // Docks a widget as the right half of the feed's horizontal splitter,
    // beside the headline list. Used by NewsScreen to park the article
    // detail pane there permanently. Pass nullptr to remove it.
    void set_detail_widget(QWidget* widget);

    // Walks the headline list's visible viewport and mark_seens every
    // article currently on screen. Appends the just-marked article ids to
    // `out_new` so callers can flush them to persistence.
    void mark_visible_seen(QSet<QString>& out_new);

    // Currently-selected article. Returns a default-constructed article if
    // no row is active.
    services::NewsArticle current_article() const;

    void show_breaking(const QVector<services::NewsCluster>& breaking_clusters);
    void clear_breaking();
    void set_loading(bool loading);
    /// When loading has finished but no articles arrived, show a non-blocking
    /// empty-state message. The default text reflects a network/refresh
    /// problem; callers can override via set_empty_state_message() before
    /// calling this with `true` to show context-specific copy (e.g., for
    /// the PTF filter producing no matches).
    /// Calling with `false` (or any successful set_wire_articles) hides it.
    void set_empty_state(bool empty);
    /// Replace the empty-state title + hint copy. Reset to defaults by
    /// passing empty strings.
    void set_empty_state_message(const QString& title, const QString& hint);
    void scroll_to(const QString& article_id);
    void set_selected(const QString& article_id);
    void select_next();
    void select_previous();

  signals:
    void article_clicked(const services::NewsArticle& article);
    void cluster_clicked(const services::NewsCluster& cluster);
    void near_bottom();

  protected:
    // Mouse drag on the source/headline boundary inside the list viewport
    // resizes the source column. Cursor switches to SplitHCursor on hover;
    // width is persisted via QSettings(news/source_col_width).
    bool eventFilter(QObject* obj, QEvent* ev) override;
    // Seeds the headline/detail split on the first show that has a real
    // width. Construction-time width() is 0, so a pixel seed there means
    // nothing.
    void showEvent(QShowEvent* ev) override;

  private slots:
    void on_item_clicked(const QModelIndex& index);
    void check_scroll_position();

  private:
    void build_breaking_banner();
    void build_skeleton();
    void remove_skeleton();
    bool is_banner_duplicate(const QString& headline) const;

    QStackedWidget* stack_ = nullptr;
    QListView* list_view_ = nullptr;        // the headline list
    QSplitter* feed_splitter_ = nullptr;    // horizontal: headline list | detail pane
    QWidget* detail_widget_ = nullptr;      // detail pane docked on the right
    NewsFeedModel* model_ = nullptr;
    NewsFeedDelegate* delegate_ = nullptr;
    bool split_seeded_ = false;             // one-shot guard for showEvent()

    // Breaking banner
    QWidget* banner_widget_ = nullptr;
    QLabel* banner_tag_ = nullptr;
    QLabel* banner_headline_ = nullptr;
    QLabel* banner_source_ = nullptr;
    QTimer* banner_dismiss_timer_ = nullptr;

    struct BreakingEntry {
        QString headline_key;
        int64_t shown_at = 0;
    };
    QVector<BreakingEntry> recent_banners_;
    int64_t global_cooldown_until_ = 0;
    static constexpr int BANNER_DEDUP_WINDOW_SEC = 1800;   // 30 min
    static constexpr int BANNER_GLOBAL_COOLDOWN_SEC = 120; // 2 min
    static constexpr int SOUND_COOLDOWN_SEC = 300;         // 5 min
    int64_t last_sound_at_ = 0;

    // Skeleton loading
    QWidget* skeleton_overlay_ = nullptr;
    QTimer* skeleton_anim_timer_ = nullptr;
    int skeleton_phase_ = 0;
    bool is_loading_ = false;

    // Empty-state widget (shown when loading is done + no articles). The
    // title + hint labels are kept as members so set_empty_state_message()
    // can rewrite them for context-specific copy (e.g., PTF filter misses).
    QWidget* empty_state_ = nullptr;
    QLabel* empty_state_title_ = nullptr;
    QLabel* empty_state_hint_ = nullptr;

    // Source-column drag-resize state
    bool dragging_source_col_ = false;
    int  drag_start_x_        = 0;
    int  drag_start_width_    = 0;
    static constexpr int kSourceColDragHotzone = 4; // px on each side of boundary
};

} // namespace fincept::screens

#pragma once
#include "screens/news/NewsBriefFormat.h"
#include "services/news/NewsCorrelationService.h"
#include "services/news/NewsMonitorService.h"
#include "services/news/NewsNlpService.h"
#include "services/news/NewsService.h"

#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

namespace fincept::screens {

/// Article reading pane — the permanent right half of the news screen,
/// beside the headline list. It is never hidden: with no article selected
/// it shows a "Select an article" placeholder, so the layout never shifts
/// under the user and there is no state where the pane has to be summoned.
class NewsDetailPanel : public QWidget {
    Q_OBJECT
  public:
    explicit NewsDetailPanel(QWidget* parent = nullptr);

    void show_article(const services::NewsArticle& article);
    void show_analysis(const services::NewsAnalysis& analysis);
    void show_analysis_error(const QString& message);
    void show_related(const QVector<services::NewsArticle>& related);
    void show_monitor_matches(const QVector<QPair<services::NewsMonitor, QStringList>>& matches);
    void show_entities(const services::EntityResult& entities);
    void show_infrastructure(const QVector<services::InfrastructureItem>& items);
    void clear();

    /// TL;DR — show a transient "loading…" placeholder while the
    /// summarize_headlines call is in flight. Renders above the article
    /// summary so it stays visible regardless of which article is selected.
    /// @param title section header — "TL;DR" (concise, current view) or
    ///        "DIGEST" (broader full-feed read), so the reading-pane label
    ///        reflects which brief was requested.
    void show_tldr_loading(const QString& title = QStringLiteral("TL;DR"));
    /// Populate the TL;DR section with the AI-generated brief. Pass an
    /// empty string to hide the section (e.g., on backend failure).
    /// A brief that could not be produced (empty view, model unavailable).
    /// Rendered in the brief section WITHOUT taking the pane from an open
    /// story — see the .cpp.
    void show_brief_unavailable(const QString& text, const QString& title);
    void show_tldr_summary(const QString& text, const QString& title = QStringLiteral("TL;DR"));
    /// Hide the TL;DR section (called when a new article is opened so the
    /// summary doesn't outlive its relevance to the feed snapshot).
    void hide_tldr();

  signals:
    void analyze_requested(const QString& article_url);
    void related_article_clicked(const services::NewsArticle& article);
    void open_in_browser(const QString& url);
    void copy_url(const QString& url);
    void bookmark_requested(const services::NewsArticle& article);

  private:
    QWidget* build_empty_state();
    QWidget* build_content_view();
    /// Shows the ARTICLE block only when an article is actually open and no
    /// brief is holding the pane. The TL;DR paths force the content page,
    /// which would otherwise reveal a permanent "Loading article…" under a
    /// brief with nothing selected.
    void sync_article_block();
    /// Show or hide everything that belongs to the open story — headline,
    /// meta row, summary, action buttons, body, and every per-story section
    /// (AI ANALYSIS, RELATED, MONITORS, ENTITIES, INFRASTRUCTURE).
    ///
    /// The reading pane shows one thing at a time. A brief is a read of the
    /// feed, not of whichever story happened to be open when the user asked
    /// for it, and leaving that story's AI ANALYSIS sitting under a TL;DR
    /// reads as though the brief were part of the analysis — which is exactly
    /// how it was reported. The opposite direction is already handled:
    /// opening an article calls hide_tldr().
    void set_article_visible(bool visible);
    /// Hide one per-story section and forget it, so it is not restored when a
    /// brief gives the pane back.
    void conceal_article_section(QWidget* section);
    /// Show one per-story section — unless a brief currently owns the pane, in
    /// which case it stays hidden and joins sections_hidden_by_brief_ so it
    /// reappears with the story.
    void reveal_article_section(QWidget* section);
    /// Per-story sections that were on screen when a brief took the pane (or
    /// were populated while it held it). set_article_visible(true) restores
    /// exactly these, so hiding and showing a brief is symmetric.
    QVector<QWidget*> sections_hidden_by_brief_;

    /// True between asking for a brief and rendering it. If the user opens an
    /// article while the request is in flight the brief is stale — the same
    /// reason hide_tldr() exists — so the late answer is dropped rather than
    /// pulling the pane out from under the story they just started reading.
    bool brief_pending_ = false;
    /// True while a TL;DR / DIGEST owns the reading pane. Set by the
    /// show_tldr_* paths, cleared by show_article() and hide_tldr().
    bool brief_owns_pane_ = false;

    // TL;DR section — rendered ABOVE the article body so it stays visible
    // when the user scrolls through the feed. Populated by NewsScreen via
    // show_tldr_summary() after NewsService::summarize_headlines() returns.
    QWidget* tldr_section_ = nullptr;
    QLabel* tldr_title_ = nullptr;  ///< section header — "TL;DR" or "DIGEST"
    QLabel* tldr_label_ = nullptr;

    // Lower half of a brief: the per-category breakdown. show_tldr_summary()
    // splits the model's output on kCategoryMarker and routes the tail here,
    // so both the one-shot TL;DR and the streaming DIGEST fill it without
    // either call site knowing about the split.
    QWidget* tldr_detail_section_ = nullptr;
    QLabel* tldr_detail_title_ = nullptr;
    QLabel* tldr_detail_label_ = nullptr;

    // Article section
    /// The loose article widgets that are not inside a section container.
    /// Held so a brief can collapse the whole story, not just parts of it.
    QWidget* article_meta_row_   = nullptr;
    QWidget* article_actions_    = nullptr;
    QWidget* article_separator_  = nullptr;
    QLabel* headline_label_ = nullptr;
    QLabel* priority_badge_ = nullptr;
    QLabel* sentiment_badge_ = nullptr;
    QLabel* tier_badge_ = nullptr;
    QLabel* category_label_ = nullptr;
    QLabel* source_label_ = nullptr;
    QLabel* time_label_ = nullptr;
    QLabel* summary_label_ = nullptr;
    QLabel* impact_label_ = nullptr;
    QLabel* tickers_label_ = nullptr;

    // Article body section — sits BELOW the action button row. Populated by
    // NewsService::extract_article_body() on each show_article(). Reused
    // across articles; the body label is replaced on each load and the
    // "loading…" state is shown while the extractor runs.
    //
    // body_label_ is a QTextBrowser, not a QLabel: QLabel's rich-text path
    // is a documented subset that silently drops block-level CSS like
    // `text-indent` on <p>, which is exactly what we need for first-line
    // paragraph indent. QTextBrowser uses the full QTextDocument render
    // pipeline and honors the full Qt rich-text subset. Configured for
    // static display (read-only, no frame, scrollbars off — outer scroll
    // area handles overflow, auto-resized to document height).
    QWidget*       body_section_ = nullptr;
    QLabel*        body_title_   = nullptr; // "ARTICLE" header, hidden on failure
    QLabel*        body_status_  = nullptr; // "loading…" / error message
    QTextBrowser*  body_label_   = nullptr; // the extracted prose itself
    // Generation token so a stale extraction callback for a previous article
    // doesn't repaint a body we no longer care about (e.g. user clicked B
    // while A was still extracting). Bumped on every show_article().
    int      body_gen_     = 0;
    // Plain-text body cached from the most recent successful extraction —
    // the SAVE button writes this to disk alongside the headline + summary
    // so saved articles include the full story, not just the metadata.
    // Cleared on each new article (before the next fetch resolves).
    QString  current_body_text_;

    // AI analysis section
    QWidget* analysis_section_ = nullptr;
    QLabel* ai_summary_ = nullptr;
    QLabel* ai_sentiment_ = nullptr;
    QLabel* ai_urgency_ = nullptr;
    QVBoxLayout* key_points_layout_ = nullptr;
    QVBoxLayout* risk_layout_ = nullptr;
    QVBoxLayout* topics_layout_ = nullptr;
    QPushButton* analyze_btn_ = nullptr;
    QPushButton* listen_btn_ = nullptr;
    QTimer* analyze_timeout_ = nullptr;

    // Monitor matches section
    QWidget* monitor_section_ = nullptr;
    QVBoxLayout* monitor_matches_layout_ = nullptr;

    // Related articles section
    QWidget* related_section_ = nullptr;
    QVBoxLayout* related_layout_ = nullptr;

    // Translate button
    QPushButton* translate_btn_ = nullptr;

    // Entities section
    QWidget* entities_section_ = nullptr;
    QVBoxLayout* entities_detail_layout_ = nullptr;

    // Infrastructure section
    QWidget* infra_section_ = nullptr;
    QVBoxLayout* infra_layout_ = nullptr;

    // Action buttons
    QPushButton* open_btn_ = nullptr;
    QPushButton* copy_btn_ = nullptr;
    QPushButton* copy_title_btn_ = nullptr;
    QPushButton* save_btn_ = nullptr;
    QPushButton* bookmark_btn_ = nullptr;

    QStackedWidget* stack_ = nullptr;
    services::NewsArticle current_article_;
    bool has_article_ = false;
    bool listening_ = false;  // local TTS read-aloud toggle
};

} // namespace fincept::screens

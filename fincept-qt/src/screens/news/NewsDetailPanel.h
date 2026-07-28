#pragma once
#include "services/news/NewsCorrelationService.h"
#include "services/news/NewsMonitorService.h"
#include "services/news/NewsNlpService.h"
#include "services/news/NewsService.h"

#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QStringView>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

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
    void show_tldr_summary(const QString& text, const QString& title = QStringLiteral("TL;DR"));
    /// Hide the TL;DR section (called when a new article is opened so the
    /// summary doesn't outlive its relevance to the feed snapshot).
    void hide_tldr();

    /// Separates the top brief from the per-category detail in a model brief.
    /// Both prompts are told to emit this on its own line; split_brief()
    /// splits on it. Absent (older cached briefs, a model that ignored the
    /// instruction) the whole text renders as the brief and the lower section
    /// stays hidden — no worse than before this existed.
    static constexpr QLatin1StringView kCategoryMarker{"<<<CATEGORIES>>>"};

    /// Splits a model brief into {top summary, lower per-category detail}.
    ///
    /// Pure and static so it can be tested without standing up the panel,
    /// which pulls in TTS, the file manager and the article repository.
    ///
    /// Streaming-aware: the DIGEST arrives in chunks, so a chunk boundary can
    /// land mid-marker and leave a dangling "<<<CATE" at the end of the text.
    /// Rendering that fragment as brief content shows the user raw protocol
    /// for a frame, so a trailing partial marker is trimmed off the summary.
    ///
    /// Defined inline so a test can call it without linking this translation
    /// unit, which drags in TTS, the file manager and the article repository.
    static QPair<QString, QString> split_brief(const QString& text) {
        const int marker = text.indexOf(kCategoryMarker);
        if (marker >= 0) {
            return {text.left(marker).trimmed(),
                    text.mid(marker + kCategoryMarker.size()).trimmed()};
        }

        // No complete marker. If the text ends with a prefix of one, a
        // streaming chunk boundary landed mid-marker — drop the fragment so
        // the user never sees "<<<CATE" tacked onto the brief. Ordinary prose
        // containing '<' (e.g. "yields fell <2%") is left alone, because the
        // run must match the marker from its very first character.
        //
        // Test each trailing run longest-first. Anchoring on lastIndexOf('<')
        // instead is wrong: a "<<" fragment ends at the second '<', so
        // trimming from there leaves the first one behind.
        //
        // Compared char by char against the Latin-1 marker — QLatin1StringView
        // is 8-bit so it cannot be viewed as a QStringView, and this keeps the
        // check allocation-free on a path that runs once per stream chunk.
        const qsizetype max_len =
            std::min<qsizetype>(kCategoryMarker.size() - 1, text.size());
        for (qsizetype len = max_len; len > 0; --len) {
            const QStringView tail = QStringView{text}.right(len);
            bool is_prefix = true;
            for (qsizetype i = 0; i < len; ++i) {
                if (tail[i] != QLatin1Char(kCategoryMarker[i])) {
                    is_prefix = false;
                    break;
                }
            }
            if (is_prefix)
                return {text.left(text.size() - len).trimmed(), QString()};
        }
        return {text, QString()};
    }

  signals:
    void analyze_requested(const QString& article_url);
    void related_article_clicked(const services::NewsArticle& article);
    void open_in_browser(const QString& url);
    void copy_url(const QString& url);
    void bookmark_requested(const services::NewsArticle& article);

  private:
    QWidget* build_empty_state();
    QWidget* build_content_view();
    /// Shows the ARTICLE block only when an article is actually open. The
    /// TL;DR paths force the content page, which would otherwise reveal a
    /// permanent "Loading article…" under a brief with nothing selected.
    void sync_article_block();

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

#include "screens/news/NewsDetailPanel.h"

#include "core/logging/Logger.h"
#include "services/file_manager/FileManagerService.h"
#include "storage/repositories/NewsArticleRepository.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

namespace fincept::screens {

namespace {

// Format extracted article text as rich-text HTML for body_label_.
//
// Design choices (typographic, deliberate):
//   - First paragraph has no indent. Standard convention — the opening
//     paragraph never indents because there's no preceding text to break
//     from.
//   - Subsequent paragraphs get a 1.8em first-line indent (≈ 3-4 chars at
//     Consolas 12pt) so paragraph breaks are visible even when the bottom
//     margin alone would be ambiguous (e.g. when a paragraph wraps to a
//     short last line).
//   - line-height: 170% — Consolas at 12pt with the default 100% leading
//     stacks too tight for long-form reading. 170% is the Bloomberg-ish
//     middle ground: airy but not magazine-spaced.
//   - Bottom margin 0.8em on each <p> separates paragraphs as discrete
//     blocks without an explicit blank line.
//   - padding-right:10px keeps the rightmost glyph off the splitter handle
//     when the user drags the middle pane narrow.
//   - <br> for soft line breaks inside a paragraph (rare — extractors
//     typically emit \n\n between paragraphs, but trafilatura occasionally
//     emits single \n inside one paragraph for byline / dateline blocks).
QString format_article_html(const QString& plain) {
    QString escaped = plain.toHtmlEscaped();
    const QStringList paras = escaped.split(QStringLiteral("\n\n"),
                                            Qt::SkipEmptyParts);
    QString html;
    html.reserve(escaped.size() + paras.size() * 64);

    // body_label_ is a QTextBrowser, which uses the full QTextDocument
    // pipeline and honors block-level CSS on <p>. Each paragraph becomes
    // a <p> with:
    //   - text-indent in PIXELS (em units don't always resolve in Qt's
    //     CSS parser; px does)
    //   - line-height in % for prose readability
    //   - margin-bottom in px for clear paragraph separation
    // First paragraph keeps text-indent:0 per the typographic convention
    // (opening paragraph never indents).
    bool first = true;
    for (QString p : paras) {
        p = p.trimmed();
        if (p.isEmpty()) continue;
        p.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
        const int indent_px = first ? 0 : 30;
        html += QStringLiteral(
                    "<p style=\"text-indent:%1px; line-height:170%; "
                    "margin-bottom:14px;\">")
                    .arg(indent_px);
        html += p;
        html += QStringLiteral("</p>");
        first = false;
    }
    return html;
}

} // namespace

NewsDetailPanel::NewsDetailPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("newsDetailOverlay");
    // Width is now governed by whatever QSplitter cell hosts this widget
    // — NewsFeedPanel::set_middle_widget seeds an initial size and the user
    // can resize via the partition handles. Setting setFixedWidth here
    // would re-lock the widget and make the splitter handles slide the
    // middle as a rigid block (the wrong-pane-moves bug, 2026-05-12).
    hide(); // start hidden — shown on article click

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Panel header with close button
    auto* header = new QWidget(this);
    header->setObjectName("newsDetailHeader");
    header->setFixedHeight(30);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(10, 0, 6, 0);
    header_layout->setSpacing(0);

    auto* title = new QLabel("ARTICLE DETAIL", header);
    title->setObjectName("newsDetailHeaderTitle");
    header_layout->addWidget(title);
    header_layout->addStretch();

    close_btn_ = new QPushButton("x", header);
    close_btn_->setObjectName("newsDetailCloseBtn");
    close_btn_->setFixedSize(22, 22);
    close_btn_->setCursor(Qt::PointingHandCursor);
    connect(close_btn_, &QPushButton::clicked, this, &NewsDetailPanel::close_panel);
    header_layout->addWidget(close_btn_);
    root->addWidget(header);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(build_empty_state());
    stack_->addWidget(build_content_view());
    stack_->setCurrentIndex(0);

    root->addWidget(stack_, 1);

    // Analyze timeout guard (30s)
    analyze_timeout_ = new QTimer(this);
    analyze_timeout_->setSingleShot(true);
    // On-device analysis (local LLM) is slower than the old cloud call — give it
    // headroom before the button re-enables as a safety net.
    analyze_timeout_->setInterval(120000);
    connect(analyze_timeout_, &QTimer::timeout, this, [this]() {
        if (analyze_btn_) {
            analyze_btn_->setText("ANALYZE");
            analyze_btn_->setEnabled(true);
        }
    });
}

QWidget* NewsDetailPanel::build_empty_state() {
    auto* empty = new QWidget(this);
    auto* layout = new QVBoxLayout(empty);
    layout->setAlignment(Qt::AlignCenter);
    auto* label = new QLabel("Select an article", empty);
    label->setObjectName("newsDetailEmpty");
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    return empty;
}

QWidget* NewsDetailPanel::build_content_view() {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget(scroll);
    content->setObjectName("newsDetailContent");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    // ── TL;DR section ──────────────────────────────────────────────────
    // Sits at the top so the brief stays in view regardless of which
    // article is currently expanded. Populated from the intel-strip
    // TL;DR button click via NewsScreen → show_tldr_summary().
    tldr_section_ = new QWidget(content);
    tldr_section_->setObjectName("newsTldrSection");
    tldr_section_->hide();
    auto* tldr_layout = new QVBoxLayout(tldr_section_);
    tldr_layout->setContentsMargins(0, 0, 0, 6);
    tldr_layout->setSpacing(2);

    auto* tldr_title = new QLabel("TL;DR", tldr_section_);
    tldr_title->setObjectName("newsTldrTitle");
    tldr_layout->addWidget(tldr_title);

    tldr_label_ = new QLabel(tldr_section_);
    tldr_label_->setObjectName("newsTldrBody");
    tldr_label_->setWordWrap(true);
    tldr_layout->addWidget(tldr_label_);

    layout->addWidget(tldr_section_);

    // Headline — rendered as a rich-text link so a click on the title itself
    // opens the article in the user's browser. The OPEN button still exists
    // for discoverability; this just removes the "the title is right there
    // but I have to click OPEN" friction the user flagged.
    //
    // Qt rich-text labels apply the document's default link color (blue,
    // underlined) regardless of inline `style="color:..."` attributes
    // (those are CSS-only and the Qt renderer ignores them). Style the
    // anchor element via QSS on the label so the headline reads in the
    // primary text color matching the rest of the detail pane.
    headline_label_ = new QLabel(content);
    headline_label_->setObjectName("newsDetailHeadline");
    headline_label_->setWordWrap(true);
    headline_label_->setTextFormat(Qt::RichText);
    headline_label_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    headline_label_->setOpenExternalLinks(false);  // route via QDesktopServices below
    headline_label_->setCursor(Qt::PointingHandCursor);
    {
        QPalette pal = headline_label_->palette();
        pal.setColor(QPalette::Link, QColor(ui::colors::TEXT_PRIMARY()));
        pal.setColor(QPalette::LinkVisited, QColor(ui::colors::TEXT_PRIMARY()));
        headline_label_->setPalette(pal);
    }
    connect(headline_label_, &QLabel::linkActivated, this, [this](const QString& url) {
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
    });
    layout->addWidget(headline_label_);

    // Meta row — badges + source/time + impact + tickers, all on a single line.
    //   [BREAKING] [NEUTRAL] [MAJOR] [ENERGY]  CNBC 2s  Impact: HIGH  $NVDA $AMD
    // Previously these lived on four separate rows (badges, source+time,
    // impact, tickers). Collapsing saves three rows of vertical space above
    // the summary and groups all the article metadata as one strip.
    auto* meta_row = new QWidget(content);
    meta_row->setObjectName("newsDetailMetaRow");
    auto* meta_layout = new QHBoxLayout(meta_row);
    meta_layout->setContentsMargins(0, 0, 0, 0);
    meta_layout->setSpacing(8);

    priority_badge_ = new QLabel(content);
    priority_badge_->setObjectName("newsDetailPriorityBadge");
    sentiment_badge_ = new QLabel(content);
    sentiment_badge_->setObjectName("newsDetailSentimentBadge");
    tier_badge_ = new QLabel(content);
    tier_badge_->setObjectName("newsDetailTierBadge");
    category_label_ = new QLabel(content);
    category_label_->setObjectName("newsDetailCategory");
    source_label_ = new QLabel(content);
    source_label_->setObjectName("newsDetailSource");
    time_label_ = new QLabel(content);
    time_label_->setObjectName("newsDetailTime");
    impact_label_ = new QLabel(content);
    impact_label_->setObjectName("newsDetailImpact");
    tickers_label_ = new QLabel(content);
    tickers_label_->setObjectName("newsDetailTickers");

    meta_layout->addWidget(priority_badge_);
    meta_layout->addWidget(sentiment_badge_);
    meta_layout->addWidget(tier_badge_);
    meta_layout->addWidget(category_label_);
    meta_layout->addSpacing(6);
    meta_layout->addWidget(source_label_);
    meta_layout->addWidget(time_label_);
    meta_layout->addSpacing(6);
    meta_layout->addWidget(impact_label_);
    meta_layout->addSpacing(6);
    meta_layout->addWidget(tickers_label_);
    meta_layout->addStretch();
    layout->addWidget(meta_row);

    // Summary
    summary_label_ = new QLabel(content);
    summary_label_->setObjectName("newsDetailSummary");
    summary_label_->setWordWrap(true);
    layout->addWidget(summary_label_);

    // Action buttons — arranged in a flow
    auto* actions = new QWidget(content);
    auto* action_layout = new QHBoxLayout(actions);
    action_layout->setContentsMargins(0, 4, 0, 4);
    action_layout->setSpacing(6);

    open_btn_ = new QPushButton("OPEN", content);
    open_btn_->setObjectName("newsDetailOpenBtn");
    open_btn_->setFixedHeight(24);
    copy_btn_ = new QPushButton("COPY URL", content);
    copy_btn_->setObjectName("newsDetailCopyBtn");
    copy_btn_->setFixedHeight(24);

    copy_title_btn_ = new QPushButton("COPY TITLE", content);
    copy_title_btn_->setObjectName("newsDetailCopyTitleBtn"); // unique — distinct QSS selector from COPY URL
    copy_title_btn_->setFixedHeight(24);
    copy_title_btn_->setFixedWidth(copy_title_btn_->fontMetrics().horizontalAdvance("COPY TITLE") + 20);
    copy_title_btn_->setToolTip("Copy article headline to clipboard");

    analyze_btn_ = new QPushButton("ANALYZE", content);
    analyze_btn_->setObjectName("newsDetailAnalyzeBtn");
    analyze_btn_->setFixedHeight(24);
    save_btn_ = new QPushButton("SAVE", content);
    save_btn_->setObjectName("newsDetailSaveBtn");
    save_btn_->setFixedHeight(24);
    save_btn_->setToolTip("Save article to File Manager");

    bookmark_btn_ = new QPushButton("BOOKMARK", content);
    bookmark_btn_->setObjectName("newsDetailSaveBtn");
    bookmark_btn_->setFixedHeight(24);
    bookmark_btn_->setToolTip("Bookmark article");
    bookmark_btn_->setCheckable(true);

    // Translate button
    translate_btn_ = new QPushButton("TRANSLATE", content);
    translate_btn_->setObjectName("newsDetailOpenBtn");
    translate_btn_->setFixedHeight(24);

    action_layout->addWidget(open_btn_);
    action_layout->addWidget(copy_btn_);
    action_layout->addWidget(copy_title_btn_);
    action_layout->addWidget(analyze_btn_);
    action_layout->addWidget(save_btn_);
    action_layout->addWidget(bookmark_btn_);
    action_layout->addWidget(translate_btn_);
    layout->addWidget(actions);

    connect(open_btn_, &QPushButton::clicked, this, [this]() {
        if (has_article_)
            QDesktopServices::openUrl(QUrl(current_article_.link));
    });
    connect(copy_btn_, &QPushButton::clicked, this, [this]() {
        if (has_article_)
            QApplication::clipboard()->setText(current_article_.link);
    });
    connect(copy_title_btn_, &QPushButton::clicked, this, [this]() {
        if (!has_article_ || current_article_.headline.isEmpty())
            return;
        QApplication::clipboard()->setText(current_article_.headline);
        copy_title_btn_->setText("COPIED");
        QTimer::singleShot(1000, this, [this]() { copy_title_btn_->setText("COPY TITLE"); });
    });
    connect(analyze_btn_, &QPushButton::clicked, this, [this]() {
        if (!has_article_)
            return;
        analyze_btn_->setText("ANALYZING...");
        analyze_btn_->setEnabled(false);
        analyze_timeout_->start();
        emit analyze_requested(current_article_.link);
    });
    connect(save_btn_, &QPushButton::clicked, this, [this]() {
        if (!has_article_)
            return;

        // Build a safe filename from headline
        QString safe = current_article_.headline;
        safe = safe.replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "").simplified();
        safe.replace(' ', '_');
        if (safe.length() > 60)
            safe = safe.left(60);
        QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        QString stored_name = "news_" + safe + "_" + ts + ".txt";
        QString dest = services::FileManagerService::instance().storage_dir() + "/" + stored_name;

        QFile f(dest);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << current_article_.headline << "\n";
            out << current_article_.source << "  |  " << current_article_.time << "\n";
            out << current_article_.link << "\n\n";
            if (!current_article_.summary.isEmpty())
                out << current_article_.summary << "\n\n";
            // Include the extracted body if available. Falls back gracefully if
            // extraction failed or is still in flight — saved file just stops
            // after the summary in that case. No need to block the SAVE on a
            // possibly-stuck extractor; users can SAVE again later for the body.
            if (!current_body_text_.isEmpty())
                out << current_body_text_ << "\n";
            f.close();
            services::FileManagerService::instance().register_file(stored_name, safe + ".txt", QFileInfo(dest).size(),
                                                                   "text/plain", "news");
            LOG_INFO("News", "Saved article: " + stored_name);
        }
    });
    connect(bookmark_btn_, &QPushButton::clicked, this, [this]() {
        if (!has_article_)
            return;
        emit bookmark_requested(current_article_);
    });
    connect(translate_btn_, &QPushButton::clicked, this, [this]() {
        if (!has_article_)
            return;
        translate_btn_->setText("...");
        translate_btn_->setEnabled(false);
        services::NewsNlpService::instance().translate_text(
            current_article_.headline + "\n\n" + current_article_.summary, "en",
            [this](bool ok, QString translated, QString detected_lang) {
                translate_btn_->setText("TRANSLATE");
                translate_btn_->setEnabled(true);
                if (ok && !translated.isEmpty()) {
                    summary_label_->setText(QString("[%1 -> EN] %2").arg(detected_lang, translated));
                }
            });
    });

    // ── Article body (extracted prose) ───────────────────────────────────
    // Sits right below the action button row — the empty area previously
    // there. Populated by NewsService::extract_article_body() each time
    // show_article() runs. Lives inside the scroll area so long articles
    // remain readable without expanding the panel further.
    body_section_ = new QWidget(content);
    body_section_->setObjectName("newsBodySection");
    auto* body_layout = new QVBoxLayout(body_section_);
    // Top margin separates the ARTICLE block from the action button row;
    // bottom margin keeps the trailing separator off the last line of prose.
    // Spacing of 6px between title / status / body groups them but lets the
    // hairline under the title breathe.
    body_layout->setContentsMargins(0, 14, 0, 8);
    body_layout->setSpacing(6);

    // Distinct objectName from #newsDetailSectionTitle so the ARTICLE header
    // can match the WIRE-feed cream (#f0e8d0) the user pointed at as the
    // reference color, without disturbing the amber section-titles used by
    // AI Analysis / Key Points / Monitor / Related / Entities / Infra.
    body_title_ = new QLabel("ARTICLE", body_section_);
    body_title_->setObjectName("newsDetailBodyTitle");
    body_layout->addWidget(body_title_);

    body_status_ = new QLabel("Loading article…", body_section_);
    body_status_->setObjectName("newsDetailBodyStatus");
    // Wrap so the multi-sentence failure message ("Could not extract … Use
    // OPEN …") flows instead of clipping at the right edge when the middle
    // pane is narrow.
    body_status_->setWordWrap(true);
    body_layout->addWidget(body_status_);

    // QTextBrowser instead of QLabel: QLabel's rich-text renderer drops
    // block-level CSS like `text-indent` on <p>, which we need for first-line
    // paragraph indent. QTextBrowser uses the full QTextDocument pipeline
    // and supports the entire Qt rich-text CSS subset.
    body_label_ = new QTextBrowser(body_section_);
    body_label_->setObjectName("newsDetailBody");
    body_label_->setReadOnly(true);
    body_label_->setFrameShape(QFrame::NoFrame);
    body_label_->setLineWrapMode(QTextEdit::WidgetWidth);
    // Outer #newsDetailContent scroll area handles overflow; suppress the
    // browser's own scrollbars so we don't get nested scrolling.
    body_label_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body_label_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body_label_->setOpenExternalLinks(false);
    body_label_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    // Font + colour: match the WIRE-feed delegate exactly so the inline body
    // reads at the same size AND tone as the headline column on the
    // left/right. QTextBrowser doesn't honour QSS reliably for either —
    // font-size falls through to qApp's 14px default; color falls through to
    // the global `QWidget { color: %2 }` rule (=text_primary #e5e5e5, a cool
    // gray rather than the wire's warmer cream). Both need to be pinned via
    // the direct Qt APIs (QFont / QPalette) to take effect.
    //
    // The literal `#f0e8d0` matches kNewsTextPrimary in NewsFeedDelegate.cpp.
    // It's hardcoded in both places (drift risk), but the wire delegate set
    // the convention and the news pane is the only consumer.
    QFont body_font(ui::fonts::DATA_FAMILY, ui::fonts::TINY);
    body_label_->setFont(body_font);
    body_label_->document()->setDefaultFont(body_font);
    QPalette body_pal = body_label_->palette();
    body_pal.setColor(QPalette::Text, QColor(0xf0, 0xe8, 0xd0));
    body_label_->setPalette(body_pal);
    // Transparent background — both on the widget (inline QSS) and the
    // viewport (autoFillBackground off) so the panel's BG_SURFACE shows
    // through.
    body_label_->setStyleSheet("QTextBrowser { background: transparent; border: none; }");
    body_label_->viewport()->setAutoFillBackground(false);
    // Auto-size to document height. With scrollbars suppressed, the widget
    // must be tall enough to show every line; the outer scroll area handles
    // overflow when the panel is shorter than the article. documentLayout's
    // documentSizeChanged fires after every reflow (content + resize), so
    // the widget tracks the document's natural height across rewraps.
    connect(body_label_->document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged,
            body_label_, [this](const QSizeF& size) {
                body_label_->setFixedHeight(static_cast<int>(size.height()) + 4);
            });
    body_layout->addWidget(body_label_);
    layout->addWidget(body_section_);

    // Separator
    auto* sep = new QWidget(content);
    sep->setFixedHeight(1);
    sep->setObjectName("newsDetailSep");
    layout->addWidget(sep);

    // AI Analysis section (hidden until populated)
    analysis_section_ = new QWidget(content);
    analysis_section_->setObjectName("newsAnalysisSection");
    analysis_section_->hide();
    auto* analysis_layout = new QVBoxLayout(analysis_section_);
    analysis_layout->setContentsMargins(0, 4, 0, 4);
    analysis_layout->setSpacing(4);

    auto* ai_title = new QLabel("AI ANALYSIS", analysis_section_);
    ai_title->setObjectName("newsDetailSectionTitle");
    analysis_layout->addWidget(ai_title);

    ai_summary_ = new QLabel(analysis_section_);
    ai_summary_->setObjectName("newsDetailAiSummary");
    ai_summary_->setWordWrap(true);
    analysis_layout->addWidget(ai_summary_);

    auto* ai_row = new QWidget(analysis_section_);
    auto* ai_row_layout = new QHBoxLayout(ai_row);
    ai_row_layout->setContentsMargins(0, 0, 0, 0);
    ai_row_layout->setSpacing(8);
    ai_sentiment_ = new QLabel(analysis_section_);
    ai_sentiment_->setObjectName("newsDetailAiSentiment");
    ai_urgency_ = new QLabel(analysis_section_);
    ai_urgency_->setObjectName("newsDetailAiUrgency");
    ai_row_layout->addWidget(ai_sentiment_);
    ai_row_layout->addWidget(ai_urgency_);
    ai_row_layout->addStretch();
    analysis_layout->addWidget(ai_row);

    auto* kp_title = new QLabel("KEY POINTS", analysis_section_);
    kp_title->setObjectName("newsDetailSubTitle");
    analysis_layout->addWidget(kp_title);

    auto* kp_container = new QWidget(analysis_section_);
    key_points_layout_ = new QVBoxLayout(kp_container);
    key_points_layout_->setContentsMargins(0, 0, 0, 0);
    key_points_layout_->setSpacing(2);
    analysis_layout->addWidget(kp_container);

    auto* risk_title = new QLabel("RISK SIGNALS", analysis_section_);
    risk_title->setObjectName("newsDetailSubTitle");
    analysis_layout->addWidget(risk_title);

    auto* risk_container = new QWidget(analysis_section_);
    risk_layout_ = new QVBoxLayout(risk_container);
    risk_layout_->setContentsMargins(0, 0, 0, 0);
    risk_layout_->setSpacing(2);
    analysis_layout->addWidget(risk_container);

    auto* topics_title = new QLabel("TOPICS", analysis_section_);
    topics_title->setObjectName("newsDetailSubTitle");
    analysis_layout->addWidget(topics_title);

    auto* topics_container = new QWidget(analysis_section_);
    topics_layout_ = new QVBoxLayout(topics_container);
    topics_layout_->setContentsMargins(0, 0, 0, 0);
    topics_layout_->setSpacing(2);
    analysis_layout->addWidget(topics_container);

    layout->addWidget(analysis_section_);

    // Monitor matches section
    monitor_section_ = new QWidget(content);
    monitor_section_->hide();
    auto* mon_layout = new QVBoxLayout(monitor_section_);
    mon_layout->setContentsMargins(0, 4, 0, 4);
    mon_layout->setSpacing(2);
    auto* mon_title = new QLabel("MONITOR MATCHES", monitor_section_);
    mon_title->setObjectName("newsDetailSectionTitle");
    mon_layout->addWidget(mon_title);
    auto* mon_container = new QWidget(monitor_section_);
    monitor_matches_layout_ = new QVBoxLayout(mon_container);
    monitor_matches_layout_->setContentsMargins(0, 0, 0, 0);
    monitor_matches_layout_->setSpacing(2);
    mon_layout->addWidget(mon_container);
    layout->addWidget(monitor_section_);

    // Related articles section
    related_section_ = new QWidget(content);
    related_section_->hide();
    auto* rel_layout_outer = new QVBoxLayout(related_section_);
    rel_layout_outer->setContentsMargins(0, 4, 0, 4);
    rel_layout_outer->setSpacing(2);
    auto* rel_title = new QLabel("RELATED", related_section_);
    rel_title->setObjectName("newsDetailSectionTitle");
    rel_layout_outer->addWidget(rel_title);
    auto* rel_container = new QWidget(related_section_);
    related_layout_ = new QVBoxLayout(rel_container);
    related_layout_->setContentsMargins(0, 0, 0, 0);
    related_layout_->setSpacing(2);
    rel_layout_outer->addWidget(rel_container);
    layout->addWidget(related_section_);

    // Entities section
    entities_section_ = new QWidget(content);
    entities_section_->hide();
    auto* ent_layout_outer = new QVBoxLayout(entities_section_);
    ent_layout_outer->setContentsMargins(0, 4, 0, 4);
    ent_layout_outer->setSpacing(2);
    auto* ent_title = new QLabel("ENTITIES", entities_section_);
    ent_title->setObjectName("newsDetailSectionTitle");
    ent_layout_outer->addWidget(ent_title);
    auto* ent_container = new QWidget(entities_section_);
    entities_detail_layout_ = new QVBoxLayout(ent_container);
    entities_detail_layout_->setContentsMargins(0, 0, 0, 0);
    entities_detail_layout_->setSpacing(2);
    ent_layout_outer->addWidget(ent_container);
    layout->addWidget(entities_section_);

    // Infrastructure section
    infra_section_ = new QWidget(content);
    infra_section_->hide();
    auto* infra_layout_outer = new QVBoxLayout(infra_section_);
    infra_layout_outer->setContentsMargins(0, 4, 0, 4);
    infra_layout_outer->setSpacing(2);
    auto* infra_title = new QLabel("NEARBY INFRASTRUCTURE", infra_section_);
    infra_title->setObjectName("newsDetailSectionTitle");
    infra_layout_outer->addWidget(infra_title);
    auto* infra_container = new QWidget(infra_section_);
    infra_layout_ = new QVBoxLayout(infra_container);
    infra_layout_->setContentsMargins(0, 0, 0, 0);
    infra_layout_->setSpacing(2);
    infra_layout_outer->addWidget(infra_container);
    layout->addWidget(infra_section_);

    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}

// ── Panel open/close ───────────────────────────────────────────────────────

void NewsDetailPanel::open_panel() {
    if (!panel_open_) {
        panel_open_ = true;
        show();
    }
}

void NewsDetailPanel::close_panel() {
    if (panel_open_) {
        panel_open_ = false;
        hide();
        emit panel_closed();
    }
}

// ── Public methods ──────────────────────────────────────────────────────────

void NewsDetailPanel::show_article(const services::NewsArticle& article) {
    current_article_ = article;
    has_article_ = true;
    stack_->setCurrentIndex(1);
    open_panel();

    // Render headline as a link to the article URL. Qt's rich-text engine
    // does NOT honour `color:inherit` — without an explicit color the anchor
    // falls back to QPalette::Link, which on our dark theme renders as a
    // dark blue indistinguishable from the background. Wrap the text in a
    // <span> with the literal TEXT_PRIMARY hex so the anchor reads in the
    // same cream/white as the rest of the article body.
    headline_label_->setText(
        QString("<a href=\"%1\" style=\"text-decoration:none;\">"
                "<span style=\"color:%2;\">%3</span></a>")
            .arg(article.link.toHtmlEscaped(),
                 ui::colors::TEXT_PRIMARY(),
                 article.headline.toHtmlEscaped()));
    headline_label_->setToolTip(article.link);

    // Priority badge
    QString pstr = services::priority_string(article.priority);
    QString pcolor = services::priority_color(article.priority);
    priority_badge_->setText(pstr);
    priority_badge_->setStyleSheet(
        QString("background: %1; color: %2; padding: 1px 6px;").arg(pcolor, ui::colors::TEXT_PRIMARY()));

    // Sentiment badge
    QString sstr = services::sentiment_string(article.sentiment);
    QString scolor = services::sentiment_color(article.sentiment);
    sentiment_badge_->setText(sstr);
    sentiment_badge_->setStyleSheet(QString("color: %1; padding: 1px 4px;").arg(scolor));

    // Tier badge
    QStringList tier_names = {"", "WIRE", "MAJOR", "SPECIALTY", "BLOG"};
    QString tier_text = (article.tier >= 1 && article.tier <= 4) ? tier_names[article.tier] : "OTHER";
    tier_badge_->setText(tier_text);

    category_label_->setText(article.category);
    time_label_->setText(services::relative_time(article.sort_ts));

    // Source with credibility flag
    if (article.source_flag != services::SourceFlag::NONE) {
        QString flag_text = services::NewsService::source_flag_label(article.source_flag);
        QString flag_color = article.source_flag == services::SourceFlag::STATE_MEDIA
                                 ? "" + QString(ui::colors::WARNING()) + ""
                                 : ui::colors::WARNING();
        source_label_->setText(article.source.toUpper() + "  [" + flag_text + "]");
        source_label_->setStyleSheet(QString("color: %1; font-weight: 700; background: transparent;").arg(flag_color));
    } else {
        source_label_->setText(article.source.toUpper());
        source_label_->setStyleSheet(
            QString("color: %1; font-weight: 700; background: transparent;").arg(ui::colors::CYAN()));
    }

    summary_label_->setText(article.summary.isEmpty() ? "No summary available." : article.summary);

    // Threat takes precedence over generic impact when classified above INFO —
    // a high-threat article gets "Threat: …" rendered in the threat colour;
    // otherwise the label falls back to the standard "Impact: …" themed via
    // the #newsDetailImpact QSS. Previously the threat branch set the label,
    // then the unconditional setText below clobbered it with "Impact: …" —
    // so a high-threat row showed "Impact: LOW" in red. The else branch also
    // clears the inline stylesheet so the QSS default takes over again after
    // a prior threat-coloured article.
    if (article.threat.level != services::ThreatLevel::INFO) {
        QString threat_text = services::threat_level_string(article.threat.level);
        QString threat_color = services::threat_level_color(article.threat.level);
        impact_label_->setText(QString("Threat: %1 (%2, %3% conf)")
                                   .arg(threat_text, article.threat.category)
                                   .arg(static_cast<int>(article.threat.confidence * 100)));
        impact_label_->setStyleSheet(QString("color: %1; background: transparent;").arg(threat_color));
    } else {
        impact_label_->setText(QString("Impact: %1").arg(services::impact_string(article.impact)));
        impact_label_->setStyleSheet(QString()); // restore default #newsDetailImpact styling
    }

    if (!article.tickers.isEmpty())
        tickers_label_->setText("$" + article.tickers.join("  $"));
    else
        tickers_label_->clear();

    // Reset analysis
    analysis_section_->hide();
    analyze_btn_->setText("ANALYZE");
    analyze_btn_->setEnabled(true);
    analyze_timeout_->stop();

    // Reflect saved state from DB
    {
        auto r = fincept::NewsArticleRepository::instance().load_saved();
        bool is_saved = false;
        if (r.is_ok()) {
            for (const auto& a : r.value()) {
                if (a.id == article.id) {
                    is_saved = true;
                    break;
                }
            }
        }
        bookmark_btn_->setChecked(is_saved);
        bookmark_btn_->setText(is_saved ? "BOOKMARKED" : "BOOKMARK");
    }

    // Clear related and monitors
    show_related({});
    monitor_section_->hide();

    // Kick off article-body extraction. Bump the generation token so any
    // late callback from a prior article is discarded — the trafilatura
    // round-trip can take 1-3s and the user may have moved on by then.
    // QPointer guards against the panel being destroyed mid-extraction
    // (dock closed, screen switched) — without it the callback fires on
    // a dangling `this`, matching the established pattern in
    // NewsScreen::on_analyze_requested.
    const int my_gen = ++body_gen_;
    body_label_->clear();
    current_body_text_.clear(); // drop the previous article's body before the new one resolves
    body_status_->setText("Loading article…");
    body_status_->show();
    body_title_->show();
    QPointer<NewsDetailPanel> self = this;
    services::NewsService::instance().extract_article_body(
        article.link,
        [self, my_gen](bool ok, QString /*title*/, QString text) {
            if (!self) return;
            if (my_gen != self->body_gen_) return; // stale — user opened a different article
            if (!ok || text.trimmed().isEmpty()) {
                self->body_status_->setText(
                    "Could not extract body (paywall, blocked, or unsupported page). "
                    "Use OPEN to read in browser.");
                self->body_label_->clear();
                return;
            }
            self->body_status_->hide();
            // Stash the plain text for the SAVE button — we already have it here,
            // and saving expects plain text on disk, not the HTML we render.
            self->current_body_text_ = text;
            // setHtml (not setText): QTextBrowser's setText treats input as
            // plain text by default; setHtml forces rich-text parsing so the
            // <p style="text-indent:…"> CSS we emit actually applies.
            self->body_label_->setHtml(format_article_html(text));
        });
}

void NewsDetailPanel::show_analysis_error(const QString& message) {
    analyze_btn_->setText("ANALYZE");
    analyze_btn_->setEnabled(true);
    analyze_timeout_->stop();
    ai_sentiment_->clear();
    ai_urgency_->clear();
    // Clear all result layouts so stale rows from a previous success don't linger
    // under the error (show_analysis clears the same three).
    for (QVBoxLayout* lay : {key_points_layout_, risk_layout_, topics_layout_}) {
        while (lay->count() > 0) {
            auto* item = lay->takeAt(0);
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }
    ai_summary_->setText(message.isEmpty() ? "AI analysis is unavailable right now." : message);
    analysis_section_->show();
}

void NewsDetailPanel::show_analysis(const services::NewsAnalysis& analysis) {
    analyze_btn_->setText("ANALYZE");
    analyze_btn_->setEnabled(true);
    analyze_timeout_->stop();

    ai_summary_->setText(analysis.summary.isEmpty() ? "No AI summary available." : analysis.summary);

    double score = std::clamp(analysis.sentiment.score, -1.0, 1.0);
    QString sent_color =
        score > 0.1 ? ui::colors::POSITIVE : (score < -0.1 ? ui::colors::NEGATIVE : ui::colors::WARNING);
    ai_sentiment_->setText(QString("Sentiment: %1%2").arg(score >= 0 ? "+" : "").arg(score, 0, 'f', 2));
    ai_sentiment_->setStyleSheet(QString("color: %1;").arg(sent_color));

    ai_urgency_->setText(QString("Urgency: %1").arg(analysis.market_impact.urgency));

    // Key points
    while (key_points_layout_->count() > 0) {
        auto* item = key_points_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    for (const auto& point : analysis.key_points) {
        auto* lbl = new QLabel(QString("- %1").arg(point), analysis_section_);
        lbl->setObjectName("newsDetailKeyPoint");
        lbl->setWordWrap(true);
        key_points_layout_->addWidget(lbl);
    }

    // Risk signals
    while (risk_layout_->count() > 0) {
        auto* item = risk_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    auto add_risk = [&](const QString& name, const services::RiskSignal& sig) {
        if (sig.level.isEmpty() || sig.level == "none")
            return;
        auto* lbl = new QLabel(QString("%1: %2").arg(name, sig.level), analysis_section_);
        lbl->setObjectName("newsDetailRisk");
        lbl->setToolTip(sig.details);
        risk_layout_->addWidget(lbl);
    };
    add_risk("Regulatory", analysis.regulatory);
    add_risk("Geopolitical", analysis.geopolitical);
    add_risk("Operational", analysis.operational);
    add_risk("Market", analysis.market);

    // Topics
    while (topics_layout_->count() > 0) {
        auto* item = topics_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    auto* topics_row = new QWidget(analysis_section_);
    auto* topics_flow = new QHBoxLayout(topics_row);
    topics_flow->setContentsMargins(0, 0, 0, 0);
    topics_flow->setSpacing(4);
    for (const auto& topic : analysis.topics) {
        auto* badge = new QLabel(topic, analysis_section_);
        badge->setObjectName("newsDetailTopicBadge");
        topics_flow->addWidget(badge);
    }
    topics_flow->addStretch();
    topics_layout_->addWidget(topics_row);

    analysis_section_->show();
}

void NewsDetailPanel::show_related(const QVector<services::NewsArticle>& related) {
    while (related_layout_->count() > 0) {
        auto* item = related_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (related.isEmpty()) {
        related_section_->hide();
        return;
    }

    related_section_->show();
    for (const auto& article : related) {
        auto* btn = new QPushButton(related_section_);
        btn->setObjectName("newsRelatedBtn");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setText(QString("%1 - %2").arg(article.source, article.headline.left(55)));
        btn->setToolTip(article.headline);

        connect(btn, &QPushButton::clicked, this, [this, article]() { emit related_article_clicked(article); });

        related_layout_->addWidget(btn);
    }
}

void NewsDetailPanel::show_monitor_matches(const QVector<QPair<services::NewsMonitor, QStringList>>& matches) {
    while (monitor_matches_layout_->count() > 0) {
        auto* item = monitor_matches_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (matches.isEmpty()) {
        monitor_section_->hide();
        return;
    }

    monitor_section_->show();
    for (const auto& [monitor, keywords] : matches) {
        auto* row = new QWidget(monitor_section_);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);

        auto* dot = new QLabel(row);
        dot->setFixedSize(6, 6);
        dot->setStyleSheet(QString("background: %1;").arg(monitor.color));
        hl->addWidget(dot);

        auto* lbl = new QLabel(QString("%1: %2").arg(monitor.label, keywords.join(", ")), row);
        lbl->setObjectName("newsMonitorMatchLabel");
        lbl->setWordWrap(true);
        hl->addWidget(lbl, 1);

        monitor_matches_layout_->addWidget(row);
    }
}

void NewsDetailPanel::show_entities(const services::EntityResult& entities) {
    while (entities_detail_layout_->count() > 0) {
        auto* item = entities_detail_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    bool has_data = false;

    for (const auto& [name, code] : entities.countries) {
        auto* lbl = new QLabel(QString("Country: %1 (%2)").arg(name, code), entities_section_);
        lbl->setObjectName("newsDetailKeyPoint");
        entities_detail_layout_->addWidget(lbl);
        has_data = true;
    }
    for (const auto& org : entities.organizations) {
        auto* lbl = new QLabel(QString("Org: %1").arg(org), entities_section_);
        lbl->setObjectName("newsDetailKeyPoint");
        entities_detail_layout_->addWidget(lbl);
        has_data = true;
    }
    for (const auto& person : entities.people) {
        auto* lbl = new QLabel(QString("Person: %1").arg(person), entities_section_);
        lbl->setObjectName("newsDetailKeyPoint");
        entities_detail_layout_->addWidget(lbl);
        has_data = true;
    }

    entities_section_->setVisible(has_data);
}

void NewsDetailPanel::show_infrastructure(const QVector<services::InfrastructureItem>& items) {
    while (infra_layout_->count() > 0) {
        auto* item = infra_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (items.isEmpty()) {
        infra_section_->hide();
        return;
    }
    infra_section_->show();

    for (const auto& inf : items) {
        QString type_icon = inf.type == "airport"
                                ? "AIR"
                                : (inf.type == "military" ? "MIL" : (inf.type == "power_plant" ? "PWR" : "PRT"));
        auto* lbl =
            new QLabel(QString("[%1] %2 — %3 km").arg(type_icon, inf.name.left(20)).arg(inf.distance_km, 0, 'f', 1),
                       infra_section_);
        lbl->setObjectName("newsDetailKeyPoint");
        infra_layout_->addWidget(lbl);
    }
}

void NewsDetailPanel::clear() {
    has_article_ = false;
    stack_->setCurrentIndex(0);
    analysis_section_->hide();
    related_section_->hide();
    monitor_section_->hide();
    entities_section_->hide();
    infra_section_->hide();
    analyze_timeout_->stop();
}

// ── TL;DR ──────────────────────────────────────────────────────────────────

void NewsDetailPanel::show_tldr_loading() {
    // Force the content stack page so the TL;DR section is reachable even
    // if no article has been selected yet. Keep the panel open while the
    // request is in flight.
    if (tldr_label_)
        tldr_label_->setText(QStringLiteral("Generating brief…"));
    if (tldr_section_)
        tldr_section_->show();
    stack_->setCurrentIndex(1);
    open_panel();
}

void NewsDetailPanel::show_tldr_summary(const QString& text) {
    if (!tldr_label_ || !tldr_section_)
        return;
    if (text.isEmpty()) {
        tldr_section_->hide();
        return;
    }
    tldr_label_->setText(text);
    tldr_section_->show();
    stack_->setCurrentIndex(1);
    open_panel();
}

void NewsDetailPanel::hide_tldr() {
    if (tldr_section_)
        tldr_section_->hide();
}

} // namespace fincept::screens

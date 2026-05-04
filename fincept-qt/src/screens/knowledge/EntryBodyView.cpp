#include "screens/knowledge/EntryBodyView.h"

#include "screens/knowledge/ContentLoader.h"
#include "ui/theme/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {

// VSCode dark + warm-brown ("Warty Brown") palette. Sans-serif body, monospace
// code, with the distinctive tan inline-code chip style. Headings stay clean
// (no border-bottom) — the visual hierarchy comes from size and weight.
constexpr const char* MONO = "font-family: 'JetBrains Mono','SF Mono','Consolas','Courier New',monospace;";
constexpr const char* SANS =
    "font-family: 'Inter','Segoe UI',-apple-system,BlinkMacSystemFont,'Helvetica Neue',Arial,sans-serif;";
constexpr const char* BODY_BG       = "#1c1815"; ///< warm dark, brown-tinted (vs VSCode #1e1e1e)
constexpr const char* BODY_TEXT     = "#d4cab0"; ///< warm cream body text
constexpr const char* HEAD_TEXT     = "#f0e8d0"; ///< near-white cream for headings + bold
constexpr const char* MUTED_TEXT    = "#a89878"; ///< muted warm tan for subtitles, captions
constexpr const char* CODE_TEXT     = "#d4a878"; ///< Warty Brown — inline code accent
constexpr const char* CODE_BG       = "#2a221a"; ///< warm-dark chip bg for inline code
constexpr const char* PRE_BG        = "#15110d"; ///< slightly darker than body for code blocks
constexpr const char* LINK_COLOR    = "#6dc1c9"; ///< muted cyan for links
constexpr const char* RULE_COLOR    = "#3a2e22"; ///< warm brown border / hr

struct DiffStyle {
    const char* label;
    QString fg;
    QString bg;
};
DiffStyle difficulty_style(const QString& d) {
    if (d == Difficulty::INTERMEDIATE) return {"INTERMEDIATE", ui::colors::BG_BASE(), ui::colors::INFO()};
    if (d == Difficulty::ADVANCED)     return {"ADVANCED",     ui::colors::BG_BASE(), ui::colors::AMBER()};
    if (d == Difficulty::PRO)          return {"PRO",          ui::colors::BG_BASE(), ui::colors::NEGATIVE()};
    return {"BEGINNER", ui::colors::BG_BASE(), ui::colors::POSITIVE()};
}

struct WarnStyle { QString border; QString tag; };
WarnStyle warning_style(const QString& severity) {
    if (severity == "high") return {ui::colors::NEGATIVE(), "RISK"};
    if (severity == "low")  return {ui::colors::INFO(),     "NOTE"};
    return {ui::colors::AMBER(), "CAUTION"};
}

QString markdown_default_css() {
    // VSCode-style markdown rendering with a warm-brown ("Warty Brown") accent.
    // Sans-serif body, monospace code, near-white bold headings, distinctive
    // tan inline-code chips. Reads like a tech-doc page rather than a terminal.
    return QString(
               "body { %0 color: %1; }"
               // Headings: clean white-ish, sans-serif, no border-bottom.
               "h1 { color: %1; %0 font-size: 22px; font-weight: 700;"
               "     margin-top: 8px; margin-bottom: 18px; }"
               "h2 { color: %1; %0 font-size: 18px; font-weight: 700;"
               "     margin-top: 28px; margin-bottom: 12px; }"
               "h3 { color: %1; %0 font-size: 16px; font-weight: 600;"
               "     margin-top: 22px; margin-bottom: 10px; }"
               "h4 { color: %1; %0 font-size: 14px; font-weight: 600;"
               "     margin-top: 18px; margin-bottom: 8px; }"
               // Body: sans-serif, comfortable size + leading, breathing paragraphs.
               "p  { color: %2; %0 font-size: 14px; line-height: 168%%; margin: 12px 0; }"
               // Lists indent + per-item leading.
               "ul, ol { margin: 12px 0; padding-left: 24px; }"
               "li { color: %2; %0 font-size: 14px; line-height: 165%%; margin: 6px 0; }"
               // Inline code — the Warty Brown chip. Tan color on warm-dark bg.
               "code { background: %3; color: %4; padding: 2px 6px; font-size: 13px; %5 }"
               // Block code: darker bg, more padding.
               "pre  { background: %6; color: %2; padding: 14px 16px; margin: 16px 0;"
               "       font-size: 13px; line-height: 158%%; %5 }"
               // Bold: brighter cream — VSCode emphasis pattern.
               "strong, b { color: %1; font-weight: 700; }"
               // Italic: keep body color; just slant.
               "em, i { color: %2; font-style: italic; }"
               // Blockquote: VSCode-style left bar + muted text.
               "blockquote { color: %7; border-left: 3px solid %8; padding: 4px 16px; margin: 16px 0; }"
               // Tables: subtle borders, padded cells.
               "table { border-collapse: collapse; margin: 16px 0; }"
               "th, td { padding: 8px 14px; border: 1px solid %8; color: %2; %0 font-size: 13px; }"
               "th { color: %1; font-weight: 700; background: %3; }"
               // Horizontal rule with air.
               "hr { background: %8; border: none; height: 1px; margin: 22px 0; }"
               "a { color: %9; text-decoration: none; }")
        .arg(QString(SANS),                     // %0 body font stack
             QString(HEAD_TEXT),                // %1 headings + bold + th
             QString(BODY_TEXT),                // %2 body/li/td
             QString(CODE_BG),                  // %3 inline-code bg + th bg
             QString(CODE_TEXT),                // %4 inline-code Warty Brown
             QString(MONO),                     // %5 mono font stack for code/pre
             QString(PRE_BG),                   // %6 block-code bg
             QString(MUTED_TEXT),               // %7 blockquote text
             QString(RULE_COLOR),               // %8 borders + hr + blockquote rule
             QString(LINK_COLOR));              // %9 link color
}

QString text_browser_ss() {
    return QString("QTextBrowser { background: %1; border: none; padding: 0;"
                   " selection-background-color: %2; }"
                   "QScrollBar:vertical { width: 6px; background: transparent; }"
                   "QScrollBar::handle:vertical { background: %3; }"
                   "QScrollBar::handle:vertical:hover { background: %4; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(QString(BODY_BG), ui::colors::ACCENT_BG(), ui::colors::BORDER_MED(), ui::colors::BORDER_BRIGHT());
}

QString scroll_ss() {
    return QString("QScrollArea { border: none; background: transparent; }"
                   "QScrollBar:vertical { width: 6px; background: transparent; }"
                   "QScrollBar::handle:vertical { background: %1; }"
                   "QScrollBar::handle:vertical:hover { background: %2; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(ui::colors::BORDER_MED(), ui::colors::BORDER_BRIGHT());
}

QString preprocess_crossrefs(QString markdown) {
    static const QRegularExpression re(R"(\[\[([a-z0-9_\-]+)(?:\|([^\]]+))?\]\])",
                                       QRegularExpression::CaseInsensitiveOption);
    QString out;
    out.reserve(markdown.size());
    int last = 0;
    auto it = re.globalMatch(markdown);
    while (it.hasNext()) {
        const auto m = it.next();
        out.append(markdown.mid(last, m.capturedStart() - last));
        const QString id = m.captured(1);
        const QString label = m.captured(2).isEmpty() ? id : m.captured(2);
        out.append(QString("[%1](knowledge://%2)").arg(label, id));
        last = m.capturedEnd();
    }
    out.append(markdown.mid(last));
    return out;
}

} // namespace

EntryBodyView::EntryBodyView(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(QString(BODY_BG)));
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(0, 0, 0, 0);
    root_->setSpacing(0);
    rebuild();
}

void EntryBodyView::set_entry(const KnowledgeEntry* entry) {
    current_ = entry;
    rebuild();
}

void EntryBodyView::rebuild() {
    if (content_) {
        root_->removeWidget(content_);
        content_->deleteLater();
        content_ = nullptr;
    }

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(scroll_ss());

    auto* page = new QWidget(scroll);
    page->setStyleSheet(QString("background: %1;").arg(QString(BODY_BG)));
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(32, 26, 32, 36);  // generous gutters — article margins
    vl->setSpacing(14);                       // breathing between header / warnings / body

    if (!current_) {
        auto* lbl = new QLabel("(No entry selected.)", page);
        lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                               .arg(ui::colors::TEXT_DIM(), MONO));
        vl->addWidget(lbl);
        vl->addStretch();
        scroll->setWidget(page);
        content_ = scroll;
        root_->addWidget(content_, 1);
        return;
    }

    // ── Compact header ────────────────────────────────────────────────────────
    {
        auto* top = new QHBoxLayout;
        top->setSpacing(8);
        const auto ds = difficulty_style(current_->difficulty);
        auto* diff = new QLabel(ds.label, page);
        diff->setStyleSheet(QString("color: %1; background: %2; padding: 2px 8px; font-size: 9px;"
                                    " font-weight: bold; letter-spacing: 1.5px; %3")
                                .arg(ds.fg, ds.bg, MONO));
        top->addWidget(diff);
        if (!current_->abbreviation.isEmpty()) {
            auto* abbr = new QLabel(current_->abbreviation, page);
            abbr->setStyleSheet(QString("color: %1; background: transparent; padding: 2px 0; font-size: 10px; %2")
                                    .arg(ui::colors::TEXT_TERTIARY(), MONO));
            top->addWidget(abbr);
        }
        top->addStretch();
        vl->addLayout(top);

        auto* title = new QLabel(current_->title, page);
        title->setStyleSheet(QString("color: %1; background: transparent; font-size: 24px; font-weight: 700;"
                                     " padding-top: 6px; padding-bottom: 4px; %2")
                                 .arg(QString(HEAD_TEXT), QString(SANS)));
        title->setWordWrap(true);
        vl->addWidget(title);

        if (!current_->subtitle.isEmpty()) {
            auto* sub = new QLabel(current_->subtitle, page);
            sub->setStyleSheet(QString("color: %1; background: transparent; font-size: 13px; line-height: 165%%;"
                                       " padding-bottom: 14px; %2")
                                   .arg(QString(MUTED_TEXT), QString(SANS)));
            sub->setWordWrap(true);
            vl->addWidget(sub);
        }
    }

    // ── Warnings ──────────────────────────────────────────────────────────────
    for (const auto& w : current_->warnings) {
        const auto ws = warning_style(w.severity);
        auto* box = new QFrame(page);
        box->setStyleSheet(QString("background: %1; border: 1px solid %2; border-left: 4px solid %2;")
                               .arg(ui::colors::BG_SURFACE(), ws.border));
        auto* bl = new QHBoxLayout(box);
        bl->setContentsMargins(12, 8, 12, 8);
        bl->setSpacing(8);
        auto* tag = new QLabel(QString::fromUtf8("⚠  ") + ws.tag, box);
        tag->setFixedWidth(80);
        tag->setStyleSheet(QString("color: %1; background: transparent; font-size: 9px; font-weight: bold;"
                                   " letter-spacing: 1.5px; %2")
                               .arg(ws.border, MONO));
        bl->addWidget(tag);
        auto* text = new QLabel(w.text, box);
        text->setStyleSheet(QString("color: %1; background: transparent; font-size: 12px; line-height: 155%%; %2")
                                .arg(QString(BODY_TEXT), QString(SANS)));
        text->setWordWrap(true);
        bl->addWidget(text, 1);
        vl->addWidget(box);
    }

    // ── Body markdown ─────────────────────────────────────────────────────────
    QString body = ContentLoader::instance().load_body(current_->id);
    if (body.isEmpty()) {
        auto* miss = new QLabel("(No content body.)", page);
        miss->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                                .arg(ui::colors::TEXT_DIM(), MONO));
        vl->addWidget(miss);
    } else {
        auto* browser = new QTextBrowser(page);
        browser->setOpenLinks(false);
        browser->setOpenExternalLinks(false);
        browser->setStyleSheet(text_browser_ss());
        browser->setFrameShape(QFrame::NoFrame);
        browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        browser->document()->setDefaultStyleSheet(markdown_default_css());
        browser->setMarkdown(preprocess_crossrefs(body));
        connect(browser->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, browser,
                [browser](const QSizeF& size) { browser->setFixedHeight(int(size.height()) + 24); });
        browser->document()->setTextWidth(browser->viewport()->width());
        browser->setFixedHeight(int(browser->document()->size().height()) + 24);
        connect(browser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
            if (url.scheme() == "knowledge")
                emit open_entry(url.host().isEmpty() ? url.path().mid(1) : url.host());
            else
                QDesktopServices::openUrl(url);
        });
        vl->addWidget(browser);
    }

    vl->addStretch();
    scroll->setWidget(page);
    content_ = scroll;
    root_->addWidget(content_, 1);
}

} // namespace fincept::knowledge

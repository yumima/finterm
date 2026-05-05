#include "screens/knowledge/EntryBodyView.h"

#include "screens/knowledge/ContentLoader.h"
#include "ui/theme/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QDesktopServices>
#include <QFont>
#include <QFrame>
#include <QPalette>
#include <QTextBlock>
#include <QTextCursor>
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

// VS Code markdown preview parity — warm dark palette matching the reference
// screenshot, asymmetric paragraph spacing (16px bottom, 0 top), generous
// heading margins (24/16), 1.6 line-height, h1/h2 separator rules. Fonts
// target what's actually installed on common Linux distros so the rendering
// is consistent rather than silently falling back to Qt defaults.
constexpr const char* MONO =
    "font-family: 'DejaVu Sans Mono','Liberation Mono','Menlo','Consolas','Courier New',monospace;";
constexpr const char* SANS =
    "font-family: 'Noto Sans','Cantarell','DejaVu Sans','Liberation Sans','Segoe UI','Inter','Helvetica Neue',Arial,sans-serif;";
constexpr const char* BODY_BG       = "#1f1d1b"; ///< warm dark, subtle brown undertone
constexpr const char* BODY_TEXT     = "#f0e8d0"; ///< match HEAD_TEXT — uniform reading brightness
constexpr const char* HEAD_TEXT     = "#f0e8d0"; ///< brighter cream — title, headings, bold, body
constexpr const char* MUTED_TEXT    = "#a89878"; ///< muted warm tan (subtitles, blockquote, captions)
constexpr const char* CODE_TEXT     = "#d4a878"; ///< warm tan for inline code
constexpr const char* CODE_BG       = "#2a221a"; ///< warm-dark chip background
constexpr const char* PRE_BG        = "#15110d"; ///< slightly darker than body for code blocks
constexpr const char* LINK_COLOR    = "#6dc1c9"; ///< muted cyan link
constexpr const char* RULE_COLOR    = "#3a2e22"; ///< warm border / hr / h2 separator

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

// Minimal CSS used as QTextDocument default stylesheet.
//
// We only set what Qt's setMarkdown actually honors: inline character format
// for `<code>` and `<a>` (color/background/font-family). Block layout — sizes,
// margins, line-height — is driven by setDefaultFont + QTextBlockFormat after
// setMarkdown, which is the only thing Qt's layout engine reads.
//
// IMPORTANT: never use `font-size` here. Qt ignores it for block elements but
// honors it for inline elements; mixed-size inline runs inside a block break
// the layout (the "NVDA $400 is 4× larger than body" bug). Inline elements
// inherit size from defaultFont, which is correct.
QString markdown_default_css() {
    return QString(
               "code { background: %1; color: %2; %3 }"
               "a { color: %4; text-decoration: none; }")
        .arg(QString(CODE_BG),
             QString(CODE_TEXT),
             QString(MONO),
             QString(LINK_COLOR));
}

QString text_browser_ss() {
    // Qt's setStyleSheet() takes precedence over QPalette for foreground color.
    // Putting `color:` directly in the QSS is the only reliable way to set
    // body text color — setPalette(QPalette::Text) is silently ignored when
    // a stylesheet is in play. Uses the same cream as the title (HEAD_TEXT).
    return QString("QTextBrowser { background: %1; color: %5; border: none; padding: 0;"
                   " selection-background-color: %2; }"
                   "QScrollBar:vertical { width: 6px; background: transparent; }"
                   "QScrollBar::handle:vertical { background: %3; }"
                   "QScrollBar::handle:vertical:hover { background: %4; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(QString(BODY_BG), ui::colors::ACCENT_BG(),
             ui::colors::BORDER_MED(), ui::colors::BORDER_BRIGHT(),
             QString(HEAD_TEXT));
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
    vl->setContentsMargins(40, 32, 40, 48);  // VS Code-preview-style gutters
    vl->setSpacing(16);                       // breathing between header / warnings / body

    if (!current_) {
        auto* lbl = new QLabel("(No entry selected.)", page);
        lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: %3px; %2")
                               .arg(ui::colors::TEXT_DIM(), MONO)
                               .arg(11));
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
        // Only flag advanced/pro entries — beginner & intermediate are the
        // default reading level and don't need a badge.
        const QString d = current_->difficulty;
        const bool show_pill = (d == Difficulty::ADVANCED || d == Difficulty::PRO);
        if (show_pill) {
            const auto ds = difficulty_style(d);
            auto* diff = new QLabel(ds.label, page);
            diff->setStyleSheet(QString("color: %1; background: %2; padding: 2px 8px; font-size: %4px;"
                                        " font-weight: bold; letter-spacing: 1.5px; %3")
                                    .arg(ds.fg, ds.bg, MONO)
                                    .arg(9));
            top->addWidget(diff);
        }
        if (!current_->abbreviation.isEmpty()) {
            auto* abbr = new QLabel(current_->abbreviation, page);
            abbr->setStyleSheet(QString("color: %1; background: transparent; padding: 2px 0; font-size: %3px; %2")
                                    .arg(ui::colors::TEXT_TERTIARY(), MONO)
                                    .arg(10));
            top->addWidget(abbr);
        }
        top->addStretch();
        vl->addLayout(top);

        auto* title = new QLabel(current_->title, page);
        title->setStyleSheet(QString("color: %1; background: transparent; font-size: %3px; font-weight: 700;"
                                     " padding-top: 6px; padding-bottom: 4px; %2")
                                 .arg(QString(HEAD_TEXT), QString(SANS))
                                 .arg(28));
        title->setWordWrap(true);
        vl->addWidget(title);

        if (!current_->subtitle.isEmpty()) {
            auto* sub = new QLabel(current_->subtitle, page);
            sub->setStyleSheet(QString("color: %1; background: transparent; font-size: %3px; line-height: 165%%;"
                                       " padding-bottom: 14px; %2")
                                   .arg(QString(MUTED_TEXT), QString(SANS))
                                   .arg(14));
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
        tag->setStyleSheet(QString("color: %1; background: transparent; font-size: %3px; font-weight: bold;"
                                   " letter-spacing: 1.5px; %2")
                               .arg(ws.border, MONO)
                               .arg(9));
        bl->addWidget(tag);
        auto* text = new QLabel(w.text, box);
        text->setStyleSheet(QString("color: %1; background: transparent; font-size: %3px; line-height: 155%%; %2")
                                .arg(QString(BODY_TEXT), QString(SANS))
                                .arg(12));
        text->setWordWrap(true);
        bl->addWidget(text, 1);
        vl->addWidget(box);
    }

    // ── Body markdown ─────────────────────────────────────────────────────────
    QString body = ContentLoader::instance().load_body(current_->id);
    if (body.isEmpty()) {
        auto* miss = new QLabel("(No content body.)", page);
        miss->setStyleSheet(QString("color: %1; background: transparent; font-size: %3px; %2")
                                .arg(ui::colors::TEXT_DIM(), MONO)
                                .arg(11));
        vl->addWidget(miss);
    } else {
        auto* browser = new QTextBrowser(page);
        browser->setOpenLinks(false);
        browser->setOpenExternalLinks(false);
        browser->setStyleSheet(text_browser_ss());
        browser->setFrameShape(QFrame::NoFrame);
        browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Qt's QTextDocument::setMarkdown:
        //   - sizes everything off `defaultFont` (CSS font-size ignored)
        //   - colors body text from `QPalette::Text` (CSS body color ignored)
        //   - uses internal hardcoded block margins (CSS margin/padding ignored)
        //   - DOES honor defaultStyleSheet for inline character formatting
        //     (<code> color/bg, <a> color), so our CSS isn't entirely wasted.
        //
        // To control spacing, we walk QTextBlock after setMarkdown and override
        // QTextBlockFormat margins + line-height directly — this is the only
        // Qt mechanism the layout engine actually reads.
        QFont base_font;
        base_font.setFamilies({"Noto Sans", "Cantarell", "DejaVu Sans", "Liberation Sans"});
        base_font.setPixelSize(21);
        browser->document()->setDefaultFont(base_font);
        QPalette pal = browser->palette();
        pal.setColor(QPalette::Text, QColor(HEAD_TEXT));
        pal.setColor(QPalette::WindowText, QColor(HEAD_TEXT));
        browser->setPalette(pal);
        browser->document()->setDefaultStyleSheet(markdown_default_css());
        browser->setMarkdown(preprocess_crossrefs(body));

        // Block-margin override — VS Code / GitHub Markdown CSS values.
        //   h1, h2: 24 / 16 (line-height 125)
        //   h3:     24 / 12
        //   h4:     20 /  8
        //   p, pre, table, blockquote, body: 0 / 16  (line-height 160)
        //   list item:                       0 /  4  (line-height 160)
        //
        // CRITICAL: do this BEFORE connecting documentSizeChanged. Otherwise
        // every setBlockFormat() during the walk would fire the resize signal,
        // causing recursive layout passes (freeze + garbled paint).
        // The QTextCursor::beginEditBlock / endEditBlock pair batches the
        // edits into a single layout invalidation.
        auto* doc = browser->document();
        QTextCursor batch(doc);
        batch.beginEditBlock();
        bool first_block = true;
        for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
            QTextBlockFormat fmt = blk.blockFormat();
            const int level = fmt.headingLevel();
            // VS Code preview cadence: tighter than github-markdown-css's 160%.
            // Body lines around 1.5×, headings around 1.15× (just enough to keep
            // descenders clear). These match the reference screenshot density.
            int top = 0, bottom = 16;
            int lh_pct = 150;
            switch (level) {
                case 1: top = 24; bottom = 16; lh_pct = 115; break;
                case 2: top = 24; bottom = 16; lh_pct = 115; break;
                case 3: top = 24; bottom = 12; lh_pct = 115; break;
                case 4: top = 20; bottom =  8; lh_pct = 115; break;
                default:
                    if (blk.textList() != nullptr) {
                        top = 0; bottom = 4; lh_pct = 150;   // adjacent list items
                    } else {
                        top = 0; bottom = 16; lh_pct = 150;  // p, pre, table, blockquote
                    }
                    break;
            }
            if (first_block) {
                top = 0;  // never push the first block down
                first_block = false;
            }
            fmt.setTopMargin(top);
            fmt.setBottomMargin(bottom);
            fmt.setLineHeight(lh_pct, QTextBlockFormat::ProportionalHeight);
            QTextCursor cur(blk);
            cur.setBlockFormat(fmt);
            // Qt auto-scales headings from defaultFont, which at 21 px base
            // would push H1 to ~42 px. Pin each level to a gentle type scale.
            if (level >= 1 && level <= 6) {
                constexpr int kHeadPx[] = {28, 25, 23, 22, 21, 21};  // H1–H6
                QFont hfont = base_font;
                hfont.setPixelSize(kHeadPx[level - 1]);
                hfont.setBold(true);
                QTextCharFormat cfmt;
                cfmt.setFont(hfont);
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                cur.mergeCharFormat(cfmt);
            }
        }
        batch.endEditBlock();

        // Connect the auto-height handler AFTER the walk has fully committed,
        // so it fires once on the final document size rather than N times.
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

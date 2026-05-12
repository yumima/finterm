#include "screens/news/NewsFeedDelegate.h"

#include "screens/news/NewsFeedModel.h"
#include "services/news/NewsService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QPainter>
#include <QStyleOptionViewItem>
#include <algorithm>

namespace fincept::screens {

// News reading palette — matches the Knowledge body so the news feed reads
// like a book at length (warm cream on warm-dark brown). The global theme
// TEXT_PRIMARY / TEXT_SECONDARY are cool greys tuned for data tables, which
// read harsh against this warm-dark background.
static const QColor kNewsTextPrimary  (0xf0, 0xe8, 0xd0);  // matches Knowledge HEAD_TEXT
static const QColor kNewsTextSecondary(0xa8, 0x98, 0x78);  // matches Knowledge MUTED_TEXT

NewsFeedDelegate::NewsFeedDelegate(QObject* parent)
    : QStyledItemDelegate(parent),
      data_font_(ui::fonts::DATA_FAMILY, ui::fonts::TINY),
      bold_font_(ui::fonts::DATA_FAMILY, ui::fonts::TINY),
      tiny_font_(ui::fonts::DATA_FAMILY, 10),
      data_fm_(data_font_),
      bold_fm_(bold_font_),
      tiny_fm_(tiny_font_) {
    bold_font_.setBold(true);
    bold_fm_ = QFontMetrics(bold_font_);
}

void NewsFeedDelegate::set_source_col_width(int px) {
    source_col_width_ = std::clamp(px, kSourceColMin, kSourceColMax);
}

QSize NewsFeedDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option);
    auto mode = index.data(ViewModeRole).toString();
    if (mode == "CLUSTERS") {
        auto cluster = index.data(ClusterRole).value<services::NewsCluster>();
        int related_count = std::max(0, cluster.source_count - 1);
        return QSize(0, 72 + std::min(related_count, 3) * 16);
    }
    return QSize(0, 26);
}

void NewsFeedDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::TextAntialiasing);

    bool selected = index.data(IsSelectedRole).toBool();
    bool hovered = option.state & QStyle::State_MouseOver;

    auto mode = index.data(ViewModeRole).toString();
    if (mode == "CLUSTERS")
        paint_cluster_card(painter, option.rect, index, selected, hovered);
    else
        paint_wire_row(painter, option.rect, index, selected, hovered);

    painter->restore();
}

void NewsFeedDelegate::paint_wire_row(QPainter* painter, const QRect& rect, const QModelIndex& index, bool selected,
                                      bool hovered) const {
    // GCC 12 false-positive: -Warray-bounds on QVariant::value<NewsArticle>()
    // inlined move/copy — the analyzer wrongly treats the temporary as
    // QVariant[1]. Suppress locally; struct layout is fixed and QVariant
    // conversion is validated by QMetaType.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
    auto article = index.data(ArticleRole).value<services::NewsArticle>();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    auto monitor_color = index.data(MonitorColorRole).toString();
    bool is_new = index.data(IsNewRole).toBool();
    int tier = index.data(SourceTierRole).toInt();

    // Background — solid deep dark to match the Knowledge column palette.
    // The previous odd-row stripe was a hair lighter (#181613) but read as
    // an out-of-place warm brown against the rest of the UI; collapse it
    // into the base so every row carries the same dark.
    static const QColor kNewsBgBase(0x13, 0x11, 0x0f);   // matches knowledge column BG
    static const QColor kNewsBgHover(0x22, 0x1e, 0x1a);  // hover lift
    static const QColor kNewsBgSel (0x33, 0x2b, 0x22);   // selected
    if (selected)
        painter->fillRect(rect, kNewsBgSel);
    else if (hovered)
        painter->fillRect(rect, kNewsBgHover);
    else
        painter->fillRect(rect, kNewsBgBase);

    int x = rect.left() + 2;
    int cy = rect.top() + rect.height() / 2;
    int text_y = rect.top() + (rect.height() + data_fm_.ascent() - data_fm_.descent()) / 2;

    // Pulse animation for new items — amber glow that fades
    int pulse = index.data(PulsePhaseRole).toInt();
    if (is_new && pulse >= 0) {
        int alpha = (pulse == 0) ? 40 : (pulse == 1 ? 25 : (pulse == 2 ? 15 : 8));
        painter->fillRect(rect, QColor(217, 119, 6, alpha)); // amber glow overlay
    }

    // New indicator — 3px amber dot
    if (is_new) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(ui::colors::AMBER()));
        painter->drawEllipse(QPoint(x + 1, cy), 2, 2);
    }
    x += 6;

    // Monitor color stripe — 3px wide
    if (!monitor_color.isEmpty()) {
        painter->fillRect(QRect(x, rect.top(), 3, rect.height()), QColor(monitor_color));
    }
    x += 5;

    // Time
    painter->setFont(tiny_font_);
    painter->setPen(kNewsTextSecondary);
    QString time_str = services::relative_time(article.sort_ts);
    painter->drawText(QRect(x, rect.top(), 36, rect.height()), Qt::AlignVCenter | Qt::AlignRight, time_str);
    x += 40;

    // Priority dot — 4px
    QString pcolor = services::priority_color(article.priority);
    if (article.priority != services::Priority::ROUTINE) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(pcolor));
        painter->drawEllipse(QPoint(x + 2, cy), 2, 2);
    }
    x += 8;

    // Source tier badge
    painter->setFont(tiny_font_);
    if (tier == 1) {
        painter->setPen(QColor(ui::colors::AMBER()));
        painter->drawText(QPoint(x, text_y), QString::fromUtf8("\xe2\x98\x85")); // ★
    } else if (tier == 2) {
        painter->setPen(kNewsTextSecondary);
        painter->drawText(QPoint(x, text_y), QString::fromUtf8("\xe2\x97\x8f")); // ●
    } else if (tier == 3) {
        painter->setPen(kNewsTextSecondary);
        painter->drawText(QPoint(x, text_y), QString::fromUtf8("\xc2\xb7")); // ·
    }
    x += 12;

    // Pre-formatted strings from model — zero allocation in paint path
    const QString source = index.data(FormattedSourceRole).toString();
    const QString lang_badge = index.data(FormattedLangRole).toString();
    const QString threat_label = index.data(FormattedThreatRole).toString();
    const QString tickers_str = index.data(FormattedTickersRole).toString();
    const QString threat_color = index.data(ThreatColorRole).toString();

    // Source name. Width is user-adjustable via the drag handle on the panel
    // — default 110 px keeps "INVESTING.COM" / "BLOOMBERG GOV" intact, but
    // the user can widen for "WALL STREET JOURNAL" or narrow it down.
    painter->setFont(bold_font_);
    painter->setPen(QColor(ui::colors::CYAN()));
    painter->drawText(QRect(x, rect.top(), source_col_width_, rect.height()),
                      Qt::AlignVCenter | Qt::AlignLeft, source);
    x += source_col_width_ + 4;

    // Language badge (if not English)
    if (!lang_badge.isEmpty()) {
        painter->setFont(tiny_font_);
        painter->setPen(kNewsTextSecondary);
        painter->drawText(QRect(x, rect.top(), 20, rect.height()), Qt::AlignVCenter | Qt::AlignCenter, lang_badge);
        x += 22;
    }

    // Threat level left border (for MEDIUM+ threats)
    if (!threat_color.isEmpty())
        painter->fillRect(QRect(rect.left(), rect.top(), 2, rect.height()), QColor(threat_color));

    // Headline — fills remaining space
    painter->setFont(data_font_);
    bool is_hot = article.priority == services::Priority::FLASH || article.priority == services::Priority::URGENT;
    if (is_hot) {
        painter->setFont(bold_font_);
        painter->setPen(kNewsTextPrimary);
    } else {
        painter->setPen(kNewsTextSecondary);
    }

    // Right-side reservation: credibility flag (~12) + threat label (~30) +
    // sentiment arrow (~16) + tickers (up to "$XXXX $YYYY" tiny font ~90) +
    // padding. 180px keeps tickers fully visible even at the worst case;
    // headline elision absorbs the difference.
    int right_reserve = 180;
    int headline_width = rect.right() - x - right_reserve;
    if (headline_width > 0) {
        QFontMetrics& fm = is_hot ? bold_fm_ : data_fm_;
        QString elided = fm.elidedText(article.headline, Qt::ElideRight, headline_width);
        painter->drawText(QRect(x, rect.top(), headline_width, rect.height()), Qt::AlignVCenter | Qt::AlignLeft,
                          elided);
    }
    x = rect.right() - right_reserve;

    // Source credibility warning
    if (article.source_flag != services::SourceFlag::NONE) {
        painter->setFont(tiny_font_);
        QString flag_label = services::NewsService::source_flag_label(article.source_flag);
        if (article.source_flag == services::SourceFlag::STATE_MEDIA) {
            painter->setPen(QColor(ui::colors::WARNING()));                          // orange warning
            painter->drawText(QPoint(x, text_y), QString::fromUtf8("\xe2\x9a\xa0")); // ⚠
            x += 12;
        } else {
            painter->setPen(QColor(ui::colors::WARNING()));
            painter->drawText(QPoint(x, text_y), "!");
            x += 8;
        }
    }

    // Threat level badge (only for MEDIUM+) — pre-formatted, no allocation
    if (!threat_label.isEmpty()) {
        painter->setFont(tiny_font_);
        painter->setPen(QColor(threat_color));
        painter->drawText(QPoint(x, text_y), threat_label);
        x += tiny_fm_.horizontalAdvance(threat_label) + 4;
    }

    // Sentiment arrow
    painter->setFont(data_font_);
    if (article.sentiment == services::Sentiment::BULLISH) {
        painter->setPen(QColor(ui::colors::POSITIVE()));
        painter->drawText(QPoint(x, text_y), QString::fromUtf8("\xe2\x96\xb2")); // ▲
    } else if (article.sentiment == services::Sentiment::BEARISH) {
        painter->setPen(QColor(ui::colors::NEGATIVE()));
        painter->drawText(QPoint(x, text_y), QString::fromUtf8("\xe2\x96\xbc")); // ▼
    } else {
        painter->setPen(kNewsTextSecondary);
        painter->drawText(QPoint(x, text_y), "-");
    }
    x += 16;

    // Tickers — pre-joined string "$AAPL $MSFT", no allocation in paint
    if (!tickers_str.isEmpty()) {
        painter->setFont(tiny_font_);
        painter->setPen(QColor(ui::colors::WARNING()));
        painter->drawText(QPoint(x, text_y), tickers_str);
        x += tiny_fm_.horizontalAdvance(tickers_str) + 6;
    }

    // Bottom border
    painter->setPen(QColor(ui::colors::BORDER_DIM()));
    painter->drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());
}

void NewsFeedDelegate::paint_cluster_card(QPainter* painter, const QRect& rect, const QModelIndex& index, bool selected,
                                          bool hovered) const {
    auto cluster = index.data(ClusterRole).value<services::NewsCluster>();
    auto monitor_color = index.data(MonitorColorRole).toString();
    bool is_new = index.data(IsNewRole).toBool();
    auto velocity_text = index.data(VelocityTextRole).toString();

    // Card background — warm-dark per the news reading palette.
    static const QColor kCardBg     (0x1f, 0x1d, 0x1b);   // matches BODY_BG
    static const QColor kCardHover  (0x2e, 0x2a, 0x24);
    static const QColor kCardSelect (0x3a, 0x32, 0x28);
    QColor bg = selected ? kCardSelect : (hovered ? kCardHover : kCardBg);
    painter->fillRect(rect, bg);

    // Card border
    painter->setPen(QColor(ui::colors::BORDER_DIM()));
    painter->drawRect(rect.adjusted(0, 0, -1, -1));

    int x = rect.left() + 8;
    int y = rect.top() + 4;

    // Monitor stripe
    if (!monitor_color.isEmpty())
        painter->fillRect(QRect(rect.left(), rect.top(), 3, rect.height()), QColor(monitor_color));

    // New indicator
    if (is_new) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(ui::colors::AMBER()));
        painter->drawEllipse(QPoint(rect.left() + 5, rect.top() + 8), 2, 2);
    }

    // Row 1: [BREAKING badge] [tier] [headline] [velocity]
    if (cluster.is_breaking) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(ui::colors::NEGATIVE()));
        QRect badge_rect(x, y, 62, 16);
        painter->drawRect(badge_rect);
        painter->setFont(tiny_font_);
        painter->setPen(kNewsTextPrimary);
        painter->drawText(badge_rect, Qt::AlignCenter, "BREAKING");
        x += 66;
    }

    // Tier badge
    painter->setFont(tiny_font_);
    if (cluster.tier == 1) {
        painter->setPen(QColor(ui::colors::AMBER()));
        painter->drawText(QPoint(x, y + 12), QString::fromUtf8("\xe2\x98\x85"));
    } else if (cluster.tier == 2) {
        painter->setPen(kNewsTextSecondary);
        painter->drawText(QPoint(x, y + 12), QString::fromUtf8("\xe2\x97\x8f"));
    }
    x += 14;

    // Headline
    painter->setFont(bold_font_);
    painter->setPen(kNewsTextPrimary);
    int headline_w = rect.right() - x - 80;
    QString elided = bold_fm_.elidedText(cluster.lead_article.headline, Qt::ElideRight, headline_w);
    painter->drawText(QRect(x, y, headline_w, 18), Qt::AlignVCenter | Qt::AlignLeft, elided);

    // Velocity badge
    if (!velocity_text.isEmpty()) {
        int vx = rect.right() - 70;
        painter->setFont(tiny_font_);
        painter->setPen(cluster.velocity == "rising" ? QColor(ui::colors::POSITIVE())
                                                     : kNewsTextSecondary);
        painter->drawText(QPoint(vx, y + 12), velocity_text);
    }

    // Row 2: source | time | N sources | sentiment
    y += 22;
    x = rect.left() + 8;
    painter->setFont(data_font_);

    // Source
    painter->setPen(QColor(ui::colors::CYAN()));
    QString source = cluster.lead_article.source.left(12).toUpper();
    painter->drawText(QPoint(x, y + 12), source);
    x += data_fm_.horizontalAdvance(source) + 10;

    // Time
    painter->setPen(kNewsTextSecondary);
    QString time_str = services::relative_time(cluster.latest_sort_ts);
    painter->drawText(QPoint(x, y + 12), time_str);
    x += data_fm_.horizontalAdvance(time_str) + 10;

    // Source count
    if (cluster.source_count > 1) {
        painter->setPen(QColor(ui::colors::WARNING()));
        QString src_text = QString("%1 sources").arg(cluster.source_count);
        painter->drawText(QPoint(x, y + 12), src_text);
        x += data_fm_.horizontalAdvance(src_text) + 10;
    }

    // Sentiment
    if (cluster.sentiment == services::Sentiment::BULLISH) {
        painter->setPen(QColor(ui::colors::POSITIVE()));
        painter->drawText(QPoint(x, y + 12), QString::fromUtf8("\xe2\x96\xb2 BULL"));
    } else if (cluster.sentiment == services::Sentiment::BEARISH) {
        painter->setPen(QColor(ui::colors::NEGATIVE()));
        painter->drawText(QPoint(x, y + 12), QString::fromUtf8("\xe2\x96\xbc BEAR"));
    }

    // Row 3: "Also: Source1, Source2, Source3"
    y += 18;
    x = rect.left() + 8;
    if (cluster.articles.size() > 1) {
        painter->setFont(tiny_font_);
        painter->setPen(kNewsTextSecondary);

        QStringList also_sources;
        QSet<QString> seen;
        seen.insert(cluster.lead_article.source);
        for (const auto& a : cluster.articles) {
            if (!seen.contains(a.source) && also_sources.size() < 3) {
                also_sources.append(a.source);
                seen.insert(a.source);
            }
        }
        if (!also_sources.isEmpty()) {
            QString also_text = "Also: " + also_sources.join(", ");
            int also_w = rect.right() - x - 8;
            also_text = tiny_fm_.elidedText(also_text, Qt::ElideRight, also_w);
            painter->drawText(QPoint(x, y + 10), also_text);
        }
    }
}

} // namespace fincept::screens

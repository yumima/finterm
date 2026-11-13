// src/ui/theme/Tooltips.cpp
#include "ui/theme/Tooltips.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QHeaderView>
#include <QHelpEvent>
#include <QProxyStyle>
#include <QStyle>
#include <QTextDocument> // Qt::mightBeRichText
#include <QToolTip>
#include <QWidget>

namespace finterm::ui::tooltips {
namespace {

/// Width the wrapped box is laid out to. Wide enough that a two-line
/// explanation doesn't become six, narrow enough to read without tracking
/// across the monitor.
constexpr int kWrapWidthPx = 380;

/// Longest text a tooltip shows (≈ 12 lines at kWrapWidthPx). A hover box is
/// for a reminder, not an article: anything longer is cut at the last
/// paragraph (or sentence) break that fits, so it can never grow down the
/// screen the way it could across it.
constexpr int kMaxChars = 600;

/// Below this a tooltip is a label ("Refresh", "Held in: Growth"), and boxing
/// it would put a paragraph-shaped frame around three words.
constexpr int kWrapThresholdChars = 55;

/// Zero wake-up delay. The hint is the only way to change it — the delay is
/// applied by QWidget's tooltip machinery before any ToolTip event is sent, so
/// no event filter can make a tooltip appear sooner than the style allows.
class InstantTooltipStyle : public QProxyStyle {
  public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override {
        if (hint == SH_ToolTip_WakeUpDelay)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

/// Plain text → a fixed-width rich-text box.
///
/// The table is not decoration. Qt's rich-text engine honours an explicit
/// table width in pixels, while `width` on a <div> or <p> is ignored — so a
/// table is what actually bounds the box. Without it, "rich text" only buys
/// word wrap at whatever width the paragraph happens to want, which for a long
/// tooltip is still the whole screen.
QString clamp(const QString& text) {
    if (text.size() <= kMaxChars)
        return text;
    const QString head = text.left(kMaxChars);
    int cut = head.lastIndexOf(QLatin1String("\n\n"));
    if (cut < kMaxChars / 2)
        cut = head.lastIndexOf(QLatin1String(". ")) + 1;
    if (cut < kMaxChars / 2)
        cut = kMaxChars;
    return head.left(cut).trimmed() + QStringLiteral(" …");
}

QString to_box(const QString& tip) {
    QString html;
    if (Qt::mightBeRichText(tip)) {
        html = tip; // rich already: keep its markup, only bound its width
    } else {
        html = clamp(tip).toHtmlEscaped();
        // Author-intended line breaks survive; the wrap does the rest.
        html.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    }
    return QStringLiteral("<qt><table width=\"%1\" cellpadding=\"0\" cellspacing=\"0\">"
                          "<tr><td>%2</td></tr></table></qt>")
        .arg(kWrapWidthPx)
        .arg(html);
}

/// The model tooltip under `pos` when `w` is an item view's viewport or a
/// header; empty otherwise.
QString item_tooltip(QWidget* w, const QPoint& pos) {
    // Events reach the view's viewport, a child of the view (headers too).
    auto* view = w->parentWidget() ? qobject_cast<QAbstractItemView*>(w->parentWidget()) : nullptr;
    if (!view || view->viewport() != w)
        return {};
    if (auto* header = qobject_cast<QHeaderView*>(view)) {
        const int section = header->logicalIndexAt(pos);
        if (section >= 0 && header->model())
            return header->model()->headerData(section, header->orientation(), Qt::ToolTipRole).toString();
        return {};
    }
    const QModelIndex idx = view->indexAt(pos);
    return idx.isValid() ? idx.data(Qt::ToolTipRole).toString() : QString();
}

class TooltipFormatter : public QObject {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() != QEvent::ToolTip)
            return QObject::eventFilter(obj, event);

        auto* w = qobject_cast<QWidget*>(obj);
        if (!w)
            return QObject::eventFilter(obj, event);

        auto* help = static_cast<QHelpEvent*>(event);
        QString tip = w->toolTip();
        // Tables and headers keep their tooltips in the model (ToolTipRole),
        // not on the widget: read the cell or section under the cursor, so
        // a long cell tooltip is boxed like any other.
        if (tip.isEmpty())
            tip = item_tooltip(w, help->pos());
        // Nothing here: somebody else answers this event.
        if (tip.isEmpty())
            return QObject::eventFilter(obj, event);

        // Short enough that one line is the better shape.
        if (tip.size() <= kWrapThresholdChars && !tip.contains(QLatin1Char('\n')))
            return QObject::eventFilter(obj, event);

        // Anchored on the cursor rather than the widget: for a wide row or a
        // full-width panel, "near the widget" can be a long way from the thing
        // being pointed at.
        QToolTip::showText(help->globalPos(), to_box(tip), w);
        return true;
    }
};

} // namespace

void install(QApplication& app) {
    // Construct the proxy from the style's KEY, not the QStyle* itself.
    // QProxyStyle takes ownership of a style passed by pointer, and
    // QApplication::setStyle() then deletes the style being replaced — which
    // would be that same object, leaving the proxy holding a dangling base.
    // The key form has QProxyStyle build its own instance instead.
    app.setStyle(new InstantTooltipStyle(app.style()->objectName()));
    app.installEventFilter(new TooltipFormatter(&app));
}

} // namespace finterm::ui::tooltips

// src/ui/theme/Tooltips.cpp
#include "ui/theme/Tooltips.h"

#include <QApplication>
#include <QHelpEvent>
#include <QProxyStyle>
#include <QStyle>
#include <QTextDocument>   // Qt::mightBeRichText
#include <QToolTip>
#include <QWidget>

namespace fincept::ui::tooltips {
namespace {

/// Width the wrapped box is laid out to. Wide enough that a two-line
/// explanation doesn't become six, narrow enough to read without tracking
/// across the monitor.
constexpr int kWrapWidthPx = 380;

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
QString to_box(const QString& plain) {
    QString html = plain.toHtmlEscaped();
    // Author-intended line breaks survive; the wrap does the rest.
    html.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    return QStringLiteral("<qt><table width=\"%1\" cellpadding=\"0\" cellspacing=\"0\">"
                          "<tr><td>%2</td></tr></table></qt>")
        .arg(kWrapWidthPx)
        .arg(html);
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

        const QString tip = w->toolTip();
        // An empty widget tooltip means somebody else answers this event.
        // Item views resolve Qt::ToolTipRole from the model inside their own
        // handler, so consuming it here would silence every table cell in the
        // app. Let the default path run.
        if (tip.isEmpty())
            return QObject::eventFilter(obj, event);

        // Already rich (it wraps by itself), or short enough that one line is
        // the better shape.
        if (Qt::mightBeRichText(tip) ||
            (tip.size() <= kWrapThresholdChars && !tip.contains(QLatin1Char('\n'))))
            return QObject::eventFilter(obj, event);

        // Anchored on the cursor rather than the widget: for a wide row or a
        // full-width panel, "near the widget" can be a long way from the thing
        // being pointed at.
        auto* help = static_cast<QHelpEvent*>(event);
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

} // namespace fincept::ui::tooltips

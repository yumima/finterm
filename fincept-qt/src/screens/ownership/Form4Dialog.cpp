#include "screens/ownership/Form4Dialog.h"

#include "ui/components/ExternalLink.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

namespace {

QString esc(const QString& s) { return s.toHtmlEscaped(); }

/// Direction and intent in one colour. Only P and S are decisions about price;
/// everything else is compensation mechanics and is left neutral, so a page of
/// vesting grants cannot read as a page of buying.
QString row_colour(const InsiderTransaction& t) {
    if (!t.open_market)
        return ui::colors::TEXT_SECONDARY();
    return t.acquired ? ui::colors::GREEN() : ui::colors::RED();
}

} // namespace

Form4Dialog::Form4Dialog(const QVector<InsiderTransaction>& filing, const QString& issuer,
                         QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Form 4 — %1").arg(issuer));
    resize(760, 520);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);

    QString html = QStringLiteral(
        "<style>"
        "body{color:%1;font-size:13px;}"
        "h2{color:%2;font-size:15px;margin:0 0 2px 0;}"
        "th{color:%3;text-align:left;padding:4px 12px 4px 0;font-weight:600;}"
        "td{padding:3px 12px 3px 0;}"
        ".dim{color:%3;}"
        "</style>")
                       .arg(ui::colors::TEXT_PRIMARY(), ui::colors::ORANGE(),
                            ui::colors::TEXT_SECONDARY());

    const auto& first = filing.first();
    html += QStringLiteral("<h2>%1</h2>").arg(esc(issuer));
    html += QStringLiteral("<div class='dim'>%1%2</div>")
                .arg(esc(first.insider),
                     first.roles.isEmpty()
                         ? QString()
                         : QStringLiteral(" · ") + esc(first.roles.join(QStringLiteral(", "))));
    if (first.filed_date.isValid())
        html += QStringLiteral("<div class='dim'>Filed %1</div>")
                    .arg(first.filed_date.toString(QStringLiteral("d MMM yyyy")));

    html += QStringLiteral(
        "<br/><table width='100%'><tr><th>Date</th><th>Transaction</th><th>Security</th>"
        "<th align='right'>Shares</th><th align='right'>Price</th>"
        "<th align='right'>Value</th><th align='right'>Held after</th></tr>");

    for (const auto& t : filing) {
        html += QStringLiteral(
                    "<tr>"
                    "<td>%1</td>"
                    "<td style='color:%2;'>%3</td>"
                    "<td class='dim'>%4</td>"
                    "<td align='right'>%5</td>"
                    "<td align='right'>%6</td>"
                    "<td align='right' style='color:%2;'>%7</td>"
                    "<td align='right' class='dim'>%8</td>"
                    "</tr>")
                    .arg(t.date.toString(QStringLiteral("yyyy-MM-dd")),
                         row_colour(t),
                         esc(t.code_label.isEmpty() ? t.code : t.code_label),
                         esc(t.security),
                         t.shares ? fmt::format_compact(*t.shares) : fmt::placeholder(),
                         // A filed price of zero is a real value — a gift, a
                         // grant — and is shown as filed. An absent price is
                         // shown as absent, never as zero.
                         t.price ? fmt::format_money(*t.price) : fmt::placeholder(),
                         t.value ? fmt::format_compact(*t.value) : fmt::placeholder(),
                         t.shares_held_after ? fmt::format_compact(*t.shares_held_after)
                                             : fmt::placeholder());
    }
    html += QStringLiteral("</table>");
    html += QStringLiteral(
                "<br/><div class='dim'>Codes: P open-market purchase · S open-market sale · "
                "A grant or award · M option exercise · F shares withheld for tax · G gift. "
                "Only P and S are decisions about price.</div>");

    auto* view = new QTextBrowser;
    view->setOpenExternalLinks(false);
    view->setHtml(html);
    view->setStyleSheet(QString("QTextBrowser{background:%1;border:1px solid %2;}")
                            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    v->addWidget(view, 1);

    auto* buttons = new QDialogButtonBox;
    // The filing itself stays one click away: this is a rendering, and the
    // reader must always be able to reach the document it was read from.
    auto* edgar = buttons->addButton(QStringLiteral("Open on EDGAR"),
                                     QDialogButtonBox::ActionRole);
    const QString url = first.source_url;
    edgar->setEnabled(!url.isEmpty());
    connect(edgar, &QPushButton::clicked, this, [url]() { ui::open_external_link(url); });
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);
}

} // namespace fincept::screens

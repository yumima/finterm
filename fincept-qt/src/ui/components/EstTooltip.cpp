// src/ui/components/EstTooltip.cpp
#include "ui/components/EstTooltip.h"

#include "ui/theme/Theme.h"

namespace fincept::ui::est {

namespace {

QString wrap(const QString& title, const QString& body) {
    return QStringLiteral(
        "<div style='font-family:Consolas,monospace;'>"
          "<div style='font-weight:700;color:%1;letter-spacing:0.5px;"
                     "margin-bottom:4px;'>%2</div>"
          "<div style='color:%3;'>%4</div>"
        "</div>")
        .arg(QString::fromLatin1(ui::colors::AMBER()),
             title,
             QString::fromLatin1(ui::colors::TEXT_SECONDARY()),
             body);
}

} // namespace

QString amount_tooltip() {
    return wrap(QStringLiteral("ESTIMATED AMOUNT"),
        QStringLiteral("STOCK Act PTRs disclose a $-range "
            "(e.g. $50,001–$100,000), not an exact value. "
            "Computations use the range midpoint."));
}

QString alpha_tooltip() {
    return wrap(QStringLiteral("ESTIMATED ALPHA"),
        QStringLiteral("YTD return of a reconstructed portfolio built from "
            "disclosed trades (using each range's midpoint) MINUS SPY's "
            "YTD return."));
}

QString return_tooltip() {
    return wrap(QStringLiteral("ESTIMATED RETURN"),
        QStringLiteral("YTD return of a reconstructed portfolio built from "
            "disclosed trades (using each range's midpoint). Not the "
            "member's actual return — they hold positions we can't see."));
}

QString net_worth_tooltip() {
    return wrap(QStringLiteral("ESTIMATED NET WORTH"),
        QStringLiteral("Senate Annual Financial Disclosure asset categories "
            "summed at midpoint. Available for senators only — House "
            "annual reports aren't machine-readable. Liabilities and "
            "private holdings are not included."));
}

QString portfolio_tooltip() {
    return wrap(QStringLiteral("ESTIMATED PORTFOLIO"),
        QStringLiteral("Reconstructed by accumulating disclosed buys and "
            "sells at each range's midpoint. Doesn't include positions "
            "outside the STOCK Act / OGE disclosure scope."));
}

QString conflict_score_tooltip() {
    return wrap(QStringLiteral("CONFLICT SCORE"),
        QStringLiteral("Holdings overlap with the official's regulatory "
            "or budget domain, weighted by holding size. Inputs: OGE "
            "Form 278 disclosed holdings + agency mandate map."));
}

QString disclosure_lag_tooltip() {
    return wrap(QStringLiteral("DISCLOSURE LAG"),
        QStringLiteral("Calendar days between the transaction date and "
            "the receipt date stamped on the filed PTR. STOCK Act "
            "requires filing within 45 days."));
}

QString with_marker(const QString& label) {
    return label + QStringLiteral(" <span style='color:")
                 + QString::fromLatin1(ui::colors::TEXT_TERTIARY())
                 + QStringLiteral(";font-size:11px;'>(est.)</span>");
}

} // namespace fincept::ui::est

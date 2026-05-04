#include "screens/knowledge/ExposurePanel.h"

#include "services/markets/MarketDataService.h"
#include "storage/repositories/PortfolioHoldingsRepository.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {

constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";

struct Criterion {
    QString metric;       ///< pe_ratio, beta, dividend_yield, etc.
    QString op;           ///< ">" "<" ">=" "<="
    double  threshold;
    bool    valid = false;
};

/// Parse "P/E > 30" / "beta > 1.5" / "yield > 4%" into a structured filter.
Criterion parse_criterion(const QString& text) {
    Criterion c;
    if (text.trimmed().isEmpty())
        return c;

    static const QRegularExpression re(R"(^\s*(.+?)\s*(>=|<=|>|<)\s*([\d\.]+)\s*%?\s*$)");
    const auto m = re.match(text);
    if (!m.hasMatch())
        return c;

    const QString name = m.captured(1).trimmed().toLower();
    c.op = m.captured(2);
    c.threshold = m.captured(3).toDouble();

    // Map the human-readable name to the InfoData field name.
    if (name == "p/e" || name == "pe" || name == "pe ratio") c.metric = "pe_ratio";
    else if (name == "fwd p/e" || name == "forward pe") c.metric = "forward_pe";
    else if (name == "p/b" || name == "pb")          c.metric = "price_to_book";
    else if (name == "yield" || name == "dividend yield") {
        c.metric = "dividend_yield";
        // yfinance returns yield as a decimal (0.025) but criteria use percent (2.5%)
        c.threshold /= 100.0;
    } else if (name == "beta")                       c.metric = "beta";
    else if (name == "roe" || name == "return on equity") {
        c.metric = "roe";
        c.threshold /= 100.0;
    } else if (name == "market cap" || name == "mcap") c.metric = "market_cap";
    else if (name == "eps")                          c.metric = "eps";
    else if (name == "debt to equity" || name == "d/e") c.metric = "debt_to_equity";
    else c.metric = name;

    c.valid = !c.metric.isEmpty();
    return c;
}

bool compare(double v, const QString& op, double t) {
    if (op == ">") return v > t;
    if (op == "<") return v < t;
    if (op == ">=") return v >= t;
    if (op == "<=") return v <= t;
    return false;
}

double pluck_metric(const services::InfoData& info, const QString& metric) {
    if (metric == "pe_ratio") return info.pe_ratio;
    if (metric == "forward_pe") return info.forward_pe;
    if (metric == "price_to_book") return info.price_to_book;
    if (metric == "dividend_yield") return info.dividend_yield;
    if (metric == "beta") return info.beta;
    if (metric == "market_cap") return info.market_cap;
    if (metric == "eps") return info.eps;
    if (metric == "roe") return info.roe;
    if (metric == "debt_to_equity") return info.debt_to_equity;
    return 0.0;
}

} // namespace

ExposurePanel::ExposurePanel(const KnowledgeEntry& entry, QWidget* parent) : QWidget(parent), entry_(entry) {
    setStyleSheet("background: transparent;");
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(0, 0, 0, 0);
    root_->setSpacing(4);

    status_ = new QLabel(QString("Checking your portfolio…"), this);
    status_->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                               .arg(ui::colors::TEXT_DIM(), MONO));
    status_->setWordWrap(true);
    root_->addWidget(status_);

    load_holdings_and_evaluate();
}

void ExposurePanel::load_holdings_and_evaluate() {
    const auto crit = parse_criterion(entry_.exposure_criterion);

    auto& repo = PortfolioHoldingsRepository::instance();
    auto result = repo.get_active();
    if (!result.is_ok()) {
        status_->setText(QString("No portfolio data yet — add holdings in the Portfolio tab to see exposure here."));
        return;
    }
    const auto holdings = result.value();
    if (holdings.isEmpty()) {
        status_->setText(QString("Your portfolio is empty — add holdings in the Portfolio tab to see exposure here."));
        return;
    }
    if (!crit.valid) {
        // No criterion — just show holding count + the explicit exposure_tickers list.
        QStringList in_portfolio;
        for (const auto& h : holdings) {
            for (const auto& t : entry_.exposure_tickers) {
                if (h.symbol.compare(t, Qt::CaseInsensitive) == 0) {
                    in_portfolio << h.symbol.toUpper();
                    break;
                }
            }
        }
        if (entry_.exposure_tickers.isEmpty()) {
            status_->setText(QString("You hold %1 position%2.").arg(holdings.size()).arg(holdings.size() == 1 ? "" : "s"));
        } else {
            status_->setText(in_portfolio.isEmpty()
                ? QString("None of the example tickers (%1) are in your portfolio.").arg(entry_.exposure_tickers.join(", "))
                : QString("In your portfolio: %1").arg(in_portfolio.join(", ")));
        }
        return;
    }

    // Fetch info for each holding and apply the criterion.
    status_->setText(QString("Checking %1 holdings against %2…").arg(holdings.size()).arg(entry_.exposure_criterion));

    QPointer<ExposurePanel> guard(this);
    auto* shared_count = new int(0);
    auto* shared_matches = new QStringList();
    const int total = holdings.size();
    for (const auto& h : holdings) {
        const QString sym = h.symbol;
        services::MarketDataService::instance().fetch_info(
            sym, [guard, sym, crit, shared_count, shared_matches, total](bool ok, services::InfoData info) {
                ++*shared_count;
                if (ok) {
                    const double v = pluck_metric(info, crit.metric);
                    if (v != 0.0 && compare(v, crit.op, crit.threshold))
                        shared_matches->push_back(sym.toUpper());
                }
                if (*shared_count >= total) {
                    if (guard) {
                        QStringList m = *shared_matches;
                        std::sort(m.begin(), m.end());
                        guard->render_results(total, m, guard->entry_.exposure_criterion);
                    }
                    delete shared_count;
                    delete shared_matches;
                }
            });
    }
}

void ExposurePanel::render_results(int total, const QStringList& matching, const QString& criterion) {
    if (matching.isEmpty()) {
        status_->setText(QString("Of your %1 holdings, none currently match: %2").arg(total).arg(criterion));
        return;
    }
    status_->setText(QString("%1 of %2 holdings match \"%3\":\n%4")
                         .arg(matching.size())
                         .arg(total)
                         .arg(criterion)
                         .arg(matching.join(", ")));
}

} // namespace fincept::knowledge

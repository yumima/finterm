#include "screens/knowledge/LiveDataPanel.h"

#include "services/markets/MarketDataService.h"
#include "ui/theme/Theme.h"

#include <QGridLayout>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::knowledge {

namespace {
constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";
} // namespace

LiveDataPanel::LiveDataPanel(const QVector<LiveExample>& examples, QWidget* parent)
    : QWidget(parent), examples_(examples) {
    setStyleSheet(QString("background: transparent;"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    grid_ = new QGridLayout;
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(14);
    grid_->setVerticalSpacing(4);
    grid_->setColumnStretch(0, 1);

    value_labels_.reserve(examples_.size());
    for (int i = 0; i < examples_.size(); ++i) {
        const auto& ex = examples_[i];
        auto* lbl = new QLabel(ex.label, this);
        lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                               .arg(ui::colors::TEXT_SECONDARY(), MONO));
        grid_->addWidget(lbl, i, 0);

        auto* val = new QLabel(QString::fromUtf8("—"), this);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val->setStyleSheet(QString("color: %1; background: transparent; font-size: 12px; font-weight: bold; %2")
                               .arg(ui::colors::AMBER(), MONO));
        grid_->addWidget(val, i, 1);
        value_labels_.push_back(val);
    }
    root->addLayout(grid_);

    fetch_all();
}

void LiveDataPanel::fetch_all() {
    // Group by ticker so we hit the service once per unique symbol.
    QHash<QString, QVector<int>> rows_by_ticker;
    for (int i = 0; i < examples_.size(); ++i) {
        const auto t = examples_[i].ticker.trimmed();
        if (!t.isEmpty())
            rows_by_ticker[t].push_back(i);
    }

    QPointer<LiveDataPanel> guard(this);
    for (auto it = rows_by_ticker.constBegin(); it != rows_by_ticker.constEnd(); ++it) {
        const QString ticker = it.key();
        const QVector<int> rows = it.value();
        services::MarketDataService::instance().fetch_info(
            ticker, [guard, ticker, rows](bool ok, services::InfoData info) {
                if (!guard)
                    return;
                for (int row : rows) {
                    if (row < 0 || row >= guard->examples_.size())
                        continue;
                    const auto& ex = guard->examples_[row];
                    if (!ok) {
                        guard->apply_value(row, 0.0, false);
                        continue;
                    }
                    const auto& m = ex.metric;
                    double v = NAN;
                    if (m == "pe_ratio") v = info.pe_ratio;
                    else if (m == "forward_pe") v = info.forward_pe;
                    else if (m == "price_to_book") v = info.price_to_book;
                    else if (m == "dividend_yield") v = info.dividend_yield;
                    else if (m == "beta") v = info.beta;
                    else if (m == "market_cap") v = info.market_cap;
                    else if (m == "eps") v = info.eps;
                    else if (m == "roe") v = info.roe;
                    else if (m == "profit_margin") v = info.profit_margin;
                    else if (m == "debt_to_equity") v = info.debt_to_equity;
                    else if (m == "current_ratio") v = info.current_ratio;
                    else if (m == "week52_high") v = info.week52_high;
                    else if (m == "week52_low") v = info.week52_low;
                    guard->apply_value(row, v, !std::isnan(v));
                }
            });
    }
}

void LiveDataPanel::apply_value(int row_index, double raw_value, bool ok) {
    if (row_index < 0 || row_index >= value_labels_.size())
        return;
    auto* lbl = value_labels_[row_index];
    if (!ok || raw_value == 0.0) {
        lbl->setText("n/a");
        lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                               .arg(ui::colors::TEXT_DIM(), MONO));
        return;
    }
    lbl->setText(format_metric(examples_[row_index].metric, raw_value));
}

QString LiveDataPanel::format_metric(const QString& metric, double v) {
    if (metric == "market_cap") {
        // Format as $X.XB / $X.XT
        const double abs_v = std::abs(v);
        if (abs_v >= 1e12) return QString("$%1T").arg(v / 1e12, 0, 'f', 2);
        if (abs_v >= 1e9)  return QString("$%1B").arg(v / 1e9, 0, 'f', 1);
        if (abs_v >= 1e6)  return QString("$%1M").arg(v / 1e6, 0, 'f', 1);
        return QString("$%1").arg(v, 0, 'f', 0);
    }
    if (metric == "dividend_yield" || metric == "roe" || metric == "profit_margin") {
        // yfinance returns these as decimals (0.025 = 2.5%) — multiply.
        const double pct = v <= 1.0 ? v * 100.0 : v;
        return QString("%1%").arg(pct, 0, 'f', 2);
    }
    if (metric == "week52_high" || metric == "week52_low") {
        return QString("$%1").arg(v, 0, 'f', 2);
    }
    // Default: 2 decimal places.
    return QString::number(v, 'f', 2);
}

} // namespace fincept::knowledge

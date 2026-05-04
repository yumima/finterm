#include "screens/knowledge/PeerChartPanel.h"

#include "services/markets/MarketDataService.h"
#include "ui/theme/Theme.h"

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QValueAxis>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {
constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";
constexpr int CHART_HEIGHT = 160;

double pluck(const services::InfoData& info, const QString& metric) {
    if (metric == "pe_ratio") return info.pe_ratio;
    if (metric == "forward_pe") return info.forward_pe;
    if (metric == "price_to_book") return info.price_to_book;
    if (metric == "dividend_yield") return info.dividend_yield * 100.0;
    if (metric == "beta") return info.beta;
    if (metric == "market_cap") return info.market_cap / 1e9; // → $B
    if (metric == "eps") return info.eps;
    if (metric == "roe") return info.roe * 100.0;
    if (metric == "debt_to_equity") return info.debt_to_equity;
    return 0.0;
}

QString metric_unit(const QString& metric) {
    if (metric == "dividend_yield" || metric == "roe" || metric == "profit_margin") return "%";
    if (metric == "market_cap") return "$B";
    return QString();
}

} // namespace

PeerChartPanel::PeerChartPanel(const QStringList& peers, const QString& metric, QWidget* parent)
    : QWidget(parent), peers_(peers), metric_(metric) {
    setStyleSheet("background: transparent;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    status_ = new QLabel(QString("Loading peer comparison (%1)…").arg(metric_), this);
    status_->setStyleSheet(QString("color: %1; background: transparent; font-size: 10px; %2")
                               .arg(ui::colors::TEXT_DIM(), MONO));
    root->addWidget(status_);

    view_ = new QChartView(this);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setFixedHeight(CHART_HEIGHT);
    view_->setStyleSheet("background: transparent; border: none;");
    root->addWidget(view_);

    load();
}

void PeerChartPanel::load() {
    if (peers_.isEmpty() || metric_.isEmpty()) {
        status_->setText("No peer set or metric configured.");
        return;
    }

    auto* shared_count = new int(0);
    auto* shared_values = new QHash<QString, double>();
    const int total = peers_.size();
    QPointer<PeerChartPanel> guard(this);

    for (const auto& sym : peers_) {
        services::MarketDataService::instance().fetch_info(
            sym, [guard, sym, shared_count, shared_values, total](bool ok, services::InfoData info) {
                ++*shared_count;
                if (ok && guard) {
                    shared_values->insert(sym.toUpper(), pluck(info, guard->metric_));
                }
                if (*shared_count >= total) {
                    if (!guard) {
                        delete shared_count;
                        delete shared_values;
                        return;
                    }

                    auto* set = new QBarSet("");
                    set->setColor(QColor(ui::colors::AMBER()));
                    set->setBorderColor(QColor(ui::colors::AMBER()));
                    QStringList categories;
                    double maxv = 0.0;
                    for (const auto& peer : guard->peers_) {
                        const double v = shared_values->value(peer.toUpper(), 0.0);
                        *set << v;
                        categories << peer.toUpper();
                        maxv = std::max(maxv, v);
                    }

                    auto* series = new QBarSeries;
                    series->append(set);
                    series->setLabelsVisible(true);
                    series->setLabelsFormat("@value");
                    series->setLabelsPosition(QAbstractBarSeries::LabelsOutsideEnd);

                    auto* chart = new QChart;
                    chart->legend()->hide();
                    chart->setMargins(QMargins(0, 0, 0, 0));
                    chart->setBackgroundBrush(QBrush(QColor(ui::colors::BG_SURFACE())));
                    chart->setBackgroundRoundness(0);
                    chart->setBackgroundVisible(true);
                    chart->setPlotAreaBackgroundBrush(QBrush(QColor(ui::colors::BG_SURFACE())));
                    chart->setPlotAreaBackgroundVisible(true);
                    chart->addSeries(series);

                    auto* x = new QBarCategoryAxis;
                    x->append(categories);
                    x->setLabelsColor(QColor(ui::colors::TEXT_SECONDARY()));
                    QFont xf = x->labelsFont();
                    xf.setFamily("Consolas");
                    xf.setPointSize(8);
                    x->setLabelsFont(xf);
                    x->setGridLineVisible(false);
                    chart->addAxis(x, Qt::AlignBottom);
                    series->attachAxis(x);

                    auto* y = new QValueAxis;
                    y->setRange(0, maxv * 1.15);
                    y->setLabelsColor(QColor(ui::colors::TEXT_DIM()));
                    QFont yf = y->labelsFont();
                    yf.setFamily("Consolas");
                    yf.setPointSize(8);
                    y->setLabelsFont(yf);
                    y->setGridLineVisible(false);
                    chart->addAxis(y, Qt::AlignLeft);
                    series->attachAxis(y);

                    guard->view_->setChart(chart);
                    guard->status_->setText(QString("Peer comparison · %1%2")
                                                .arg(guard->metric_, metric_unit(guard->metric_)));

                    delete shared_count;
                    delete shared_values;
                }
            });
    }
}

} // namespace fincept::knowledge

#include "screens/knowledge/HistoryChartPanel.h"

#include "services/markets/MarketDataService.h"
#include "ui/theme/Theme.h"

#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <QDateTimeAxis>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace fincept::knowledge {

namespace {
constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";
constexpr int CHART_HEIGHT = 140;
} // namespace

HistoryChartPanel::HistoryChartPanel(const QString& ticker, const QString& period, QWidget* parent)
    : QWidget(parent), ticker_(ticker), period_(period.isEmpty() ? "1y" : period) {
    setStyleSheet("background: transparent;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    status_ = new QLabel(QString("Loading %1 history…").arg(ticker_), this);
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

void HistoryChartPanel::load() {
    QPointer<HistoryChartPanel> guard(this);
    services::MarketDataService::instance().fetch_history(
        ticker_, period_, "1d", [guard](bool ok, QVector<services::HistoryPoint> pts) {
            if (!guard)
                return;
            if (!ok || pts.isEmpty()) {
                guard->status_->setText(QString("No history data for %1.").arg(guard->ticker_));
                return;
            }
            auto* line = new QLineSeries;
            QPen pen(QColor(ui::colors::AMBER()));
            pen.setWidth(2);
            line->setPen(pen);
            double lo = std::numeric_limits<double>::infinity();
            double hi = -std::numeric_limits<double>::infinity();
            for (const auto& p : pts) {
                line->append(double(p.timestamp) * 1000.0, p.close);
                lo = std::min(lo, p.close);
                hi = std::max(hi, p.close);
            }
            const double pct = (pts.last().close - pts.first().close) / pts.first().close * 100.0;
            const QString delta_color = pct >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE();
            const QString sign = pct >= 0 ? "+" : "";
            guard->status_->setText(
                QString("<span style='color:%1;'>%2 close $%3</span> · "
                        "<span style='color:%4;'>%5%6%</span> over %7")
                    .arg(ui::colors::TEXT_SECONDARY(), guard->ticker_)
                    .arg(pts.last().close, 0, 'f', 2)
                    .arg(delta_color, sign)
                    .arg(pct, 0, 'f', 1)
                    .arg(guard->period_));

            auto* chart = new QChart;
            chart->legend()->hide();
            chart->setMargins(QMargins(0, 0, 0, 0));
            chart->setBackgroundBrush(QBrush(QColor(ui::colors::BG_SURFACE())));
            chart->setBackgroundRoundness(0);
            chart->setBackgroundVisible(true);
            chart->setPlotAreaBackgroundBrush(QBrush(QColor(ui::colors::BG_SURFACE())));
            chart->setPlotAreaBackgroundVisible(true);
            chart->addSeries(line);

            auto* x = new QDateTimeAxis;
            x->setVisible(false);
            x->setRange(QDateTime::fromMSecsSinceEpoch(qint64(line->at(0).x())),
                        QDateTime::fromMSecsSinceEpoch(qint64(line->at(line->count() - 1).x())));
            chart->addAxis(x, Qt::AlignBottom);
            line->attachAxis(x);

            auto* y = new QValueAxis;
            y->setVisible(false);
            const double pad = (hi - lo) * 0.08;
            y->setRange(lo - pad, hi + pad);
            chart->addAxis(y, Qt::AlignLeft);
            line->attachAxis(y);

            guard->view_->setChart(chart);
        });
}

} // namespace fincept::knowledge

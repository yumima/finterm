#include "screens/dashboard/widgets/MarketChartWidget.h"

#include "screens/dashboard/canvas/DashboardCanvas.h"
#include "screens/markets/MarketChartDialog.h"
#include "ui/theme/Theme.h"

namespace finterm::screens::widgets {

MarketChartWidget::MarketChartWidget(const QJsonObject& cfg, QWidget* parent)
    : BaseWidget(QStringLiteral("CHART · LINKED"), parent, ui::colors::AMBER()) {
    view_ = new MarketChartView(this);
    view_->set_closable(false); // the tile has its own ✕
    content_layout()->setContentsMargins(8, 6, 8, 6);
    content_layout()->addWidget(view_, 1);
    connect(view_, &MarketChartView::symbol_changed, this, [this](const QString& sym, const QString& label) {
        if (sym == symbol_)
            return;
        symbol_ = sym;
        label_ = label;
        emit config_changed(config()); // reopens on the last symbol shown
    });
    connect(this, &BaseWidget::refresh_requested, this, [this]() { view_->set_symbol(symbol_, label_); });
    apply_config(cfg);
}

MarketChartWidget::~MarketChartWidget() {
    link(false);
}

QJsonObject MarketChartWidget::config() const {
    return QJsonObject{{"symbol", symbol_}, {"label", label_}};
}

void MarketChartWidget::apply_config(const QJsonObject& cfg) {
    const QString sym = cfg.value(QLatin1String("symbol")).toString().trimmed();
    if (!sym.isEmpty()) {
        symbol_ = sym;
        label_ = cfg.value(QLatin1String("label")).toString();
    }
    if (isVisible())
        view_->set_symbol(symbol_, label_);
}

void MarketChartWidget::link(bool on) {
    if (!on) {
        if (scope_)
            MarketChartDialog::unlink_chart(scope_, view_);
        scope_.clear();
        return;
    }
    // The canvas this tile is on (the widgets beside it share it).
    for (QWidget* w = parentWidget(); w; w = w->parentWidget())
        if (qobject_cast<DashboardCanvas*>(w)) {
            scope_ = w;
            MarketChartDialog::link_chart(w, view_);
            return;
        }
}

void MarketChartWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    link(true);
    if (view_->symbol() != symbol_)
        view_->set_symbol(symbol_, label_);
}

void MarketChartWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    link(false);
}

} // namespace finterm::screens::widgets

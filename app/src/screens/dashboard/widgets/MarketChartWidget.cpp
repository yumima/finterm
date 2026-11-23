#include "screens/dashboard/widgets/MarketChartWidget.h"

#include "screens/dashboard/canvas/SelectionLink.h"
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
        if (link_ && counted_)
            link_->remove_chart();
        counted_ = false;
        if (link_)
            disconnect(link_, nullptr, this, nullptr);
        link_.clear();
        return;
    }
    if (link_)
        return;
    link_ = SelectionLink::of(this);
    if (!link_)
        return; // not on a canvas: a plain chart
    link_->add_chart();
    counted_ = true;
    connect(link_, &SelectionLink::selected, this,
            [this](const QString& sym, const QString& label) { view_->set_symbol(sym, label); });
    // Selected while this tile was off screen (another page, another tab).
    if (!link_->symbol().isEmpty() && link_->symbol() != symbol_)
        view_->set_symbol(link_->symbol(), link_->label());
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

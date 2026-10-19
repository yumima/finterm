#include "screens/dashboard/widgets/StockQuoteWidget.h"

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QtNumeric>

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"

#include <QFrame>

namespace fincept::screens::widgets {

StockQuoteWidget::StockQuoteWidget(const QString& symbol, QWidget* parent)
    : BaseWidget(QString("QUOTE: %1").arg(symbol.toUpper()), parent), symbol_(symbol.toUpper()) {
    build_body();

    connect(this, &BaseWidget::refresh_requested, this, &StockQuoteWidget::refresh_data);

    apply_styles();
    set_loading(true);
}

void StockQuoteWidget::build_body() {
    auto* vl = content_layout();
    vl->setContentsMargins(12, 8, 12, 8);
    vl->setSpacing(8);

    // ── Price row ──
    auto* price_row = new QWidget(this);
    auto* prl = new QHBoxLayout(price_row);
    prl->setContentsMargins(0, 0, 0, 0);
    prl->setSpacing(8);

    price_label_ = new QLabel("--");
    prl->addWidget(price_label_);

    auto* change_col = new QWidget(this);
    auto* ccl = new QVBoxLayout(change_col);
    ccl->setContentsMargins(0, 4, 0, 0);
    ccl->setSpacing(0);

    arrow_label_ = new QLabel;
    ccl->addWidget(arrow_label_);

    change_label_ = new QLabel("--");
    ccl->addWidget(change_label_);

    prl->addWidget(change_col);
    prl->addStretch();

    ticker_label_ = new QLabel(symbol_);
    prl->addWidget(ticker_label_);

    vl->addWidget(price_row);

    // ── Separator ──
    sep_ = new QFrame;
    sep_->setFixedHeight(1);
    vl->addWidget(sep_);

    // ── Stats grid ──
    auto* stats = new QWidget(this);
    auto* gl = new QGridLayout(stats);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(6);

    auto make_stat = [&](int row, int col, const QString& label, QLabel*& val_out) {
        auto* cell = new QWidget(this);
        stat_cells_.append(cell);
        auto* cl = new QVBoxLayout(cell);
        cl->setContentsMargins(8, 6, 8, 6);
        cl->setSpacing(2);

        auto* lbl = new QLabel(label);
        stat_labels_.append(lbl);
        cl->addWidget(lbl);

        val_out = new QLabel("--");
        stat_values_.append(val_out);
        cl->addWidget(val_out);

        gl->addWidget(cell, row, col);
    };

    make_stat(0, 0, "OPEN", open_val_);
    make_stat(0, 1, "PREV CLOSE", prev_val_);
    make_stat(1, 0, "HIGH", high_val_);
    make_stat(1, 1, "LOW", low_val_);
    make_stat(2, 0, "VOLUME", volume_val_);

    vl->addWidget(stats);
    vl->addStretch();
}

void StockQuoteWidget::apply_styles() {
    price_label_->setStyleSheet(QString("color: %1; font-size: 28px; font-weight: bold; background: transparent;")
                                    .arg(ui::colors::TEXT_PRIMARY()));
    arrow_label_->setStyleSheet("background: transparent;");
    change_label_->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent;")
                                     .arg(ui::colors::TEXT_SECONDARY()));
    ticker_label_->setStyleSheet(
        QString("color: %1; font-size: 14px; font-weight: bold; background: transparent;").arg(ui::colors::AMBER()));
    sep_->setStyleSheet(QString("background: %1;").arg(ui::colors::BORDER_DIM()));

    for (auto* cell : stat_cells_)
        cell->setStyleSheet(QString("background: %1; border-radius: 2px;").arg(ui::colors::BG_RAISED()));
    for (auto* lbl : stat_labels_)
        lbl->setStyleSheet(
            QString("color: %1; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
    for (auto* val : stat_values_)
        val->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent;")
                               .arg(ui::colors::TEXT_PRIMARY()));
}

void StockQuoteWidget::on_theme_changed() {
    apply_styles();
}

void StockQuoteWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!hub_active_)
        hub_resubscribe();
}

void StockQuoteWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (hub_active_)
        hub_unsubscribe_all();
}

void StockQuoteWidget::set_symbol(const QString& symbol) {
    symbol_ = symbol.toUpper();
    set_title(QString("QUOTE: %1").arg(symbol_));
    // A new symbol has no data yet: the previous symbol's numbers must not sit
    // under the new title (an error then shows instead of being suppressed).
    has_data_ = false;
    price_label_->setText(QStringLiteral("--"));
    change_label_->setText(QStringLiteral("--"));
    arrow_label_->clear();
    for (auto* v : stat_values_)
        v->setText(QStringLiteral("--"));
    apply_styles(); // neutral colours: no direction for a symbol with no data
    clear_error();
    // Re-subscribe to the new topic; old sub for previous symbol is
    // dropped wholesale by `unsubscribe(this)` inside hub_resubscribe().
    if (isVisible())
        hub_resubscribe();
}

void StockQuoteWidget::refresh_data() {
    datahub::DataHub::instance().request(QStringLiteral("market:quote:") + symbol_);
}


void StockQuoteWidget::hub_resubscribe() {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    hub.unsubscribe_errors(this, QStringLiteral("market:quote:") + symbol_);
    const QString topic = QStringLiteral("market:quote:") + symbol_;
    hub.subscribe(this, topic, [this](const QVariant& v) {
        if (!v.canConvert<services::QuoteData>())
            return;
        set_loading(false);
        clear_error();
        populate(v.value<services::QuoteData>());
    });
    // A failed quote refresh would otherwise leave the spinner running forever
    // and the "--" placeholders looking like a (wrong) empty state. Surface the
    // failure — but only when no quote has loaded yet: a transient error after
    // good data keeps the good data on screen.
    hub.subscribe_errors(this, topic, [this](const QString&) {
        set_loading(false);
        if (!has_data_)
            set_error(QStringLiteral("Quote unavailable"));
    });
    hub_active_ = true;
}

void StockQuoteWidget::hub_unsubscribe_all() {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    hub.unsubscribe_errors(this, QStringLiteral("market:quote:") + symbol_);
    hub_active_ = false;
}


void StockQuoteWidget::populate(const services::QuoteData& q) {
    has_data_ = true;
    price_label_->setText(QString("$%1").arg(q.price, 0, 'f', 2));

    const bool known = std::isfinite(q.change_pct) && std::isfinite(q.change);
    bool positive = q.change_pct >= 0;
    QString color = ui::change_color(known ? q.change_pct : qQNaN());

    arrow_label_->setText(!known ? QString() : positive ? QString(QChar(0x25B2)) : QString(QChar(0x25BC)));
    arrow_label_->setStyleSheet(QString("color: %1; background: transparent;").arg(color));

    // No previous close → change unknown: show the placeholder, not "+0.00 (+0.00%)".
    change_label_->setText(!known ? ui::formatting::placeholder()
                                  : QString("%1%2 (%3%4%)")
                                        .arg(positive ? "+" : "")
                                        .arg(q.change, 0, 'f', 2)
                                        .arg(positive ? "+" : "")
                                        .arg(q.change_pct, 0, 'f', 2));
    change_label_->setStyleSheet(
        QString("color: %1; font-weight: bold; background: transparent;").arg(color));

    price_label_->setStyleSheet(
        QString("color: %1; font-size: 28px; font-weight: bold; background: transparent;").arg(color));

    auto fmt = [](double v) { return v > 0 ? QString("$%1").arg(v, 0, 'f', 2) : QString("--"); };
    // QuoteData carries no open price; showing the high under "Open" was a
    // fabricated value. Leave it unavailable.
    open_val_->setText(ui::formatting::placeholder());
    high_val_->setText(fmt(q.high));
    low_val_->setText(fmt(q.low));
    prev_val_->setText(fmt(q.price - q.change));

    volume_val_->setText(ui::formatting::format_compact_volume(q.volume));
}

} // namespace fincept::screens::widgets

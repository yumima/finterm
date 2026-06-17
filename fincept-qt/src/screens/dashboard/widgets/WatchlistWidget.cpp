#include "screens/dashboard/widgets/WatchlistWidget.h"

#include "ui/charts/InlineSparkline.h"
#include "ui/theme/Theme.h"

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"

#include <QLabel>
#include <QTableWidgetItem>

namespace fincept::screens::widgets {

WatchlistWidget::WatchlistWidget(QWidget* parent)
    : BaseWidget("WATCHLIST", parent, ui::colors::INFO),
      symbols_({"AAPL", "MSFT", "GOOGL", "AMZN", "NVDA", "TSLA", "META", "JPM"}) {

    auto* vl = content_layout();

    // Symbol input bar
    auto* input_row = new QWidget(this);
    auto* irl = new QHBoxLayout(input_row);
    irl->setContentsMargins(4, 4, 4, 4);
    irl->setSpacing(4);

    symbols_label_ = new QLabel("SYMBOLS:");
    irl->addWidget(symbols_label_);

    symbols_input_ = new QLineEdit(symbols_.join(", "));
    irl->addWidget(symbols_input_, 1);

    go_btn_ = new QPushButton("GO");
    go_btn_->setFixedWidth(32);
    connect(go_btn_, &QPushButton::clicked, this, [this]() {
        QString text = symbols_input_->text().trimmed().toUpper();
        symbols_.clear();
        for (auto& s : text.split(",")) {
            QString trimmed = s.trimmed();
            if (!trimmed.isEmpty())
                symbols_ << trimmed;
        }
        // Dynamic symbol set: drop any cached rows for symbols no longer
        // tracked, rebuild the persistent table rows for the new set, then
        // rewire subscriptions.
        row_cache_.clear();
        rebuild_rows();
        hub_resubscribe();
    });
    irl->addWidget(go_btn_);

    vl->addWidget(input_row);

    // Table
    table_ = new ui::DataTable;
    table_->set_headers({"SYMBOL", "PRICE", "CHG", "CHG%", "TREND"});
    table_->set_column_widths({100, 90, 80, 70, 90});
    vl->addWidget(table_);

    // Coalesce: each cycle delivers a quote + a sparkline callback per symbol
    // (2N). Collapse the burst into one in-place table update.
    coalesce_ = new QTimer(this);
    coalesce_->setSingleShot(true);
    coalesce_->setInterval(16);
    connect(coalesce_, &QTimer::timeout, this, &WatchlistWidget::render_from_cache);

    rebuild_rows();

    connect(this, &BaseWidget::refresh_requested, this, &WatchlistWidget::refresh_data);

    apply_styles();
    set_loading(true);

}

void WatchlistWidget::apply_styles() {
    symbols_label_->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent;")
                                      .arg(ui::colors::TEXT_SECONDARY()));
    symbols_input_->setStyleSheet(
        QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                "font-size: 10px; padding: 2px 6px; font-family: Consolas; }"
                "QLineEdit:focus { border-color: %4; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER()));
    go_btn_->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: none; "
                                   "font-weight: bold; padding: 3px; }"
                                   "QPushButton:hover { background: %1; }")
                               .arg(ui::colors::AMBER(), ui::colors::BG_BASE()));
}

void WatchlistWidget::on_theme_changed() {
    apply_styles();
}

void WatchlistWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!hub_active_)
        hub_resubscribe();
}

void WatchlistWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (hub_active_)
        hub_unsubscribe_all();
}

void WatchlistWidget::refresh_data() {
    if (symbols_.isEmpty())
        return;
    // User-triggered refresh: force-kick the hub for each current symbol.
    auto& hub = datahub::DataHub::instance();
    QStringList topics;
    topics.reserve(symbols_.size());
    for (const auto& sym : symbols_)
        topics.append(QStringLiteral("market:quote:") + sym);
    hub.request(topics, /*force=*/true);  // user-triggered: bypass min_interval
}


void WatchlistWidget::hub_resubscribe() {
    auto& hub = datahub::DataHub::instance();
    // Drop old subscriptions wholesale — symbol set may have changed.
    hub.unsubscribe(this);
    sparkline_cache_.clear();
    for (const auto& sym : symbols_) {
        const QString topic = QStringLiteral("market:quote:") + sym;
        hub.subscribe(this, topic, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            row_cache_.insert(sym, v.value<services::QuoteData>());
            set_loading(false);
            coalesce_->start();
        });
        // Surface a quote fetch failure rather than spinning forever. The input
        // bar must survive (the user re-enters symbols there), so instead of
        // set_error() — which would tear the whole body down — show an error row
        // in the table. Only while nothing has loaded yet; once any symbol
        // delivers, render_from_cache() overwrites the row with real data.
        hub.subscribe_errors(this, topic, [this](const QString&) {
            set_loading(false);
            if (row_cache_.isEmpty() && table_) {
                // Replace the persistent rows with a single error row. The
                // mappings are dropped so a later success rebuilds cleanly.
                table_->clear_data();
                row_index_.clear();
                spark_widgets_.clear();
                table_->add_row({QStringLiteral("Quotes unavailable"), QString{}, QString{}, QString{}, QString{}});
            }
        });
        hub.subscribe(this, QStringLiteral("market:sparkline:") + sym, [this, sym](const QVariant& v) {
            if (!v.canConvert<QVector<double>>())
                return;
            sparkline_cache_.insert(sym, v.value<QVector<double>>());
            coalesce_->start();
        });
    }
    hub_active_ = true;
}

void WatchlistWidget::hub_unsubscribe_all() {
    datahub::DataHub::instance().unsubscribe(this);
    // Drop per-symbol error subscriptions too (data unsubscribe doesn't touch
    // the error channel). The symbol set is dynamic, so clear every current one.
    auto& hub = datahub::DataHub::instance();
    for (const auto& sym : symbols_)
        hub.unsubscribe_errors(this, QStringLiteral("market:quote:") + sym);
    hub_active_ = false;
}

void WatchlistWidget::rebuild_rows() {
    // Build one persistent row + sparkline per current symbol, in symbols_
    // order. Symbols without a quote yet show "--" placeholders rather than
    // being hidden, so the visible rows stay contiguous — interspersed hidden
    // rows would break the table's alternating-row striping.
    table_->clear_data();
    row_index_.clear();
    spark_widgets_.clear();

    for (const auto& sym : symbols_) {
        table_->add_row({sym, QStringLiteral("--"), QStringLiteral("--"), QStringLiteral("--"), QString{}});
        const int row = table_->rowCount() - 1;
        row_index_.insert(sym, row);

        auto* spark = new ui::InlineSparkline(table_);
        table_->setCellWidget(row, 4, spark);
        spark_widgets_.insert(sym, spark);
    }
}

void WatchlistWidget::render_from_cache() {
    if (!table_)
        return;
    // The error path may have torn the persistent rows down; rebuild if the
    // mapping no longer matches the current symbol set.
    if (row_index_.size() != symbols_.size())
        rebuild_rows();

    for (const auto& sym : symbols_) {
        const auto idx_it = row_index_.constFind(sym);
        if (idx_it == row_index_.constEnd())
            continue;
        const int row = idx_it.value();

        const auto it = row_cache_.constFind(sym);
        if (it == row_cache_.constEnd()) {
            // No quote yet — show the symbol as pending ("--") with neutral
            // color, keeping the row visible/contiguous (correct striping).
            if (auto* c = table_->item(row, 1)) c->setText(QStringLiteral("--"));
            if (auto* c = table_->item(row, 2)) c->setText(QStringLiteral("--"));
            if (auto* c = table_->item(row, 3)) c->setText(QStringLiteral("--"));
            table_->set_cell_color(row, 2, ui::colors::TEXT_SECONDARY());
            table_->set_cell_color(row, 3, ui::colors::TEXT_SECONDARY());
            continue;
        }
        const auto& q = it.value();

        if (auto* c = table_->item(row, 0))
            c->setText(q.symbol);
        if (auto* c = table_->item(row, 1))
            c->setText(QString("$%1").arg(q.price, 0, 'f', 2));
        if (auto* c = table_->item(row, 2))
            c->setText(QString("%1%2").arg(q.change >= 0 ? "+" : "").arg(q.change, 0, 'f', 2));
        if (auto* c = table_->item(row, 3))
            c->setText(QString("%1%2%").arg(q.change_pct >= 0 ? "+" : "").arg(q.change_pct, 0, 'f', 2));
        table_->set_cell_color(row, 2, ui::change_color(q.change_pct));
        table_->set_cell_color(row, 3, ui::change_color(q.change_pct));

        const auto sit = sparkline_cache_.constFind(sym);
        auto* spark = spark_widgets_.value(sym, nullptr);
        if (spark && sit != sparkline_cache_.constEnd())
            spark->set_points(sit.value());
    }
}


void WatchlistWidget::populate(const QVector<services::QuoteData>& quotes) {
    table_->clear_data();

    for (const auto& q : quotes) {
        table_->add_row({q.symbol, QString("$%1").arg(q.price, 0, 'f', 2),
                         QString("%1%2").arg(q.change >= 0 ? "+" : "").arg(q.change, 0, 'f', 2),
                         QString("%1%2%").arg(q.change_pct >= 0 ? "+" : "").arg(q.change_pct, 0, 'f', 2),
                         QString{}});
        int row = table_->rowCount() - 1;
        table_->set_cell_color(row, 2, ui::change_color(q.change_pct));
        table_->set_cell_color(row, 3, ui::change_color(q.change_pct));

        auto* spark = new ui::InlineSparkline(table_);
        const auto sit = sparkline_cache_.constFind(q.symbol);
        if (sit != sparkline_cache_.constEnd())
            spark->set_points(sit.value());
        table_->setCellWidget(row, 4, spark);
    }
}

} // namespace fincept::screens::widgets

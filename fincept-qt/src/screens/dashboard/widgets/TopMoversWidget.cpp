#include "screens/dashboard/widgets/TopMoversWidget.h"

#include "ui/charts/InlineSparkline.h"
#include "ui/theme/Theme.h"

#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"

#include <QPointer>

namespace fincept::screens::widgets {

TopMoversWidget::TopMoversWidget(QWidget* parent) : BaseWidget("TOP MOVERS", parent) {
    build_body();

    connect(this, &BaseWidget::refresh_requested, this, &TopMoversWidget::refresh_data);

    apply_styles();
    set_loading(true);
    // Kick off the initial screener fetch — no fixed symbol list anymore;
    // the daemon's yfinance.screen('day_gainers'/'day_losers') is the
    // authoritative source of top movers.
    refresh_data();
}

void TopMoversWidget::build_body() {
    // Tab toggle
    auto* tab_bar = new QWidget(this);
    tab_bar->setFixedHeight(26);
    auto* tl = new QHBoxLayout(tab_bar);
    tl->setContentsMargins(4, 2, 4, 2);
    tl->setSpacing(0);

    gainers_btn_ = new QPushButton(QString(QChar(0x25B2)) + " GAINERS");
    losers_btn_ = new QPushButton(QString(QChar(0x25BC)) + " LOSERS");

    connect(gainers_btn_, &QPushButton::clicked, this, [this]() { show_tab(true); });
    connect(losers_btn_, &QPushButton::clicked, this, [this]() { show_tab(false); });

    tl->addWidget(gainers_btn_, 1);
    tl->addWidget(losers_btn_, 1);

    content_layout()->addWidget(tab_bar);

    // Table
    table_ = new ui::DataTable;
    table_->set_headers({"SYMBOL", "PRICE", "CHG%", "TREND"});
    table_->set_column_widths({100, 90, 80, 90});
    content_layout()->addWidget(table_);
}

void TopMoversWidget::apply_styles() {
    show_tab(showing_gainers_);
}

void TopMoversWidget::on_theme_changed() {
    apply_styles();
}

void TopMoversWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    hub_active_ = true;
    resubscribe_sparklines();
}

void TopMoversWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (hub_active_) {
        datahub::DataHub::instance().unsubscribe(this);
        // Drop the tracking list too — otherwise resubscribe_sparklines() on
        // the next showEvent would skip every symbol as "already subscribed"
        // and the widget would render with no live sparklines.
        subscribed_symbols_.clear();
        hub_active_ = false;
    }
}

void TopMoversWidget::refresh_data() {
    QPointer<TopMoversWidget> self = this;
    services::MarketDataService::instance().fetch_top_movers(
        10, [self](bool ok, services::MarketDataService::TopMovers tm) {
            if (!self)
                return;
            if (!ok) {
                // Genuine fetch failure (daemon/yfinance error). Clear the
                // spinner so it doesn't hang forever. Only replace the body
                // with an error label when we have nothing to show yet — a
                // failed refresh shouldn't blow away movers already on screen.
                self->set_loading(false);
                if (self->cached_movers_.gainers.isEmpty() && self->cached_movers_.losers.isEmpty()) {
                    self->set_error(QStringLiteral("Couldn't load top movers"));
                    // set_error() deleteLater()'d the tab bar + table. Null the
                    // raw pointers now so show_tab()/rebuild_from_cache() treat
                    // the body as absent and rebuild it on the next success.
                    self->table_ = nullptr;
                    self->gainers_btn_ = nullptr;
                    self->losers_btn_ = nullptr;
                }
                return;
            }
            self->cached_movers_ = std::move(tm);
            self->set_loading(false);
            self->resubscribe_sparklines();
            self->rebuild_from_cache();
        });
}

void TopMoversWidget::resubscribe_sparklines() {
    if (!hub_active_)
        return;
    auto& hub = datahub::DataHub::instance();
    QStringList wanted;
    wanted.reserve(cached_movers_.gainers.size() + cached_movers_.losers.size());
    for (const auto& q : cached_movers_.gainers)
        wanted.append(q.symbol);
    for (const auto& q : cached_movers_.losers)
        wanted.append(q.symbol);

    for (const auto& sym : wanted) {
        if (subscribed_symbols_.contains(sym))
            continue;
        hub.subscribe(this, QStringLiteral("market:sparkline:") + sym,
                      [this, sym](const QVariant& v) {
                          if (!v.canConvert<QVector<double>>())
                              return;
                          sparkline_cache_.insert(sym, v.value<QVector<double>>());
                          rebuild_from_cache();
                      });
        subscribed_symbols_.append(sym);
    }
}

void TopMoversWidget::rebuild_from_cache() {
    // A prior fetch error may have replaced the body with set_error()'s label,
    // deleting the tab bar and table. Recreate them before rendering data again.
    if (!table_)
        build_body();
    show_tab(showing_gainers_);
}

void TopMoversWidget::show_tab(bool gainers) {
    // After a fetch error, set_error() tears down the body; the buttons/table
    // are gone until the next successful refresh rebuilds them. Guard so
    // apply_styles()/theme-change can't dereference deleted widgets.
    if (!table_ || !gainers_btn_ || !losers_btn_)
        return;

    showing_gainers_ = gainers;

    auto active_g = QString("QPushButton { background: %1; color: %2; border: none; "
                            "font-weight: bold; padding: 4px; }")
                        .arg(ui::colors::POSITIVE(), ui::colors::BG_BASE());
    auto active_l = QString("QPushButton { background: %1; color: %2; border: none; "
                            "font-weight: bold; padding: 4px; }")
                        .arg(ui::colors::NEGATIVE(), ui::colors::BG_BASE());
    auto inactive = QString("QPushButton { background: %1; color: %2; border: none; "
                            "font-weight: bold; padding: 4px; }")
                        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY());

    gainers_btn_->setStyleSheet(gainers ? active_g : inactive);
    losers_btn_->setStyleSheet(gainers ? inactive : active_l);

    table_->clear_data();

    const auto& rows = gainers ? cached_movers_.gainers : cached_movers_.losers;
    int count = std::min(rows.size(), qsizetype(6));
    for (int i = 0; i < count; ++i) {
        const auto& q = rows[i];
        table_->add_row({q.symbol, QString("$%1").arg(q.price, 0, 'f', 2),
                         QString("%1%2%").arg(q.change_pct >= 0 ? "+" : "").arg(q.change_pct, 0, 'f', 2),
                         QString{}});
        int row = table_->rowCount() - 1;
        table_->set_cell_color(row, 2, ui::change_color(q.change_pct));

        auto* spark = new ui::InlineSparkline(table_);
        const auto sit = sparkline_cache_.constFind(q.symbol);
        if (sit != sparkline_cache_.constEnd())
            spark->set_points(sit.value());
        table_->setCellWidget(row, 3, spark);
    }
}

} // namespace fincept::screens::widgets

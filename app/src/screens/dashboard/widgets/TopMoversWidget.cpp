#include "screens/dashboard/widgets/TopMoversWidget.h"
#include "screens/dashboard/canvas/SelectionLink.h"

#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "screens/markets/MarketChartDialog.h"
#include "ui/charts/InlineSparkline.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace finterm::screens::widgets {

TopMoversWidget::TopMoversWidget(QWidget* parent) : BaseWidget("TOP MOVERS", parent) {
    build_body();

    // Coalesce per-symbol sparkline callbacks into a single re-render per burst.
    coalesce_ = new QTimer(this);
    coalesce_->setSingleShot(true);
    coalesce_->setInterval(16);
    connect(coalesce_, &QTimer::timeout, this, &TopMoversWidget::rebuild_from_cache);

    // A user refresh asks the hub to re-read the shared movers topic now.
    connect(this, &BaseWidget::refresh_requested, this, [] {
        datahub::DataHub::instance().request(services::MarketDataService::kMoversTopic, /*force=*/true);
    });
    live_ = new services::LiveQuoteFollower(this, [this](const services::QuoteData& q) {
        if (services::LiveQuoteFollower::merge_movers(cached_movers_, q))
            coalesce_->start();
    });

    apply_styles();
    set_loading(true); // the movers topic answers on show
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
    // Left click opens the row's chart. Rows are a reused pool, so read the
    // symbol off the row at click time; a blank pool row has none.
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    SelectionLink::follow_rows(table_, [this](int r) {
        auto* it = table_->item(r, 0);
        return it ? it->text().trimmed() : QString();
    });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* it = table_->item(row, 0);
        const QString sym = it ? it->text().trimmed() : QString();
        if (!sym.isEmpty())
            screens::MarketChartDialog::show_for(sym, QString(), this);
    });

    // Build the persistent row pool once: up to kMaxRows rows, each with a
    // reusable InlineSparkline. render_table() updates these in place rather
    // than clear_data() + re-adding rows (and new sparklines) every tick.
    spark_widgets_.clear();
    spark_widgets_.reserve(kMaxRows);
    for (int i = 0; i < kMaxRows; ++i) {
        table_->add_row({QString{}, QString{}, QString{}, QString{}});
        auto* spark = new ui::InlineSparkline(table_);
        table_->setCellWidget(i, 3, spark);
        spark_widgets_.append(spark);
        table_->setRowHidden(i, true);
    }
}

void TopMoversWidget::apply_styles() {
    show_tab(showing_gainers_);
}

void TopMoversWidget::on_theme_changed() {
    apply_styles();
}

void TopMoversWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!hub_active_) { // a second show without a hide must not subscribe twice
        hub_active_ = true;
        subscribe_movers();
    }
    resubscribe_sparklines();
    follow_live();
}

void TopMoversWidget::follow_live() {
    QStringList syms;
    if (isVisible())
        for (const auto* list : {&cached_movers_.gainers, &cached_movers_.losers})
            for (const auto& q : *list)
                syms << q.symbol;
    live_->set_symbols(syms);
}

void TopMoversWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    live_->clear();
    if (hub_active_) {
        datahub::DataHub::instance().unsubscribe(this); // data AND error subscriptions
        // Drop the tracking list too — otherwise resubscribe_sparklines() on
        // the next showEvent would skip every symbol as "already subscribed"
        // and the widget would render with no live sparklines.
        subscribed_symbols_.clear();
        hub_active_ = false;
    }
}

void TopMoversWidget::subscribe_movers() {
    // The day's movers: one hub topic every screen reads (MarketDataService
    // refreshes it once a minute while anyone is subscribed). A subscribe
    // delivers the cached list at once.
    auto& hub = datahub::DataHub::instance();
    const QString& topic = services::MarketDataService::kMoversTopic;
    hub.subscribe(this, topic, [this](const QVariant& v) {
        if (v.canConvert<services::MarketDataService::TopMovers>())
            on_movers(v.value<services::MarketDataService::TopMovers>());
    });
    hub.subscribe_errors(this, topic, [this](const QString& why) {
        // Movers already on screen stay; say so only when there is nothing.
        set_loading(false);
        if (cached_movers_.gainers.isEmpty() && cached_movers_.losers.isEmpty())
            set_load_error(QStringLiteral("top movers"), why);
    });
}

void TopMoversWidget::on_movers(const services::MarketDataService::TopMovers& tm) {
    cached_movers_ = tm.truncated(kRows);
    services::LiveQuoteFollower::refresh_movers(cached_movers_); // the tape is newer than the screener
    set_loading(false);
    clear_error();
    resubscribe_sparklines();
    follow_live();
    rebuild_from_cache();
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
        hub.subscribe(this, QStringLiteral("market:sparkline:") + sym, [this, sym](const QVariant& v) {
            if (!v.canConvert<QVector<double>>())
                return;
            sparkline_cache_.insert(sym, v.value<QVector<double>>());
            coalesce_->start();
        });
        subscribed_symbols_.append(sym);
    }
}

void TopMoversWidget::rebuild_from_cache() {
    render_table(); // data only: the tab styles change on a tab switch / theme, not per tick
}

void TopMoversWidget::show_tab(bool gainers) {
    showing_gainers_ = gainers;
    style_tabs();
    render_table();
}

// Tab-button styling. Depends only on which tab is active + the theme, so it
// runs on tab switch and theme change — not on every data tick (which used to
// rebuild three stylesheets per refresh).
void TopMoversWidget::style_tabs() {
    const auto active_g = QString("QPushButton { background: %1; color: %2; border: none; "
                                  "font-weight: bold; padding: 4px; }")
                              .arg(ui::colors::POSITIVE(), ui::colors::BG_BASE());
    const auto active_l = QString("QPushButton { background: %1; color: %2; border: none; "
                                  "font-weight: bold; padding: 4px; }")
                              .arg(ui::colors::NEGATIVE(), ui::colors::BG_BASE());
    const auto inactive = QString("QPushButton { background: %1; color: %2; border: none; "
                                  "font-weight: bold; padding: 4px; }")
                              .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY());
    gainers_btn_->setStyleSheet(showing_gainers_ ? active_g : inactive);
    losers_btn_->setStyleSheet(showing_gainers_ ? inactive : active_l);
}

// Fill the persistent rows from the active list, updating cells + sparklines in
// place. Unused slots are hidden so the visible row set matches the data count.
void TopMoversWidget::render_table() {
    // A gainer that has since turned negative (or a loser positive) on live
    // prints no longer belongs under the tab's label: shown again only once
    // the next screener list says so — as Market Pulse does.
    QVector<services::QuoteData> rows;
    for (const auto& q : showing_gainers_ ? cached_movers_.gainers : cached_movers_.losers)
        if (std::isfinite(q.change_pct) && (showing_gainers_ ? q.change_pct > 0 : q.change_pct < 0))
            rows.append(q);
    const int count = std::min<int>(rows.size(), kMaxRows);
    for (int i = 0; i < count; ++i) {
        const auto& q = rows[i];
        if (auto* c = table_->item(i, 0))
            c->setText(q.symbol);
        if (auto* c = table_->item(i, 1))
            c->setText(QString("$%1").arg(q.price, 0, 'f', 2));
        if (auto* c = table_->item(i, 2))
            c->setText(ui::formatting::format_percent(q.change_pct, 2, true));
        table_->set_cell_color(i, 2, ui::change_color(q.change_pct));

        if (i < spark_widgets_.size()) {
            const auto sit = sparkline_cache_.constFind(q.symbol);
            if (sit != sparkline_cache_.constEnd())
                spark_widgets_[i]->set_points(sit.value());
            else
                spark_widgets_[i]->clear();
        }
        table_->setRowHidden(i, false);
    }
    for (int i = count; i < kMaxRows; ++i)
        table_->setRowHidden(i, true);
}

} // namespace finterm::screens::widgets

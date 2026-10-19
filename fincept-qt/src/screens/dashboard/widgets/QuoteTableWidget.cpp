#include "screens/dashboard/widgets/QuoteTableWidget.h"

#include "screens/markets/MarketChartDialog.h"

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"

#include <QHeaderView>
#include <QTableWidgetItem>

#include <cmath>

namespace fincept::screens::widgets {

QuoteTableWidget::QuoteTableWidget(const QString& title, const QStringList& symbols,
                                   const QMap<QString, QString>& label_map, int price_decimals,
                                   const QString& accent_color, QWidget* parent)
    : BaseWidget(title, parent, accent_color),
      symbols_(symbols),
      label_map_(label_map),
      price_decimals_(price_decimals) {
    build_table();

    connect(this, &BaseWidget::refresh_requested, this, &QuoteTableWidget::refresh_data);

    apply_styles();
    set_loading(true);
}

namespace {
// Slow on purpose: the future's 1-minute bars are ~10 minutes delayed, so a
// faster poll only re-reads the same number. One batched call (~0.5 s) a
// minute, and only while the tile is on screen.
constexpr int kAftRefreshMs = 60'000;
constexpr int kColAft = 4;
constexpr auto kAftTip =
    "After-hours move of each index, implied by its future: the future's move since the cash "
    "index's last close, against the future's own price at that close.\n\n"
    "Hover a cell for its session — PRE: toward (or, while the market is open, into) the next "
    "open; POST: the same evening as the close — and the two prices it is measured between.\n\n"
    "A dash means there is no honest number for that row right now; hover it for why (no future "
    "on the feed for that index, still fetching, or no futures print since the close yet).";
} // namespace

void QuoteTableWidget::build_table() {
    table_ = new ui::DataTable;
    if (aft_enabled_) {
        apply_aft_columns();
    } else {
        table_->set_headers({"SYMBOL", "PRICE", "CHG", "CHG%"});
        table_->set_column_widths({130, 100, 80, 70});
    }
    content_layout()->addWidget(table_);
    if (aft_enabled_)
        set_aft_header(aft_failed_ ? QStringLiteral(" !") : QString());
    // Left click opens the row's chart.
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= row_symbols_.size())
            return;
        const QString sym = row_symbols_[row];
        MarketChartDialog::show_for(sym, label_map_.value(sym, sym), this);
    });
}

void QuoteTableWidget::enable_after_hours() {
    if (aft_enabled_)
        return;
    aft_enabled_ = true;
    if (table_)
        apply_aft_columns();
    set_aft_header(QString());
    aft_timer_ = new QTimer(this);
    aft_timer_->setInterval(kAftRefreshMs);
    connect(aft_timer_, &QTimer::timeout, this, [this] { fetch_after_hours(); });
    if (isVisible()) {
        aft_timer_->start();
        fetch_after_hours();
    }
}

void QuoteTableWidget::apply_aft_columns() {
    table_->set_headers({"SYMBOL", "PRICE", "CHG", "CHG%", "AFT%"});
    // Five columns don't fit a default-width tile at fixed widths. The numbers
    // size to their contents so none is ever cut; the name takes what is left
    // and is the one that elides when the tile is narrow.
    auto* h = table_->horizontalHeader();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c <= kColAft; ++c)
        h->setSectionResizeMode(c, QHeaderView::ResizeToContents);
}

void QuoteTableWidget::set_aft_header(const QString& suffix, const QString& extra_tip) {
    if (!table_ || table_->columnCount() <= kColAft)
        return;
    auto* h = table_->horizontalHeaderItem(kColAft);
    if (!h)
        return;
    h->setText(QStringLiteral("AFT%") + suffix);
    h->setToolTip(extra_tip.isEmpty() ? QString::fromUtf8(kAftTip)
                                      : QString::fromUtf8(kAftTip) + QStringLiteral("\n\n") + extra_tip);
}

void QuoteTableWidget::fetch_after_hours() {
    if (!aft_enabled_ || aft_in_flight_)
        return;
    aft_in_flight_ = true;
    // The suffix is the only sign a first fetch is still running; without it
    // an empty column is indistinguishable from a broken one.
    if (aft_.isEmpty())
        set_aft_header(QStringLiteral(" ·"), QStringLiteral("Fetching index futures…"));
    services::fetch_index_after_hours(
        symbols_, this, [this](bool ok, QHash<QString, services::IndexAfterHoursQuote> fresh, QString err) {
            aft_in_flight_ = false;
            if (!ok) {
                // Keep the last good numbers on screen; say the refresh failed.
                aft_failed_ = true;
                set_aft_header(QStringLiteral(" !"),
                               QStringLiteral("The last fetch failed: %1\nRetrying in a minute.").arg(err));
                return;
            }
            aft_failed_ = false;
            // Replace, don't merge: an index that lost its sound reading (a
            // new close with no futures print after it yet) must go back to a
            // dash, not keep the previous evening's move under today's close.
            aft_ = fresh;
            set_aft_header(QString());
            render_from_cache();
        });
}

void QuoteTableWidget::apply_styles() {
    // QuoteTableWidget delegates styling to DataTable; nothing extra needed.
}

void QuoteTableWidget::on_theme_changed() {
    apply_styles();
}

void QuoteTableWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!hub_active_)
        hub_subscribe_all();
    if (aft_timer_ && !aft_timer_->isActive()) {
        aft_timer_->start();
        fetch_after_hours();
    }
}

void QuoteTableWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (hub_active_)
        hub_unsubscribe_all();
    if (aft_timer_)
        aft_timer_->stop();
}

void QuoteTableWidget::refresh_data() {
    // User-triggered refresh: force-bypass min_interval so a user tap on the
    // refresh button always kicks a fetch. Producer-side rate limit still
    // applies, so rage-clicking can't hammer upstream.
    auto& hub = datahub::DataHub::instance();
    QStringList topics;
    topics.reserve(symbols_.size());
    for (const auto& sym : symbols_)
        topics.append(QStringLiteral("market:quote:") + sym);
    hub.request(topics, /*force=*/true);
    fetch_after_hours();
}


void QuoteTableWidget::hub_subscribe_all() {
    auto& hub = datahub::DataHub::instance();
    for (const auto& sym : symbols_) {
        const QString topic = QStringLiteral("market:quote:") + sym;
        hub.subscribe(this, topic, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            row_cache_.insert(sym, v.value<services::QuoteData>());
            set_loading(false);
            // A prior full-batch error may have replaced the table with
            // set_error()'s label. Recreate the table before rendering again.
            if (!table_)
                build_table();
            render_from_cache();
        });
    }
    // Surface a refresh failure rather than spinning forever. Only error when no
    // quote has arrived for any symbol yet — once the cache holds data a single
    // symbol's transient error must not wipe the populated table.
    hub.subscribe_pattern_errors(this, QStringLiteral("market:quote:*"),
                                 [this](const QString&, const QString&) {
                                     set_loading(false);
                                     if (row_cache_.isEmpty() && table_) {
                                         set_error(QStringLiteral("Quotes unavailable"));
                                         table_ = nullptr; // set_error() deleteLater()'d it
                                     }
                                 });
    hub_active_ = true;
}

void QuoteTableWidget::hub_unsubscribe_all() {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    hub.unsubscribe_pattern_errors(this, QStringLiteral("market:quote:*"));
    hub_active_ = false;
    // Keep row_cache_ so a quick hide/show doesn't flash an empty table —
    // the first hub delivery on re-subscribe will refresh it anyway.
}

void QuoteTableWidget::render_from_cache() {
    if (!table_)
        return;
    table_->clear_data();
    row_symbols_.clear();
    for (const auto& sym : symbols_) {
        auto it = row_cache_.constFind(sym);
        if (it == row_cache_.constEnd())
            continue;
        const auto& q = it.value();
        QString display_name = label_map_.value(q.symbol, q.symbol);
        QString price_str = QString::number(q.price, 'f', price_decimals_);
        double chg_abs = q.change;
        QString chg_str = std::isfinite(chg_abs)
            ? QString("%1%2").arg(chg_abs >= 0 ? "+" : "").arg(chg_abs, 0, 'f', price_decimals_)
            : ui::formatting::placeholder();
        QString pct_str = ui::formatting::format_percent(q.change_pct, 2, true);

        QStringList cells{display_name, price_str, chg_str, pct_str};
        const auto aft = aft_.constFind(q.symbol);
        if (aft_enabled_)
            // The session (PRE/POST) is in the tooltip: a default-width tile
            // has no room for it beside the number without truncating CHG%.
            cells << (aft != aft_.constEnd() ? ui::formatting::format_percent(aft->pct, 2, true)
                                             : ui::formatting::placeholder());
        table_->add_row(cells);
        row_symbols_.append(q.symbol);
        int row = table_->rowCount() - 1;
        table_->set_cell_color(row, 2, ui::change_color(q.change_pct));
        table_->set_cell_color(row, 3, ui::change_color(q.change_pct));
        if (aft_enabled_) {
            if (auto* it = table_->item(row, kColAft)) {
                if (aft != aft_.constEnd()) {
                    it->setForeground(QColor(ui::change_color(aft->pct)));
                    it->setToolTip(services::index_after_hours_tooltip(*aft, display_name));
                } else {
                    it->setToolTip(services::index_after_hours_unavailable_tooltip(q.symbol, display_name));
                }
            }
        }
    }
}


} // namespace fincept::screens::widgets

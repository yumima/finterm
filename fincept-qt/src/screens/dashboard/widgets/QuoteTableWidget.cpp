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
// The futures bars are ~10 minutes delayed, so polling faster than this only
// re-reads the same number. One batched call (~0.6 s), shared by every tile
// and chart window through the daemon's cache, and only while on screen.
constexpr int kFutRefreshMs = 60'000;
constexpr int kColFut = 4;
constexpr auto kFutTip =
    "Each index's futures quote: the future's change since its previous close, live day and "
    "night — after the cash close this is the index's after-hours move. Hover a cell for the "
    "contract, its last price and the close it is measured from.\n\n"
    "A dash means there is no honest number for that row right now; hover it for why (no future "
    "on the feed for that index, or still fetching).";
} // namespace

void QuoteTableWidget::build_table() {
    table_ = new ui::DataTable;
    if (fut_enabled_) {
        apply_fut_columns();
    } else {
        table_->set_headers({"SYMBOL", "PRICE", "CHG", "CHG%"});
        table_->set_column_widths({130, 100, 80, 70});
    }
    content_layout()->addWidget(table_);
    if (fut_enabled_) {
        set_fut_header(fut_failed_ ? QStringLiteral(" !") : QString());
        // The viewport, not the table: the table's own Resize arrives before
        // its viewport has taken the new size, so the fit would use the old.
        table_->viewport()->installEventFilter(this);
    }
    // Left click opens the row's chart.
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= row_symbols_.size())
            return;
        const QString sym = row_symbols_[row];
        MarketChartDialog::show_for(sym, label_map_.value(sym, sym), this);
    });
}

void QuoteTableWidget::enable_futures() {
    if (fut_enabled_)
        return;
    fut_enabled_ = true;
    if (table_) {
        apply_fut_columns();
        table_->viewport()->installEventFilter(this);
    }
    set_fut_header(QString());
    fut_timer_ = new QTimer(this);
    fut_timer_->setInterval(kFutRefreshMs);
    connect(fut_timer_, &QTimer::timeout, this, [this] { fetch_futures(); });
    if (isVisible()) {
        fut_timer_->start();
        fetch_futures();
    }
}

void QuoteTableWidget::apply_fut_columns() {
    table_->set_headers({"SYMBOL", "PRICE", "CHG", "CHG%", "FUT%"});
    // Widths are set by fit_columns(), not by the header's resize modes.
    auto* h = table_->horizontalHeader();
    h->setStretchLastSection(false);
    for (int c = 0; c <= kColFut; ++c)
        h->setSectionResizeMode(c, QHeaderView::Interactive);
    fit_columns();
}

void QuoteTableWidget::fit_columns() {
    if (!table_ || !fut_enabled_)
        return;
    // Every column gets what its widest cell needs. Spare width is shared by
    // the number columns, not handed to the name: a stretched SYMBOL column
    // leaves a gulf between the name and its price. Short of width, the name
    // gives way first (it elides), so no number is ever cut.
    auto* h = table_->horizontalHeader();
    const int n = table_->columnCount();
    table_->resizeColumnsToContents();   // measures cells and header alike
    QVector<int> need(n);
    int total = 0;
    for (int c = 0; c < n; ++c) {
        need[c] = table_->columnWidth(c);
        total += need[c];
    }
    const int avail = table_->viewport()->width();
    int extra = avail - total;
    if (extra < 0) {
        need[0] = std::max(h->sectionSizeHint(0), need[0] + extra);
        extra = 0;
    }
    const int share = n > 1 ? extra / (n - 1) : 0;
    int rem = n > 1 ? extra % (n - 1) : 0;
    for (int c = 0; c < n; ++c) {
        int w = need[c];
        if (c > 0) {
            w += share + (rem > 0 ? 1 : 0);
            if (rem > 0)
                --rem;
        }
        h->resizeSection(c, w);
    }
}

bool QuoteTableWidget::eventFilter(QObject* obj, QEvent* e) {
    if (table_ && obj == table_->viewport() && e->type() == QEvent::Resize)
        fit_columns();
    return BaseWidget::eventFilter(obj, e);
}

void QuoteTableWidget::set_fut_header(const QString& suffix, const QString& extra_tip) {
    if (!table_ || table_->columnCount() <= kColFut)
        return;
    auto* h = table_->horizontalHeaderItem(kColFut);
    if (!h)
        return;
    h->setText(QStringLiteral("FUT%") + suffix);
    h->setToolTip(extra_tip.isEmpty() ? QString::fromUtf8(kFutTip)
                                      : QString::fromUtf8(kFutTip) + QStringLiteral("\n\n") + extra_tip);
    // The " !" marker is the only sign a refresh failed; it must not be cut.
    schedule_fit();
}

void QuoteTableWidget::schedule_fit() {
    // Coalesced: each symbol's quote arrives separately and re-renders the
    // table, so measuring on every delivery would re-measure every cell a
    // dozen times per refresh. Once per event-loop pass is enough.
    if (fit_pending_)
        return;
    fit_pending_ = true;
    QTimer::singleShot(0, this, [this]() {
        fit_pending_ = false;
        fit_columns();
    });
}

void QuoteTableWidget::fetch_futures() {
    if (!fut_enabled_ || fut_in_flight_)
        return;
    fut_in_flight_ = true;
    // The suffix is the only sign a first fetch is still running; without it
    // an empty column is indistinguishable from a broken one.
    if (fut_.isEmpty())
        set_fut_header(QStringLiteral(" ·"), QStringLiteral("Fetching index futures…"));
    services::fetch_index_futures(
        symbols_, this, [this](bool ok, QHash<QString, services::IndexFutureQuote> fresh, QString err) {
            fut_in_flight_ = false;
            if (!ok) {
                // Keep the last good numbers on screen; say the refresh failed.
                fut_failed_ = true;
                set_fut_header(QStringLiteral(" !"),
                               QStringLiteral("The last fetch failed: %1\nRetrying shortly.").arg(err));
                return;
            }
            fut_failed_ = false;
            // Replace, don't merge: a row that lost its sound reading goes
            // back to a dash rather than keeping a quote against an old settle.
            fut_ = fresh;
            set_fut_header(QString());
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
    if (fut_timer_ && !fut_timer_->isActive()) {
        fut_timer_->start();
        fetch_futures();
    }
}

void QuoteTableWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (hub_active_)
        hub_unsubscribe_all();
    if (fut_timer_)
        fut_timer_->stop();
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
    fetch_futures();
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
        const auto fut = fut_.constFind(q.symbol);
        if (fut_enabled_)
            cells << (fut != fut_.constEnd() ? ui::formatting::format_percent(fut->pct, 2, true)
                                             : ui::formatting::placeholder());
        table_->add_row(cells);
        row_symbols_.append(q.symbol);
        int row = table_->rowCount() - 1;
        table_->set_cell_color(row, 2, ui::change_color(q.change_pct));
        table_->set_cell_color(row, 3, ui::change_color(q.change_pct));
        if (fut_enabled_) {
            if (auto* it = table_->item(row, kColFut)) {
                if (fut != fut_.constEnd()) {
                    it->setForeground(QColor(ui::change_color(fut->pct)));
                    it->setToolTip(services::index_future_tooltip(*fut, display_name));
                } else {
                    it->setToolTip(services::index_future_unavailable_tooltip(q.symbol, display_name));
                }
            }
        }
    }
    schedule_fit();
}


} // namespace fincept::screens::widgets

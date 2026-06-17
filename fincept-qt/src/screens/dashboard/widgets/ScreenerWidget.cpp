#include "screens/dashboard/widgets/ScreenerWidget.h"

#include "ui/theme/Theme.h"

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"

#include <QFrame>
#include <QHBoxLayout>

#include <algorithm>

namespace fincept::screens::widgets {

// Broad basket: large-caps across sectors for screening
static const QStringList kScreenerSymbols = {
    "AAPL", "MSFT",  "GOOGL", "AMZN", "NVDA", "META", "TSLA", "NFLX", "AMD",  "INTC", "JPM",  "GS",   "BAC",  "WFC",
    "MS",   "BRK-B", "V",     "MA",   "WMT",  "COST", "TGT",  "HD",   "AMGN", "PFE",  "JNJ",  "MRK",  "XOM",  "CVX",
    "SLB",  "NEE",   "DUK",   "SO",   "CAT",  "GE",   "HON",  "RTX",  "PLTR", "COIN", "SOFI", "PYPL", "SNAP", "UBER"};

ScreenerWidget::ScreenerWidget(QWidget* parent) : BaseWidget("STOCK SCREENER", parent, ui::colors::INFO()) {
    build_body();

    // Coalesce the burst of per-symbol quote callbacks (42/cycle) into a single
    // recompute+re-render per cycle.
    coalesce_ = new QTimer(this);
    coalesce_->setSingleShot(true);
    coalesce_->setInterval(16);
    connect(coalesce_, &QTimer::timeout, this, &ScreenerWidget::rebuild_all_quotes);

    connect(this, &BaseWidget::refresh_requested, this, &ScreenerWidget::refresh_data);

    apply_styles();
    set_loading(true);
}

void ScreenerWidget::build_body() {
    auto* vl = content_layout();
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // Filter bar
    filter_bar_ = new QWidget(this);
    auto* fl = new QHBoxLayout(filter_bar_);
    fl->setContentsMargins(8, 6, 8, 6);
    fl->setSpacing(8);

    filter_lbl_ = new QLabel("SORT BY");
    fl->addWidget(filter_lbl_);

    filter_combo_ = new QComboBox;
    filter_combo_->addItems({"% CHANGE ↑", "% CHANGE ↓", "VOLUME ↓", "PRICE ↓", "PRICE ↑"});
    fl->addWidget(filter_combo_);
    fl->addStretch();

    count_lbl_ = new QLabel(QString("%1 symbols").arg(kScreenerSymbols.size()));
    fl->addWidget(count_lbl_);

    vl->addWidget(filter_bar_);

    // Column headers
    header_ = new QWidget(this);
    auto* hl = new QHBoxLayout(header_);
    hl->setContentsMargins(8, 3, 8, 3);

    auto make_hdr = [&](const QString& t, int s, Qt::Alignment a = Qt::AlignLeft) {
        auto* l = new QLabel(t);
        l->setAlignment(a);
        header_labels_.append(l);
        hl->addWidget(l, s);
    };
    make_hdr("SYMBOL", 2);
    make_hdr("PRICE", 2, Qt::AlignRight);
    make_hdr("CHG%", 1, Qt::AlignRight);
    make_hdr("VOLUME", 2, Qt::AlignRight);
    vl->addWidget(header_);

    // Scrollable list
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);

    auto* list_widget = new QWidget(this);
    list_widget->setStyleSheet("background: transparent;");
    list_layout_ = new QVBoxLayout(list_widget);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);

    build_rows();
    list_layout_->addStretch();

    scroll_->setWidget(list_widget);
    vl->addWidget(scroll_, 1);

    connect(filter_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ScreenerWidget::apply_filter);
}

// Build the persistent pool of up to 20 row widgets once. render_rows() reuses
// these, hiding the slots past the current result count.
void ScreenerWidget::build_rows() {
    rows_.clear();
    for (int i = 0; i < 20; ++i) {
        Row row;
        row.container = new QWidget(this);
        auto* rl = new QHBoxLayout(row.container);
        rl->setContentsMargins(8, 4, 8, 4);

        row.sym = new QLabel(QString{});
        rl->addWidget(row.sym, 2);

        row.price = new QLabel(QString{});
        row.price->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rl->addWidget(row.price, 2);

        row.chg = new QLabel(QString{});
        row.chg->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rl->addWidget(row.chg, 1);

        row.vol = new QLabel(QString{});
        row.vol->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rl->addWidget(row.vol, 2);

        row.container->hide();
        style_row(row, i % 2 != 0);
        list_layout_->addWidget(row.container);
        rows_.append(row);
    }
}

// Static (theme/striping-driven) styling for a row. The change-cell color is
// data-driven and set in render_rows().
void ScreenerWidget::style_row(Row& row, bool alt) {
    if (!row.container)
        return;
    row.container->setStyleSheet(
        QString("background: %1;").arg(alt ? ui::colors::BG_RAISED() : QStringLiteral("transparent")));
    row.sym->setStyleSheet(
        QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;").arg(ui::colors::INFO()));
    row.price->setStyleSheet(
        QString("color: %1; font-size: 10px; background: transparent;").arg(ui::colors::TEXT_PRIMARY()));
    row.vol->setStyleSheet(
        QString("color: %1; font-size: 10px; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
}

void ScreenerWidget::apply_styles() {
    // Body may be torn down by set_error() while a fetch failure is showing.
    if (!filter_bar_)
        return;
    filter_bar_->setStyleSheet(QString("background: %1;").arg(ui::colors::BG_RAISED()));
    filter_lbl_->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent;")
                                   .arg(ui::colors::TEXT_SECONDARY()));
    filter_combo_->setStyleSheet(
        QString("QComboBox { background: %1; color: %2; border: 1px solid %3; font-size: 10px; padding: 2px 6px; }"
                "QComboBox::drop-down { border: none; }"
                "QComboBox QAbstractItemView { background: %1; color: %2; border: 1px solid %3; }")
            .arg(ui::colors::BG_BASE())
            .arg(ui::colors::TEXT_PRIMARY())
            .arg(ui::colors::BORDER_MED()));
    count_lbl_->setStyleSheet(
        QString("color: %1; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));

    header_->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                               .arg(ui::colors::BG_RAISED())
                               .arg(ui::colors::BORDER_DIM()));
    for (auto* lbl : header_labels_) {
        lbl->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent;")
                               .arg(ui::colors::TEXT_SECONDARY()));
    }

    scroll_->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { width: 4px; background: transparent; }" +
        QString("QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }")
            .arg(ui::colors::BORDER_MED()) +
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    // Restyle the persistent rows' static (theme/striping-driven) bits.
    for (int i = 0; i < rows_.size(); ++i)
        style_row(rows_[i], i % 2 != 0);

    // Re-render data rows with current tokens if data exists (re-resolves the
    // data-driven change-cell colors).
    if (!all_quotes_.isEmpty())
        apply_filter();
}

void ScreenerWidget::on_theme_changed() {
    apply_styles();
}

void ScreenerWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!hub_active_)
        hub_subscribe_all();
}

void ScreenerWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (hub_active_)
        hub_unsubscribe_all();
}

void ScreenerWidget::refresh_data() {
    auto& hub = datahub::DataHub::instance();
    QStringList topics;
    topics.reserve(kScreenerSymbols.size());
    for (const auto& sym : kScreenerSymbols)
        topics.append(QStringLiteral("market:quote:") + sym);
    hub.request(topics, /*force=*/true);  // user-triggered: bypass min_interval
}


void ScreenerWidget::hub_subscribe_all() {
    auto& hub = datahub::DataHub::instance();
    for (const auto& sym : kScreenerSymbols) {
        const QString topic = QStringLiteral("market:quote:") + sym;
        hub.subscribe(this, topic, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            row_cache_.insert(sym, v.value<services::QuoteData>());
            set_loading(false);
            // A prior full-batch error may have replaced the body with
            // set_error()'s label — rebuild before rendering recovered data.
            if (!list_layout_)
                build_body();
            // Coalesce: cache now, render once when the burst settles.
            coalesce_->start();
        });
    }
    // Surface a full-batch fetch failure instead of an endless spinner. Error
    // only while nothing has loaded — once quotes are cached, a single symbol's
    // transient error must not wipe the populated screener.
    hub.subscribe_pattern_errors(this, QStringLiteral("market:quote:*"),
                                 [this](const QString&, const QString&) {
                                     set_loading(false);
                                     if (row_cache_.isEmpty() && list_layout_) {
                                         set_error(QStringLiteral("Screener data unavailable"));
                                         // set_error() deleteLater()'d the body; null the persistent
                                         // pointers so apply_styles()/render rebuild instead of
                                         // dereferencing deleted widgets.
                                         filter_combo_ = nullptr;
                                         list_layout_ = nullptr;
                                         filter_bar_ = nullptr;
                                         filter_lbl_ = nullptr;
                                         count_lbl_ = nullptr;
                                         header_ = nullptr;
                                         header_labels_.clear();
                                         scroll_ = nullptr;
                                         // The persistent row pool lived under the
                                         // (now-deleted) scroll list; drop the dangling
                                         // pointers so build_rows() recreates them.
                                         rows_.clear();
                                     }
                                 });
    hub_active_ = true;
}

void ScreenerWidget::hub_unsubscribe_all() {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    hub.unsubscribe_pattern_errors(this, QStringLiteral("market:quote:*"));
    hub_active_ = false;
}

void ScreenerWidget::rebuild_all_quotes() {
    all_quotes_.clear();
    all_quotes_.reserve(row_cache_.size());
    for (const auto& sym : kScreenerSymbols) {
        auto it = row_cache_.constFind(sym);
        if (it != row_cache_.constEnd())
            all_quotes_.append(it.value());
    }
    apply_filter();
}


void ScreenerWidget::apply_filter() {
    if (all_quotes_.isEmpty())
        return;

    QVector<services::QuoteData> sorted = all_quotes_;
    int idx = filter_combo_ ? filter_combo_->currentIndex() : 0;

    switch (idx) {
        case 0: // % change asc (top gainers first)
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.change_pct > b.change_pct; });
            break;
        case 1: // % change desc (top losers first)
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.change_pct < b.change_pct; });
            break;
        case 2: // volume desc
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.volume > b.volume; });
            break;
        case 3: // price desc
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.price > b.price; });
            break;
        case 4: // price asc
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.price < b.price; });
            break;
    }

    // Show top 20
    if (sorted.size() > 20)
        sorted.resize(20);
    render_rows(sorted);
}

void ScreenerWidget::render_rows(const QVector<services::QuoteData>& rows) {
    if (!list_layout_ || rows_.isEmpty())
        build_body();

    // Update the persistent row pool in place. The slot's symbol changes as the
    // sort order changes, but the widgets are reused — no teardown/recreate.
    const int n = std::min<int>(rows.size(), rows_.size());
    for (int i = 0; i < n; ++i) {
        const auto& q = rows[i];
        Row& row = rows_[i];

        row.sym->setText(q.symbol);
        row.price->setText(QString("$%1").arg(q.price, 0, 'f', 2));

        row.chg->setText(QString("%1%2%").arg(q.change_pct >= 0 ? "+" : "").arg(q.change_pct, 0, 'f', 2));
        const QString chg_col = q.change_pct > 0   ? ui::colors::POSITIVE()
                                : q.change_pct < 0 ? ui::colors::NEGATIVE()
                                                   : ui::colors::TEXT_PRIMARY();
        row.chg->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;").arg(chg_col));

        // Format volume: e.g. 45.2M
        QString vol_str;
        if (q.volume >= 1e9)
            vol_str = QString("%1B").arg(q.volume / 1e9, 0, 'f', 1);
        else if (q.volume >= 1e6)
            vol_str = QString("%1M").arg(q.volume / 1e6, 0, 'f', 1);
        else if (q.volume >= 1e3)
            vol_str = QString("%1K").arg(q.volume / 1e3, 0, 'f', 0);
        else
            vol_str = QString::number(static_cast<long long>(q.volume));
        row.vol->setText(vol_str);

        row.container->show();
    }
    // Hide unused slots so the visible row set matches the result count.
    for (int i = n; i < rows_.size(); ++i)
        if (rows_[i].container)
            rows_[i].container->hide();
}

} // namespace fincept::screens::widgets

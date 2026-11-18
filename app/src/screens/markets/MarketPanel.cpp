#include "screens/markets/MarketPanel.h"


#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "services/markets/LastKnownQuote.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QPointer>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace finterm::screens {

MarketPanel::MarketPanel(const MarketPanelConfig& config, QWidget* parent) : QWidget(parent), config_(config) {
    if (config_.column_order.isEmpty())
        config_.column_order = default_market_columns();
    setMinimumWidth(180);
    setMinimumHeight(kHeaderH + kColHeaderH + kRowH * 4); // always room for at least 4 rows
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    build_ui();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });
    refresh_theme();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void MarketPanel::build_ui() {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // --- Header (28px) ---
    header_ = new QWidget(this);
    header_->setFixedHeight(kHeaderH);
    auto* hhl = new QHBoxLayout(header_);
    hhl->setContentsMargins(8, 0, 4, 0);
    hhl->setSpacing(2);

    title_label_ = new QLabel(config_.title.toUpper());
    hhl->addWidget(title_label_);
    hhl->addStretch();

    auto make_hdr_btn = [](const QString& text) -> QPushButton* {
        auto* b = new QPushButton(text);
        b->setFixedHeight(20);
        b->setCursor(Qt::PointingHandCursor);
        b->setFlat(true);
        return b;
    };

    cols_btn_ = make_hdr_btn("[COLS]");
    edit_btn_ = make_hdr_btn("[EDIT]");
    delete_btn_ = make_hdr_btn("[DEL]");

    hhl->addWidget(cols_btn_);
    hhl->addWidget(edit_btn_);
    hhl->addWidget(delete_btn_);

    connect(cols_btn_, &QPushButton::clicked, this, &MarketPanel::open_cols_dropdown);
    connect(edit_btn_, &QPushButton::clicked, this, [this]() { emit edit_requested(config_.id); });
    connect(delete_btn_, &QPushButton::clicked, this, [this]() { emit delete_requested(config_.id); });

    vl->addWidget(header_);

    // --- Body (fills remaining height) ---
    body_ = new QWidget(this);
    auto* bl = new QVBoxLayout(body_);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);

    // Data table
    table_ = new QTableWidget(body_);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(kRowH);
    table_->horizontalHeader()->setFixedHeight(kColHeaderH);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Prevent the table's content-based sizeHint from dominating splitter layout.
    // Without this, QTableWidget returns a large sizeHint proportional to row count,
    // causing the first panel in each column to grab disproportionate space.
    table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    setup_table_columns();

    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableWidget::customContextMenuRequested, this, &MarketPanel::show_row_context_menu);
    // Left click opens the row's chart.
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row >= 0 && row < row_symbols_.size() && !row_symbols_[row].first.isEmpty())
            emit symbol_activated(row_symbols_[row].first, row_symbols_[row].second);
    });

    table_->setVisible(false); // hidden until first data arrives
    bl->addWidget(table_);

    // Error state (hidden by default)
    error_widget_ = new QWidget(body_);
    auto* el = new QVBoxLayout(error_widget_);
    el->setAlignment(Qt::AlignCenter);
    error_label_ = new QLabel;
    error_label_->setAlignment(Qt::AlignCenter);
    retry_btn_ = new QPushButton("[RETRY]");
    retry_btn_->setCursor(Qt::PointingHandCursor);
    retry_btn_->setFlat(true);
    connect(retry_btn_, &QPushButton::clicked, this, &MarketPanel::refresh);
    el->addWidget(error_label_);
    el->addWidget(retry_btn_);
    error_widget_->setVisible(false);
    bl->addWidget(error_widget_);

    // Loading overlay (visible until first data)
    loading_widget_ = new QWidget(body_);
    auto* ll = new QVBoxLayout(loading_widget_);
    ll->setAlignment(Qt::AlignCenter);
    loading_label_ = new QLabel("  ⠋  LOADING");
    loading_label_->setAlignment(Qt::AlignCenter);
    ll->addWidget(loading_label_);
    bl->addWidget(loading_widget_);

    loading_timer_ = new QTimer(this);
    loading_timer_->setInterval(120);
    connect(loading_timer_, &QTimer::timeout, this, &MarketPanel::tick_loading_anim);

    // A hydrated row turns current when its poll lands, but the hub skips
    // re-publishing a byte-identical quote — no delivery would undim it.
    stale_timer_ = new QTimer(this);
    stale_timer_->setInterval(5000);
    connect(stale_timer_, &QTimer::timeout, this, &MarketPanel::recheck_stale);

    vl->addWidget(body_, 1);
}

void MarketPanel::setup_table_columns() {
    const QStringList& cols = config_.column_order;
    table_->setColumnCount(cols.size());

    // Set header labels with alignment matching cell alignment
    for (int i = 0; i < cols.size(); ++i) {
        const QString& c = cols[i];
        auto* hdr = new QTableWidgetItem(c);
        bool is_text = (c == "SYMBOL" || c == "NAME");
        hdr->setTextAlignment(is_text ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignRight | Qt::AlignVCenter));
        table_->setHorizontalHeaderItem(i, hdr);
    }

    // All columns stretch to fill available width evenly.
    for (int i = 0; i < cols.size(); ++i)
        table_->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
}

// ---------------------------------------------------------------------------
// Column configurator
// ---------------------------------------------------------------------------

void MarketPanel::open_cols_dropdown() {
    auto* menu = new QMenu(this);
    menu->setStyleSheet(
        QString("QMenu{background:%1;border:1px solid %2;color:%3;font-size:12px;font-family:monospace;}"
                "QMenu::item{padding:4px 16px;}"
                "QMenu::item:selected{background:%4;}"
                "QMenu::indicator{width:12px;height:12px;}")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BG_HOVER()));

    // "OPEN" is deliberately absent: QuoteData carries no open price, so the
    // column could only ever render "--". A selectable column that is always
    // empty reads as missing data rather than as an unsupported field.
    const QStringList optional = {"CHG", "CHG%", "HIGH", "LOW", "VOL", "BID", "ASK", "NAME", "PRESSURE"};

    for (const QString& col : optional) {
        auto* act = menu->addAction(col);
        act->setCheckable(true);
        act->setChecked(config_.column_order.contains(col));
        // Enforce max 7 columns (SYMBOL + LAST are locked = 2, so max 5 optional)
        if (!act->isChecked() && config_.column_order.size() >= 7)
            act->setEnabled(false);
        connect(act, &QAction::toggled, this, [this, col](bool checked) {
            if (checked) {
                if (!config_.column_order.contains(col) && config_.column_order.size() < 7)
                    config_.column_order.append(col);
            } else {
                config_.column_order.removeAll(col);
                // Always keep SYMBOL and LAST
                if (!config_.column_order.contains("SYMBOL"))
                    config_.column_order.prepend("SYMBOL");
                if (!config_.column_order.contains("LAST")) {
                    int idx = config_.column_order.indexOf("SYMBOL");
                    config_.column_order.insert(idx + 1, "LAST");
                }
            }
            setup_table_columns();
            if (has_data_)
                populate(cached_quotes_);
            emit config_changed(config_);
        });
    }

    menu->exec(cols_btn_->mapToGlobal(QPoint(0, cols_btn_->height())));
    menu->deleteLater();
}

// ---------------------------------------------------------------------------
// Data population
// ---------------------------------------------------------------------------

void MarketPanel::refresh() {
    title_label_->setText(config_.title.toUpper() + "  ...");

    if (!has_data_)
        show_loading();

    hub_resubscribe();
}

void MarketPanel::rebuild_from_cache() {
    // Nothing known for any symbol yet: keep the loading state.
    if (std::none_of(config_.symbols.cbegin(), config_.symbols.cend(),
                     [this](const QString& sym) { return row_cache_.contains(sym); }))
        return;
    // One row per configured symbol, in order. A symbol with no quote at all
    // keeps its row with dashes rather than silently dropping out of the list.
    cached_quotes_.clear();
    cached_quotes_.reserve(config_.symbols.size());
    for (const auto& sym : config_.symbols) {
        if (const auto it = row_cache_.constFind(sym); it != row_cache_.constEnd()) {
            cached_quotes_.append(it.value());
        } else {
            services::QuoteData missing;
            missing.symbol = sym;
            missing.price = missing.change = missing.change_pct = missing.high = missing.low = missing.volume =
                services::kUnknownNum;
            cached_quotes_.append(missing);
        }
    }
    const bool first_data = !has_data_;
    has_data_ = true;
    title_label_->setText(config_.title.toUpper());
    if (first_data)
        hide_loading();
    show_data();
}

void MarketPanel::hub_resubscribe() {
    auto& hub = datahub::DataHub::instance();

    if (config_.symbols.isEmpty()) {
        hub.unsubscribe(this);
        hub_active_ = false;
        emit refresh_finished();
        return;
    }

    // Fresh subscribe if never active or symbol set was reconfigured.
    // Cheaper to always re-wire since MarketsScreen rarely calls refresh()
    // outside of user action / auto-refresh tick, and the hub dedupes topics.
    hub.unsubscribe(this);
    row_cache_.clear();
    pending_initial_.clear();
    for (const auto& sym : config_.symbols)
        pending_initial_.insert(sym);
    refresh_inflight_ = true;

    QStringList topics;
    topics.reserve(config_.symbols.size());
    for (const QString& sym : config_.symbols) {
        const QString topic = QStringLiteral("market:quote:") + sym;
        topics.append(topic);
        hub.subscribe(this, topic, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            row_cache_.insert(sym, v.value<services::QuoteData>());
            quote_render_.request(); // one render per tick batch
            // Drain the initial-set counter on first delivery for this symbol.
            if (refresh_inflight_ && pending_initial_.remove(sym) && pending_initial_.isEmpty()) {
                refresh_inflight_ = false;
                emit refresh_finished();
            }
        });
    }
    // force=true: on cold start, DataHub::subscribe auto-triggers a fetch,
    // but on user-initiated refresh_all() (F5 / auto-refresh tick / panel
    // reconfig) we want to bypass min_interval. Producer rate limit still
    // paces upstream calls.
    hub.request(topics, /*force=*/true);
    hub_active_ = true;
    stale_timer_->start();
    // subscribe() replays only values younger than the TTL; a symbol whose
    // refresh keeps failing would otherwise never get a row back.
    if (services::seed_last_known_quotes(config_.symbols, row_cache_) > 0)
        quote_render_.request();
}

QSet<QString> MarketPanel::stale_symbols(const QVector<services::QuoteData>& quotes) {
    // Every row, not only those that fit: recheck_stale() compares against
    // this, and a symbol below the fold must not make the sets differ forever.
    QSet<QString> stale;
    for (const auto& q : quotes)
        if (std::isfinite(q.price) && q.price > 0 && services::last_known_is_stale(q.symbol))
            stale.insert(q.symbol);
    return stale;
}

void MarketPanel::recheck_stale() {
    if (!has_data_ || !isVisible())
        return;
    if (stale_symbols(cached_quotes_) != stale_shown_)
        populate(cached_quotes_);
}

void MarketPanel::hub_unsubscribe_all() {
    if (!hub_active_)
        return;
    datahub::DataHub::instance().unsubscribe(this);
    hub_active_ = false;
    stale_timer_->stop();
    refresh_inflight_ = false;
    pending_initial_.clear();
}

void MarketPanel::show_loading() {
    loading_frame_ = 0;
    loading_widget_->setVisible(true);
    table_->setVisible(false);
    error_widget_->setVisible(false);
    loading_timer_->start();
}

void MarketPanel::hide_loading() {
    loading_timer_->stop();
    loading_widget_->setVisible(false);
}

void MarketPanel::tick_loading_anim() {
    // Braille spinner: ⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏
    static const char* const kFrames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    constexpr int kCount = 10;
    loading_frame_ = (loading_frame_ + 1) % kCount;
    loading_label_->setText(QString("  %1  LOADING").arg(kFrames[loading_frame_]));
}

void MarketPanel::show_data() {
    error_widget_->setVisible(false);
    table_->setVisible(true);
    // Defer populate until after the splitter has performed layout so body_->height() is valid
    QPointer<MarketPanel> self = this;
    QMetaObject::invokeMethod(
        this,
        [self]() {
            if (self)
                self->populate(self->cached_quotes_);
        },
        Qt::QueuedConnection);
}

void MarketPanel::show_error(const QString& msg) {
    table_->setVisible(false);
    error_widget_->setVisible(true);
    error_label_->setText(msg);
}

void MarketPanel::populate(const QVector<services::QuoteData>& quotes) {
    const int body_h = body_->height();
    // Use actual visible rows if laid out, otherwise show all quotes (resizeEvent will trim)
    const int visible = (body_h > kColHeaderH + kRowH) ? (body_h - kColHeaderH) / kRowH : quotes.size();
    const int count = qMin(quotes.size(), qMax(visible, 0));
    table_->setRowCount(count);

    row_symbols_.resize(count);
    stale_shown_ = stale_symbols(quotes);
    for (int row = 0; row < count; ++row) {
        const auto& q = quotes[row];
        row_symbols_[row] = {q.symbol, q.name};
        const bool has_price = std::isfinite(q.price) && q.price > 0;
        // A quote from an earlier session (hydrated) or whose refresh failed
        // is shown dimmed and says so; it is never drawn as the current price.
        const bool stale = stale_shown_.contains(q.symbol);
        const QString tip = !has_price ? QStringLiteral("No quote yet for %1.").arg(q.symbol)
                            : stale    ? QStringLiteral("STALE: the last known quote for %1 (an earlier "
                                                         "session's, or its refresh failed); waiting for a fresh one.")
                                          .arg(q.symbol)
                                       : QString();
        auto num = [](double v, int dp) {
            return std::isfinite(v) ? QString::number(v, 'f', dp) : ui::formatting::placeholder();
        };
        bool pos = q.change >= 0;
        // No previous close → change unknown (NaN): "—", not a 0.00 arrow.
        const bool chg_known = std::isfinite(q.change) && std::isfinite(q.change_pct);
        // A stale row's change is the old session's: no gain/loss colour.
        const QString cc = stale ? ui::colors::TEXT_SECONDARY() : pos ? ui::colors::POSITIVE() : ui::colors::NEGATIVE();
        const QString arr = pos ? QString::fromUtf8("\xe2\x96\xb2") : QString::fromUtf8("\xe2\x96\xbc");
        int prec = q.price > 1.0 ? 2 : 4;

        // Reuse the row's items and touch only what changed: this runs on every
        // live tick batch, and a fresh item per cell repaints the whole panel.
        auto put = [this, &tip](int r, int c, const QString& text, const QString& color,
                                Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter) {
            QTableWidgetItem* item = table_->item(r, c);
            if (!item) {
                item = new QTableWidgetItem(text);
                item->setForeground(QColor(color));
                item->setTextAlignment(align);
                item->setToolTip(tip);
                table_->setItem(r, c, item);
                return;
            }
            if (item->text() != text)
                item->setText(text);
            if (item->toolTip() != tip)
                item->setToolTip(tip);
            const QColor fg(color);
            if (item->foreground().color() != fg)
                item->setForeground(fg);
            if (item->textAlignment() != int(align))
                item->setTextAlignment(align);
        };

        const QStringList& cols = config_.column_order;
        for (int ci = 0; ci < cols.size(); ++ci) {
            const QString& col = cols[ci];
            if (col == "SYMBOL")
                put(row, ci, q.symbol, ui::colors::TEXT_PRIMARY(), Qt::AlignLeft | Qt::AlignVCenter);
            else if (col == "NAME")
                put(row, ci, q.name, ui::colors::TEXT_SECONDARY(), Qt::AlignLeft | Qt::AlignVCenter);
            else if (col == "LAST")
                put(row, ci, has_price ? QString::number(q.price, 'f', prec) : ui::formatting::placeholder(),
                    has_price && !stale ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY());
            else if (col == "CHG")
                put(row, ci,
                    chg_known ? QString("%1 %2").arg(arr).arg(std::abs(q.change), 0, 'f', 2)
                              : ui::formatting::placeholder(),
                    chg_known ? cc : ui::colors::TEXT_SECONDARY());
            else if (col == "CHG%")
                put(row, ci,
                    chg_known ? QString("%1%2%").arg(arr).arg(std::abs(q.change_pct), 0, 'f', 2)
                              : ui::formatting::placeholder(),
                    chg_known ? cc : ui::colors::TEXT_SECONDARY());
            else if (col == "HIGH")
                put(row, ci, num(q.high, 2), ui::colors::TEXT_SECONDARY());
            else if (col == "LOW")
                put(row, ci, num(q.low, 2), ui::colors::TEXT_SECONDARY());
            else if (col == "VOL")
                put(row, ci, ui::formatting::format_compact_volume(q.volume), ui::colors::TEXT_SECONDARY());
            // Same precision as LAST — at 2dp a sub-$1 symbol's bid and ask
            // round to the same number and the spread reads as zero.
            else if (col == "BID")
                put(row, ci, q.bid > 0 ? QString::number(q.bid, 'f', prec) : QStringLiteral("--"),
                    ui::colors::TEXT_SECONDARY());
            else if (col == "ASK")
                put(row, ci, q.ask > 0 ? QString::number(q.ask, 'f', prec) : QStringLiteral("--"),
                    ui::colors::TEXT_SECONDARY());
            // OPEN is not carried by QuoteData, so this could only ever
            // render "--". A column the user can select and that is
            // permanently empty reads as missing data rather than as an
            // unsupported field; it is removed from the selectable set (see
            // MarketPanelConfig) and this arm is gone with it.
            else if (col == "PRESSURE") {
                QString label = "--";
                QString color = ui::colors::TEXT_SECONDARY();
                // BOTH sizes must be present. yfinance routinely reports one
                // side and not the other; with an `||` test the missing side
                // reads as 0 and an absent ask turns into a confident green
                // "BUY" — a signal manufactured out of missing data. An
                // imbalance is only meaningful against a known other side.
                if (q.bid_size > 0 && q.ask_size > 0) {
                    if (q.bid_size > q.ask_size) {
                        label = "BUY";
                        color = ui::colors::POSITIVE();
                    } else if (q.ask_size > q.bid_size) {
                        label = "SELL";
                        color = ui::colors::NEGATIVE();
                    } else {
                        label = "NEUTRAL";
                        color = ui::colors::WARNING();
                    }
                }
                put(row, ci, label, color);
            }
        }
    }
}

void MarketPanel::update_visible_rows() {
    if (!table_ || !body_)
        return;
    const int visible = (body_->height() - kColHeaderH) / kRowH;
    table_->setRowCount(qMax(visible, 0));
}

// ---------------------------------------------------------------------------
// Size hints — small uniform value so QSplitter distributes equal height
// ---------------------------------------------------------------------------

QSize MarketPanel::sizeHint() const {
    return QSize(180, 138); // kHeaderH + kColHeaderH + kRowH*4
}

QSize MarketPanel::minimumSizeHint() const {
    return QSize(180, 138);
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void MarketPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (has_data_)
        populate(cached_quotes_);
    else
        update_visible_rows();
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

void MarketPanel::refresh_theme() {
    const QString ff = ui::fonts::DATA_FAMILY;
    const int fdata = ui::fonts::font_px(-2);  // table cell font size (base-2, e.g. 12px at base 14)
    const int fhdr = ui::fonts::font_px(-2);   // column header font size
    const int fbtn = ui::fonts::font_px(-3);   // button font size (base-3, e.g. 11px)
    const int ftitle = ui::fonts::font_px(-1); // panel title font size (base-1, e.g. 13px)

    setStyleSheet(
        QString("background:%1;border:1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));

    if (header_) {
        header_->setStyleSheet(
            QString("background:%1;border-bottom:1px solid %2;").arg(ui::colors::BG_RAISED(), ui::colors::AMBER()));
    }

    const QString btn_ss = QString("QPushButton{background:transparent;color:%1;border:none;"
                                   "font-size:%2px;font-family:'%3';padding:0 4px;}"
                                   "QPushButton:hover{color:%4;}")
                               .arg(ui::colors::TEXT_SECONDARY())
                               .arg(fbtn)
                               .arg(ff)
                               .arg(ui::colors::TEXT_PRIMARY());
    if (cols_btn_)
        cols_btn_->setStyleSheet(btn_ss);
    if (edit_btn_)
        edit_btn_->setStyleSheet(btn_ss);
    if (delete_btn_)
        delete_btn_->setStyleSheet(btn_ss);

    if (title_label_)
        title_label_->setStyleSheet(
            QString("color:%1;font-size:%2px;font-weight:700;font-family:'%3';background:transparent;")
                .arg(ui::colors::TEXT_PRIMARY())
                .arg(ftitle)
                .arg(ff));

    if (error_label_)
        error_label_->setStyleSheet(QString("color:%1;font-size:%2px;font-family:'%3';background:transparent;")
                                        .arg(ui::colors::NEGATIVE())
                                        .arg(fdata)
                                        .arg(ff));

    if (loading_label_)
        loading_label_->setStyleSheet(
            QString("color:%1;font-size:%2px;font-family:'%3';background:transparent;letter-spacing:2px;")
                .arg(ui::colors::TEXT_SECONDARY())
                .arg(fdata)
                .arg(ff));

    if (loading_widget_)
        loading_widget_->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    if (retry_btn_)
        retry_btn_->setStyleSheet(QString("QPushButton{background:transparent;color:%1;border:none;"
                                          "font-size:%2px;font-family:'%3';padding:0 4px;}"
                                          "QPushButton:hover{color:%4;}")
                                      .arg(ui::colors::TEXT_SECONDARY())
                                      .arg(fbtn)
                                      .arg(ff)
                                      .arg(ui::colors::TEXT_PRIMARY()));

    if (table_) {
        table_->setStyleSheet(QString("QTableWidget{background:%1;alternate-background-color:%2;border:none;"
                                      "font-size:%6px;font-family:'%7';}"
                                      "QTableWidget::item{padding:0 4px;}"
                                      "QTableWidget::item:selected{background:transparent;}"
                                      "QHeaderView::section{background:%3;color:%4;border:none;"
                                      "border-bottom:1px solid %5;font-size:%6px;font-weight:600;"
                                      "font-family:'%7';padding:0 4px;}")
                                  .arg(ui::colors::BG_BASE(), ui::colors::BG_SURFACE(), ui::colors::BG_BASE(),
                                       ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM())
                                  .arg(fhdr)
                                  .arg(ff));
    }
}

void MarketPanel::show_row_context_menu(const QPoint& pos) {
    auto* it = table_->itemAt(pos);
    if (!it)
        return;

    // Find SYMBOL by name rather than hard-coding column 0 — finterm allows
    // user-configurable column order, so SYMBOL's index isn't fixed.
    const int sym_col = config_.column_order.indexOf(QStringLiteral("SYMBOL"));
    if (sym_col < 0)
        return; // SYMBOL not shown — nothing meaningful to copy

    auto* sym_item = table_->item(it->row(), sym_col);
    if (!sym_item || sym_item->text().isEmpty())
        return;

    QMenu menu(this);
    const QString sym = sym_item->text();
    QAction* copy_act = menu.addAction(QStringLiteral("Copy Symbol  (%1)").arg(sym));
    connect(copy_act, &QAction::triggered, this, [sym]() { QApplication::clipboard()->setText(sym); });
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

} // namespace finterm::screens

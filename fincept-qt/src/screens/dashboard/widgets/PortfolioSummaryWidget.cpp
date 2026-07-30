#include "screens/dashboard/widgets/PortfolioSummaryWidget.h"

#include "core/logging/Logger.h"
#include "python/PythonWorker.h"
#include "services/portfolio/PortfolioService.h"
#include "ui/theme/Theme.h"

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QScrollArea>
#include <QSettings>
#include <QTimeZone>

#include <optional>

namespace fincept::screens::widgets {

// QSettings key remembering which portfolio the dashboard widget last showed.
static constexpr auto kSelectedPortfolioKey = "dashboard/portfolio_id";

// How often the after-hours column re-fetches while the widget is on screen.
// Deliberately slow: `extended_hours` downloads 5 days of 1-minute bars with
// pre/post included for every holding, which is a far heavier call than the
// quote stream feeding the other columns.
static constexpr int kAftRefreshMs = 60'000;

namespace {

/// JSON number → optional. The daemon sends `null` for "no extended print",
/// which must not collapse into a 0.0 the column would render as "flat".
std::optional<double> json_num(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    if (v.isNull() || v.isUndefined() || !v.isDouble())
        return std::nullopt;
    return v.toDouble();
}

} // namespace

PortfolioSummaryWidget::PortfolioSummaryWidget(QWidget* parent)
    : BaseWidget("PORTFOLIO SUMMARY", parent, ui::colors::POSITIVE) {
    auto* vl = content_layout();
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(6);

    // ── Portfolio selector — lives on the title bar (next to refresh/close)
    //    so it doesn't spend a whole content row. ──
    portfolio_combo_ = new QComboBox(this);
    portfolio_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    portfolio_combo_->setFixedHeight(18);
    portfolio_combo_->setMaximumWidth(150);
    portfolio_combo_->setCursor(Qt::PointingHandCursor);
    connect(portfolio_combo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (suppress_combo_signal_ || idx < 0 || idx >= portfolios_.size())
            return;
        select_portfolio(portfolios_.at(idx).id);
    });
    add_title_bar_control(portfolio_combo_);
    // Combo styling is applied with the rest of the widget in apply_styles().

    // ── Summary card — inline "LABEL  value" pairs, two per row ──
    // 4-column grid: [label0][value0][label1][value1] per row.
    summary_card_ = new QWidget(this);
    auto* sl = new QGridLayout(summary_card_);
    sl->setContentsMargins(10, 6, 10, 6);
    sl->setHorizontalSpacing(6);
    sl->setVerticalSpacing(4);
    // 6 columns = 3 "label value" pairs; each value column stretches so the
    // pairs spread evenly across the card's full width.
    sl->setColumnStretch(1, 1);
    sl->setColumnStretch(3, 1);
    sl->setColumnStretch(5, 1);

    // Each metric: label in even column, value in odd column, same row.
    auto make_metric = [&](const QString& label, QLabel*& value_out, int row, int pair) {
        auto* lbl = new QLabel(label);
        metric_labels_.append(lbl);
        sl->addWidget(lbl, row, pair * 2, Qt::AlignLeft | Qt::AlignVCenter);

        value_out = new QLabel("--");
        metric_values_.append(value_out);
        sl->addWidget(value_out, row, pair * 2 + 1, Qt::AlignLeft | Qt::AlignVCenter);
    };

    // Two rows × three pairs — width is plentiful, vertical space is precious.
    make_metric("TOTAL VALUE", total_value_lbl_,   0, 0);
    make_metric("DAY P&L",     day_pnl_lbl_,       0, 1);
    make_metric("DAY CHG%",    day_chg_pct_lbl_,   0, 2);
    make_metric("HOLDINGS",    num_holdings_lbl_,  1, 0);
    make_metric("TOTAL P&L",   total_pnl_lbl_,     1, 1);
    make_metric("TOTAL CHG%",  total_chg_pct_lbl_, 1, 2);

    vl->addWidget(summary_card_);

    // ── Holdings list header ──
    // Equal-width columns: every cell has stretch=1, so columns distribute
    // the available width evenly. SYM is no longer wider than the others.
    header_row_ = new QWidget(this);
    auto* hl = new QHBoxLayout(header_row_);
    hl->setContentsMargins(8, 3, 8, 3);

    auto make_hdr_lbl = [&](const QString& t, Qt::Alignment a = Qt::AlignLeft) {
        auto* l = new QLabel(t);
        l->setAlignment(a);
        header_labels_.append(l);
        hl->addWidget(l, 1); // every column gets equal stretch
        return l;
    };
    make_hdr_lbl("SYM");
    make_hdr_lbl("SHARES",  Qt::AlignRight);
    make_hdr_lbl("PRICE",   Qt::AlignRight);
    make_hdr_lbl("VALUE",   Qt::AlignRight);
    auto* pnl_hdr = make_hdr_lbl("P&L%", Qt::AlignRight);
    pnl_hdr->setToolTip(QStringLiteral(
        "Return on cost for the position: (price − average cost) ÷ average cost. "
        "The cash figure is in TOTAL P&L above."));
    auto* aft_hdr = make_hdr_lbl("AFT%", Qt::AlignRight);
    aft_hdr->setToolTip(QStringLiteral(
        "Extended-hours move against the last regular close — pre-market before the "
        "open, post-market after it.\n\n"
        "Blank during the regular session: by then the last extended print is already "
        "inside the price the other columns are quoting, so repeating it here would "
        "double-count it."));
    vl->addWidget(header_row_);

    // Scrollable holdings list
    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);

    auto* list_widget = new QWidget(this);
    list_widget->setStyleSheet("background: transparent;");
    list_layout_ = new QVBoxLayout(list_widget);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);
    list_layout_->addStretch();

    scroll_area_->setWidget(list_widget);
    vl->addWidget(scroll_area_, 1);

    connect(this, &BaseWidget::refresh_requested, this, [this] {
        load_holdings();
        fetch_aft();
    });

    // Extended hours refresh on its own slow cadence — see kAftRefreshMs. It
    // runs only while the widget is on screen (showEvent/hideEvent), so a
    // dashboard tab the user isn't looking at costs nothing.
    aft_timer_ = new QTimer(this);
    aft_timer_->setInterval(kAftRefreshMs);
    connect(aft_timer_, &QTimer::timeout, this, [this] { fetch_aft(); });

    // Stay in sync with the real portfolio backend the Portfolio screen uses.
    auto& svc = services::PortfolioService::instance();
    connect(&svc, &services::PortfolioService::portfolios_loaded, this,
            &PortfolioSummaryWidget::on_portfolios_loaded);
    connect(&svc, &services::PortfolioService::summary_loaded, this,
            &PortfolioSummaryWidget::on_summary_loaded);
    // A buy/sell in the shown portfolio (from anywhere) refreshes this widget.
    auto refresh_if_selected = [this](const QString& portfolio_id) {
        if (!selected_id_.isEmpty() && portfolio_id == selected_id_)
            services::PortfolioService::instance().refresh_summary(selected_id_);
    };
    connect(&svc, &services::PortfolioService::asset_added, this, refresh_if_selected);
    connect(&svc, &services::PortfolioService::asset_sold, this, refresh_if_selected);

    // Restore the last-shown portfolio; the real id is reconciled once the
    // portfolio list loads (the persisted one may have since been deleted).
    selected_id_ = QSettings().value(kSelectedPortfolioKey).toString();

    apply_styles();
    set_loading(true);

}

void PortfolioSummaryWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!hub_active_)
        load_holdings();
    aft_timer_->start();
    fetch_aft();
}

void PortfolioSummaryWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (hub_active_)
        hub_unsubscribe_all();
    aft_timer_->stop();
    // Supersede anything in flight: an off-screen tile shouldn't repaint, and
    // by the time it is shown again the numbers would be stale anyway.
    ++aft_gen_;
    aft_in_flight_ = false;
}

void PortfolioSummaryWidget::apply_styles() {
    summary_card_->setStyleSheet(QString("background: %1; border-radius: 2px;").arg(ui::colors::BG_RAISED()));
    for (auto* lbl : metric_labels_)
        lbl->setStyleSheet(
            QString("color: %1; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
    for (auto* val : metric_values_)
        val->setStyleSheet(
            QString("color: %1; font-weight: bold; background: transparent;")
                .arg(ui::colors::TEXT_PRIMARY()));
    header_row_->setStyleSheet(QString("background: %1;").arg(ui::colors::BG_RAISED()));
    for (auto* lbl : header_labels_)
        lbl->setStyleSheet(
            QString("color: %1; font-weight: bold; background: transparent;")
                .arg(ui::colors::TEXT_SECONDARY()));
    scroll_area_->setStyleSheet(
        QString("QScrollArea { border: none; background: transparent; }"
                "QScrollBar:vertical { width: 4px; background: transparent; }"
                "QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(ui::colors::BORDER_MED()));

    // Title-bar portfolio selector — explicit dark field + dark popup. The
    // app-global combo style (ThemeManager) doesn't reach a combo nested under
    // a widget that carries its own stylesheet, so without this the field and
    // dropdown render white-on-white.
    if (portfolio_combo_)
        portfolio_combo_->setStyleSheet(
            QString("QComboBox { background:%1; color:%2; border:1px solid %3; border-radius:2px;"
                    "  padding:0 4px; font-size:%4; }"
                    "QComboBox:hover { border-color:%5; }"
                    "QComboBox::drop-down { border:none; width:14px; }"
                    "QComboBox QAbstractItemView { background:%1; color:%2; border:1px solid %3;"
                    "  selection-background-color:%6; selection-color:%2; outline:none; }")
                .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED(),
                     ui::fonts::ui_px(), ui::colors::AMBER(), ui::colors::BG_HOVER()));
}

void PortfolioSummaryWidget::on_theme_changed() {
    apply_styles();
    if (!last_holdings_.isEmpty() && !last_quotes_.isEmpty())
        render(last_holdings_, last_quotes_);
}

void PortfolioSummaryWidget::load_holdings() {
    // First call kicks off the portfolio list; on_portfolios_loaded() takes
    // over from there (fill dropdown → load the selected portfolio's summary).
    // Later calls (re-show, manual refresh) just re-pull the selected summary.
    if (!portfolios_loaded_) {
        services::PortfolioService::instance().load_portfolios();
        return;
    }
    if (!selected_id_.isEmpty())
        services::PortfolioService::instance().load_summary(selected_id_);
}

void PortfolioSummaryWidget::on_portfolios_loaded(QVector<portfolio::Portfolio> portfolios) {
    portfolios_ = portfolios;
    portfolios_loaded_ = true;

    suppress_combo_signal_ = true;
    portfolio_combo_->clear();
    for (const auto& p : portfolios_)
        portfolio_combo_->addItem(p.name.isEmpty() ? p.id : p.name);
    suppress_combo_signal_ = false;

    if (portfolios_.isEmpty()) {
        // No portfolios configured yet — show an empty summary, not stale data.
        selected_id_.clear();
        portfolio_combo_->setVisible(false);
        hub_unsubscribe_all();
        render({}, {});
        set_loading(false);
        return;
    }
    portfolio_combo_->setVisible(true);

    // Reconcile the persisted selection against the current list — the saved
    // portfolio may have been deleted since last run (or this is first run).
    int idx = 0;
    for (int i = 0; i < portfolios_.size(); ++i) {
        if (portfolios_.at(i).id == selected_id_) {
            idx = i;
            break;
        }
    }

    suppress_combo_signal_ = true;
    portfolio_combo_->setCurrentIndex(idx);
    suppress_combo_signal_ = false;

    // portfolios_loaded is a global signal — it also fires when the Portfolio
    // screen loads. Only pull the summary now if we're actually on screen;
    // otherwise just remember the choice and let showEvent() load it on show,
    // so a hidden dashboard widget doesn't trigger a needless daemon fetch.
    const QString id = portfolios_.at(idx).id;
    if (isVisible()) {
        select_portfolio(id);
    } else {
        selected_id_ = id;
        QSettings().setValue(kSelectedPortfolioKey, id);
    }
}

void PortfolioSummaryWidget::select_portfolio(const QString& id) {
    selected_id_ = id;
    QSettings().setValue(kSelectedPortfolioKey, id);
    set_loading(true);
    services::PortfolioService::instance().load_summary(id);
}

void PortfolioSummaryWidget::on_summary_loaded(const portfolio::PortfolioSummary& summary) {
    // summary_loaded fires for every portfolio anyone loads (incl. the
    // Portfolio screen's own selection) — only react to the one we're showing.
    if (summary.portfolio.id != selected_id_)
        return;

    QVector<Holding> holdings;
    holdings.reserve(summary.holdings.size());
    for (const auto& h : summary.holdings) {
        if (h.symbol.isEmpty() || h.quantity <= 0)
            continue;
        holdings.append(Holding{h.symbol, h.quantity, h.avg_buy_price});
    }

    if (holdings.isEmpty()) {
        hub_unsubscribe_all();
        render({}, {});
        set_loading(false);
        return;
    }

    // Hand off to the live-quote path: subscribe to market:quote:<sym> for each
    // holding so prices keep ticking after this initial paint.
    fetch_prices(holdings);
}

void PortfolioSummaryWidget::fetch_prices(const QVector<Holding>& holdings) {
    last_holdings_ = holdings;
    hub_resubscribe(holdings);
    fetch_aft();
}

// ── After-hours column ───────────────────────────────────────────────────────

PortfolioSummaryWidget::ExtSession PortfolioSummaryWidget::current_session() {
    const QDateTime et = QDateTime::currentDateTime().toTimeZone(QTimeZone("America/New_York"));
    if (et.date().dayOfWeek() >= 6)             // Sat / Sun
        return ExtSession::Closed;
    const QTime t = et.time();
    if (t < QTime(4, 0))    return ExtSession::Closed;
    if (t < QTime(9, 30))   return ExtSession::Pre;
    if (t < QTime(16, 0))   return ExtSession::Regular;
    if (t < QTime(20, 0))   return ExtSession::Post;
    // Past 20:00 the session is over, but the evening's post-market move is
    // still the most recent thing that happened and hasn't been absorbed by a
    // regular session yet — so it stays on screen rather than blanking at 8pm.
    return ExtSession::Closed;
}

void PortfolioSummaryWidget::fetch_aft() {
    if (last_holdings_.isEmpty()) {
        aft_.clear();
        return;
    }
    // Nothing an extended print could tell the reader during the session: the
    // last one happened before the open and is already inside the price every
    // other column is quoting. Clear rather than leave this morning's
    // pre-market number sitting under the header all afternoon.
    if (current_session() == ExtSession::Regular) {
        if (!aft_.isEmpty()) {
            aft_.clear();
            rebuild_from_cache();
        }
        return;
    }
    if (aft_in_flight_)
        return;

    QJsonArray syms;
    for (const auto& h : last_holdings_)
        syms.append(h.symbol);

    QJsonObject payload;
    payload.insert(QStringLiteral("symbols"), syms);

    const quint64 gen = ++aft_gen_;
    aft_in_flight_ = true;
    QPointer<PortfolioSummaryWidget> guard(this);

    python::PythonWorker::instance().submit(
        QStringLiteral("extended_hours"), payload,
        [guard, gen](bool ok, QJsonObject result, QString err) {
            if (!guard || gen != guard->aft_gen_)
                return;  // superseded by a newer fetch, or the tile is gone
            guard->aft_in_flight_ = false;
            if (!ok) {
                // Keep whatever the column already shows. A failed background
                // refresh is a reason to leave the last good number up, not to
                // blank a column the user may be reading.
                LOG_WARN("PortfolioSummary",
                         QString("extended-hours fetch failed: %1").arg(err.left(120)));
                return;
            }
            const QJsonArray rows = result.contains(QStringLiteral("_value"))
                                        ? result.value(QStringLiteral("_value")).toArray()
                                        : result.value(QStringLiteral("data")).toArray();
            QHash<QString, AftQuote> fresh;
            for (const auto& v : rows) {
                const auto o = v.toObject();
                const QString sym = o.value(QStringLiteral("symbol")).toString();
                if (sym.isEmpty())
                    continue;
                // Read the field belonging to the session rather than the
                // daemon's `ext_change_pct`. That one falls back across
                // sessions for display (post when pre is missing, and the
                // reverse), so in the first minutes after the close — before
                // any post-market bar exists — it would report this morning's
                // pre-market move under a header that says AFT.
                const QString sess = o.value(QStringLiteral("session")).toString();
                const bool pre = sess.isEmpty() ? (current_session() == ExtSession::Pre)
                                                : sess == QLatin1String("PRE");
                const auto ext = json_num(o, pre ? "pre_market" : "post_market");
                const auto regular = json_num(o, "regular");
                if (!ext || !regular || *regular <= 0)
                    continue;
                fresh.insert(sym, AftQuote{(*ext - *regular) / *regular * 100.0, *ext,
                                           *regular, sess});
            }
            guard->aft_ = fresh;
            guard->rebuild_from_cache();
        });
}


void PortfolioSummaryWidget::hub_resubscribe(const QVector<Holding>& holdings) {
    auto& hub = datahub::DataHub::instance();
    // Holdings set may have changed since last subscribe — wipe all and
    // re-register so we don't leave stale topic subs behind.
    hub.unsubscribe(this);
    row_cache_.clear();
    for (const auto& h : holdings) {
        const QString sym = h.symbol;
        hub.subscribe(this, QStringLiteral("market:quote:") + sym, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            row_cache_.insert(sym, v.value<services::QuoteData>());
            set_loading(false);
            rebuild_from_cache();
        });
    }
    hub_active_ = true;
}

void PortfolioSummaryWidget::hub_unsubscribe_all() {
    datahub::DataHub::instance().unsubscribe(this);
    hub_active_ = false;
}

void PortfolioSummaryWidget::rebuild_from_cache() {
    QVector<services::QuoteData> quotes;
    quotes.reserve(row_cache_.size());
    for (const auto& h : last_holdings_) {
        auto it = row_cache_.constFind(h.symbol);
        if (it != row_cache_.constEnd())
            quotes.append(it.value());
    }
    if (!quotes.isEmpty())
        render(last_holdings_, quotes);
}


void PortfolioSummaryWidget::render(const QVector<Holding>& holdings, const QVector<services::QuoteData>& quotes) {
    last_holdings_ = holdings;
    last_quotes_ = quotes;

    QMap<QString, const services::QuoteData*> qmap;
    for (const auto& q : last_quotes_)
        qmap[q.symbol] = &q;

    double total_value = 0;
    double total_cost = 0;
    double day_pnl = 0;

    // Clear list
    while (list_layout_->count() > 0) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    bool alt = false;
    for (const auto& h : holdings) {
        const services::QuoteData* q = qmap.value(h.symbol, nullptr);
        double price = q ? q->price : 0;
        double value = price * h.shares;
        double cost = h.avg_cost * h.shares;
        double pnl = value - cost;
        double day_chg = q ? (q->change * h.shares) : 0;

        total_value += value;
        total_cost += cost;
        day_pnl += day_chg;

        // Row
        auto* row = new QWidget(this);
        row->setStyleSheet(QString("background: %1;").arg(alt ? ui::colors::BG_RAISED() : "transparent"));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 4, 8, 4);

        auto cell = [&](const QString& text, int stretch, Qt::Alignment align, const QString& color) {
            auto* lbl = new QLabel(text);
            lbl->setAlignment(align);
            lbl->setStyleSheet(QString("color: %1; background: transparent;").arg(color));
            rl->addWidget(lbl, stretch);
            return lbl;
        };

        cell(h.symbol, 1, Qt::AlignLeft, ui::colors::TEXT_PRIMARY);
        cell(QString::number(h.shares, 'f', h.shares == (int)h.shares ? 0 : 2), 1, Qt::AlignRight,
             ui::colors::TEXT_SECONDARY);
        cell(price > 0 ? QString("$%1").arg(price, 0, 'f', 2) : "--", 1, Qt::AlignRight, ui::colors::TEXT_PRIMARY);
        cell(value > 0 ? QString("$%1").arg(value, 0, 'f', 0) : "--", 1, Qt::AlignRight, ui::colors::TEXT_PRIMARY);

        // P&L as return on cost. The cash number for the whole book is in the
        // summary card above; per row the percentage is what compares across
        // positions of different sizes. Needs a cost basis and a live price —
        // a position with either missing would otherwise read as −100%.
        QString pnl_str = QStringLiteral("--");
        QString pnl_color = QString(ui::colors::TEXT_SECONDARY);
        QString pnl_tip = QStringLiteral("No average cost recorded for this position.");
        if (cost > 0 && price > 0) {
            const double pnl_pct = pnl / cost * 100.0;
            pnl_str = QString("%1%2%").arg(pnl_pct >= 0 ? "+" : "").arg(pnl_pct, 0, 'f', 2);
            pnl_color = pnl_pct >= 0 ? QString(ui::colors::POSITIVE) : QString(ui::colors::NEGATIVE);
            pnl_tip = QString("%1 on a $%2 cost basis (avg $%3 per share)")
                          .arg(pnl >= 0 ? QString("+$%1").arg(pnl, 0, 'f', 2)
                                        : QString("-$%1").arg(-pnl, 0, 'f', 2))
                          .arg(cost, 0, 'f', 2)
                          .arg(h.avg_cost, 0, 'f', 2);
        }
        cell(pnl_str, 1, Qt::AlignRight, pnl_color)->setToolTip(pnl_tip);

        // Per-row DAY CHG% — quote already gives us the daily percent change.
        QString chg_pct_str;
        QString chg_pct_color;
        if (q) {
            chg_pct_str = QString("%1%2%").arg(q->change_pct >= 0 ? "+" : "")
                                          .arg(q->change_pct, 0, 'f', 2);
            chg_pct_color = q->change_pct >= 0 ? QString(ui::colors::POSITIVE) : QString(ui::colors::NEGATIVE);
        } else {
            chg_pct_str = "--";
            chg_pct_color = QString(ui::colors::TEXT_SECONDARY);
        }
        cell(chg_pct_str, 1, Qt::AlignRight, chg_pct_color);

        // AFT% — the extended-hours move, from a different feed than the rest
        // of the row and blank whenever there is no extended print to show.
        QString aft_str = QStringLiteral("--");
        QString aft_color = QString(ui::colors::TEXT_SECONDARY);
        QString aft_tip = current_session() == ExtSession::Regular
                              ? QStringLiteral("Regular session — the last extended-hours move is "
                                               "already reflected in the price.")
                              : QStringLiteral("No extended-hours trading in this name yet.");
        if (const auto it = aft_.constFind(h.symbol); it != aft_.constEnd()) {
            aft_str = QString("%1%2%").arg(it->pct >= 0 ? "+" : "").arg(it->pct, 0, 'f', 2);
            aft_color = it->pct >= 0 ? QString(ui::colors::POSITIVE) : QString(ui::colors::NEGATIVE);
            aft_tip = QString("%1-market $%2 against the $%3 close%4")
                          .arg(it->session == QLatin1String("PRE") ? QStringLiteral("Pre")
                                                                   : QStringLiteral("Post"))
                          .arg(it->price, 0, 'f', 2)
                          .arg(it->regular, 0, 'f', 2)
                          .arg(h.shares > 0
                                   ? QString("\n%1 on this position")
                                         .arg((it->price - it->regular) * h.shares >= 0
                                                  ? QString("+$%1").arg((it->price - it->regular) * h.shares,
                                                                        0, 'f', 2)
                                                  : QString("-$%1").arg(-(it->price - it->regular) * h.shares,
                                                                        0, 'f', 2))
                                   : QString());
        }
        cell(aft_str, 1, Qt::AlignRight, aft_color)->setToolTip(aft_tip);

        list_layout_->addWidget(row);
        alt = !alt;
    }
    list_layout_->addStretch();

    // Update summary labels
    total_value_lbl_->setText(QString("$%1").arg(total_value, 0, 'f', 0));
    num_holdings_lbl_->setText(QString::number(holdings.size()));

    const double total_pnl = total_value - total_cost;
    const double day_basis = total_value - day_pnl;
    const bool   have_day  = day_basis > 0;
    const bool   have_tot  = total_cost > 0;
    const double day_pct   = have_day ? (day_pnl  / day_basis) * 100.0 : 0.0;
    const double total_pct = have_tot ? (total_pnl / total_cost) * 100.0 : 0.0;

    auto signed_dollars = [](double v) {
        return v >= 0 ? QString("+$%1").arg(v, 0, 'f', 0)
                      : QString("-$%1").arg(-v, 0, 'f', 0);
    };
    auto signed_pct = [](double v) {
        return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(v, 0, 'f', 2);
    };
    auto pl_style = [](double v) {
        return QString("color: %1; font-weight: bold; background: transparent;")
            .arg(v >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE());
    };
    const QString muted_style =
        QString("color: %1; font-weight: bold; background: transparent;")
            .arg(ui::colors::TEXT_SECONDARY());

    day_pnl_lbl_->setText(signed_dollars(day_pnl));
    day_pnl_lbl_->setStyleSheet(pl_style(day_pnl));
    if (have_day) {
        day_chg_pct_lbl_->setText(signed_pct(day_pct));
        day_chg_pct_lbl_->setStyleSheet(pl_style(day_pct));
    } else {
        day_chg_pct_lbl_->setText("--");
        day_chg_pct_lbl_->setStyleSheet(muted_style);
    }

    total_pnl_lbl_->setText(signed_dollars(total_pnl));
    total_pnl_lbl_->setStyleSheet(pl_style(total_pnl));
    if (have_tot) {
        total_chg_pct_lbl_->setText(signed_pct(total_pct));
        total_chg_pct_lbl_->setStyleSheet(pl_style(total_pct));
    } else {
        total_chg_pct_lbl_->setText("--");
        total_chg_pct_lbl_->setStyleSheet(muted_style);
    }
}

} // namespace fincept::screens::widgets

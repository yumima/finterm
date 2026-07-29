// src/screens/dashboard/widgets/EarningsCalendarWidget.cpp
#include "screens/dashboard/widgets/EarningsCalendarWidget.h"

#include "screens/dashboard/widgets/EarningsMath.h"

#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "python/PythonWorker.h"
#include "services/portfolio/PortfolioService.h"
#include "ui/theme/Theme.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::screens::widgets {

using earnings::fmt_eps;
using earnings::Growth;
using earnings::growth_verdict;
using earnings::parse_money;

// Default portfolio for a fresh tile — the same key PortfolioSummaryWidget
// persists, so a new tile opens on the book the user already picked.
static constexpr auto kSelectedPortfolioKey = "dashboard/portfolio_id";


EarningsCalendarWidget::EarningsCalendarWidget(const QJsonObject& cfg, QWidget* parent)
    : BaseWidget("EARNINGS CALENDAR", parent, ui::colors::AMBER()) {
    build_body();
    build_portfolio_selector();

    // Toggling several checkboxes shouldn't fire a fan-out per click.
    selection_debounce_ = new QTimer(this);
    selection_debounce_->setSingleShot(true);
    selection_debounce_->setInterval(400);
    connect(selection_debounce_, &QTimer::timeout, this, [this]() {
        emit config_changed(config());
        refresh_data();
    });

    const QString default_id = QSettings().value(kSelectedPortfolioKey).toString();
    if (!default_id.isEmpty())
        selected_ids_ = {default_id};

    auto& svc = services::PortfolioService::instance();
    connect(&svc, &services::PortfolioService::portfolios_loaded, this,
            &EarningsCalendarWidget::on_portfolios_loaded);
    connect(&svc, &services::PortfolioService::summary_loaded, this,
            &EarningsCalendarWidget::on_summary_loaded);
    // Without this a failed summary load leaves the tile on "Loading…" forever
    // — the work that clears it only starts from summary_loaded.
    connect(&svc, &services::PortfolioService::summary_error, this,
            [this](QString portfolio_id, QString error) {
                if (!awaiting_summaries_.remove(portfolio_id))
                    return;
                LOG_WARN("EarningsCal", QString("summary failed for %1: %2").arg(portfolio_id, error.left(120)));
                if (awaiting_summaries_.isEmpty())
                    on_holdings_ready();
            });

    connect(this, &BaseWidget::refresh_requested, this, [this] { refresh_data(); });

    apply_config(cfg);
    apply_styles();
}

void EarningsCalendarWidget::build_body() {
    auto* vl = content_layout();
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // ── View toggle ──────────────────────────────────────────────────────────
    auto* tab_bar = new QWidget(this);
    tab_bar->setFixedHeight(24);
    auto* tl = new QHBoxLayout(tab_bar);
    tl->setContentsMargins(4, 2, 4, 2);
    tl->setSpacing(0);

    week_btn_ = new QPushButton("THIS WEEK");
    portfolio_btn_ = new QPushButton("PORTFOLIO");
    week_btn_->setCursor(Qt::PointingHandCursor);
    portfolio_btn_->setCursor(Qt::PointingHandCursor);
    connect(week_btn_, &QPushButton::clicked, this, [this]() { set_mode(Mode::ThisWeek); });
    connect(portfolio_btn_, &QPushButton::clicked, this, [this]() { set_mode(Mode::Portfolio); });
    tl->addWidget(week_btn_, 1);
    tl->addWidget(portfolio_btn_, 1);
    vl->addWidget(tab_bar);

    // ── Column headers ───────────────────────────────────────────────────────
    header_widget_ = new QWidget(this);
    auto* hl = new QHBoxLayout(header_widget_);
    hl->setContentsMargins(8, 4, 8, 4);

    auto make_hdr = [&](const QString& text, int stretch, Qt::Alignment align = Qt::AlignLeft) {
        auto* lbl = new QLabel(text);
        lbl->setAlignment(align | Qt::AlignVCenter);
        header_labels_.append(lbl);
        hl->addWidget(lbl, stretch);
        return lbl;
    };
    make_hdr("DATE", 3);
    make_hdr("SYMBOL", 2);
    make_hdr("WHEN", 2);
    // Centred, and the values below are centred to match — right-aligning the
    // header while the number sat somewhere else in the cell is what made the
    // column look out of line.
    auto* eps_hdr = make_hdr("EPS EST", 3, Qt::AlignHCenter);
    eps_hdr->setToolTip(
        QStringLiteral("Consensus EPS estimate for the quarter being reported.\n"
                       "▲ green / ▼ red compares it with the same quarter a year ago —\n"
                       "i.e. whether analysts expect growth, not a beat or a miss."));
    surprise_header_ = make_hdr("LAST Q vs EST", 3, Qt::AlignHCenter);
    surprise_header_->setToolTip(
        QStringLiteral("Last quarter that was actually reported: how far the reported EPS\n"
                       "landed above (+) or below (−) the consensus estimate at the time.\n"
                       "History — it says nothing about the coming print."));
    weight_header_ = make_hdr("WT%", 2, Qt::AlignRight);
    vl->addWidget(header_widget_);

    header_sep_ = new QFrame;
    header_sep_->setFixedHeight(1);
    vl->addWidget(header_sep_);

    // Status lives outside the scroll area so populate()'s clear loop can never
    // delete it (same dangling-pointer trap IpoCalendarWidget documents).
    status_label_ = new QLabel("Loading…");
    status_label_->setAlignment(Qt::AlignCenter);
    status_label_->setWordWrap(true);
    vl->addWidget(status_label_);

    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);
    scroll_area_->hide();

    auto* list_widget = new QWidget(this);
    list_widget->setStyleSheet("background: transparent;");
    list_layout_ = new QVBoxLayout(list_widget);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);
    list_layout_->addStretch();

    scroll_area_->setWidget(list_widget);
    vl->addWidget(scroll_area_, 1);
}

// ── Portfolio selector (multi-select combo) ──────────────────────────────────

void EarningsCalendarWidget::build_portfolio_selector() {
    portfolio_combo_ = new QComboBox(this);
    portfolio_combo_->setFixedHeight(18);
    portfolio_combo_->setMinimumWidth(110);
    portfolio_combo_->setMaximumWidth(170);
    portfolio_combo_->setCursor(Qt::PointingHandCursor);
    // Editable + read-only line edit is the standard multi-select-combo recipe:
    // it lets the closed combo show a summary ("3 portfolios") instead of being
    // stuck displaying whichever row happens to be current.
    portfolio_combo_->setEditable(true);
    portfolio_combo_->lineEdit()->setReadOnly(true);
    portfolio_combo_->lineEdit()->setCursor(Qt::PointingHandCursor);
    portfolio_combo_->lineEdit()->installEventFilter(this); // click opens the popup

    portfolio_model_ = new QStandardItemModel(this);
    portfolio_combo_->setModel(portfolio_model_);
    // A styled delegate is required for the check indicators to render at all
    // once the view carries a stylesheet.
    portfolio_combo_->setItemDelegate(new QStyledItemDelegate(portfolio_combo_));
    portfolio_combo_->view()->viewport()->installEventFilter(this);

    connect(portfolio_model_, &QStandardItemModel::itemChanged, this,
            &EarningsCalendarWidget::on_portfolio_item_changed);
    // Clicking a row moves the current index, and an editable combo mirrors
    // that row's text into the line edit — which would replace our summary
    // ("3 portfolios") with whatever was clicked last. Put it back.
    connect(portfolio_combo_, &QComboBox::currentIndexChanged, this,
            [this](int) { update_selector_text(); });

    add_title_bar_control(portfolio_combo_);
    portfolio_combo_->setVisible(false);
}

bool EarningsCalendarWidget::eventFilter(QObject* obj, QEvent* event) {
    if (portfolio_combo_) {
        // Clicking the read-only line edit should open the list — otherwise the
        // only hit target is the tiny arrow.
        if (obj == portfolio_combo_->lineEdit() && event->type() == QEvent::MouseButtonRelease) {
            portfolio_combo_->showPopup();
            return true;
        }
        // Toggle the row under the cursor and keep the popup open, so several
        // portfolios can be ticked in one visit.
        if (obj == portfolio_combo_->view()->viewport() && event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            const QModelIndex idx = portfolio_combo_->view()->indexAt(me->pos());
            if (idx.isValid()) {
                if (auto* item = portfolio_model_->itemFromIndex(idx))
                    item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            }
            return true;
        }
    }
    return BaseWidget::eventFilter(obj, event);
}

void EarningsCalendarWidget::rebuild_portfolio_model() {
    suppress_item_signal_ = true;
    portfolio_model_->clear();

    auto add_item = [&](const QString& text, bool checked) {
        auto* item = new QStandardItem(text);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        portfolio_model_->appendRow(item);
        return item;
    };

    add_item(QStringLiteral("All portfolios"), all_portfolios_);
    for (const auto& p : portfolios_)
        add_item(p.name.isEmpty() ? p.id : p.name, all_portfolios_ || selected_ids_.contains(p.id));

    suppress_item_signal_ = false;
    update_selector_text();
}

void EarningsCalendarWidget::on_portfolio_item_changed(QStandardItem* item) {
    if (suppress_item_signal_ || !item)
        return;

    suppress_item_signal_ = true;
    const int row = item->row();
    if (row == 0) {
        // "All portfolios" drives every other row. Unticking it clears the set
        // rather than leaving a phantom all-checked state.
        const bool on = item->checkState() == Qt::Checked;
        all_portfolios_ = on;
        for (int r = 1; r < portfolio_model_->rowCount(); ++r)
            portfolio_model_->item(r)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    } else {
        int checked = 0;
        for (int r = 1; r < portfolio_model_->rowCount(); ++r)
            if (portfolio_model_->item(r)->checkState() == Qt::Checked)
                ++checked;
        // Everything ticked individually IS "all" — keep the header row honest
        // so newly created portfolios get picked up automatically.
        all_portfolios_ = (checked == portfolios_.size() && checked > 0);
        portfolio_model_->item(0)->setCheckState(all_portfolios_ ? Qt::Checked : Qt::Unchecked);
    }

    selected_ids_.clear();
    for (int r = 1; r < portfolio_model_->rowCount(); ++r) {
        if (portfolio_model_->item(r)->checkState() == Qt::Checked && (r - 1) < portfolios_.size())
            selected_ids_.append(portfolios_.at(r - 1).id);
    }
    suppress_item_signal_ = false;

    update_selector_text();
    if (!selected_ids_.isEmpty())
        QSettings().setValue(kSelectedPortfolioKey, selected_ids_.first());
    selection_debounce_->start();
}

void EarningsCalendarWidget::update_selector_text() {
    if (!portfolio_combo_ || !portfolio_combo_->lineEdit())
        return;
    QString text;
    if (all_portfolios_)
        text = QStringLiteral("All portfolios");
    else if (selected_ids_.isEmpty())
        text = QStringLiteral("No portfolio");
    else if (selected_ids_.size() == 1) {
        text = selected_ids_.first();
        for (const auto& p : portfolios_)
            if (p.id == selected_ids_.first())
                text = p.name.isEmpty() ? p.id : p.name;
    } else {
        text = QStringLiteral("%1 portfolios").arg(selected_ids_.size());
    }
    portfolio_combo_->lineEdit()->setText(text);
    portfolio_combo_->setToolTip(text);
}

QStringList EarningsCalendarWidget::effective_portfolio_ids() const {
    if (all_portfolios_) {
        QStringList ids;
        ids.reserve(portfolios_.size());
        for (const auto& p : portfolios_)
            ids.append(p.id);
        return ids;
    }
    return selected_ids_;
}

// ── Config ───────────────────────────────────────────────────────────────────

QJsonObject EarningsCalendarWidget::config() const {
    QJsonArray ids;
    for (const auto& id : selected_ids_)
        ids.append(id);
    return QJsonObject{{"mode", mode_ == Mode::Portfolio ? "portfolio" : "week"},
                       {"all_portfolios", all_portfolios_},
                       {"portfolio_ids", ids}};
}

void EarningsCalendarWidget::apply_config(const QJsonObject& cfg) {
    mode_ = cfg.value("mode").toString() == "portfolio" ? Mode::Portfolio : Mode::ThisWeek;
    all_portfolios_ = cfg.value("all_portfolios").toBool(false);

    if (cfg.contains("portfolio_ids")) {
        selected_ids_.clear();
        for (const auto& v : cfg.value("portfolio_ids").toArray())
            if (!v.toString().isEmpty())
                selected_ids_.append(v.toString());
    } else if (!cfg.value("portfolio_id").toString().isEmpty()) {
        selected_ids_ = {cfg.value("portfolio_id").toString()}; // pre-multi-select tiles
    }

    portfolio_combo_->setVisible(true);
    rebuild_portfolio_model();
    apply_styles();
    // Don't fetch here — a tile is built while hidden during layout restore.
    // showEvent() does the first load.
    loaded_once_ = false;
}

void EarningsCalendarWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!loaded_once_)
        refresh_data();
}

void EarningsCalendarWidget::set_mode(Mode m) {
    if (m == mode_ && loaded_once_)
        return;
    mode_ = m;
    apply_styles();
    emit config_changed(config());
    refresh_data();
}

void EarningsCalendarWidget::refresh_data() {
    loaded_once_ = true;
    ++epoch_;
    pending_days_ = 0;
    pending_symbols_ = 0;
    ok_days_ = 0;
    ok_symbols_ = 0;
    week_rows_ready_ = false;
    entries_.clear();
    note_.clear();
    holdings_by_portfolio_.clear();
    awaiting_summaries_.clear();
    set_loading(true);
    show_status(QStringLiteral("Loading…"));

    // The week view needs holdings too — that's what the held marker is.
    load_holdings();
    if (mode_ == Mode::ThisWeek)
        load_week();
}

// ── This week — Nasdaq public earnings calendar ──────────────────────────────

void EarningsCalendarWidget::load_week() {
    // "This week" = today through Friday. On a weekend there is nothing left in
    // the current week, so roll forward to the next one — an empty list would
    // read as a data failure.
    QDate start = QDate::currentDate();
    if (start.dayOfWeek() > 5)
        start = start.addDays(8 - start.dayOfWeek()); // Sat→Mon(+2), Sun→Mon(+1)
    const QDate end = start.addDays(5 - start.dayOfWeek());

    const qint64 epoch = epoch_;
    for (QDate d = start; d <= end; d = d.addDays(1)) {
        ++pending_days_;
        fetch_day(d, epoch);
    }
}

void EarningsCalendarWidget::fetch_day(const QDate& date, qint64 epoch) {
    const QString url =
        QStringLiteral("https://api.nasdaq.com/api/calendar/earnings?date=") + date.toString(Qt::ISODate);

    QPointer<EarningsCalendarWidget> self = this;
    HttpClient::instance().get(url, [self, date, epoch](Result<QJsonDocument> res) {
        if (!self || epoch != self->epoch_)
            return;

        if (res.is_ok() && res.value().isObject()) {
            ++self->ok_days_;
            // A market holiday returns a valid response with a null/absent data
            // object — that's "nothing scheduled", not a failure.
            const auto rows = res.value().object().value("data").toObject().value("rows").toArray();
            for (const auto& v : rows) {
                const auto o = v.toObject();
                Entry e;
                e.symbol = o["symbol"].toString().trimmed();
                if (e.symbol.isEmpty())
                    continue;
                e.date = date;
                e.company = o["name"].toString();
                const QString t = o["time"].toString();
                e.when = t == "time-pre-market"    ? QStringLiteral("BMO")
                         : t == "time-after-hours" ? QStringLiteral("AMC")
                                                   : QStringLiteral("—");
                e.has_est = parse_money(o["epsForecast"].toString(), &e.eps_est);
                e.has_ly = parse_money(o["lastYearEPS"].toString(), &e.eps_ly);
                e.fiscal_quarter = o["fiscalQuarterEnding"].toString();
                e.num_ests = o["noOfEsts"].toString().toInt();
                e.ly_report_date = o["lastYearRptDt"].toString();
                double mc = 0;
                parse_money(o["marketCap"].toString(), &mc);
                e.rank = mc;
                self->entries_.append(e);
            }
        }

        if (--self->pending_days_ > 0)
            return;
        self->week_fetches_done();
    });
}

void EarningsCalendarWidget::week_fetches_done() {
    if (ok_days_ == 0) {
        set_loading(false);
        show_status(QStringLiteral("Couldn't reach the Nasdaq earnings calendar."));
        return;
    }

    // Date first, then the biggest names within each day.
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.date != b.date)
            return a.date < b.date;
        if (a.rank != b.rank)
            return a.rank > b.rank;
        return a.symbol < b.symbol;
    });
    if (entries_.size() > kMaxWeekRows) {
        note_ = QStringLiteral("Showing the %1 largest of %2 companies reporting this week.")
                    .arg(kMaxWeekRows)
                    .arg(entries_.size());
        entries_.resize(kMaxWeekRows);
    }

    week_rows_ready_ = true;
    set_loading(false);
    rebuild_held_index();
    populate();

    // Surprise history costs one yfinance call per symbol, so only pay it for
    // names the user actually holds — the rows they'll read closely.
    if (awaiting_summaries_.isEmpty()) {
        QStringList held_here;
        for (const auto& e : entries_)
            if (e.held && !held_here.contains(e.symbol))
                held_here.append(e.symbol);
        if (!held_here.isEmpty())
            fetch_symbol_earnings(held_here, epoch_, /*portfolio_view=*/false);
    }
}

// ── Holdings ─────────────────────────────────────────────────────────────────

void EarningsCalendarWidget::load_holdings() {
    if (!portfolios_loaded_) {
        services::PortfolioService::instance().load_portfolios();
        return; // on_portfolios_loaded() continues
    }

    const QStringList ids = effective_portfolio_ids();
    if (ids.isEmpty()) {
        held_value_.clear();
        held_portfolios_.clear();
        held_total_mv_ = 0;
        on_holdings_ready();
        return;
    }
    awaiting_summaries_ = QSet<QString>(ids.cbegin(), ids.cend());
    for (const auto& id : ids)
        services::PortfolioService::instance().load_summary(id);
}

void EarningsCalendarWidget::on_portfolios_loaded(QVector<portfolio::Portfolio> portfolios) {
    portfolios_ = portfolios;
    portfolios_loaded_ = true;

    // Drop selections whose portfolio has since been deleted; fall back to the
    // first one so the tile still shows something.
    QStringList live;
    for (const auto& id : selected_ids_)
        for (const auto& p : portfolios_)
            if (p.id == id) {
                live.append(id);
                break;
            }
    selected_ids_ = live;
    if (selected_ids_.isEmpty() && !all_portfolios_ && !portfolios_.isEmpty())
        selected_ids_ = {portfolios_.first().id};

    rebuild_portfolio_model();

    // portfolios_loaded is a global signal — it also fires when the Portfolio
    // screen loads. Only pull summaries if we're on screen and actually waiting
    // on them.
    if (isVisible() && loaded_once_ && holdings_by_portfolio_.isEmpty())
        load_holdings();
}

void EarningsCalendarWidget::on_summary_loaded(const portfolio::PortfolioSummary& summary) {
    const QString id = summary.portfolio.id;
    if (!effective_portfolio_ids().contains(id))
        return;

    // PortfolioService emits twice (disk cache, then live). Both land here;
    // storing per portfolio means the second emit refreshes values instead of
    // double-counting them.
    QVector<HoldingRow> rows;
    rows.reserve(summary.holdings.size());
    for (const auto& h : summary.holdings) {
        if (h.symbol.isEmpty() || h.quantity <= 0)
            continue;
        rows.append({h.symbol, h.market_value});
    }
    holdings_by_portfolio_.insert(id, rows);
    rebuild_held_index();

    if (awaiting_summaries_.remove(id) && awaiting_summaries_.isEmpty()) {
        on_holdings_ready();
        return;
    }
    // A late re-emit: refresh weights/markers in place, no new fan-out.
    if (awaiting_summaries_.isEmpty() && (week_rows_ready_ || !entries_.isEmpty())) {
        for (auto& e : entries_) {
            e.held = held_value_.contains(e.symbol);
            e.weight = (held_total_mv_ > 0) ? held_value_.value(e.symbol) / held_total_mv_ * 100.0 : 0;
            e.portfolios = held_portfolios_.value(e.symbol);
        }
        populate();
    }
}

void EarningsCalendarWidget::rebuild_held_index() {
    held_value_.clear();
    held_portfolios_.clear();
    held_total_mv_ = 0;

    for (auto it = holdings_by_portfolio_.cbegin(); it != holdings_by_portfolio_.cend(); ++it) {
        QString name = it.key();
        for (const auto& p : portfolios_)
            if (p.id == it.key()) {
                name = p.name.isEmpty() ? p.id : p.name;
                break;
            }
        for (const auto& row : it.value()) {
            held_value_[row.symbol] += row.market_value;
            held_total_mv_ += row.market_value;
            auto& names = held_portfolios_[row.symbol];
            if (!names.contains(name))
                names.append(name);
        }
    }

    for (auto& e : entries_) {
        e.held = held_value_.contains(e.symbol);
        e.weight = (held_total_mv_ > 0) ? held_value_.value(e.symbol) / held_total_mv_ * 100.0 : 0;
        e.portfolios = held_portfolios_.value(e.symbol);
    }
}

void EarningsCalendarWidget::on_holdings_ready() {
    rebuild_held_index();

    if (mode_ == Mode::ThisWeek) {
        if (week_rows_ready_) {
            populate();
            QStringList held_here;
            for (const auto& e : entries_)
                if (e.held && !held_here.contains(e.symbol))
                    held_here.append(e.symbol);
            if (!held_here.isEmpty())
                fetch_symbol_earnings(held_here, epoch_, /*portfolio_view=*/false);
        }
        return;
    }

    // Portfolio view — the holdings ARE the row set.
    QStringList symbols;
    for (auto it = held_value_.cbegin(); it != held_value_.cend(); ++it)
        symbols.append(it.key());
    std::sort(symbols.begin(), symbols.end());

    if (symbols.isEmpty()) {
        set_loading(false);
        show_status(effective_portfolio_ids().isEmpty()
                        ? QStringLiteral("No portfolio selected.")
                        : QStringLiteral("The selected portfolios have no holdings."));
        return;
    }
    if (symbols.size() > kMaxSymbolFetches) {
        note_ = QStringLiteral("Checked the first %1 of %2 holdings.")
                    .arg(kMaxSymbolFetches)
                    .arg(symbols.size());
        LOG_INFO("EarningsCal", QString("capping earnings lookup at %1 of %2 holdings")
                                    .arg(kMaxSymbolFetches)
                                    .arg(symbols.size()));
        symbols = symbols.mid(0, kMaxSymbolFetches);
    }
    fetch_symbol_earnings(symbols, epoch_, /*portfolio_view=*/true);
}

// ── Per-symbol earnings dates (yfinance) ─────────────────────────────────────

void EarningsCalendarWidget::fetch_symbol_earnings(const QStringList& symbols, qint64 epoch, bool portfolio_view) {
    pending_symbols_ = symbols.size();
    ok_symbols_ = 0;

    QPointer<EarningsCalendarWidget> self = this;
    for (const QString& sym : symbols) {
        QJsonObject payload;
        payload["symbol"] = sym;
        // ~13 months back covers the year-ago comparison quarter and the last
        // reported surprise; 180 days forward covers the next scheduled print.
        payload["lookback_days"] = 400;
        payload["lookahead_days"] = 180;

        python::PythonWorker::instance().submit(
            "earnings_dates", payload,
            [self, sym, epoch, portfolio_view](bool ok, QJsonObject result, QString err) {
                if (!self || epoch != self->epoch_)
                    return;

                if (ok && !result.contains("error")) {
                    ++self->ok_symbols_;
                    self->apply_symbol_result(sym, result, portfolio_view);
                } else if (!ok) {
                    LOG_WARN("EarningsCal",
                             QString("earnings_dates failed for %1: %2").arg(sym, err.left(120)));
                }

                if (--self->pending_symbols_ > 0)
                    return;

                self->set_loading(false);
                if (portfolio_view && self->ok_symbols_ == 0) {
                    self->show_status(QStringLiteral("Couldn't load earnings dates for these holdings."));
                    return;
                }
                if (portfolio_view) {
                    std::sort(self->entries_.begin(), self->entries_.end(),
                              [](const Entry& a, const Entry& b) {
                                  return a.date != b.date ? a.date < b.date : a.symbol < b.symbol;
                              });
                }
                self->populate();
            },
            python::PythonWorker::kNetworkActionTimeoutMs);
    }
}

void EarningsCalendarWidget::apply_symbol_result(const QString& symbol, const QJsonObject& result,
                                                 bool portfolio_view) {
    const QDate today = QDate::currentDate();
    const auto dates = result.value("dates").toArray();

    struct Point {
        QDate date;
        double estimate = 0;
        bool has_estimate = false;
        double actual = 0;
        bool has_actual = false;
        double surprise = 0;
        bool has_surprise = false;
    };
    QVector<Point> past, future;

    for (const auto& v : dates) {
        const auto o = v.toObject();
        const qint64 ts = static_cast<qint64>(o["timestamp"].toDouble());
        const QDate d = QDateTime::fromSecsSinceEpoch(ts, QTimeZone::UTC).date();
        if (!d.isValid())
            continue;
        Point p;
        p.date = d;
        p.has_estimate = o.value("eps_estimate").isDouble();
        p.estimate = o.value("eps_estimate").toDouble();
        p.has_actual = o.value("eps_actual").isDouble();
        p.actual = o.value("eps_actual").toDouble();
        p.has_surprise = o.value("surprise_pct").isDouble();
        p.surprise = o.value("surprise_pct").toDouble();
        (d < today ? past : future).append(p);
    }

    // Most recent reported quarter — the surprise badge.
    double surprise = 0;
    bool has_surprise = false;
    for (auto it = past.crbegin(); it != past.crend(); ++it) {
        if (it->has_surprise) {
            surprise = it->surprise;
            has_surprise = true;
            break;
        }
    }

    // Year-ago comparison quarter: the reported quarter closest to one year
    // before @p print_date. A ±45-day window keeps a shifted fiscal calendar
    // from matching the wrong quarter.
    auto year_ago_actual = [&past](const QDate& print_date, double* out) {
        const QDate target = print_date.addDays(-365);
        qint64 best_gap = 46;
        bool found = false;
        for (const auto& p : past) {
            if (!p.has_actual)
                continue;
            const qint64 gap = std::abs(p.date.daysTo(target));
            if (gap < best_gap) {
                best_gap = gap;
                *out = p.actual;
                found = true;
            }
        }
        return found;
    };

    if (!portfolio_view) {
        // Week view: the row already exists from the Nasdaq feed. Fill in the
        // badge, and — where yfinance also has this print — switch the
        // estimate AND the year-ago figure over to it as a pair.
        //
        // Not cosmetic: the portfolio view is yfinance-only, so leaving the
        // Nasdaq estimate here let one symbol show ▼ red in one tab and ▲ green
        // in the other. Whichever panel is "better" matters far less than a row
        // comparing two numbers from the same panel.
        for (auto& e : entries_) {
            if (e.symbol != symbol)
                continue;
            if (has_surprise) {
                e.surprise_pct = surprise;
                e.has_surprise = true;
            }
            const Point* next = nullptr;
            for (const auto& f : future) {
                if (f.date == e.date || std::abs(f.date.daysTo(e.date)) <= 3) {
                    next = &f; // same print, allowing for a date-only skew
                    break;
                }
            }
            if (!next || !next->has_estimate)
                continue; // keep the Nasdaq pair intact rather than mixing
            double ly = 0;
            if (!year_ago_actual(next->date, &ly))
                continue;
            e.eps_est = next->estimate;
            e.has_est = true;
            e.eps_ly = ly;
            e.has_ly = true;
            e.est_from_yf = true;
        }
        return;
    }

    if (future.isEmpty())
        return; // nothing scheduled — ETFs, money-market funds, etc.
    const Point& next = future.first();

    Entry e;
    e.date = next.date;
    e.symbol = symbol;
    e.has_est = next.has_estimate;
    e.eps_est = next.estimate;
    e.surprise_pct = surprise;
    e.has_surprise = has_surprise;
    const qint64 days = today.daysTo(next.date);
    e.when = days == 0 ? QStringLiteral("today") : QStringLiteral("in %1d").arg(days);
    e.held = true;
    e.est_from_yf = true;
    e.weight = (held_total_mv_ > 0) ? held_value_.value(symbol) / held_total_mv_ * 100.0 : 0;
    e.portfolios = held_portfolios_.value(symbol);
    e.has_ly = year_ago_actual(next.date, &e.eps_ly);

    entries_.append(e);
}

// ── Rendering ────────────────────────────────────────────────────────────────

void EarningsCalendarWidget::show_status(const QString& text) {
    status_label_->setText(text);
    status_label_->setVisible(true);
    scroll_area_->hide();
}

void EarningsCalendarWidget::populate() {
    while (list_layout_->count() > 0) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    weight_header_->setVisible(mode_ == Mode::Portfolio);

    if (entries_.isEmpty()) {
        show_status(mode_ == Mode::ThisWeek
                        ? QStringLiteral("No earnings scheduled for the rest of this week.")
                        : QStringLiteral("No earnings scheduled for these holdings in the next 6 months."));
        list_layout_->addStretch();
        return;
    }

    const QDate today = QDate::currentDate();
    bool alt = false;
    QDate prev_date;

    for (const auto& e : entries_) {
        auto* row = new QWidget(this);
        row->setStyleSheet(QString("background: %1;").arg(alt ? ui::colors::BG_RAISED() : "transparent"));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 3, 8, 3);

        auto cell = [&](const QString& text, Qt::Alignment align, const QString& color) {
            auto* lbl = new QLabel(text);
            // Always pin the vertical centre: QLabel drops to top alignment
            // when only a horizontal flag is given, which left cells sitting
            // at slightly different heights across the row.
            lbl->setAlignment(align | Qt::AlignVCenter);
            lbl->setStyleSheet(QString("color: %1; background: transparent;").arg(color));
            return lbl;
        };

        // Repeating the date on every row of a busy day is noise — print it once
        // per date so the day boundary reads.
        const bool new_day = e.date != prev_date;
        prev_date = e.date;
        rl->addWidget(cell(new_day ? e.date.toString("ddd d MMM") : QString(), Qt::AlignLeft,
                           e.date == today ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY()),
                      3);

        // Held marker: the "does this hit my book" answer, at a glance.
        auto* sym_lbl = cell((e.held ? QStringLiteral("● ") : QStringLiteral("  ")) + e.symbol, Qt::AlignLeft,
                             e.held ? ui::colors::AMBER() : ui::colors::CYAN());
        QStringList tip;
        if (!e.company.isEmpty())
            tip << e.company;
        if (!e.fiscal_quarter.isEmpty())
            tip << QStringLiteral("Fiscal quarter ending %1").arg(e.fiscal_quarter);
        if (e.has_ly)
            tip << QStringLiteral("Year-ago EPS %1%2")
                       .arg(fmt_eps(e.eps_ly),
                            e.ly_report_date.isEmpty() ? QString()
                                                       : QStringLiteral(" (reported %1)").arg(e.ly_report_date));
        if (e.num_ests > 0)
            tip << QStringLiteral("%1 analyst estimates").arg(e.num_ests);
        if (e.rank > 0)
            tip << QStringLiteral("Market cap $%1B").arg(e.rank / 1e9, 0, 'f', 1);
        if (!e.portfolios.isEmpty())
            tip << QStringLiteral("Held in: %1").arg(e.portfolios.join(QStringLiteral(", ")));
        if (!tip.isEmpty())
            sym_lbl->setToolTip(tip.join(QLatin1Char('\n')));
        rl->addWidget(sym_lbl, 2);

        rl->addWidget(cell(e.when, Qt::AlignLeft, ui::colors::TEXT_SECONDARY()), 2);

        // EPS estimate, coloured by expected growth vs the year-ago quarter.
        QString eps_colour = ui::colors::TEXT_PRIMARY();
        QString eps_text = QStringLiteral("—");
        QString eps_tip = QStringLiteral("No consensus estimate published for this print.");
        if (e.has_est) {
            eps_text = fmt_eps(e.eps_est);
            const QString source = e.est_from_yf ? QStringLiteral("Yahoo Finance")
                                                 : QStringLiteral("Nasdaq");
            const Growth g = e.has_ly ? growth_verdict(e.eps_est, e.eps_ly) : Growth::Unknown;
            switch (g) {
                case Growth::Up:
                    eps_colour = ui::colors::POSITIVE();
                    eps_text += QStringLiteral(" ▲");
                    eps_tip = QStringLiteral("Consensus %1 for the coming quarter — above the %2 reported a "
                                             "year ago, so analysts expect growth.\nBoth figures: %3.")
                                  .arg(fmt_eps(e.eps_est), fmt_eps(e.eps_ly), source);
                    break;
                case Growth::Down:
                    eps_colour = ui::colors::NEGATIVE();
                    eps_text += QStringLiteral(" ▼");
                    eps_tip = QStringLiteral("Consensus %1 for the coming quarter — below the %2 reported a "
                                             "year ago, so analysts expect a decline.\nBoth figures: %3.")
                                  .arg(fmt_eps(e.eps_est), fmt_eps(e.eps_ly), source);
                    break;
                case Growth::Flat:
                    eps_tip = QStringLiteral("Consensus %1 for the coming quarter — within 1%% of the %2 "
                                             "reported a year ago, so effectively flat. Consensus panels "
                                             "differ by more than that, so it isn't called either way."
                                             "\nBoth figures: %3.")
                                  .arg(fmt_eps(e.eps_est), fmt_eps(e.eps_ly), source);
                    break;
                case Growth::Unknown:
                    eps_tip = QStringLiteral("Consensus %1 for the coming quarter (%2). No year-ago figure to "
                                             "compare against.")
                                  .arg(fmt_eps(e.eps_est), source);
                    break;
            }
        }
        auto* eps_lbl = cell(eps_text, Qt::AlignHCenter, eps_colour);
        eps_lbl->setToolTip(eps_tip);
        rl->addWidget(eps_lbl, 3);

        // Last reported quarter's surprise — its own column, because it's a
        // different quarter and a different question from the estimate.
        QString surp_text = QStringLiteral("—");
        QString surp_colour = ui::colors::TEXT_SECONDARY();
        QString surp_tip = QStringLiteral("No reported-quarter history available for this symbol.");
        if (e.has_surprise) {
            surp_text = QStringLiteral("%1%2%")
                            .arg(e.surprise_pct >= 0 ? QStringLiteral("+") : QString())
                            .arg(e.surprise_pct, 0, 'f', 1);
            surp_colour = e.surprise_pct >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE();
            surp_tip = QStringLiteral("Last quarter's reported EPS came in %1% %2 the consensus estimate.")
                           .arg(std::abs(e.surprise_pct), 0, 'f', 1)
                           .arg(e.surprise_pct >= 0 ? QStringLiteral("above") : QStringLiteral("below"));
        }
        auto* surp_lbl = cell(surp_text, Qt::AlignHCenter, surp_colour);
        surp_lbl->setToolTip(surp_tip);
        rl->addWidget(surp_lbl, 3);

        if (mode_ == Mode::Portfolio) {
            rl->addWidget(cell(e.weight > 0 ? QStringLiteral("%1%").arg(e.weight, 0, 'f', 1) : QStringLiteral("—"),
                               Qt::AlignRight, ui::colors::TEXT_SECONDARY()),
                          2);
        }

        list_layout_->addWidget(row);
        alt = !alt;
    }
    list_layout_->addStretch();

    status_label_->setVisible(!note_.isEmpty());
    if (!note_.isEmpty())
        status_label_->setText(note_);
    scroll_area_->show();
}

// ── Styling ──────────────────────────────────────────────────────────────────

void EarningsCalendarWidget::apply_styles() {
    const QString active =
        QString("QPushButton { background:%1; color:%2; border:none; font-weight:bold;"
                "  font-size:%3; padding:2px; }")
            .arg(ui::colors::AMBER_DIM(), ui::colors::AMBER(), ui::fonts::ui_px());
    const QString idle =
        QString("QPushButton { background:transparent; color:%1; border:none; font-size:%2; padding:2px; }"
                "QPushButton:hover { color:%3; }")
            .arg(ui::colors::TEXT_SECONDARY(), ui::fonts::ui_px(), ui::colors::TEXT_PRIMARY());

    if (week_btn_)
        week_btn_->setStyleSheet(mode_ == Mode::ThisWeek ? active : idle);
    if (portfolio_btn_)
        portfolio_btn_->setStyleSheet(mode_ == Mode::Portfolio ? active : idle);
    // Follow the mode here too, not only in populate() — a view switch that
    // lands on a status message ("no holdings") never reaches populate() and
    // would leave the other view's WT% header standing.
    if (weight_header_)
        weight_header_->setVisible(mode_ == Mode::Portfolio);

    if (header_widget_)
        header_widget_->setStyleSheet(QString("background: %1;").arg(ui::colors::BG_RAISED()));
    for (auto* lbl : header_labels_)
        lbl->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent; font-size:%2;")
                               .arg(ui::colors::TEXT_SECONDARY(), ui::fonts::ui_px()));
    if (header_sep_)
        header_sep_->setStyleSheet(QString("background: %1;").arg(ui::colors::BORDER_DIM()));
    if (status_label_)
        status_label_->setStyleSheet(QString("color: %1; background: transparent; padding: 8px; font-size:%2;")
                                         .arg(ui::colors::TEXT_SECONDARY(), ui::fonts::ui_px()));
    if (scroll_area_)
        scroll_area_->setStyleSheet(
            QString("QScrollArea { border: none; background: transparent; }"
                    "QScrollBar:vertical { width: 4px; background: transparent; }"
                    "QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }"
                    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
                .arg(ui::colors::BORDER_MED()));

    // Same reasoning as PortfolioSummaryWidget: the app-global combo style
    // doesn't reach a combo nested under a widget carrying its own stylesheet,
    // so the field would render white-on-white without this.
    if (portfolio_combo_)
        portfolio_combo_->setStyleSheet(
            QString("QComboBox { background:%1; color:%2; border:1px solid %3; border-radius:2px;"
                    "  padding:0 4px; font-size:%4; }"
                    "QComboBox:hover { border-color:%5; }"
                    "QComboBox QLineEdit { background:transparent; color:%2; border:none; }"
                    "QComboBox::drop-down { border:none; width:14px; }"
                    "QComboBox QAbstractItemView { background:%1; color:%2; border:1px solid %3;"
                    "  selection-background-color:%6; selection-color:%2; outline:none; }")
                .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED(),
                     ui::fonts::ui_px(), ui::colors::AMBER(), ui::colors::BG_HOVER()));
}

void EarningsCalendarWidget::on_theme_changed() {
    apply_styles();
    if (!entries_.isEmpty())
        populate(); // rows carry inline colours — rebuild them
}

} // namespace fincept::screens::widgets

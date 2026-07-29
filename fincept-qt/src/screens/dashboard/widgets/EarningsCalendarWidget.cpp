// src/screens/dashboard/widgets/EarningsCalendarWidget.cpp
#include "screens/dashboard/widgets/EarningsCalendarWidget.h"

#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "python/PythonWorker.h"
#include "services/portfolio/PortfolioService.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QTimeZone>

#include <algorithm>

namespace fincept::screens::widgets {

// Shared with PortfolioSummaryWidget on purpose: "which portfolio does the
// dashboard mean" is one decision, not one per tile.
static constexpr auto kSelectedPortfolioKey = "dashboard/portfolio_id";

EarningsCalendarWidget::EarningsCalendarWidget(const QJsonObject& cfg, QWidget* parent)
    : BaseWidget("EARNINGS CALENDAR", parent, ui::colors::AMBER()) {
    build_body();

    // Portfolio selector — title bar, like PortfolioSummaryWidget, so it
    // doesn't cost a content row. Only meaningful in the portfolio view.
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
    portfolio_combo_->setVisible(false);

    selected_id_ = QSettings().value(kSelectedPortfolioKey).toString();

    auto& svc = services::PortfolioService::instance();
    connect(&svc, &services::PortfolioService::portfolios_loaded, this,
            &EarningsCalendarWidget::on_portfolios_loaded);
    connect(&svc, &services::PortfolioService::summary_loaded, this,
            &EarningsCalendarWidget::on_summary_loaded);
    // Without this a failed summary load leaves the tile on "Loading…" forever
    // — the fan-out that clears it is only started by summary_loaded.
    connect(&svc, &services::PortfolioService::summary_error, this,
            [this](QString portfolio_id, QString error) {
                if (mode_ != Mode::Portfolio || portfolio_id != selected_id_ || pending_ > 0)
                    return;
                set_loading(false);
                show_status(QStringLiteral("Couldn't load the portfolio: %1").arg(error.left(120)));
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
        lbl->setAlignment(align);
        header_labels_.append(lbl);
        hl->addWidget(lbl, stretch);
    };
    make_hdr("DATE", 3);
    make_hdr("SYMBOL", 2);
    make_hdr("WHEN", 2);
    make_hdr("EPS EST", 2, Qt::AlignRight);
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

// ── Config ───────────────────────────────────────────────────────────────────

QJsonObject EarningsCalendarWidget::config() const {
    return QJsonObject{{"mode", mode_ == Mode::Portfolio ? "portfolio" : "week"},
                       {"portfolio_id", selected_id_}};
}

void EarningsCalendarWidget::apply_config(const QJsonObject& cfg) {
    mode_ = cfg.value("mode").toString() == "portfolio" ? Mode::Portfolio : Mode::ThisWeek;
    // Per-instance portfolio, so two tiles can watch two portfolios. The
    // dashboard-wide QSettings choice is only the default for a fresh tile.
    const QString cfg_id = cfg.value("portfolio_id").toString();
    if (!cfg_id.isEmpty())
        selected_id_ = cfg_id;
    portfolio_combo_->setVisible(mode_ == Mode::Portfolio);
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
    portfolio_combo_->setVisible(mode_ == Mode::Portfolio);
    apply_styles();
    emit config_changed(config()); // canvas persists the choice
    refresh_data();
}

void EarningsCalendarWidget::refresh_data() {
    loaded_once_ = true;
    ++epoch_;
    pending_ = 0;
    ok_fetches_ = 0;
    entries_.clear();
    note_.clear();
    set_loading(true);
    show_status(QStringLiteral("Loading…"));
    if (mode_ == Mode::ThisWeek)
        load_week();
    else
        load_portfolio();
}

// ── This week — Nasdaq public earnings calendar ──────────────────────────────

void EarningsCalendarWidget::load_week() {
    // "This week" = today through Friday. On a weekend there is nothing left in
    // the current week, so roll forward to the next one — an empty list would
    // read as a data failure.
    QDate start = QDate::currentDate();
    if (start.dayOfWeek() > 5)
        start = start.addDays(8 - start.dayOfWeek()); // Sat→Mon(+2), Sun→Mon(+1)
    const QDate end = start.addDays(5 - start.dayOfWeek()); // Friday of that week

    const qint64 epoch = epoch_;
    for (QDate d = start; d <= end; d = d.addDays(1)) {
        ++pending_;
        fetch_day(d, epoch);
    }
    if (pending_ == 0) {
        set_loading(false);
        show_status(QStringLiteral("No trading days left this week."));
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
            ++self->ok_fetches_;
            // A market holiday returns a valid response with a null/absent
            // data object — that's "nothing scheduled", not a failure.
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
                e.eps_est = o["epsForecast"].toString().trimmed();
                if (e.eps_est.isEmpty())
                    e.eps_est = QStringLiteral("—");
                // "$4,994,876,028,480" → 4.99e12. Anything unparseable ranks
                // last rather than dropping the row.
                QString mc = o["marketCap"].toString();
                mc.remove(QLatin1Char('$')).remove(QLatin1Char(','));
                e.rank = mc.toDouble();
                self->entries_.append(e);
            }
        }

        if (--self->pending_ > 0)
            return;

        self->set_loading(false);
        if (self->ok_fetches_ == 0) {
            self->show_status(QStringLiteral("Couldn't reach the Nasdaq earnings calendar."));
            return;
        }
        // Date first, then the biggest names within each day.
        std::sort(self->entries_.begin(), self->entries_.end(), [](const Entry& a, const Entry& b) {
            if (a.date != b.date)
                return a.date < b.date;
            if (a.rank != b.rank)
                return a.rank > b.rank;
            return a.symbol < b.symbol;
        });
        if (self->entries_.size() > kMaxWeekRows) {
            self->note_ = QStringLiteral("Showing the %1 largest of %2 companies reporting this week.")
                              .arg(kMaxWeekRows)
                              .arg(self->entries_.size());
            self->entries_.resize(kMaxWeekRows);
        }
        self->populate();
    });
}

// ── Portfolio — one yfinance earnings-dates call per holding ─────────────────

void EarningsCalendarWidget::load_portfolio() {
    if (!portfolios_loaded_) {
        services::PortfolioService::instance().load_portfolios();
        return; // on_portfolios_loaded() continues
    }
    if (selected_id_.isEmpty()) {
        set_loading(false);
        show_status(QStringLiteral("No portfolio configured."));
        return;
    }
    services::PortfolioService::instance().load_summary(selected_id_);
}

void EarningsCalendarWidget::on_portfolios_loaded(QVector<portfolio::Portfolio> portfolios) {
    portfolios_ = portfolios;
    portfolios_loaded_ = true;

    suppress_combo_signal_ = true;
    portfolio_combo_->clear();
    for (const auto& p : portfolios_)
        portfolio_combo_->addItem(p.name.isEmpty() ? p.id : p.name);
    suppress_combo_signal_ = false;

    if (portfolios_.isEmpty()) {
        selected_id_.clear();
        portfolio_combo_->setVisible(false);
        if (mode_ == Mode::Portfolio) {
            set_loading(false);
            show_status(QStringLiteral("No portfolio configured."));
        }
        return;
    }
    portfolio_combo_->setVisible(mode_ == Mode::Portfolio);

    // The persisted portfolio may have been deleted since last run.
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
    selected_id_ = portfolios_.at(idx).id;

    // portfolios_loaded is a global signal — it also fires when the Portfolio
    // screen loads. Only pull a summary if we're actually showing that view.
    if (mode_ == Mode::Portfolio && isVisible())
        services::PortfolioService::instance().load_summary(selected_id_);
}

void EarningsCalendarWidget::select_portfolio(const QString& id) {
    selected_id_ = id;
    QSettings().setValue(kSelectedPortfolioKey, id); // dashboard-wide default
    emit config_changed(config());                   // this tile's own choice
    if (mode_ == Mode::Portfolio)
        refresh_data();
}

void EarningsCalendarWidget::on_summary_loaded(const portfolio::PortfolioSummary& summary) {
    // summary_loaded fires for every portfolio anyone loads — and the service
    // emits twice (disk cache, then live). Ignore anything that isn't the
    // portfolio this tile is showing.
    if (mode_ != Mode::Portfolio || summary.portfolio.id != selected_id_)
        return;
    // A fan-out already in flight for this portfolio: let it finish rather
    // than restarting on the service's second (live) emit.
    if (pending_ > 0)
        return;

    QStringList symbols;
    for (const auto& h : summary.holdings) {
        if (h.symbol.isEmpty() || h.quantity <= 0)
            continue;
        if (!symbols.contains(h.symbol))
            symbols.append(h.symbol);
    }

    if (symbols.isEmpty()) {
        set_loading(false);
        show_status(QStringLiteral("This portfolio has no holdings."));
        return;
    }
    if (symbols.size() > kMaxPortfolioSymbols) {
        note_ = QStringLiteral("Checked the first %1 of %2 holdings.")
                    .arg(kMaxPortfolioSymbols)
                    .arg(symbols.size());
        LOG_INFO("EarningsCal", QString("Portfolio %1 has %2 holdings — capping earnings lookup at %3")
                                    .arg(selected_id_)
                                    .arg(symbols.size())
                                    .arg(kMaxPortfolioSymbols));
        symbols = symbols.mid(0, kMaxPortfolioSymbols);
    }

    entries_.clear();
    fetch_symbol_earnings(symbols, epoch_);
}

void EarningsCalendarWidget::fetch_symbol_earnings(const QStringList& symbols, qint64 epoch) {
    pending_ = symbols.size();
    ok_fetches_ = 0;

    const QDate today = QDate::currentDate();
    QPointer<EarningsCalendarWidget> self = this;

    for (const QString& sym : symbols) {
        QJsonObject payload;
        payload["symbol"] = sym;
        // Past prints belong to the research screen, not a "what's coming"
        // tile: ask only for the window ahead.
        payload["lookback_days"] = 0;
        payload["lookahead_days"] = 180;

        python::PythonWorker::instance().submit(
            "earnings_dates", payload,
            [self, sym, epoch, today](bool ok, QJsonObject result, QString err) {
                if (!self || epoch != self->epoch_)
                    return;

                if (ok && !result.contains("error")) {
                    ++self->ok_fetches_;
                    // Dates come back oldest → newest; the first one that isn't
                    // in the past is the next print.
                    const auto dates = result.value("dates").toArray();
                    for (const auto& v : dates) {
                        const auto o = v.toObject();
                        const qint64 ts = static_cast<qint64>(o["timestamp"].toDouble());
                        const QDate d = QDateTime::fromSecsSinceEpoch(ts, QTimeZone::UTC).date();
                        if (!d.isValid() || d < today)
                            continue;
                        Entry e;
                        e.date = d;
                        e.symbol = sym;
                        const qint64 days = today.daysTo(d);
                        e.when = days == 0 ? QStringLiteral("today")
                                           : QStringLiteral("in %1d").arg(days);
                        const auto est = o.value("eps_estimate");
                        e.eps_est = est.isDouble() ? QStringLiteral("$%1").arg(est.toDouble(), 0, 'f', 2)
                                                   : QStringLiteral("—");
                        self->entries_.append(e);
                        break;
                    }
                } else if (!ok) {
                    LOG_WARN("EarningsCal",
                             QString("earnings_dates failed for %1: %2").arg(sym, err.left(120)));
                }

                if (--self->pending_ > 0)
                    return;

                self->set_loading(false);
                if (self->ok_fetches_ == 0) {
                    self->show_status(QStringLiteral("Couldn't load earnings dates for these holdings."));
                    return;
                }
                std::sort(self->entries_.begin(), self->entries_.end(), [](const Entry& a, const Entry& b) {
                    return a.date != b.date ? a.date < b.date : a.symbol < b.symbol;
                });
                self->populate();
            },
            python::PythonWorker::kNetworkActionTimeoutMs);
    }
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
            lbl->setAlignment(align);
            lbl->setStyleSheet(QString("color: %1; background: transparent;").arg(color));
            return lbl;
        };

        // Repeating the date on every row of a long day is noise — print it
        // once per date and leave the rest blank, so the day boundary reads.
        const bool new_day = e.date != prev_date;
        prev_date = e.date;
        auto* date_lbl = cell(new_day ? e.date.toString("ddd d MMM") : QString(), Qt::AlignLeft,
                              e.date == today ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY());
        rl->addWidget(date_lbl, 3);

        auto* sym_lbl = cell(e.symbol, Qt::AlignLeft, ui::colors::CYAN());
        if (!e.company.isEmpty())
            sym_lbl->setToolTip(e.company);
        rl->addWidget(sym_lbl, 2);

        rl->addWidget(cell(e.when, Qt::AlignLeft, ui::colors::TEXT_SECONDARY()), 2);
        rl->addWidget(cell(e.eps_est, Qt::AlignRight, ui::colors::TEXT_PRIMARY()), 2);

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

// src/screens/markets/MarketChartDialog.cpp
#include "screens/markets/MarketChartDialog.h"

#include "core/events/EventBus.h"
#include "core/util/BarTime.h"
#include "datahub/DataHubMetaTypes.h"
#include "datahub/DataHub.h"
#include "screens/equity_research/EquityOverviewTab.h"
#include "screens/markets/MarketsScreen.h"
#include "services/futures/FuturesCatalog.h"
#include "services/markets/ConditionService.h"
#include "services/markets/IndexFutures.h"
#include "services/markets/LastKnownQuote.h"
#include "services/markets/MarketConditions.h"
#include "services/markets/MarketDataService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>

#include <limits>

namespace finterm::screens {

QPointer<MarketChartDialog> MarketChartDialog::instance_;

QString MarketChartView::symbol() const {
    return symbol_;
}

void MarketChartView::set_closable(bool on) {
    if (auto* b = findChild<QPushButton*>(QStringLiteral("close")))
        b->setVisible(on);
}

namespace {

struct PeriodDef {
    const char* label;
    const char* period;   // yfinance period
    const char* interval; // bar size
    bool intraday;
};
// Bar sizes keep every window inside the canvas's 1,500-bar budget: 5Y daily
// is ~1,260 bars, and MAX goes monthly because an index like the Dow has more
// than 1,500 weeks of history.
constexpr PeriodDef kPeriods[] = {
    {"1D", "1d", "5m", true},   {"5D", "5d", "15m", true},  {"1M", "1mo", "1d", false},
    {"3M", "3mo", "1d", false}, {"6M", "6mo", "1d", false}, {"YTD", "ytd", "1d", false},
    {"1Y", "1y", "1d", false},  {"5Y", "5y", "1d", false},  {"MAX", "max", "1mo", false},
};

const PeriodDef& period_def(const QString& label) {
    for (const auto& p : kPeriods)
        if (label == QLatin1String(p.label))
            return p;
    return kPeriods[6]; // 1Y
}

// The reader's choices, kept for the session so every chart opens the way
// the last one was left.
struct Prefs {
    QString period = QStringLiteral("1Y");
    int style = static_cast<int>(ResearchCandleCanvas::SeriesStyle::Area);
    bool log = false;
    bool vol = true;
    bool sma20 = false;
    bool sma50 = false;
    bool sma200 = false;
    bool fut = false;
};
Prefs& prefs() {
    static Prefs p;
    return p;
}

QString button_style(const QString& accent) {
    return QString("QPushButton{background:transparent;color:%1;border:1px solid %2;font-size:12px;"
                   "font-weight:700;padding:3px 9px;border-radius:2px;}"
                   "QPushButton:checked{background:%3;color:%4;border-color:%3;}"
                   "QPushButton:hover:!checked{color:%5;border-color:%5;}")
        .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), accent, ui::colors::BG_BASE(),
             ui::colors::TEXT_PRIMARY());
}

QPushButton* make_button(const QString& text, const QString& accent, bool checkable = true) {
    auto* b = new QPushButton(text);
    b->setCheckable(checkable);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setStyleSheet(button_style(accent));
    return b;
}

QLabel* make_label(const QString& color, int px, bool bold = false, bool mono = false) {
    auto* l = new QLabel;
    l->setStyleSheet(QString("color:%1;font-size:%2px;%3%4background:transparent;")
                         .arg(color)
                         .arg(px)
                         .arg(bold ? "font-weight:700;" : "")
                         .arg(mono ? "font-family:monospace;" : ""));
    return l;
}

QString price_text(double v) {
    const int dp = std::abs(v) >= 1000 ? 2 : std::abs(v) >= 1 ? 2 : 4;
    return QLocale(QLocale::English).toString(v, 'f', dp);
}

/// Only plain listed securities have an Equity Research page worth opening;
/// indices (^DJI), futures and FX (=X, =F) and coins (-USD) don't.
bool has_research_page(const QString& sym) {
    return !sym.startsWith('^') && !sym.contains('=') && !sym.endsWith(QLatin1String("-USD")) &&
           !services::ConditionService::is_derived(sym);
}

/// The first session a derived (daily, non-Yahoo) series shows for `period`;
/// invalid = all of it. 1D / 5D have no meaning for a daily series and read
/// as a month.
QDate derived_start(const QString& period, const QDate& last) {
    if (period == QLatin1String("1D") || period == QLatin1String("5D") || period == QLatin1String("1M"))
        return last.addMonths(-1);
    if (period == QLatin1String("3M"))
        return last.addMonths(-3);
    if (period == QLatin1String("6M"))
        return last.addMonths(-6);
    if (period == QLatin1String("YTD"))
        return QDate(last.year(), 1, 1);
    if (period == QLatin1String("1Y"))
        return last.addYears(-1);
    if (period == QLatin1String("5Y"))
        return last.addYears(-5);
    return {};
}

} // namespace

namespace {
constexpr const char* kTargetProp = "chart_symbol";
constexpr const char* kLabelProp = "chart_label";

/// One shared filter for every clickable row in the app.
class ChartClickFilter : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (e->type() != QEvent::MouseButtonRelease)
            return false;
        auto* me = static_cast<QMouseEvent*>(e);
        auto* w = qobject_cast<QWidget*>(obj);
        if (!w || me->button() != Qt::LeftButton || !w->rect().contains(me->position().toPoint()))
            return false;
        const QString sym = w->property(kTargetProp).toString();
        if (sym.isEmpty())
            return false;
        MarketChartDialog::show_for(sym, w->property(kLabelProp).toString(), w);
        return true;
    }
};

ChartClickFilter* click_filter() {
    static auto* f = new ChartClickFilter(qApp);
    return f;
}
} // namespace

void MarketChartDialog::make_clickable(QWidget* w) {
    if (!w)
        return;
    w->installEventFilter(click_filter());
    w->setCursor(Qt::PointingHandCursor);
}

void MarketChartDialog::set_target(QWidget* w, const QString& symbol, const QString& label) {
    if (!w)
        return;
    w->setProperty(kTargetProp, symbol);
    w->setProperty(kLabelProp, label);
}

namespace {
/// Linked charts by the canvas (or page) they sit on — security linking.
QHash<QWidget*, QPointer<MarketChartView>>& linked() {
    static QHash<QWidget*, QPointer<MarketChartView>> h;
    return h;
}
} // namespace

void MarketChartDialog::link_chart(QWidget* scope, MarketChartView* view) {
    if (scope && view)
        linked().insert(scope, view);
}

void MarketChartDialog::unlink_chart(QWidget* scope, MarketChartView* view) {
    // Only this view's link: a second chart on the same canvas keeps its own.
    const auto it = linked().find(scope);
    if (it != linked().end() && (it.value() == view || !it.value()))
        linked().erase(it);
}

bool MarketChartDialog::route_to_linked(const QString& symbol, const QString& label, QWidget* from) {
    // The nearest ancestor of the clicked widget that has a linked chart
    // takes the symbol when that chart is on screen — the click stays on the
    // canvas (security linking). Otherwise the dialog opens.
    for (QWidget* w = from; w; w = w->parentWidget()) {
        const auto it = linked().constFind(w);
        if (it == linked().constEnd())
            continue;
        MarketChartView* v = it.value();
        if (v && v->isVisible()) {
            v->set_symbol(symbol, label);
            return true;
        }
        return false;
    }
    return false;
}

void MarketChartDialog::show_for(const QString& symbol, const QString& label, QWidget* from) {
    if (symbol.trimmed().isEmpty())
        return;
    if (route_to_linked(symbol.trimmed(), label, from))
        return;
    QWidget* host = from ? from->window() : nullptr;
    if (!instance_) {
        instance_ = new MarketChartDialog(host);
        // Centre over the main window, sized to it, so it reads as part of
        // the terminal rather than a stray window.
        if (host) {
            const QRect r = host->geometry();
            const QSize sz(std::max(760, r.width() * 7 / 10), std::max(480, r.height() * 7 / 10));
            instance_->resize(sz);
            instance_->move(r.center() - QPoint(sz.width() / 2, sz.height() / 2));
        } else {
            instance_->resize(1000, 620);
        }
    }
    instance_->view_->set_symbol(symbol.trimmed(), label);
    instance_->show();
    instance_->raise();
    instance_->activateWindow();
}

MarketChartDialog::MarketChartDialog(QWidget* parent) : QDialog(parent) {
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setStyleSheet(QString("QDialog{background:%1;}").arg(ui::colors::BG_SURFACE()));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    view_ = new MarketChartView(this);
    root->addWidget(view_);
    connect(view_, &MarketChartView::close_requested, this, &QDialog::close);
    connect(view_, &MarketChartView::title_changed, this, &QWidget::setWindowTitle);
}

MarketChartView::MarketChartView(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus); // number keys step the periods
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    // ── Header: what, where it is, how it moved over the window ─────────────
    auto* head = new QHBoxLayout;
    head->setSpacing(14);
    title_ = make_label(ui::colors::AMBER(), 18, true);
    head->addWidget(title_);
    last_ = make_label(ui::colors::TEXT_PRIMARY(), 18, true, true);
    head->addWidget(last_);
    change_ = make_label(ui::colors::TEXT_PRIMARY(), 14, true, true);
    head->addWidget(change_);
    fut_ = make_label(ui::colors::TEXT_PRIMARY(), 14, true, true);
    fut_->hide();
    head->addWidget(fut_);
    head->addStretch(1);
    auto* er_btn = make_button(QStringLiteral("OPEN IN RESEARCH"), ui::colors::AMBER(), false);
    er_btn->setObjectName(QStringLiteral("er"));
    connect(er_btn, &QPushButton::clicked, this, [this]() {
        EventBus::instance().publish("nav.split_alongside", QVariantMap{{"screen_id", "equity_research"}});
        EventBus::instance().publish("equity_research.load_symbol", QVariantMap{{"symbol", symbol_}});
    });
    head->addWidget(er_btn);
    auto* fut_btn = make_button(QStringLiteral("CURVE ▸"), ui::colors::AMBER(), false);
    fut_btn->setObjectName(QStringLiteral("futures"));
    fut_btn->setToolTip(QStringLiteral("Open this market in MARKETS ▸ FUTURES: forward curve, contract months, "
                                       "spreads, positioning and the ticket"));
    fut_btn->hide();
    connect(fut_btn, &QPushButton::clicked, this, [this]() {
        const QString root = services::futures::root_of(symbol_);
        if (!root.isEmpty())
            MarketsScreen::request_futures(root);
    });
    head->addWidget(fut_btn);
    auto* close_btn = make_button(QStringLiteral("✕"), ui::colors::AMBER(), false);
    close_btn->setToolTip(QStringLiteral("Close (Esc)"));
    close_btn->setObjectName(QStringLiteral("close"));
    connect(close_btn, &QPushButton::clicked, this, &MarketChartView::close_requested);
    head->addWidget(close_btn);
    root->addLayout(head);

    // ── Controls: period · style · overlays ─────────────────────────────────
    auto* ctl = new QHBoxLayout;
    ctl->setSpacing(4);
    for (const auto& p : kPeriods) {
        const QString label = QString::fromLatin1(p.label);
        auto* b = make_button(label, ui::colors::AMBER());
        connect(b, &QPushButton::clicked, this, [this, label]() {
            prefs().period = label;
            sync_buttons();
            load();
        });
        period_btns_.insert(label, b);
        ctl->addWidget(b);
    }
    ctl->addSpacing(16);
    using Style = ResearchCandleCanvas::SeriesStyle;
    for (const auto& [style, text] :
         {std::pair{Style::Area, "AREA"}, std::pair{Style::Line, "LINE"}, std::pair{Style::Candles, "CANDLES"}}) {
        auto* b = make_button(QString::fromLatin1(text), ui::colors::CYAN());
        const int s = static_cast<int>(style);
        connect(b, &QPushButton::clicked, this, [this, s]() {
            prefs().style = s;
            canvas_->set_series_style(static_cast<Style>(s));
            sync_buttons();
        });
        style_btns_.insert(s, b);
        ctl->addWidget(b);
    }
    ctl->addSpacing(16);
    struct Toggle {
        const char* key;
        const char* text;
        const char* color;
        const char* tip;
    };
    const Toggle toggles[] = {
        {"log", "LOG", "#94a3b8", "Logarithmic price scale — equal heights are equal percentage moves."},
        {"vol", "VOL", "#94a3b8", "Volume beneath the price. Indices and FX often report none."},
        {"sma20", "SMA20", "#3b82f6", "20-bar simple moving average."},
        {"sma50", "SMA50", "#a855f7", "50-bar simple moving average."},
        {"sma200", "SMA200", "#eab308", "200-bar simple moving average."},
        {"fut", "FUT", "#f97316",
         "The index's futures quote — contract, last price and change since its previous close, "
         "live day and night. Shown for indices with a future on the feed."},
    };
    for (const auto& t : toggles) {
        const QString key = QString::fromLatin1(t.key);
        auto* b = make_button(QString::fromLatin1(t.text), QString::fromLatin1(t.color));
        b->setToolTip(QString::fromLatin1(t.tip));
        connect(b, &QPushButton::toggled, this, [this, key](bool on) {
            auto& pr = prefs();
            if (key == "log") {
                pr.log = on;
                canvas_->set_log_scale(on);
            } else if (key == "vol") {
                pr.vol = on;
                canvas_->set_show_volume(on);
            } else if (key == "sma20") {
                pr.sma20 = on;
                canvas_->set_show_sma(20, on);
            } else if (key == "sma50") {
                pr.sma50 = on;
                canvas_->set_show_sma(50, on);
            } else if (key == "sma200") {
                pr.sma200 = on;
                canvas_->set_show_sma(200, on);
            } else if (key == "fut") {
                pr.fut = on;
                update_fut_visibility();
            }
        });
        toggle_btns_.insert(key, b);
        ctl->addWidget(b);
    }
    ctl->addStretch(1);
    root->addLayout(ctl);

    // ── Crosshair readout ────────────────────────────────────────────────────
    readout_ = make_label(ui::colors::TEXT_SECONDARY(), 12, false, true);
    root->addWidget(readout_);
    // The symbol's condition readings — the same cond:stats answer the
    // condition lists show, so the chart and the row never disagree.
    cond_ = make_label(ui::colors::TEXT_SECONDARY(), 12, false, true);
    cond_->setTextFormat(Qt::RichText);
    cond_->setToolTip(QStringLiteral(
        "From the last 5 years of daily bars: distance from the 200-day mean, where the last close ranks in "
        "the past 1 and 5 years (0 = lowest), RSI(14), and ADX(14) — TREND at 25+, RANGE under 20."));
    cond_->hide();
    root->addWidget(cond_);

    canvas_ = new ResearchCandleCanvas(this);
    root->addWidget(canvas_, 1);
    overlay_ = new ui::LoadingOverlay(canvas_);
    connect(canvas_, &ResearchCandleCanvas::hover_changed, this, [this](int i) { update_header(i); });

    // Apply the session's choices before the first draw.
    const auto& pr = prefs();
    canvas_->set_series_style(static_cast<Style>(pr.style));
    canvas_->set_log_scale(pr.log);
    canvas_->set_show_volume(pr.vol);
    canvas_->set_show_sma(20, pr.sma20);
    canvas_->set_show_sma(50, pr.sma50);
    canvas_->set_show_sma(200, pr.sma200);

    // The futures bars run ~10 minutes behind; polling faster re-reads the
    // same number. Shares the dashboard's request through the daemon's cache.
    fut_timer_ = new QTimer(this);
    fut_timer_->setInterval(60'000);
    connect(fut_timer_, &QTimer::timeout, this, [this]() { fetch_futures(); });
    // Live prints of the quoted contract between fetches.
    connect(&services::MarketDataService::instance(), &services::MarketDataService::stream_ticks, this,
            [this](const QVector<services::StreamTick>& ticks) {
                if (!fut_quote_ || !isVisible())
                    return;
                bool moved = false;
                for (const auto& t : ticks)
                    moved = services::apply_future_tick(*fut_quote_, t.symbol, t.price, t.change, t.time_ms) || moved;
                if (moved)
                    paint_fut();
            });
    sync_buttons();
}

void MarketChartView::update_fut_visibility() {
    const bool has_future = services::index_future_for(symbol_) != nullptr;
    if (auto* b = toggle_btns_.value(QStringLiteral("fut")))
        b->setVisible(has_future);
    const bool on = has_future && prefs().fut;
    if (!on) {
        fut_timer_->stop();
        ++fut_seq_; // drop any reply still in flight
        fut_quote_.reset();
        sync_fut_stream();
        fut_->clear();
        fut_->hide();
        return;
    }
    fut_->show();
    if (!fut_timer_->isActive())
        fut_timer_->start();
    fetch_futures();
}

namespace {
/// The FUT readout's style — one string for every state it is painted in.
QString fut_style(const QString& color) {
    return QString("color:%1;font-size:14px;font-weight:700;font-family:monospace;background:transparent;").arg(color);
}
} // namespace

void MarketChartView::fetch_futures() {
    const int seq = ++fut_seq_;
    const QString sym = symbol_;
    auto paint = [this](const QString& color) { ui::set_style_if_changed(fut_, fut_style(color)); };
    if (fut_->text().isEmpty()) {
        const auto* f = services::index_future_for(sym);
        fut_->setText(QStringLiteral("%1 …").arg(f ? f->root : QStringLiteral("FUT")));
        fut_->setToolTip({});
        paint(ui::colors::TEXT_TERTIARY());
    }
    services::fetch_index_futures(
        {sym}, this, [this, seq, sym, paint](bool ok, QHash<QString, services::IndexFutureQuote> rows, QString err) {
            if (seq != fut_seq_ || sym != symbol_)
                return; // superseded by another symbol or toggle
            const auto it = rows.constFind(sym);
            if (!ok) {
                // Keep a good quote on screen through a failed refresh.
                if (!fut_->toolTip().isEmpty())
                    return;
                fut_->setText(QStringLiteral("FUT !"));
                fut_->setToolTip(QStringLiteral("Futures fetch failed: %1").arg(err));
                paint(ui::colors::TEXT_TERTIARY());
            } else if (it == rows.constEnd()) {
                fut_->setText(QStringLiteral("FUT —"));
                fut_->setToolTip(services::index_future_unavailable_tooltip(sym, label_));
                paint(ui::colors::TEXT_TERTIARY());
            } else {
                // A live print newer than the fetched quote stays, and a
                // pending row (quote refused) never blanks a streamed one.
                const bool keep = fut_quote_ && fut_quote_->contract == it->contract &&
                                  !services::is_pending(*fut_quote_) &&
                                  (it->refused ||
                                   (fut_quote_->ref_price == it->ref_price && fut_quote_->ts > it->ts));
                if (!keep)
                    fut_quote_ = *it;
                paint_fut();
                sync_fut_stream();
                return;
            }
            fut_quote_.reset();
            sync_fut_stream();
        });
}

void MarketChartView::paint_fut() {
    if (!fut_quote_)
        return;
    const auto& q = *fut_quote_;
    if (services::is_pending(q)) { // named contract, no number yet: its next print fills it
        fut_->setText(QStringLiteral("%1 —").arg(services::contract_label(q)));
        fut_->setToolTip(services::index_future_unavailable_tooltip(q.index, label_));
        ui::set_style_if_changed(fut_, fut_style(ui::colors::TEXT_TERTIARY()));
        return;
    }
    // "ESZ26 7,740.75 +38.00 (+0.49%)"
    fut_->setText(QStringLiteral("%1 %2 %3%4 (%5)")
                      .arg(services::contract_label(q), price_text(q.price), q.change >= 0 ? "+" : "−",
                           price_text(std::abs(q.change)), ui::formatting::format_percent(q.pct, 2, true)));
    fut_->setToolTip(services::index_future_tooltip(q, label_));
    ui::set_style_if_changed(fut_, fut_style(q.pct >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));
}

void MarketChartView::sync_fut_stream() {
    QStringList contracts;
    if (fut_quote_ && isVisible() && prefs().fut)
        contracts << fut_quote_->contract;
    services::MarketDataService::instance().set_stream_extras(this, contracts);
}

void MarketChartView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    sync_fut_stream();
    sync_conditions();
}

void MarketChartView::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    sync_fut_stream();
    sync_conditions(); // not visible: drops the subscription
}

void MarketChartView::sync_buttons() {
    const auto& pr = prefs();
    for (auto it = period_btns_.begin(); it != period_btns_.end(); ++it)
        it.value()->setChecked(it.key() == pr.period);
    for (auto it = style_btns_.begin(); it != style_btns_.end(); ++it)
        it.value()->setChecked(it.key() == pr.style);
    const QHash<QString, bool> on{{"log", pr.log},     {"vol", pr.vol},       {"sma20", pr.sma20},
                                  {"sma50", pr.sma50}, {"sma200", pr.sma200}, {"fut", pr.fut}};
    for (auto it = toggle_btns_.begin(); it != toggle_btns_.end(); ++it) {
        const QSignalBlocker block(it.value()); // state only; the canvas already agrees
        it.value()->setChecked(on.value(it.key()));
    }
}

void MarketChartView::set_symbol(const QString& symbol, const QString& label) {
    const bool changed = symbol != symbol_;
    symbol_ = symbol;
    label_ = label.isEmpty() ? symbol : label;
    emit title_changed(QStringLiteral("%1 — chart").arg(label_));
    // A futures market: its curve and contract months are one click away.
    const QString root = services::futures::root_of(symbol_);
    if (auto* fb = findChild<QPushButton*>(QStringLiteral("futures")))
        fb->setVisible(!root.isEmpty());
    title_->setText(label_ == symbol_
                        ? symbol_
                        : QStringLiteral("%1  <span style='color:%2;font-size:13px'>%3</span>")
                              .arg(label_.toHtmlEscaped(), ui::colors::TEXT_TERTIARY(), symbol_.toHtmlEscaped()));
    if (auto* er = findChild<QPushButton*>(QStringLiteral("er")))
        er->setVisible(has_research_page(symbol_));
    const bool derived = services::ConditionService::is_derived(symbol_);
    for (const char* p : {"1D", "5D"})
        if (auto* b = period_btns_.value(QString::fromLatin1(p)))
            b->setEnabled(!derived); // a daily series has no intraday view
    if (changed)
        emit symbol_changed(symbol_, label_);
    if (changed) {
        fut_quote_.reset();
        sync_fut_stream();
        fut_->clear();
        fut_->setToolTip({});
        sync_conditions();
        load();
    }
    update_fut_visibility();
}

void MarketChartView::sync_conditions() {
    auto& hub = datahub::DataHub::instance();
    if (!cond_topic_.isEmpty()) {
        hub.unsubscribe(this, cond_topic_);
        hub.unsubscribe_errors(this, cond_topic_);
        hub.unsubscribe(this, cond_quote_topic_);
        cond_topic_.clear();
        cond_quote_topic_.clear();
    }
    cond_data_ = {};
    cond_error_.clear();
    cond_live_ = std::numeric_limits<double>::quiet_NaN();
    if (!isVisible() || symbol_.isEmpty() || services::ConditionService::is_derived(symbol_)) {
        paint_conditions();
        return;
    }
    auto& svc = services::ConditionService::instance();
    svc.ensure_registered_with_hub();
    cond_topic_ = services::ConditionService::stats_topic(symbol_);
    cond_data_ = svc.last_known(cond_topic_); // painted at once; the hub's answer follows
    QPointer<MarketChartView> self = this;
    const QString topic = cond_topic_;
    hub.subscribe(this, topic, [self, topic](const QVariant& v) {
        if (!self || topic != self->cond_topic_ || !v.canConvert<QJsonObject>())
            return;
        self->cond_data_ = v.toJsonObject();
        self->cond_error_.clear();
        self->paint_conditions();
    });
    hub.subscribe_errors(this, topic, [self, topic](const QString& err) {
        if (!self || topic != self->cond_topic_)
            return;
        self->cond_error_ = err;
        self->paint_conditions();
    });
    // The live quote the condition lists read: vs 200D and the 1-year
    // percentile are at the current price (conditions::live_reading).
    cond_quote_topic_ = QStringLiteral("market:quote:") + symbol_;
    const QString sym = symbol_;
    hub.subscribe(this, cond_quote_topic_, [self, sym](const QVariant& v) {
        if (!self || sym != self->symbol_ || !v.canConvert<services::QuoteData>())
            return;
        const auto q = v.value<services::QuoteData>();
        self->cond_live_ = !services::last_known_is_stale(sym) && q.price > 0 ? q.price
                                                                               : std::numeric_limits<double>::quiet_NaN();
        self->paint_conditions();
    });
    paint_conditions();
}

void MarketChartView::paint_conditions() {
    namespace cnd = services::conditions;
    const QJsonObject& c = cond_data_;
    if (c.isEmpty()) {
        cond_->setVisible(isVisible() && !services::ConditionService::is_derived(symbol_));
        cond_->setText(cond_error_.isEmpty()
                           ? QStringLiteral("Conditions: loading daily history…")
                           : QStringLiteral("Conditions unavailable: %1").arg(cond_error_.toHtmlEscaped()));
        return;
    }
    auto n = [&c](const char* k) {
        const QJsonValue v = c.value(QLatin1String(k));
        return v.isDouble() ? v.toDouble() : cnd::kNaN;
    };
    auto fmt = [](double v, int dp, bool sign, const QString& suffix = {}) {
        if (!std::isfinite(v))
            return QString(QChar(0x2014));
        return (sign && v > 0 ? QStringLiteral("+") : QString()) + QString::number(v, 'f', dp) + suffix;
    };
    QVector<cnd::DatedValue> closes;
    for (const auto& v : c.value("closes").toArray()) {
        const QJsonArray a = v.toArray();
        const QDate d = QDate::fromString(a.at(0).toString(), Qt::ISODate);
        if (d.isValid() && a.at(1).isDouble())
            closes.append({d, a.at(1).toDouble()});
    }
    const auto lr = cnd::live_reading(closes, cond_live_, core::bartime::market_today_et());
    const auto regime = cnd::regime_from_key(c.value("regime").toString());
    const QString rcol = regime == cnd::Regime::TrendUp     ? ui::colors::POSITIVE()
                         : regime == cnd::Regime::TrendDown ? ui::colors::NEGATIVE()
                         : regime == cnd::Regime::Range     ? ui::colors::CYAN()
                                                            : ui::colors::TEXT_SECONDARY();
    const QString sep = QStringLiteral("   ·   ");
    cond_->setText(QStringLiteral("vs 200D %1%2 1Y pctl %3%4 5Y pctl %5%6 RSI %7%8 ADX %9 "
                                  "<span style='color:%10;font-weight:700'>%11</span>%12 daily bars to %13")
                       .arg(fmt(lr.vs_sma200_pct, 1, true, QStringLiteral("%")), sep, fmt(lr.pctile_1y, 0, false), sep,
                            fmt(n("p5y"), 0, false), sep, fmt(n("rsi"), 0, false), sep, fmt(n("adx"), 0, false))
                       .arg(rcol, cnd::regime_label(regime), sep,
                            c.value("as_of").toString() +
                                (lr.live ? QStringLiteral(" (vs 200D, 1Y at the live price)") : QString())));
    cond_->show();
}

void MarketChartView::load() {
    const auto& def = period_def(prefs().period);
    canvas_->set_intraday(def.intraday);
    const int seq = ++request_seq_;
    canvas_->clear();
    overlay_->show_loading(QStringLiteral("LOADING %1…").arg(symbol_));
    last_->clear();
    change_->clear();
    readout_->clear();
    QPointer<MarketChartView> self = this;
    const QString sym = symbol_;
    if (services::ConditionService::is_derived(sym)) {
        // FRED / Treasury / correlation: one daily value per session, drawn
        // as a line (open = high = low = close), from the series the widget
        // rows read.
        canvas_->set_intraday(false);
        const QString period = prefs().period;
        services::ConditionService::instance().fetch_series(
            sym, [self, seq, period](bool ok, QVector<QPair<QDate, double>> pts, QString err) {
                if (!self || seq != self->request_seq_)
                    return;
                self->overlay_->hide_loading();
                if (!ok) {
                    self->canvas_->clear();
                    self->canvas_->set_placeholder_state(ResearchCandleCanvas::PlaceholderState::Error);
                    self->readout_->setText(err);
                    return;
                }
                const QDate from = pts.isEmpty() ? QDate() : derived_start(period, pts.last().first);
                QVector<services::equity::Candle> candles;
                for (const auto& p : pts) {
                    if (from.isValid() && p.first < from)
                        continue;
                    services::equity::Candle c;
                    // A session, stamped at its midnight (UTC) — decoded with date().
                    c.timestamp = QDateTime(p.first, QTime(0, 0), QTimeZone::utc()).toSecsSinceEpoch();
                    c.open = c.high = c.low = c.close = p.second;
                    candles.append(c);
                }
                // The canvas draws at most 1,500 bars: a long daily series
                // (25 years of the fed funds rate) is thinned evenly, the
                // newest value always kept.
                constexpr int kMaxBars = 1500;
                if (candles.size() > kMaxBars) {
                    const int step = static_cast<int>((candles.size() + kMaxBars - 1) / kMaxBars);
                    QVector<services::equity::Candle> thin;
                    for (int i = static_cast<int>(candles.size()) - 1; i >= 0; i -= step)
                        thin.prepend(candles[i]);
                    candles = thin;
                }
                self->apply(candles);
            });
        return;
    }
    services::MarketDataService::instance().fetch_bars(
        sym, QString::fromLatin1(def.period), QString::fromLatin1(def.interval),
        [self, seq](const services::MarketDataService::BarsReply& r) {
            // A later click (another period or symbol) supersedes this one.
            if (!self || seq != self->request_seq_)
                return;
            self->overlay_->hide_loading();
            if (!r.ok) {
                self->canvas_->clear();
                self->canvas_->set_placeholder_state(ResearchCandleCanvas::PlaceholderState::Error);
                return;
            }
            self->apply(r.bars);
        });
}

void MarketChartView::apply(const QVector<services::equity::Candle>& candles) {
    overlay_->hide_loading();
    if (candles.isEmpty()) {
        canvas_->clear();
        canvas_->set_placeholder_state(ResearchCandleCanvas::PlaceholderState::NoData);
        last_->clear();
        change_->clear();
        return;
    }
    canvas_->set_candles(candles, QString());
    update_header(-1);
}

void MarketChartView::update_header(int hover_idx) {
    const auto& c = canvas_->candles();
    if (c.isEmpty())
        return;
    const auto& last = c.last();
    last_->setText(services::ConditionService::is_derived(symbol_)
                       ? QString::number(last.close, 'f', std::abs(last.close) < 10 ? 3 : 2)
                       : price_text(last.close));
    // Change over the window shown: first bar's close to the last. Against the
    // first OPEN a 1D chart would quietly drop the overnight gap.
    const double base = c.first().close;
    if (services::ConditionService::is_derived(symbol_)) {
        // A rate, a spread or a correlation: the change is in its own units —
        // a percent of a spread (or of a negative correlation) means nothing.
        const double chg = last.close - base;
        change_->setText(QStringLiteral("%1%2  %3")
                             .arg(chg >= 0 ? "+" : "−", QString::number(std::abs(chg), 'f', std::abs(chg) < 10 ? 3 : 1),
                                  prefs().period));
        change_->setStyleSheet(QString("color:%1;font-size:14px;font-weight:700;font-family:monospace;"
                                       "background:transparent;")
                                   .arg(ui::colors::TEXT_PRIMARY()));
    } else if (base != 0.0) {
        const double chg = last.close - base;
        const double pct = chg / base * 100.0;
        change_->setText(QStringLiteral("%1%2  (%3%4%)  %5")
                             .arg(chg >= 0 ? "+" : "−", price_text(std::abs(chg)), pct >= 0 ? "+" : "−",
                                  QString::number(std::abs(pct), 'f', 2), prefs().period));
        change_->setStyleSheet(QString("color:%1;font-size:14px;font-weight:700;font-family:monospace;"
                                       "background:transparent;")
                                   .arg(chg >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));
    }
    const int i = hover_idx >= 0 && hover_idx < c.size() ? hover_idx : c.size() - 1;
    const auto& b = c[i];
    const bool intraday = period_def(prefs().period).intraday;
    // EVENT-STAMP: intraday bar — ET; daily bar — the exchange session.
    const QString when = intraday ? QDateTime::fromSecsSinceEpoch(b.timestamp)
                                        .toTimeZone(QTimeZone("America/New_York"))
                                        .toString(QStringLiteral("ddd d MMM HH:mm 'ET'"))
                                  : b.date().toString(QStringLiteral("ddd d MMM yyyy"));
    // A derived series (FRED, Treasury, correlation) has one value a session.
    QString text = services::ConditionService::is_derived(symbol_)
                       ? QStringLiteral("%1   %2").arg(when, QString::number(b.close, 'f', std::abs(b.close) < 10 ? 3 : 2))
                       : QStringLiteral("%1   O %2   H %3   L %4   C %5")
                             .arg(when, price_text(b.open), price_text(b.high), price_text(b.low), price_text(b.close));
    if (b.volume > 0)
        text += QStringLiteral("   Vol %1").arg(ui::formatting::format_compact_volume(b.volume));
    if (hover_idx < 0)
        text = QStringLiteral("Latest · ") + text;
    readout_->setText(text);
}

void MarketChartView::keyPressEvent(QKeyEvent* e) {
    // Number keys step through the periods.
    const int n = e->key() - Qt::Key_1;
    if (n >= 0 && n < static_cast<int>(std::size(kPeriods))) {
        prefs().period = QString::fromLatin1(kPeriods[n].label);
        sync_buttons();
        load();
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace finterm::screens

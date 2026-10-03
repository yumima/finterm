// src/screens/markets/MarketChartDialog.cpp
#include "screens/markets/MarketChartDialog.h"

#include "core/events/EventBus.h"
#include "screens/equity_research/EquityOverviewTab.h"
#include "services/markets/MarketDataService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimeZone>
#include <QVBoxLayout>

namespace fincept::screens {

QPointer<MarketChartDialog> MarketChartDialog::instance_;

namespace {

struct PeriodDef {
    const char* label;
    const char* period;     // yfinance period
    const char* interval;   // bar size
    bool intraday;
};
// Bar sizes keep every window inside the canvas's 1,500-bar budget: 5Y daily
// is ~1,260 bars, and MAX goes monthly because an index like the Dow has more
// than 1,500 weeks of history.
constexpr PeriodDef kPeriods[] = {
    {"1D", "1d", "5m", true},    {"5D", "5d", "15m", true},  {"1M", "1mo", "1d", false},
    {"3M", "3mo", "1d", false},  {"6M", "6mo", "1d", false}, {"YTD", "ytd", "1d", false},
    {"1Y", "1y", "1d", false},   {"5Y", "5y", "1d", false},  {"MAX", "max", "1mo", false},
};

const PeriodDef& period_def(const QString& label) {
    for (const auto& p : kPeriods)
        if (label == QLatin1String(p.label)) return p;
    return kPeriods[6];   // 1Y
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
    return !sym.startsWith('^') && !sym.contains('=') && !sym.endsWith(QLatin1String("-USD"));
}

} // namespace

void MarketChartDialog::show_for(const QString& symbol, const QString& label, QWidget* from) {
    if (symbol.trimmed().isEmpty())
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
    instance_->set_symbol(symbol.trimmed(), label);
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
    head->addStretch(1);
    auto* er_btn = make_button(QStringLiteral("OPEN IN RESEARCH"), ui::colors::AMBER(), false);
    er_btn->setObjectName(QStringLiteral("er"));
    connect(er_btn, &QPushButton::clicked, this, [this]() {
        EventBus::instance().publish("nav.split_alongside", QVariantMap{{"screen_id", "equity_research"}});
        EventBus::instance().publish("equity_research.load_symbol", QVariantMap{{"symbol", symbol_}});
    });
    head->addWidget(er_btn);
    auto* close_btn = make_button(QStringLiteral("✕"), ui::colors::AMBER(), false);
    close_btn->setToolTip(QStringLiteral("Close (Esc)"));
    connect(close_btn, &QPushButton::clicked, this, &QDialog::close);
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
    for (const auto& [style, text] : {std::pair{Style::Area, "AREA"}, std::pair{Style::Line, "LINE"},
                                      std::pair{Style::Candles, "CANDLES"}}) {
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
    struct Toggle { const char* key; const char* text; const char* color; const char* tip; };
    const Toggle toggles[] = {
        {"log", "LOG", "#94a3b8", "Logarithmic price scale — equal heights are equal percentage moves."},
        {"vol", "VOL", "#94a3b8", "Volume beneath the price. Indices and FX often report none."},
        {"sma20", "SMA20", "#3b82f6", "20-bar simple moving average."},
        {"sma50", "SMA50", "#a855f7", "50-bar simple moving average."},
        {"sma200", "SMA200", "#eab308", "200-bar simple moving average."},
    };
    for (const auto& t : toggles) {
        const QString key = QString::fromLatin1(t.key);
        auto* b = make_button(QString::fromLatin1(t.text), QString::fromLatin1(t.color));
        b->setToolTip(QString::fromLatin1(t.tip));
        connect(b, &QPushButton::toggled, this, [this, key](bool on) {
            auto& pr = prefs();
            if (key == "log") { pr.log = on; canvas_->set_log_scale(on); }
            else if (key == "vol") { pr.vol = on; canvas_->set_show_volume(on); }
            else if (key == "sma20") { pr.sma20 = on; canvas_->set_show_sma(20, on); }
            else if (key == "sma50") { pr.sma50 = on; canvas_->set_show_sma(50, on); }
            else if (key == "sma200") { pr.sma200 = on; canvas_->set_show_sma(200, on); }
        });
        toggle_btns_.insert(key, b);
        ctl->addWidget(b);
    }
    ctl->addStretch(1);
    root->addLayout(ctl);

    // ── Crosshair readout ────────────────────────────────────────────────────
    readout_ = make_label(ui::colors::TEXT_SECONDARY(), 12, false, true);
    root->addWidget(readout_);

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
    sync_buttons();
}

void MarketChartDialog::sync_buttons() {
    const auto& pr = prefs();
    for (auto it = period_btns_.begin(); it != period_btns_.end(); ++it)
        it.value()->setChecked(it.key() == pr.period);
    for (auto it = style_btns_.begin(); it != style_btns_.end(); ++it)
        it.value()->setChecked(it.key() == pr.style);
    const QHash<QString, bool> on{{"log", pr.log}, {"vol", pr.vol}, {"sma20", pr.sma20},
                                  {"sma50", pr.sma50}, {"sma200", pr.sma200}};
    for (auto it = toggle_btns_.begin(); it != toggle_btns_.end(); ++it) {
        const QSignalBlocker block(it.value());   // state only; the canvas already agrees
        it.value()->setChecked(on.value(it.key()));
    }
}

void MarketChartDialog::set_symbol(const QString& symbol, const QString& label) {
    const bool changed = symbol != symbol_;
    symbol_ = symbol;
    label_ = label.isEmpty() ? symbol : label;
    setWindowTitle(QStringLiteral("%1 — chart").arg(label_));
    title_->setText(label_ == symbol_ ? symbol_ : QStringLiteral("%1  <span style='color:%2;font-size:13px'>%3</span>")
                                                      .arg(label_.toHtmlEscaped(), ui::colors::TEXT_TERTIARY(),
                                                           symbol_.toHtmlEscaped()));
    if (auto* er = findChild<QPushButton*>(QStringLiteral("er")))
        er->setVisible(has_research_page(symbol_));
    if (changed)
        load();
}

void MarketChartDialog::load() {
    const auto& def = period_def(prefs().period);
    canvas_->set_intraday(def.intraday);
    const QString key = symbol_ + QLatin1Char('|') + QLatin1String(def.label);
    const int seq = ++request_seq_;
    if (cache_.contains(key)) {
        apply(cache_.value(key));
        return;
    }
    canvas_->clear();
    overlay_->show_loading(QStringLiteral("LOADING %1…").arg(symbol_));
    last_->clear();
    change_->clear();
    readout_->clear();
    QPointer<MarketChartDialog> self = this;
    const QString sym = symbol_;
    services::MarketDataService::instance().fetch_history(
        sym, QString::fromLatin1(def.period), QString::fromLatin1(def.interval),
        [self, seq, key](bool ok, QVector<services::HistoryPoint> pts) {
            // A later click (another period or symbol) supersedes this one.
            if (!self || seq != self->request_seq_)
                return;
            self->overlay_->hide_loading();
            if (!ok) {
                self->canvas_->clear();
                self->canvas_->set_placeholder_state(ResearchCandleCanvas::PlaceholderState::Error);
                return;
            }
            QVector<services::equity::Candle> candles;
            candles.reserve(pts.size());
            for (const auto& p : pts) {
                services::equity::Candle c;
                c.timestamp = p.timestamp;
                c.open = p.open;
                c.high = p.high;
                c.low = p.low;
                c.close = p.close;
                c.volume = p.volume;
                candles.append(c);
            }
            if (!candles.isEmpty())
                self->cache_.insert(key, candles);
            self->apply(candles);
        });
}

void MarketChartDialog::apply(const QVector<services::equity::Candle>& candles) {
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

void MarketChartDialog::update_header(int hover_idx) {
    const auto& c = canvas_->candles();
    if (c.isEmpty())
        return;
    const auto& last = c.last();
    last_->setText(price_text(last.close));
    // Change over the window shown: first bar's close to the last. Against the
    // first OPEN a 1D chart would quietly drop the overnight gap.
    const double base = c.first().close;
    if (base != 0.0) {
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
    QString text = QStringLiteral("%1   O %2   H %3   L %4   C %5")
                       .arg(when, price_text(b.open), price_text(b.high), price_text(b.low), price_text(b.close));
    if (b.volume > 0)
        text += QStringLiteral("   Vol %1").arg(ui::formatting::format_compact_volume(b.volume));
    if (hover_idx < 0)
        text = QStringLiteral("Latest · ") + text;
    readout_->setText(text);
}

void MarketChartDialog::keyPressEvent(QKeyEvent* e) {
    // Number keys step through the periods; Esc closes (QDialog's default).
    const int n = e->key() - Qt::Key_1;
    if (n >= 0 && n < static_cast<int>(std::size(kPeriods))) {
        prefs().period = QString::fromLatin1(kPeriods[n].label);
        sync_buttons();
        load();
        return;
    }
    QDialog::keyPressEvent(e);
}

} // namespace fincept::screens

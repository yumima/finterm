#include "screens/portfolio/PortfolioFuturesView.h"

#include "python/PythonWorker.h"
#include "screens/futures/FuturesContracts.h"
#include "screens/futures/FuturesQuoteCache.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

namespace {

QString lbl_ss(const QString& color, bool bold = false, int px = -1) {
    using namespace ui;
    const int sz = (px > 0) ? px : fonts::font_px(-2);
    return QString("color:%1;background:transparent;font-size:%2px;font-family:'%3';%4")
        .arg(color).arg(sz).arg(fonts::DATA_FAMILY()).arg(bold ? "font-weight:bold;" : "");
}

QString fmt_num(double v, int decimals = 2) {
    if (!std::isfinite(v)) return "—";
    return QString::number(v, 'f', decimals);
}

QString fmt_signed(double v, int decimals = 2) {
    if (!std::isfinite(v)) return "—";
    return (v > 0 ? "+" : "") + QString::number(v, 'f', decimals);
}

QString fmt_qty(double v) {
    if (!std::isfinite(v)) return "—";
    return QString::number(v, 'f', v == std::trunc(v) ? 0 : 4);
}

QString color_for_change(double v) {
    using namespace ui;
    if (v > 0) return colors::POSITIVE();
    if (v < 0) return colors::NEGATIVE();
    return colors::TEXT_SECONDARY();
}

QString table_ss() {
    using namespace ui;
    return QString(
        "QTableWidget { background:%1; color:%2; gridline-color:%3; border:none;"
        " font-family:'%4'; font-size:%5px; alternate-background-color:%6; }"
        "QHeaderView::section { background:%7; color:%2; padding:4px 6px;"
        " border:none; border-bottom:1px solid %3; font-weight:600; }")
        .arg(colors::BG_BASE())
        .arg(colors::TEXT_PRIMARY())
        .arg(colors::BORDER_DIM())
        .arg(fonts::DATA_FAMILY())
        .arg(fonts::font_px(-2))
        .arg(colors::BG_SURFACE())
        .arg(colors::BG_RAISED());
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

bool PortfolioFuturesView::looks_like_future(const QString& symbol) {
    if (symbol.isEmpty()) return false;
    // yfinance continuous future
    if (symbol.endsWith("=F", Qt::CaseInsensitive)) return true;
    // TradingView style "/ES"
    if (symbol.startsWith('/')) return true;
    // Catalog root match (ES, NQ, CL, etc.)
    if (futures::find_contract(symbol.toUpper())) return true;
    // CME contract month form: 2-3 letters + month code (FGHJKMNQUVXZ) + 1-2 digits.
    static const QRegularExpression re("^[A-Z0-9]{1,3}[FGHJKMNQUVXZ]\\d{1,2}$");
    if (re.match(symbol.toUpper()).hasMatch()) return true;
    return false;
}

QString PortfolioFuturesView::futures_root(const QString& symbol) {
    QString s = symbol.toUpper();
    if (s.startsWith('/')) s = s.mid(1);
    if (s.endsWith("=F")) s.chop(2);
    static const QRegularExpression re("^([A-Z0-9]{1,3})[FGHJKMNQUVXZ]\\d{1,2}$");
    auto m = re.match(s);
    if (m.hasMatch()) return m.captured(1);
    return s;
}

namespace {
// Smoke test for symbol classification — runs once at process start to catch
// regressions in `looks_like_future` / `futures_root` before the UI uses them.
// Failures only land in DEBUG builds (Q_ASSERT is a no-op in release).
struct SymbolClassificationSelfTest {
    SymbolClassificationSelfTest() {
        Q_ASSERT(PortfolioFuturesView::looks_like_future("ES"));
        Q_ASSERT(PortfolioFuturesView::looks_like_future("ES=F"));
        Q_ASSERT(PortfolioFuturesView::looks_like_future("/ES"));
        Q_ASSERT(PortfolioFuturesView::looks_like_future("ESM6"));
        Q_ASSERT(!PortfolioFuturesView::looks_like_future("AAPL"));
        Q_ASSERT(!PortfolioFuturesView::looks_like_future(""));
        Q_ASSERT(PortfolioFuturesView::futures_root("ES") == "ES");
        Q_ASSERT(PortfolioFuturesView::futures_root("ES=F") == "ES");
        Q_ASSERT(PortfolioFuturesView::futures_root("/ES") == "ES");
        Q_ASSERT(PortfolioFuturesView::futures_root("ESM6") == "ES");
    }
};
static const SymbolClassificationSelfTest _symbol_test_;
} // namespace

// ─────────────────────────────────────────────────────────────────────────────

PortfolioFuturesView::PortfolioFuturesView(QWidget* parent) : QWidget(parent) {
    build_ui();
    connect(&futures::FuturesQuoteCache::instance(),
            &futures::FuturesQuoteCache::quotes_updated,
            this, &PortfolioFuturesView::on_quote_cache_updated);
    // Periodic extended-hours refresh. Without this the table only
    // updated when PortfolioService re-emitted a summary, which is gated
    // by a 5-minute TTL cache — so live pre/post-market prints would
    // sit stale until the user manually refreshed. 20 s matches the
    // futures cache cadence so both tables tick together.
    ext_refresh_timer_ = new QTimer(this);
    ext_refresh_timer_->setInterval(kExtRefreshIntervalMs);
    connect(ext_refresh_timer_, &QTimer::timeout, this, [this]() {
        // Timer-driven refreshes skip if one is already in flight — a
        // slow yfinance response shouldn't cause request inflation.
        // set_summary's direct refresh bypasses this guard so new
        // holdings still get fresh data immediately on arrival.
        if (ext_in_flight_) return;
        refresh_extended_hours();
    });
    // Hydrate ext_quotes_ from the disk snapshot so the table paints
    // with stale-but-real values on first paint, instead of "—"
    // everywhere until the first 20 s refresh returns.
    load_ext_cache();
}

void PortfolioFuturesView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Subscribe to live futures quotes only while the view is visible.
    // The cache reference-counts subscribers so the timer pauses when
    // every consumer is hidden.
    futures::FuturesQuoteCache::instance().retain();
    // Start the extended-hours refresh tick while visible. set_summary
    // (which runs whenever the parent emits a new summary) still fires
    // an immediate refresh; this timer keeps it ticking even when no
    // new summary arrives.
    if (ext_refresh_timer_ && !ext_refresh_timer_->isActive()) {
        ext_refresh_timer_->start();
    }
}

void PortfolioFuturesView::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    futures::FuturesQuoteCache::instance().release();
    if (ext_refresh_timer_) ext_refresh_timer_->stop();
}

void PortfolioFuturesView::build_ui() {
    using namespace ui;
    setStyleSheet(QString("background:%1;").arg(colors::BG_BASE()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── Section 1: Futures positions ────────────────────────────────────────
    {
        auto* hdr = new QHBoxLayout;
        auto* title = new QLabel("FUTURES POSITIONS");
        title->setStyleSheet(lbl_ss(colors::AMBER(), true, fonts::font_px(1)));
        hdr->addWidget(title);
        hdr->addStretch();
        futures_status_ = new QLabel;
        futures_status_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
        hdr->addWidget(futures_status_);
        root->addLayout(hdr);

        futures_table_ = new QTableWidget(0, 9, this);
        futures_table_->setHorizontalHeaderLabels(
            {"Symbol", "Name", "Qty", "Avg Cost", "Last", "Chg %", "Volume", "Market Value", "Unrealized P&L"});
        futures_table_->verticalHeader()->setVisible(false);
        futures_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        futures_table_->setShowGrid(false);
        futures_table_->setAlternatingRowColors(true);
        futures_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        futures_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        futures_table_->setStyleSheet(table_ss());
        root->addWidget(futures_table_, 1);
    }

    // ── Section 2: Extended hours ───────────────────────────────────────────
    {
        auto* hdr = new QHBoxLayout;
        auto* title = new QLabel("EXTENDED HOURS · PRE / POST MARKET");
        title->setStyleSheet(lbl_ss(colors::AMBER(), true, fonts::font_px(1)));
        hdr->addWidget(title);
        hdr->addStretch();
        extended_status_ = new QLabel;
        extended_status_->setStyleSheet(lbl_ss(colors::TEXT_SECONDARY(), false, fonts::font_px(-3)));
        hdr->addWidget(extended_status_);
        root->addLayout(hdr);

        extended_table_ = new QTableWidget(0, 7, this);
        extended_table_->setHorizontalHeaderLabels(
            {"Symbol", "Reg Close", "Ext Last", "Δ", "Δ%", "Session", "Mkt Value Δ"});
        extended_table_->verticalHeader()->setVisible(false);
        extended_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        extended_table_->setShowGrid(false);
        extended_table_->setAlternatingRowColors(true);
        extended_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        extended_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        extended_table_->setStyleSheet(table_ss());
        root->addWidget(extended_table_, 1);
    }
}

void PortfolioFuturesView::set_summary(const portfolio::PortfolioSummary& summary, const QString& currency) {
    summary_ = summary;
    if (!currency.isEmpty()) currency_ = currency;
    populate_futures();
    populate_extended();
    refresh_extended_hours();
    // Kick the futures cache so live quotes for futures positions stay fresh.
    futures::FuturesQuoteCache::instance().refresh();
}

void PortfolioFuturesView::on_quote_cache_updated() {
    populate_futures();
}

// ── Futures positions ──────────────────────────────────────────────────────

void PortfolioFuturesView::populate_futures() {
    using namespace ui;

    // Filter holdings to futures-like symbols.
    QVector<portfolio::HoldingWithQuote> futures_holdings;
    for (const auto& h : summary_.holdings) {
        if (looks_like_future(h.symbol)) futures_holdings.append(h);
    }

    if (futures_holdings.isEmpty()) {
        futures_table_->setRowCount(0);
        futures_status_->setText("no futures positions");
        return;
    }

    auto& cache = futures::FuturesQuoteCache::instance();
    futures_status_->setText(cache.has_data()
                                  ? QString("live · %1").arg(cache.last_refresh().toString("HH:mm:ss"))
                                  : "loading quotes…");

    futures_table_->setRowCount(futures_holdings.size());
    for (int i = 0; i < futures_holdings.size(); ++i) {
        const auto& h = futures_holdings[i];
        const QString root = futures_root(h.symbol);

        // Live data preference: catalog cache first, fall back to portfolio engine.
        double last = h.current_price;
        double chg_pct = h.day_change_percent;
        double volume = 0;
        QString display_name = h.symbol;
        for (const auto& q : cache.all_quotes()) {
            if (q.symbol == root) {
                last = q.last;
                chg_pct = q.change_pct;
                volume = q.volume;
                display_name = q.name;
                break;
            }
        }

        const double mv = last * h.quantity;
        const double pnl = mv - (h.avg_buy_price * h.quantity);

        auto add = [&](int col, const QString& text, const QString& color = QString()) {
            auto* it = new QTableWidgetItem(text);
            if (!color.isEmpty()) it->setForeground(QColor(color));
            if (col >= 2) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            futures_table_->setItem(i, col, it);
        };
        add(0, h.symbol);
        add(1, display_name);
        add(2, fmt_qty(h.quantity));
        add(3, fmt_num(h.avg_buy_price, 4));
        add(4, fmt_num(last, 4));
        add(5, fmt_signed(chg_pct, 2) + "%", color_for_change(chg_pct));
        // Unified K/M/B/T at one decimal via the shared layer (was M@2dp, K@1dp,
        // and lacked a B tier). 0 renders as "0"; only NaN/inf/negative → "—".
        add(6, ui::formatting::format_compact_volume(volume));
        add(7, fmt_num(mv, 2));
        add(8, fmt_signed(pnl, 2), color_for_change(pnl));
    }
}

// ── Extended hours ─────────────────────────────────────────────────────────

void PortfolioFuturesView::populate_extended() {
    using namespace ui;

    if (summary_.holdings.isEmpty()) {
        extended_table_->setRowCount(0);
        extended_status_->setText("no holdings");
        return;
    }

    extended_table_->setRowCount(summary_.holdings.size());
    for (int i = 0; i < summary_.holdings.size(); ++i) {
        const auto& h = summary_.holdings[i];
        const auto it = ext_quotes_.constFind(h.symbol);
        const bool have = (it != ext_quotes_.constEnd() && it->has_ext);

        const double regular = have ? it->regular : h.current_price;
        const double ext = have ? it->ext_price : 0.0;
        const double chg = have ? it->ext_change : 0.0;
        const double pct = have ? it->ext_change_pct : 0.0;
        const QString session = have ? it->session : QString("—");
        const double mv_delta = have ? (chg * h.quantity) : 0.0;

        auto add = [&](int col, const QString& text, const QString& color = QString()) {
            auto* widg = new QTableWidgetItem(text);
            if (!color.isEmpty()) widg->setForeground(QColor(color));
            if (col >= 1) widg->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            extended_table_->setItem(i, col, widg);
        };
        add(0, h.symbol);
        add(1, fmt_num(regular, 2));
        add(2, have ? fmt_num(ext, 2) : QString("—"));
        add(3, have ? fmt_signed(chg, 2) : QString("—"), color_for_change(chg));
        add(4, have ? fmt_signed(pct, 2) + "%" : QString("—"), color_for_change(chg));
        add(5, session);
        add(6, have ? fmt_signed(mv_delta, 2) : QString("—"), color_for_change(mv_delta));
    }
}

void PortfolioFuturesView::refresh_extended_hours() {
    if (summary_.holdings.isEmpty()) {
        extended_status_->setText("no holdings");
        return;
    }

    QJsonArray syms;
    for (const auto& h : summary_.holdings) syms.append(h.symbol);

    extended_status_->setText("loading ext hours…");
    const quint64 my_gen = ++ext_gen_;
    ext_in_flight_ = true;

    QJsonObject payload;
    payload.insert("symbols", syms);

    // Persistent yfinance daemon — saves the ~1.5s pandas/yfinance import
    // we'd otherwise pay every time the user opens this view.
    python::PythonWorker::instance().submit(
        "extended_hours", payload,
        [this, my_gen](bool ok, QJsonObject result, QString /*err*/) {
            ext_in_flight_ = false;  // release first, before any early-return
            if (my_gen != ext_gen_) return;  // superseded
            if (!ok) {
                extended_status_->setText("ext hours error");
                return;
            }
            // Daemon returns a flat array; PythonWorker wraps as {_value: [...]}.
            const QJsonArray rows = result.contains("_value")
                                         ? result.value("_value").toArray()
                                         : result.value("data").toArray();
            // Merge (don't clear): if yfinance dropped a symbol from this
            // round, the previous value stays in ext_quotes_ rather than
            // disappearing entirely. Matches FuturesQuoteCache::refresh's
            // pattern and avoids disk-durable data loss on the next save.
            for (const auto& v : rows) {
                const auto o = v.toObject();
                ExtQuote q;
                q.regular = o.value("regular").toDouble();
                const auto ext_val = o.value("ext_price");
                if (ext_val.isNull() || ext_val.isUndefined()) {
                    q.has_ext = false;
                } else {
                    q.has_ext = true;
                    q.ext_price = ext_val.toDouble();
                    q.ext_change = o.value("ext_change").toDouble();
                    q.ext_change_pct = o.value("ext_change_pct").toDouble();
                }
                q.session = o.value("session").toString();
                ext_quotes_.insert(o.value("symbol").toString(), q);
            }
            extended_status_->setText(QString("yfinance · %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
            // Skip the disk write when the response had no rows — that
            // path is rare (only happens if the symbols list was empty
            // or the daemon errored upstream) and would otherwise no-op
            // overwrite a perfectly good prior snapshot.
            if (!rows.isEmpty()) save_ext_cache();
            populate_extended();
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

QString PortfolioFuturesView::ext_cache_path() const {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString dir  = root + "/cache/portfolio_ext";
    QDir().mkpath(dir);
    return dir + "/quotes.json";
}

void PortfolioFuturesView::save_ext_cache() const {
    QJsonArray arr;
    for (auto it = ext_quotes_.cbegin(); it != ext_quotes_.cend(); ++it) {
        const auto& q = it.value();
        QJsonObject o;
        o.insert("symbol",         it.key());
        o.insert("regular",        q.regular);
        o.insert("has_ext",        q.has_ext);
        if (q.has_ext) {
            o.insert("ext_price",      q.ext_price);
            o.insert("ext_change",     q.ext_change);
            o.insert("ext_change_pct", q.ext_change_pct);
        }
        o.insert("session",        q.session);
        arr.append(o);
    }
    QJsonObject root;
    root.insert("saved_at", QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert("quotes",   arr);

    // Atomic write: QSaveFile commits on success, otherwise leaves the
    // previous snapshot intact.
    QSaveFile f(ext_cache_path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void PortfolioFuturesView::load_ext_cache() {
    QFile f(ext_cache_path());
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto bytes = f.readAll();
    f.close();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) return;
    const auto arr = doc.object().value("quotes").toArray();
    for (const auto& v : arr) {
        const auto o = v.toObject();
        const QString sym = o.value("symbol").toString();
        if (sym.isEmpty()) continue;
        ExtQuote q;
        q.regular         = o.value("regular").toDouble();
        q.has_ext         = o.value("has_ext").toBool();
        q.ext_price       = o.value("ext_price").toDouble();
        q.ext_change      = o.value("ext_change").toDouble();
        q.ext_change_pct  = o.value("ext_change_pct").toDouble();
        q.session         = o.value("session").toString();
        ext_quotes_.insert(sym, q);
    }
}

} // namespace fincept::screens

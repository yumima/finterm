#include "screens/dashboard/widgets/ConditionTableWidget.h"

#include "core/util/BarTime.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "screens/dashboard/widgets/ConditionAddDialog.h"
#include "screens/markets/MarketChartDialog.h"
#include "services/markets/ConditionService.h"
#include "services/markets/LastKnownQuote.h"
#include "services/markets/MarketConditions.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/tables/DataTable.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QTimer>

#include <cmath>

namespace finterm::screens::widgets {

namespace cnd = services::conditions;
using services::ConditionService;

namespace {

constexpr const char* kAddText = "+  ADD";
constexpr const char* kRemoveText = "−";

QString dash() {
    return ui::formatting::placeholder();
}

double jnum(const QJsonObject& o, const char* k) {
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toDouble() : cnd::kNaN;
}

QString num(double v, int dp) {
    if (!std::isfinite(v))
        return dash();
    return QLocale(QLocale::English).toString(v, 'f', dp);
}

QString signed_num(double v, int dp, const QString& suffix = {}) {
    if (!std::isfinite(v))
        return dash();
    const QString body = QLocale(QLocale::English).toString(std::abs(v), 'f', dp);
    if (body.toDouble() == 0.0)
        return body + suffix;
    return (v > 0 ? QStringLiteral("+") : QString(QChar(0x2212))) + body + suffix;
}

int price_dp(double v) {
    return std::isfinite(v) && std::abs(v) < 10 ? 4 : 2;
}

QVector<cnd::DatedValue> points_of(const QJsonObject& o) {
    QVector<cnd::DatedValue> out;
    for (const auto& v : o.value(QLatin1String("points")).toArray()) {
        const QJsonArray a = v.toArray();
        const QDate d = QDate::fromString(a.at(0).toString(), Qt::ISODate);
        if (d.isValid() && a.at(1).isDouble())
            out.append({d, a.at(1).toDouble()});
    }
    return out;
}

/// Percent rank of the latest value among the trailing `years` of points;
/// NaN unless the series reaches back that far.
double trailing_rank(const QVector<cnd::DatedValue>& xs, int years) {
    if (xs.isEmpty())
        return cnd::kNaN;
    const QDate start = xs.last().date.addYears(-years);
    if (xs.first().date > start.addDays(years >= 10 ? 120 : 10))
        return cnd::kNaN;
    QVector<double> w;
    for (const auto& x : xs)
        if (x.date > start)
            w.append(x.value);
    return cnd::percent_rank(w, xs.last().value);
}

/// "2026-11-19", "Oct 2026" (monthly), "Q2 2026" (quarterly).
QString period_text(const QDate& d, const QDate& prev) {
    if (!d.isValid())
        return dash();
    const qint64 gap = prev.isValid() ? prev.daysTo(d) : 1;
    if (gap >= 80)
        return QStringLiteral("Q%1 %2").arg((d.month() - 1) / 3 + 1).arg(d.year());
    if (gap >= 28)
        return QLocale(QLocale::English).toString(d, QStringLiteral("MMM yyyy"));
    return d.toString(Qt::ISODate);
}

QString age_text(const QJsonObject& o) {
    const double t = jnum(o, "t");
    if (!std::isfinite(t))
        return {};
    // EVENT-STAMP: when the answer was fetched — a real instant, local time.
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(t)).toString(QStringLiteral("d MMM HH:mm"));
}

QString pctile_color(double p) {
    if (!std::isfinite(p))
        return ui::colors::TEXT_TERTIARY();
    if (p >= 90 || p <= 10)
        return ui::colors::AMBER(); // at an extreme of its own history
    return ui::colors::TEXT_PRIMARY();
}

QString regime_color(cnd::Regime r) {
    switch (r) {
        case cnd::Regime::TrendUp:
            return ui::colors::POSITIVE();
        case cnd::Regime::TrendDown:
            return ui::colors::NEGATIVE();
        case cnd::Regime::Range:
            return ui::colors::CYAN();
        default:
            return ui::colors::TEXT_SECONDARY();
    }
}

} // namespace

ConditionTableWidget::ConditionTableWidget(const QString& title, Mode mode, QVector<ConditionRowDef> defaults,
                                           const QJsonObject& cfg, const QString& accent, QWidget* parent)
    : BaseWidget(title, parent, accent), mode_(mode), defaults_(std::move(defaults)) {
    ConditionService::instance().ensure_registered_with_hub();
    table_ = new ui::DataTable;
    if (mode_ == Mode::Market)
        table_->set_headers({"NAME", "LAST", "CHG%", "vs200D", "1Y PCTL", "TREND", ""});
    else
        table_->set_headers({"NAME", "LATEST", "CHG", "AS OF", "10Y PCTL", ""});
    auto* h = table_->horizontalHeader();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(QHeaderView::ResizeToContents);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    if (auto* it = table_->horizontalHeaderItem(3))
        it->setToolTip(mode_ == Mode::Market
                           ? QStringLiteral("Last daily close against its 200-day simple moving average.")
                           : QStringLiteral("The period the latest observation is for."));
    if (auto* it = table_->horizontalHeaderItem(4))
        it->setToolTip(mode_ == Mode::Market
                           ? QStringLiteral("Where the last daily close ranks among the past year's closes (0 = "
                                            "lowest, 100 = highest). Amber at or beyond 10 / 90.")
                           : QStringLiteral("Where the latest value ranks among the past ten years' values. Amber "
                                            "at or beyond 10 / 90; — when the series is shorter than ten years."));
    if (mode_ == Mode::Market)
        if (auto* it = table_->horizontalHeaderItem(5))
            it->setToolTip(QStringLiteral("ADX(14) on daily bars: TREND ↑/↓ at 25 or above (direction from ±DI), "
                                          "RANGE below 20 (mean-reverting conditions), MIXED between."));
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableWidget::cellClicked, this, &ConditionTableWidget::on_cell_clicked);
    connect(table_, &QWidget::customContextMenuRequested, this, &ConditionTableWidget::on_context_menu);
    content_layout()->addWidget(table_, 1);

    footer_ = new QLabel;
    footer_->setWordWrap(true);
    footer_->hide();
    content_layout()->addWidget(footer_);
    note_ = new QLabel;
    note_->setWordWrap(true);
    note_->hide();
    content_layout()->addWidget(note_);
    restyle_labels();

    stale_timer_ = new QTimer(this);
    stale_timer_->setInterval(30'000); // a quote turning stale has no delivery of its own
    connect(stale_timer_, &QTimer::timeout, this, [this]() { render_.request(); });
    connect(this, &BaseWidget::refresh_requested, this, [this]() {
        QStringList topics;
        for (const auto& r : rows_) {
            topics << topic_for(r.symbol);
            if (!cnd::is_fred_key(r.symbol))
                topics << QStringLiteral("market:quote:") + r.symbol;
        }
        datahub::DataHub::instance().request(topics, /*force=*/true);
    });
    apply_config(cfg);
}

QString ConditionTableWidget::topic_for(const QString& sym) const {
    if (cnd::is_fred_key(sym))
        return ConditionService::fred_topic(sym.mid(5).trimmed().toUpper());
    return ConditionService::stats_topic(sym);
}

QString ConditionTableWidget::row_label(const ConditionRowDef& r) const {
    if (!r.label.isEmpty())
        return r.label;
    const QString known = ConditionService::derived_label(r.symbol);
    return known.isEmpty() ? r.symbol : known;
}

QJsonObject ConditionTableWidget::config() const {
    QJsonArray arr;
    for (const auto& r : rows_)
        arr.append(QJsonObject{{"symbol", r.symbol}, {"label", r.label}});
    return QJsonObject{{"rows", arr}};
}

void ConditionTableWidget::apply_config(const QJsonObject& cfg) {
    QVector<ConditionRowDef> rows;
    if (cfg.contains(QLatin1String("rows"))) {
        for (const auto& v : cfg.value(QLatin1String("rows")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString sym = o.value("symbol").toString().trimmed();
            if (!sym.isEmpty())
                rows.append({sym, o.value("label").toString()});
        }
    } else {
        rows = defaults_; // a fresh tile: the list the doc asked for
    }
    const bool was = subscribed_;
    if (was)
        unsubscribe_all();
    rows_ = rows;
    if (was)
        subscribe_all();
    render_.request();
}

void ConditionTableWidget::set_footer(const QStringList& extra, FooterFn fn) {
    footer_syms_ = extra;
    footer_fn_ = std::move(fn);
    if (subscribed_) {
        unsubscribe_all();
        subscribe_all();
    }
}

void ConditionTableWidget::set_note(const QString& note) {
    note_->setText(note);
    note_->setVisible(!note.isEmpty());
}

void ConditionTableWidget::restyle_labels() {
    const QString ss = QString("color:%1;font-size:11px;padding:2px 6px;background:transparent;");
    footer_->setStyleSheet(ss.arg(ui::colors::TEXT_SECONDARY()));
    note_->setStyleSheet(ss.arg(ui::colors::TEXT_TERTIARY()));
}

void ConditionTableWidget::on_theme_changed() {
    restyle_labels();
    render_.request();
}

void ConditionTableWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!subscribed_)
        subscribe_all();
    stale_timer_->start();
}

void ConditionTableWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (subscribed_)
        unsubscribe_all();
    stale_timer_->stop();
}

void ConditionTableWidget::subscribe_all() {
    auto& hub = datahub::DataHub::instance();
    auto& svc = ConditionService::instance();
    QStringList quote_syms;
    for (const auto& r : rows_)
        if (!cnd::is_fred_key(r.symbol) && !quote_syms.contains(r.symbol))
            quote_syms << r.symbol;
    for (const auto& s : footer_syms_)
        if (!quote_syms.contains(s))
            quote_syms << s;

    // Cache first: whatever is known paints now; the hub's answers follow.
    services::seed_last_known_quotes(quote_syms, quotes_);
    for (const auto& r : rows_) {
        const QString t = topic_for(r.symbol);
        if (!cond_.contains(t)) {
            const QJsonObject held = svc.last_known(t);
            if (!held.isEmpty())
                cond_.insert(t, held);
        }
    }

    QPointer<ConditionTableWidget> self = this;
    for (const auto& sym : quote_syms) {
        hub.subscribe(this, QStringLiteral("market:quote:") + sym, [self, sym](const QVariant& v) {
            if (!self || !v.canConvert<services::QuoteData>())
                return;
            self->quotes_.insert(sym, v.value<services::QuoteData>());
            self->render_.request();
        });
    }
    for (const auto& r : rows_) {
        const QString t = topic_for(r.symbol);
        hub.subscribe(this, t, [self, t](const QVariant& v) {
            if (!self || !v.canConvert<QJsonObject>())
                return;
            self->cond_.insert(t, v.toJsonObject());
            self->cond_fresh_.insert(t);
            self->cond_error_.remove(t);
            self->render_.request();
        });
        hub.subscribe_errors(this, t, [self, t](const QString& err) {
            if (!self)
                return;
            self->cond_error_.insert(t, err);
            self->render_.request();
        });
    }
    subscribed_ = true;
    set_loading(false);
    render_.request();
}

void ConditionTableWidget::unsubscribe_all() {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    for (const auto& r : rows_)
        hub.unsubscribe_errors(this, topic_for(r.symbol));
    subscribed_ = false;
}

void ConditionTableWidget::render() {
    if (!table_)
        return;
    QVector<QStringList> rows;
    rows.reserve(rows_.size() + 1);
    const int ncols = mode_ == Mode::Market ? 7 : 6;
    struct Paint {
        QString c[7];
        QString tip;
        bool dim = false;
    };
    QVector<Paint> paints;
    for (const auto& r : rows_) {
        const QString t = topic_for(r.symbol);
        const QJsonObject c = cond_.value(t);
        const bool fresh = cond_fresh_.contains(t);
        const QString err = cond_error_.value(t);
        QStringList cells(ncols);
        Paint p;
        cells[0] = row_label(r);
        cells[ncols - 1] = QString::fromUtf8(kRemoveText);
        p.c[0] = ui::colors::TEXT_PRIMARY();
        p.c[ncols - 1] = ui::colors::TEXT_TERTIARY();
        QStringList tip{QStringLiteral("<b>%1</b> <span style='color:%2'>%3</span>")
                            .arg(row_label(r).toHtmlEscaped(), ui::colors::TEXT_TERTIARY(), r.symbol.toHtmlEscaped())};

        if (cnd::is_fred_key(r.symbol)) {
            const auto pts = points_of(c);
            const auto ls = cnd::level_stats(pts);
            if (mode_ == Mode::Market) {
                // A FRED row here is a spread or rate in percent: its move is
                // shown in bp, uncoloured (a widening spread is not "up = good").
                cells[1] = num(ls.last, 2);
                cells[2] = signed_num(ls.change() * 100.0, std::abs(ls.change() * 100.0) < 10 ? 1 : 0,
                                      QStringLiteral("bp"));
                cells[3] = dash();
                const double p1 = trailing_rank(pts, 1);
                cells[4] = std::isfinite(p1) ? QString::number(std::lround(p1)) : dash();
                cells[5] = dash();
                p.c[1] = ui::colors::WHITE();
                p.c[2] = ui::colors::TEXT_SECONDARY();
                p.c[4] = pctile_color(p1);
            } else {
                cells[1] = num(ls.last, 2) + (r.symbol.toUpper().endsWith(QLatin1String("@YOY")) && ls.valid()
                                                  ? QStringLiteral("%")
                                                  : QString());
                cells[2] = signed_num(ls.change(), 2);
                cells[3] = period_text(ls.as_of, ls.prev_date);
                cells[4] = std::isfinite(ls.pctile_10y) ? QString::number(std::lround(ls.pctile_10y)) : dash();
                p.c[1] = ui::colors::WHITE();
                p.c[2] = ui::colors::TEXT_SECONDARY();
                p.c[3] = ui::colors::TEXT_SECONDARY();
                p.c[4] = pctile_color(ls.pctile_10y);
            }
            if (ls.valid())
                tip << QStringLiteral("Latest %1 for %2 · previous %3 (%4)")
                           .arg(num(ls.last, 3), ls.as_of.toString(Qt::ISODate), num(ls.prev, 3),
                                ls.prev_date.toString(Qt::ISODate))
                    << QStringLiteral("10-year percentile %1")
                           .arg(std::isfinite(ls.pctile_10y) ? QString::number(std::lround(ls.pctile_10y))
                                                             : QStringLiteral("— (under ten years of data)"))
                    << QStringLiteral("Source: FRED, St. Louis Fed (public download)");
        } else {
            const auto qit = quotes_.constFind(r.symbol);
            const bool has_q = qit != quotes_.constEnd() && std::isfinite(qit->price) && qit->price > 0;
            const bool q_stale = has_q && services::last_known_is_stale(r.symbol);
            cells[1] = has_q ? num(qit->price, price_dp(qit->price)) : dash();
            cells[2] = has_q ? ui::formatting::format_percent(qit->change_pct, 2, true) : dash();
            p.c[1] = q_stale ? ui::colors::TEXT_SECONDARY() : ui::colors::WHITE();
            p.c[2] = q_stale || !has_q ? ui::colors::TEXT_SECONDARY() : ui::change_color(qit->change_pct);
            // At the live price against completed sessions (live_reading) —
            // the chart's condition strip computes the very same.
            QVector<cnd::DatedValue> closes;
            for (const auto& v : c.value("closes").toArray()) {
                const QJsonArray a = v.toArray();
                const QDate d = QDate::fromString(a.at(0).toString(), Qt::ISODate);
                if (d.isValid() && a.at(1).isDouble())
                    closes.append({d, a.at(1).toDouble()});
            }
            const auto lr = cnd::live_reading(closes, has_q && !q_stale ? qit->price : cnd::kNaN,
                                              core::bartime::market_today_et());
            const double vs = lr.vs_sma200_pct, p1 = lr.pctile_1y;
            const auto regime = cnd::regime_from_key(c.value("regime").toString());
            cells[3] = signed_num(vs, 1, QStringLiteral("%"));
            cells[4] = std::isfinite(p1) ? QString::number(std::lround(p1)) : dash();
            cells[5] = cnd::regime_label(regime);
            p.c[3] = std::isfinite(vs) ? ui::change_color(vs) : ui::colors::TEXT_TERTIARY();
            p.c[4] = pctile_color(p1);
            p.c[5] = regime_color(regime);
            if (q_stale)
                tip << QStringLiteral("Price: stale — waiting for a fresh quote");
            if (c.contains("break_date"))
                tip << QStringLiteral("<span style='color:%1'>Readings use the history since %2 only: the vendor's "
                                      "series jumps ×%3 that day</span>")
                           .arg(ui::colors::WARNING(), c.value("break_date").toString(),
                                QString::number(c.value("break_ratio").toDouble(), 'f', 2));
            if (!c.isEmpty()) {
                tip << QStringLiteral("Daily bars to %1%2")
                           .arg(c.value("as_of").toString(),
                                lr.live ? QStringLiteral(" · vs 200D and 1Y at the live price") : QString())
                    << QStringLiteral("SMA 20 / 50 / 200: %1 / %2 / %3")
                           .arg(num(jnum(c, "sma20"), price_dp(jnum(c, "sma20"))),
                                num(jnum(c, "sma50"), price_dp(jnum(c, "sma50"))),
                                num(jnum(c, "sma200"), price_dp(jnum(c, "sma200"))))
                    << QStringLiteral("RSI 14: %1 · ADX 14: %2 (+DI %3 / −DI %4)")
                           .arg(num(jnum(c, "rsi"), 0), num(jnum(c, "adx"), 0), num(jnum(c, "pdi"), 0),
                                num(jnum(c, "mdi"), 0))
                    << QStringLiteral("1M %1 · 1Y percentile %2 · 5Y percentile %3")
                           .arg(signed_num(jnum(c, "ret1m"), 1, QStringLiteral("%")),
                                std::isfinite(p1) ? QString::number(std::lround(p1)) : dash(),
                                std::isfinite(jnum(c, "p5y")) ? QString::number(std::lround(jnum(c, "p5y")))
                                                               : QStringLiteral("— (under 5 years of history)"));
            }
        }
        if (!c.isEmpty() && !fresh) {
            p.dim = true;
            tip << QStringLiteral("Readings from %1 — updating…").arg(age_text(c));
        } else if (!c.isEmpty()) {
            tip << QStringLiteral("Readings fetched %1").arg(age_text(c));
        }
        if (!err.isEmpty())
            tip << QStringLiteral("<span style='color:%1'>Last refresh failed: %2</span>")
                       .arg(ui::colors::NEGATIVE(), err.toHtmlEscaped());
        else if (c.isEmpty())
            tip << QStringLiteral("Loading…");
        tip << QStringLiteral("<i>Click for the chart · right-click to move or remove</i>");
        p.tip = tip.join(QStringLiteral("<br>"));
        rows.append(cells);
        paints.append(p);
    }
    QStringList add(ncols);
    add[0] = QString::fromUtf8(kAddText);
    rows.append(add);

    table_->set_rows(rows);
    for (int i = 0; i < paints.size(); ++i) {
        const auto& p = paints[i];
        for (int col = 0; col < ncols; ++col) {
            auto* it = table_->item(i, col);
            if (!it)
                continue;
            QString color = p.c[col].isEmpty() ? ui::colors::TEXT_PRIMARY() : p.c[col];
            const bool cond_col = mode_ == Mode::Market ? (col >= 3 && col <= 5) : (col >= 1 && col <= 4);
            if (p.dim && cond_col)
                color = ui::colors::TEXT_TERTIARY();
            it->setForeground(QColor(color));
            it->setToolTip(col == ncols - 1 ? QStringLiteral("Remove from this list") : p.tip);
        }
    }
    if (auto* it = table_->item(paints.size(), 0)) {
        it->setForeground(QColor(ui::colors::AMBER()));
        it->setToolTip(mode_ == Mode::Macro ? QStringLiteral("Add a FRED series")
                                            : QStringLiteral("Add a symbol to this list"));
    }

    if (footer_fn_) {
        const QString f = footer_fn_(quotes_);
        footer_->setText(f);
        footer_->setVisible(!f.isEmpty());
    }
}

void ConditionTableWidget::on_cell_clicked(int row, int col) {
    if (row == rows_.size()) {
        add_row();
        return;
    }
    if (row < 0 || row >= rows_.size())
        return;
    const int ncols = mode_ == Mode::Market ? 7 : 6;
    if (col == ncols - 1) {
        remove_row(row);
        return;
    }
    const auto& r = rows_[row];
    MarketChartDialog::show_for(r.symbol, row_label(r), this);
}

void ConditionTableWidget::on_context_menu(const QPoint& pos) {
    const int row = table_->rowAt(pos.y());
    QMenu menu(this);
    if (row >= 0 && row < rows_.size()) {
        auto* up = menu.addAction(QStringLiteral("Move up"));
        up->setEnabled(row > 0);
        connect(up, &QAction::triggered, this, [this, row]() { move_row(row, -1); });
        auto* down = menu.addAction(QStringLiteral("Move down"));
        down->setEnabled(row < rows_.size() - 1);
        connect(down, &QAction::triggered, this, [this, row]() { move_row(row, +1); });
        connect(menu.addAction(QStringLiteral("Remove")), &QAction::triggered, this,
                [this, row]() { remove_row(row); });
        menu.addSeparator();
    }
    connect(menu.addAction(QStringLiteral("Add…")), &QAction::triggered, this, [this]() { add_row(); });
    connect(menu.addAction(QStringLiteral("Reset to the default list")), &QAction::triggered, this, [this]() {
        apply_config({});
        edited();
    });
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

void ConditionTableWidget::add_row() {
    ConditionAddDialog dlg(mode_ == Mode::Macro ? ConditionAddDialog::Kind::Fred : ConditionAddDialog::Kind::Symbol,
                           this);
    if (dlg.exec() != QDialog::Accepted || dlg.symbol().isEmpty())
        return;
    for (const auto& r : rows_)
        if (r.symbol.compare(dlg.symbol(), Qt::CaseInsensitive) == 0)
            return; // already listed
    QJsonObject cfg = config();
    QJsonArray arr = cfg.value("rows").toArray();
    arr.append(QJsonObject{{"symbol", dlg.symbol()}, {"label", dlg.label()}});
    cfg["rows"] = arr;
    apply_config(cfg);
    edited();
}

void ConditionTableWidget::remove_row(int i) {
    if (i < 0 || i >= rows_.size())
        return;
    QJsonObject cfg = config();
    QJsonArray arr = cfg.value("rows").toArray();
    arr.removeAt(i);
    cfg["rows"] = arr; // an empty list stays empty (not the defaults)
    apply_config(cfg);
    edited();
}

void ConditionTableWidget::move_row(int i, int delta) {
    const int j = i + delta;
    if (i < 0 || j < 0 || i >= rows_.size() || j >= rows_.size())
        return;
    QJsonObject cfg = config();
    QJsonArray arr = cfg.value("rows").toArray();
    const QJsonValue a = arr.at(i);
    arr.replace(i, arr.at(j));
    arr.replace(j, a);
    cfg["rows"] = arr;
    apply_config(cfg);
    edited();
}

void ConditionTableWidget::edited() {
    emit config_changed(config());
}

} // namespace finterm::screens::widgets

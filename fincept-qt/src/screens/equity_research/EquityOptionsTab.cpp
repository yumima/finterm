// src/screens/equity_research/EquityOptionsTab.cpp
#include "screens/equity_research/EquityOptionsTab.h"

#include "core/logging/Logger.h"
#include "python/PythonWorker.h"
#include "screens/surface_analytics/Surface3DWidget.h"
#include "screens/surface_analytics/SurfaceLineWidget.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace fincept::screens {

namespace fmt = fincept::ui::formatting;

namespace {

// A chain is one Yahoo request per expiry (up to 12), fetched four at a time.
constexpr int kFetchTimeoutMs = 90'000;
// The smile chart stays legible with this many curves; expiries are sampled
// evenly across the loaded ones (nearest and farthest always kept).
constexpr int kMaxSmileCurves = 6;
// The surface mesh's column cap. Past it, strikes listed in fewer than half
// the expiries go first (one-off strikes turn the mesh into mostly holes),
// then the ones farthest from spot. Nothing is filled in either way; the
// TABLE view always lists every quote.
constexpr size_t kMaxSurfaceStrikes = 60;

enum View { kSurface = 0, kSmile = 1, kTerm = 2, kTable = 3, kMessage = 4 };

} // namespace

EquityOptionsTab::EquityOptionsTab(QWidget* parent) : QWidget(parent) {
    build_ui();
    show_message(QStringLiteral("Open a symbol to load its option chain."));
}

void EquityOptionsTab::build_ui() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(10, 10, 10, 10);
    vl->setSpacing(8);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(2);
    const QString btn_css =
        QString("QPushButton { background:transparent; color:%1; border:1px solid %2; padding:5px 14px; "
                "font-size:12px; font-weight:700; }"
                "QPushButton:hover { border-color:%3; color:%3; }"
                "QPushButton:checked { border-color:%3; color:%3; }")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER());
    const QStringList names = {QStringLiteral("SURFACE"), QStringLiteral("SMILE"), QStringLiteral("TERM"),
                               QStringLiteral("TABLE")};
    for (int i = 0; i < names.size(); ++i) {
        auto* b = new QPushButton(names[i]);
        b->setCheckable(true);
        b->setStyleSheet(btn_css);
        connect(b, &QPushButton::clicked, this, [this, i]() { set_view(i); });
        view_buttons_.push_back(b);
        bar->addWidget(b);
    }
    bar->addSpacing(12);

    status_label_ = new QLabel;
    status_label_->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    bar->addWidget(status_label_, 1);

    source_label_ = new QLabel;
    source_label_->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    bar->addWidget(source_label_);

    refresh_btn_ = new QPushButton(QStringLiteral("↻ REFRESH"));
    refresh_btn_->setStyleSheet(btn_css);
    refresh_btn_->setToolTip(QStringLiteral("Re-fetch the chain (Yahoo quotes are delayed; "
                                            "results are cached for 5 minutes)"));
    connect(refresh_btn_, &QPushButton::clicked, this, [this]() { fetch(); });
    bar->addWidget(refresh_btn_);
    vl->addLayout(bar);

    stack_ = new QStackedWidget;
    surface_ = new fincept::surface::Surface3DWidget;
    smile_ = new fincept::surface::SurfaceLineWidget;
    term_ = new fincept::surface::SurfaceLineWidget;

    table_ = new QTableWidget;
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({"EXPIRY", "DTE", "STRIKE", "TYPE", "IV %", "BID / ASK", "OPEN INT"});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSortingEnabled(true);
    table_->setStyleSheet(QString("QTableWidget { background:%1; color:%2; gridline-color:%3; border:1px solid %3; "
                                  "font-size:12px; }"
                                  "QHeaderView::section { background:%4; color:%5; border:0; padding:4px; "
                                  "font-size:12px; font-weight:700; }")
                              .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                                   ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY()));

    message_ = new QLabel;
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->setStyleSheet(QString("color:%1; font-size:13px; background:%2; border:1px solid %3;")
                                .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE(),
                                     ui::colors::BORDER_DIM()));

    stack_->addWidget(surface_); // kSurface
    stack_->addWidget(smile_);   // kSmile
    stack_->addWidget(term_);    // kTerm
    stack_->addWidget(table_);   // kTable
    stack_->addWidget(message_); // kMessage
    vl->addWidget(stack_, 1);

    set_view(kSurface);
}

void EquityOptionsTab::set_view(int index) {
    view_ = index;
    for (int i = 0; i < static_cast<int>(view_buttons_.size()); ++i)
        view_buttons_[i]->setChecked(i == index);
    if (!points_.empty())
        stack_->setCurrentIndex(index);
}

void EquityOptionsTab::set_symbol(const QString& symbol, bool force) {
    const QString s = symbol.trimmed().toUpper();
    if (s.isEmpty())
        return;
    if (!force && s == symbol_ && (in_flight_ || loaded_symbol_ == s))
        return;
    symbol_ = s;
    fetch();
}

void EquityOptionsTab::fetch() {
    if (symbol_.isEmpty())
        return;
    // A new fetch supersedes any older one still running: its result is
    // ignored by generation, so a slow chain for the previous ticker can
    // never paint under this one's header.
    const quint64 gen = ++generation_;
    in_flight_ = true;
    points_.clear();
    atm_.clear();
    spot_ = 0;
    data_as_of_ = QDateTime();
    loaded_symbol_.clear();
    refresh_btn_->setEnabled(false);
    source_label_->clear();
    status_label_->setText(QStringLiteral("Loading %1 option chain…").arg(symbol_));
    show_message(QStringLiteral("Loading %1 option chain from Yahoo — one request per expiry…").arg(symbol_));

    QJsonObject payload;
    payload["symbol"] = symbol_;
    QPointer<EquityOptionsTab> self = this;
    const QString sym = symbol_;
    python::PythonWorker::instance().submit(
        QStringLiteral("options_surface"), payload,
        [self, gen, sym](bool ok, QJsonObject result, QString err) {
            if (!self || gen != self->generation_)
                return;
            self->on_result(sym, ok, result, err);
        },
        kFetchTimeoutMs);
}

void EquityOptionsTab::on_result(const QString& symbol, bool ok, const QJsonObject& obj, const QString& err) {
    in_flight_ = false;
    refresh_btn_->setEnabled(true);
    // The worker reports ok=true for anything the action RETURNED, so the
    // action's own {"error": ...} (no spot, every chain failed) lands here
    // as a success unless checked.
    const QString failure = !ok ? err : obj.value("error").toString();
    if (!ok || !failure.isEmpty()) {
        status_label_->setText(QStringLiteral("Option chain unavailable"));
        show_message(QStringLiteral("%1 option chain unavailable:\n%2").arg(symbol, failure));
        LOG_WARN("EquityOptions", "options_surface " + symbol + " failed: " + failure);
        return; // not marked loaded — re-activating the tab retries
    }
    // A listing that may itself have failed is shown, but not remembered as
    // this symbol's answer, so re-opening the tab asks again.
    if (!obj.contains("listing_error"))
        loaded_symbol_ = symbol;

    spot_ = obj.value("spot").toDouble(0);
    for (const auto& v : obj.value("points").toArray()) {
        const QJsonObject o = v.toObject();
        Point p;
        p.expiry = o.value("expiry").toString();
        p.dte = o.value("dte").toInt();
        p.strike = o.value("strike").toDouble();
        p.is_call = o.value("type").toString() == QLatin1String("call");
        p.iv = o.value("iv").toDouble();
        p.bid = o.value("bid").toDouble();
        p.ask = o.value("ask").toDouble();
        p.oi = o.value("oi").isDouble() ? o.value("oi").toInt() : -1;
        if (p.strike > 0 && p.iv > 0)
            points_.push_back(p);
    }
    for (const auto& v : obj.value("atm").toArray()) {
        const QJsonObject o = v.toObject();
        atm_.push_back({o.value("expiry").toString(), o.value("dte").toInt(), o.value("strike").toDouble(),
                        o.value("iv").toDouble()});
    }
    std::sort(atm_.begin(), atm_.end(), [](const Atm& a, const Atm& b) { return a.dte < b.dte; });

    data_as_of_ = QDateTime::fromString(obj.value("as_of").toString(), Qt::ISODate);
    source_label_->setText(QStringLiteral("%1 · %2")
                               .arg(obj.value("source").toString(QStringLiteral("Yahoo Finance")),
                                    fmt::format_datetime(data_as_of_, fmt::DateStyle::Time)));
    emit loaded();

    if (points_.empty()) {
        const QString reason = obj.value("reason").toString(QStringLiteral("no usable quotes"));
        status_label_->setText(QStringLiteral("No option data"));
        show_message(QStringLiteral("%1: %2.").arg(symbol, reason));
        return;
    }

    const int used = obj.value("expiries_used").toInt();
    const int listed = obj.value("expiries_listed").toInt();
    const int dropped = obj.value("dropped_one_sided").toInt();
    QString status = QStringLiteral("Spot %1 · %2 quotes · %3 of %4 expiries · OTM, two-sided only")
                         .arg(QString::number(spot_, 'f', 2))
                         .arg(points_.size())
                         .arg(used)
                         .arg(listed);
    if (dropped > 0)
        status += QStringLiteral(" (%1 OTM without a two-sided quote left out)").arg(dropped);
    const QString chain_err = obj.value("chain_error").toString();
    if (!chain_err.isEmpty()) {
        status += QStringLiteral(" · some expiries failed");
        status_label_->setToolTip(chain_err);
    } else {
        status_label_->setToolTip(QString());
    }
    status_label_->setText(status);
    render();
}

void EquityOptionsTab::show_message(const QString& text) {
    surface_->clear();
    smile_->clear();
    term_->clear();
    table_->setRowCount(0);
    message_->setText(text);
    stack_->setCurrentIndex(kMessage);
}

void EquityOptionsTab::render() {
    render_surface();
    render_smile();
    render_term();
    render_table();
    stack_->setCurrentIndex(view_);
}

void EquityOptionsTab::render_surface() {
    // Rows = expiries (by DTE), cols = strikes. A strike an expiry does not
    // quote stays NaN — Surface3DWidget draws it as a hole.
    std::set<int> dtes;
    std::map<double, int> strike_count; // strike -> expiries quoting it
    for (const auto& p : points_) {
        dtes.insert(p.dte);
    }
    {
        std::set<std::pair<double, int>> seen;
        for (const auto& p : points_)
            if (seen.insert({p.strike, p.dte}).second)
                ++strike_count[p.strike];
    }
    std::vector<double> strikes;
    const size_t min_expiries = strike_count.size() > kMaxSurfaceStrikes ? (dtes.size() + 1) / 2 : 1;
    for (const auto& [k, n] : strike_count)
        if (static_cast<size_t>(n) >= min_expiries)
            strikes.push_back(k);
    if (strikes.size() > kMaxSurfaceStrikes) {
        // Still too dense (e.g. SPY's $1 strikes): keep the strikes nearest
        // the money, where the surface carries the information.
        std::sort(strikes.begin(), strikes.end(),
                  [this](double a, double b) { return std::abs(a - spot_) < std::abs(b - spot_); });
        strikes.resize(kMaxSurfaceStrikes);
        std::sort(strikes.begin(), strikes.end());
    }

    const std::vector<int> dte_vec(dtes.begin(), dtes.end());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::vector<float>> grid(dte_vec.size(), std::vector<float>(strikes.size(), nan));
    float mn = std::numeric_limits<float>::max(), mx = std::numeric_limits<float>::lowest();
    for (const auto& p : points_) {
        const auto r = std::lower_bound(dte_vec.begin(), dte_vec.end(), p.dte) - dte_vec.begin();
        const auto c_it = std::lower_bound(strikes.begin(), strikes.end(), p.strike);
        if (c_it == strikes.end() || *c_it != p.strike)
            continue; // strike filtered out above
        const float v = static_cast<float>(p.iv * 100.0);
        grid[r][c_it - strikes.begin()] = v;
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    if (strikes.size() < 2 || dte_vec.size() < 2) {
        surface_->clear();
        surface_->set_empty_text(QStringLiteral("Not enough strikes and expiries for a surface — "
                                                "see SMILE / TERM / TABLE."));
        return;
    }
    std::vector<std::string> col_labels, row_labels;
    for (double k : strikes)
        col_labels.push_back(QString::number(k, 'g', 10).toStdString()); // 102.5 stays 102.5
    for (int d : dte_vec)
        row_labels.push_back(std::to_string(d) + "D");
    surface_->set_surface(grid, "STRIKE", "IV %", "DTE", mn, mx, false, &col_labels, &row_labels);
}

void EquityOptionsTab::render_smile() {
    if (!(spot_ > 0)) {
        smile_->clear();
        return;
    }
    std::map<int, std::vector<const Point*>> by_dte;
    for (const auto& p : points_)
        by_dte[p.dte].push_back(&p);
    std::vector<int> dtes;
    for (const auto& [d, _] : by_dte)
        dtes.push_back(d);

    std::vector<int> picked;
    if (static_cast<int>(dtes.size()) <= kMaxSmileCurves) {
        picked = dtes;
    } else {
        std::set<int> idx;
        for (int i = 0; i < kMaxSmileCurves; ++i)
            idx.insert(static_cast<int>(std::lround(double(i) * (dtes.size() - 1) / (kMaxSmileCurves - 1))));
        for (int i : idx)
            picked.push_back(dtes[i]);
    }

    const QColor palette[] = {QColor(ui::colors::AMBER()),   QColor(ui::colors::CYAN()),
                              QColor(ui::colors::POSITIVE()), QColor(ui::colors::NEGATIVE()),
                              QColor(ui::colors::WARNING()),  QColor(ui::colors::TEXT_PRIMARY())};
    std::vector<fincept::surface::SurfaceLineWidget::Series> series;
    for (size_t i = 0; i < picked.size(); ++i) {
        auto pts = by_dte[picked[i]];
        std::sort(pts.begin(), pts.end(), [](const Point* a, const Point* b) { return a->strike < b->strike; });
        fincept::surface::SurfaceLineWidget::Series s;
        s.name = QStringLiteral("%1D (%2)").arg(picked[i]).arg(pts.front()->expiry);
        s.color = palette[i % std::size(palette)];
        for (const Point* p : pts) {
            s.x_values.push_back(static_cast<float>(p->strike / spot_ * 100.0));
            s.y_values.push_back(static_cast<float>(p->iv * 100.0));
        }
        series.push_back(std::move(s));
    }
    smile_->set_series(QStringLiteral("VOLATILITY SMILE — IV BY MONEYNESS"), series,
                       QStringLiteral("STRIKE / SPOT %"), QStringLiteral("IV %"));
}

void EquityOptionsTab::render_term() {
    if (atm_.empty()) {
        term_->clear();
        return;
    }
    std::vector<float> xs, ys;
    for (const auto& a : atm_) {
        xs.push_back(static_cast<float>(a.dte));
        ys.push_back(static_cast<float>(a.iv * 100.0));
    }
    term_->set_curve(QStringLiteral("ATM IV TERM STRUCTURE"), xs, ys, {}, QStringLiteral("DAYS TO EXPIRY"),
                     QStringLiteral("ATM IV %"), QColor(ui::colors::AMBER()));
}

void EquityOptionsTab::render_table() {
    table_->setSortingEnabled(false);
    table_->setRowCount(static_cast<int>(points_.size()));
    int r = 0;
    for (const auto& p : points_) {
        table_->setItem(r, 0, new QTableWidgetItem(p.expiry));
        auto* dte = new QTableWidgetItem;
        dte->setData(Qt::DisplayRole, p.dte);
        table_->setItem(r, 1, dte);
        auto* k = new QTableWidgetItem;
        k->setData(Qt::DisplayRole, p.strike);
        table_->setItem(r, 2, k);
        table_->setItem(r, 3, new QTableWidgetItem(p.is_call ? QStringLiteral("CALL") : QStringLiteral("PUT")));
        auto* iv = new QTableWidgetItem;
        iv->setData(Qt::DisplayRole, std::round(p.iv * 10000.0) / 100.0);
        table_->setItem(r, 4, iv);
        table_->setItem(r, 5, new QTableWidgetItem(QStringLiteral("%1 / %2").arg(
                                  QString::number(p.bid, 'f', 2), QString::number(p.ask, 'f', 2))));
        auto* oi = new QTableWidgetItem;
        if (p.oi >= 0)
            oi->setData(Qt::DisplayRole, p.oi);
        else
            oi->setText(fmt::placeholder());
        table_->setItem(r, 6, oi);
        ++r;
    }
    table_->setSortingEnabled(true);
    table_->sortByColumn(1, Qt::AscendingOrder);
}

} // namespace fincept::screens

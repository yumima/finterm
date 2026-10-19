#include "screens/screener/ScreenerScreen.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "screens/ownership/OwnershipUi.h"
#include "services/util/DiskCache.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

constexpr const char* TAG = "Screener";
namespace ou = ownership_ui;
using screener::Kind;

// The scan downloads ~1,700 two-year price histories on a cold cache (about a
// minute); with a warm one it is the TradingView request plus the math.
constexpr int kScanTimeoutMs = 10 * 60 * 1000;

constexpr int kSortKey = Qt::UserRole + 10;
constexpr int kMissingRole = Qt::UserRole + 11;

/// Numeric sort with absent values LAST in both directions — a "lowest P/E
/// first" sort must not open on every name that has no earnings.
class SortItem : public QTableWidgetItem {
  public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override {
        const bool am = data(kMissingRole).toBool(), bm = other.data(kMissingRole).toBool();
        if (am || bm) {
            if (am == bm)
                return false;
            const bool ascending = tableWidget() && tableWidget()->horizontalHeader()->sortIndicatorOrder() ==
                                                        Qt::AscendingOrder;
            return ascending ? bm : am;
        }
        const QVariant a = data(kSortKey), b = other.data(kSortKey);
        if (a.isValid() && b.isValid())
            return a.toDouble() < b.toDouble();
        return text().compare(other.text(), Qt::CaseInsensitive) < 0;
    }
};

QTableWidgetItem* metric_item(Kind kind, const std::optional<double>& v) {
    auto* it = new SortItem(screener::format_value(kind, v));
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (v)
        it->setData(kSortKey, *v);
    else
        it->setData(kMissingRole, true);
    const QString c = screener::colour_for(kind, v);
    if (!c.isEmpty())
        it->setForeground(QColor(c));
    return it;
}

QTableWidgetItem* text_item(const QString& s, const QString& colour = {}) {
    auto* it = new SortItem(s.isEmpty() ? ui::formatting::placeholder() : s);
    it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (s.isEmpty())
        it->setData(kMissingRole, true);
    if (!colour.isEmpty())
        it->setForeground(QColor(colour));
    return it;
}

QString signed_pct_html(const std::optional<double>& v) {
    if (!v)
        return ui::formatting::placeholder();
    const QString c = *v > 0 ? ui::colors::GREEN() : (*v < 0 ? ui::colors::RED() : ui::colors::TEXT_PRIMARY());
    return QStringLiteral("<span style='color:%1'>%2</span>")
        .arg(c, ui::formatting::format_percent(*v * 100.0, 1, true));
}

const QStringList kBaseHeaders = {"#", "Symbol", "Name", "Sector", "Price", "1D", "Mkt cap"};
const QStringList kBaseKeys = {"price", "chg_1d", "mcap"};

} // namespace

ScreenerScreen::ScreenerScreen(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void ScreenerScreen::build_ui() {
    setStyleSheet(ou::table_stylesheet() +
                  QString("QListWidget{background:%1;color:%2;border:1px solid %3;outline:0;}"
                          "QListWidget::item{padding:6px 6px;border-bottom:1px solid %3;}"
                          "QListWidget::item:selected{background:%4;color:%5;}"
                          "QTextBrowser{background:%1;color:%2;border:1px solid %3;}"
                          "QComboBox{background:%6;color:%2;border:1px solid %3;padding:2px 6px;}"
                          "QPushButton{background:%6;color:%2;border:1px solid %3;padding:3px 12px;"
                          "font-weight:700;}"
                          "QPushButton:hover{background:%4;}"
                          "QPushButton:disabled{color:%7;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                           ui::colors::BG_HOVER(), ui::colors::AMBER(), ui::colors::BG_SURFACE(),
                           ui::colors::TEXT_SECONDARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(6);

    // ── header ───────────────────────────────────────────────────────────────
    auto* head = new QHBoxLayout;
    auto title = ou::section_title(QStringLiteral("STOCK SCREENER"));
    head->addWidget(title.widget);
    head->addSpacing(12);
    auto* ulbl = new QLabel(QStringLiteral("Universe"));
    ulbl->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    head->addWidget(ulbl);
    universe_ = new QComboBox;
    universe_->addItem(QStringLiteral("US stocks ≥ $10B"), 10e9);
    universe_->addItem(QStringLiteral("US stocks ≥ $2B"), 2e9);
    universe_->addItem(QStringLiteral("US stocks ≥ $300M"), 3e8);
    universe_->setCurrentIndex(1);
    universe_->setToolTip(QStringLiteral("Primary-listed US common stocks above this market cap"));
    head->addWidget(universe_);
    refresh_btn_ = new QPushButton(QStringLiteral("REFRESH"));
    refresh_btn_->setCursor(Qt::PointingHandCursor);
    refresh_btn_->setToolTip(QStringLiteral("Re-run every screen (prices are re-downloaded only after a new close)"));
    head->addWidget(refresh_btn_);
    market_ = new QLabel;
    market_->setTextFormat(Qt::RichText);
    head->addSpacing(12);
    head->addWidget(market_);
    head->addStretch(1);
    status_ = new QLabel;
    status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    head->addWidget(status_);
    root->addLayout(head);

    // ── body: strategies | results | rules + evidence ────────────────────────
    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);

    list_ = new QListWidget;
    list_->setMinimumWidth(220);
    split->addWidget(list_);

    auto* mid = new QWidget;
    auto* ml = new QVBoxLayout(mid);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(4);
    table_title_ = new QLabel;
    table_title_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::AMBER()));
    ml->addWidget(table_title_);
    table_ = ou::make_table(kBaseHeaders);
    table_->setToolTip(QStringLiteral("Double-click a row to open it in Equity Research"));
    ml->addWidget(table_, 1);
    split->addWidget(mid);

    card_ = new QTextBrowser;
    card_->setOpenExternalLinks(false);
    card_->setMinimumWidth(300);
    split->addWidget(card_);

    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({240, 900, 380});
    root->addWidget(split, 1);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= list_->count())
            return;
        show_strategy(list_->item(row)->data(Qt::UserRole).toString());
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* it = table_->item(row, 1))
            emit navigate_to_screen(QStringLiteral("equity_research"), it->text());
    });
    connect(refresh_btn_, &QPushButton::clicked, this, [this]() { run_scan(false); });
    connect(universe_, &QComboBox::currentIndexChanged, this, [this](int) {
        // Bring this universe current, showing its last result meanwhile —
        // or nothing: the previous universe's lists must not stay up under
        // this universe's name.
        run_scan(false);
        const QJsonDocument doc = services::util::DiskCache(QStringLiteral("screener")).load(cache_name());
        if (doc.isObject()) {
            apply_result(screener::parse_scan(doc.object()), true);
        } else {
            result_ = {};
            list_->clear();
            table_->setRowCount(0);
            table_title_->clear();
            card_->clear();
            market_->clear();
        }
    });
}

double ScreenerScreen::min_mcap() const {
    return universe_->currentData().toDouble();
}

QString ScreenerScreen::cache_name() const {
    return QStringLiteral("scan_v1_%1.json").arg(qint64(min_mcap()));
}

void ScreenerScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (shown_once_)
        return;
    shown_once_ = true;
    run_scan(false);
    const QJsonDocument doc = services::util::DiskCache(QStringLiteral("screener")).load(cache_name());
    if (doc.isObject())
        apply_result(screener::parse_scan(doc.object()), true);
}

void ScreenerScreen::set_status(const QString& text, bool error) {
    status_->setText(text);
    status_->setStyleSheet(
        QString("color:%1;").arg(error ? ui::colors::RED() : ui::colors::TEXT_SECONDARY()));
}

void ScreenerScreen::run_scan(bool force) {
    const int gen = ++scan_gen_;
    running_ = true;
    refresh_btn_->setEnabled(false);
    set_status(QStringLiteral("Running screens…"));
    const double mcap = min_mcap();
    const QString name = cache_name();
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"min_mcap", mcap}, {"force", force}}).toJson(QJsonDocument::Compact));
    QPointer<ScreenerScreen> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("screener_data.py"), {QStringLiteral("scan"), payload},
        [self, gen, name](python::PythonResult res) {
            if (!self)
                return;
            const bool current = gen == self->scan_gen_;
            if (current) {
                self->running_ = false;
                self->refresh_btn_->setEnabled(true);
            }
            if (!res.success) {
                if (!current)
                    return;
                const QString err = res.error.isEmpty() ? QStringLiteral("screener scan failed")
                                                        : res.error.left(200);
                LOG_WARN(TAG, "scan failed: " + err);
                self->set_status(QStringLiteral("Scan failed — ") + err, true);
                return;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(python::extract_json(res.output).toUtf8());
            const auto r = screener::parse_scan(doc.object());
            if (!doc.isObject() || !r.error.isEmpty()) {
                if (!current)
                    return;
                const QString err = r.error.isEmpty() ? QStringLiteral("unreadable scan output") : r.error;
                LOG_WARN(TAG, "scan error: " + err);
                self->set_status(QStringLiteral("Scan failed — ") + err.left(200), true);
                return;
            }
            // A superseded universe's result is still that universe's latest:
            // keep it for the next time it is selected, just don't show it.
            if (gen >= self->saved_gen_.value(name, -1)) {
                self->saved_gen_.insert(name, gen);
                services::util::DiskCache(QStringLiteral("screener")).save(name, doc);
            }
            if (current)
                self->apply_result(r, false);
        },
        [self, gen](QString line, bool is_stderr) {
            if (!self || gen != self->scan_gen_ || !is_stderr || !line.startsWith(QLatin1String("PROGRESS ")))
                return;
            // "PROGRESS <done> <total> <text>"
            const QString text = line.section(QLatin1Char(' '), 3);
            if (!text.isEmpty())
                self->set_status(text + QStringLiteral("…"));
        },
        kScanTimeoutMs);
}

void ScreenerScreen::apply_result(const screener::ScanResult& r, bool from_cache) {
    result_ = r;
    QStringList bits;
    if (!r.asof.isEmpty()) {
        if (r.intraday)
            bits << QStringLiteral("Intraday %1, prices of %2").arg(r.asof, r.prices_fetched);
        else
            bits << QStringLiteral("As of %1 close").arg(r.asof);
    }
    if (r.stale_prices > 0)
        bits << QStringLiteral("%1 names on older prices (oldest %2)").arg(r.stale_prices).arg(r.prices_fetched);
    bits << QStringLiteral("%1 stocks (%2 priced)").arg(r.universe_n).arg(r.priced);
    if (from_cache && running_)
        bits << QStringLiteral("refreshing…");
    set_status(bits.join(QStringLiteral(" · ")));
    if (r.has_market) {
        auto mark = [](bool up, const QString& what) {
            return QStringLiteral("<span style='color:%1'>%2 %3</span>")
                .arg(up ? ui::colors::GREEN() : ui::colors::RED(), up ? QStringLiteral("▲") : QStringLiteral("▼"),
                     what);
        };
        market_->setText(QStringLiteral("<span style='color:%1'>Market (SPY):</span> %2 &nbsp; %3")
                             .arg(ui::colors::TEXT_SECONDARY(),
                                  mark(r.above50, r.above50 ? QStringLiteral("above 50D") : QStringLiteral("below 50D")),
                                  mark(r.above200, r.above200 ? QStringLiteral("above 200D")
                                                              : QStringLiteral("below 200D"))));
        market_->setToolTip(QStringLiteral(
            "CAN SLIM's \"M\" and Minervini's market filter. Shown, not applied: moving to cash on it "
            "made every screen worse over 2010-2026 (see the evidence card)."));
    } else {
        market_->clear();
    }
    populate_strategies();
}

void ScreenerScreen::populate_strategies() {
    const QSignalBlocker block(list_);
    list_->clear();
    int select = 0;
    for (int i = 0; i < result_.strategies.size(); ++i) {
        const auto& s = result_.strategies[i];
        auto* it = new QListWidgetItem(QStringLiteral("%1\n%2 · %3 names").arg(s.name, s.family).arg(s.symbols.size()));
        it->setData(Qt::UserRole, s.id);
        it->setToolTip(s.summary);
        list_->addItem(it);
        if (s.id == current_id_)
            select = i;
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(select);
        show_strategy(list_->item(select)->data(Qt::UserRole).toString());
    }
}

void ScreenerScreen::show_strategy(const QString& id) {
    const auto* s = result_.strategy(id);
    if (!s)
        return;
    current_id_ = id;
    fill_table(*s);
    fill_card(*s);
}

void ScreenerScreen::fill_table(const screener::Strategy& s) {
    QStringList headers = kBaseHeaders;
    QVector<const screener::ColumnDef*> extra;
    for (const QString& key : s.columns) {
        if (const auto* d = screener::column_def(key)) {
            extra << d;
            headers << QString::fromLatin1(d->header);
        }
    }
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    for (int c = 0; c < extra.size(); ++c)
        if (auto* h = table_->horizontalHeaderItem(kBaseHeaders.size() + c))
            h->setToolTip(QString::fromUtf8(extra[c]->tip));
    table_->setRowCount(s.symbols.size());
    for (int row = 0; row < s.symbols.size(); ++row) {
        const QString& sym = s.symbols[row];
        const QJsonObject o = result_.rows.value(sym);
        int col = 0;
        table_->setItem(row, col++, metric_item(Kind::Int, double(row + 1)));
        table_->setItem(row, col++, text_item(sym, ui::colors::AMBER()));
        table_->setItem(row, col++, text_item(o.value(QStringLiteral("name")).toString()));
        table_->setItem(row, col++, text_item(o.value(QStringLiteral("sector")).toString()));
        for (const QString& key : kBaseKeys) {
            const auto* d = screener::column_def(key);
            table_->setItem(row, col++, metric_item(d->kind, screener::num(o, key)));
        }
        for (const auto* d : extra)
            table_->setItem(row, col++, metric_item(d->kind, screener::num(o, QString::fromLatin1(d->key))));
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    table_->resizeColumnsToContents();
    // Name and sector are context; the numbers are the point, and with
    // Equity Research split alongside the pane is half as wide.
    if (table_->columnWidth(2) > 200)
        table_->setColumnWidth(2, 200);
    if (table_->columnWidth(3) > 150)
        table_->setColumnWidth(3, 150);
    table_title_->setText(s.symbols.isEmpty()
                              ? QStringLiteral("%1 — no stock passes today").arg(s.name)
                              : QStringLiteral("%1 — %2 names, ranked by %3")
                                    .arg(s.name)
                                    .arg(s.symbols.size())
                                    .arg(s.rank_by.toLower()));
}

void ScreenerScreen::fill_card(const screener::Strategy& s) {
    namespace fmtn = ui::formatting;
    const QString dim = ui::colors::TEXT_SECONDARY();
    QString h;
    h += QStringLiteral("<div style='font-size:13px;font-weight:700;color:%1'>%2</div>")
             .arg(ui::colors::AMBER(), s.name.toHtmlEscaped());
    h += QStringLiteral("<div style='color:%1;margin-top:2px'>%2</div>").arg(dim, s.source.toHtmlEscaped());
    h += QStringLiteral("<p>%1</p>").arg(s.summary.toHtmlEscaped());
    h += QStringLiteral("<div style='font-weight:700;color:%1'>RULES</div><ul style='margin-left:-20px'>")
             .arg(ui::colors::AMBER());
    for (const QString& r : s.rules)
        h += QStringLiteral("<li>%1</li>").arg(r.toHtmlEscaped());
    h += QStringLiteral("</ul><div style='color:%1'>Ranked by: %2</div>").arg(dim, s.rank_by.toHtmlEscaped());

    h += QStringLiteral("<div style='font-weight:700;color:%1;margin-top:10px'>MEASURED RECORD</div>")
             .arg(ui::colors::AMBER());
    const auto& ev = s.evidence;
    if (ev.tested) {
        const bool significant = ev.t && std::abs(*ev.t) >= 2.0;
        h += QStringLiteral("<table cellspacing='0' cellpadding='2'>"
                            "<tr><td style='color:%1'>Excess / yr vs index</td><td align='right'>%2</td></tr>"
                            "<tr><td style='color:%1'>t-stat</td><td align='right'>%3</td></tr>"
                            "<tr><td style='color:%1'>Max drawdown</td><td align='right'>%4</td></tr>")
                 .arg(dim, signed_pct_html(ev.excess),
                      ev.t ? QString::number(*ev.t, 'f', 2) + (significant ? QString() : QStringLiteral(" (noise)"))
                           : fmtn::placeholder(),
                      ev.max_dd ? fmtn::format_percent(*ev.max_dd * 100.0, 1) : fmtn::placeholder());
        for (int i = 0; i < ev.subperiods.size() && i < result_.subperiods.size(); ++i)
            h += QStringLiteral("<tr><td style='color:%1'>&nbsp;&nbsp;%2</td><td align='right'>%3</td></tr>")
                     .arg(dim, result_.subperiods[i].toHtmlEscaped(), signed_pct_html(ev.subperiods[i]));
        h += QStringLiteral("</table>");
    } else {
        h += QStringLiteral("<div style='color:%1'>Not backtested.</div>").arg(dim);
    }
    if (!ev.note.isEmpty())
        h += QStringLiteral("<p>%1</p>").arg(ev.note.toHtmlEscaped());
    if (!result_.basis.isEmpty())
        h += QStringLiteral("<p style='color:%1;font-size:11px'>Basis: %2. |t| &lt; 2 is "
                            "indistinguishable from luck. No screen tested beat the index with "
                            "significance; a list here is where the method would look, not a "
                            "forecast.</p>")
                 .arg(dim, result_.basis.toHtmlEscaped());
    card_->setHtml(h);
}

void ScreenerScreen::restore_state(const QVariantMap& state) {
    const QString id = state.value(QStringLiteral("strategy")).toString();
    if (!id.isEmpty())
        current_id_ = id;
    const double m = state.value(QStringLiteral("min_mcap")).toDouble();
    if (m > 0) {
        const int idx = universe_->findData(m);
        if (idx >= 0) {
            const QSignalBlocker block(universe_);
            universe_->setCurrentIndex(idx);
        }
    }
}

QVariantMap ScreenerScreen::save_state() const {
    return {{QStringLiteral("strategy"), current_id_}, {QStringLiteral("min_mcap"), min_mcap()}};
}

} // namespace fincept::screens

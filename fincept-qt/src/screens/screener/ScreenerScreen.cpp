#include "screens/screener/ScreenerScreen.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "screens/ownership/OwnershipUi.h"
#include "screens/screener/ScreenerChart.h"
#include "services/util/DiskCache.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

constexpr const char* TAG = "Screener";
namespace ou = ownership_ui;
using screener::Kind;

const QString kOverview = QStringLiteral("__overview__");

// The scan downloads ~1,700 two-year price histories on a cold cache (about a
// minute); with a warm one it is the TradingView request plus the math.
constexpr int kScanTimeoutMs = 10 * 60 * 1000;
constexpr int kSparkBars = 126; // ~6 months in the table's trend column

constexpr int kSortKey = Qt::UserRole + 10;
constexpr int kMissingRole = Qt::UserRole + 11;
constexpr int kSparkRole = Qt::UserRole + 12;

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

/// The trend column: a 6-month close line drawn in the cell, green if it ends
/// above where it started. A delegate rather than a cell widget, so it sorts
/// with its row and costs nothing per row.
class SparkDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        QStyledItemDelegate::paint(p, opt, idx);
        const QList<double> y = idx.data(kSparkRole).value<QList<double>>();
        double lo = INFINITY, hi = -INFINITY, first = NAN, last = NAN;
        for (double v : y)
            if (std::isfinite(v)) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                if (!std::isfinite(first))
                    first = v;
                last = v;
            }
        if (y.size() < 2 || !std::isfinite(lo) || hi <= lo)
            return;
        const QRectF r = QRectF(opt.rect).adjusted(4, 3, -4, -3);
        QPainterPath path;
        bool down = false;
        for (int i = 0; i < y.size(); ++i) {
            if (!std::isfinite(y[i])) {
                down = false;
                continue;
            }
            const QPointF pt(r.left() + r.width() * i / (y.size() - 1.0),
                             r.bottom() - r.height() * (y[i] - lo) / (hi - lo));
            if (down)
                path.lineTo(pt);
            else
                path.moveTo(pt);
            down = true;
        }
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(last >= first ? ui::colors::GREEN() : ui::colors::RED()), 1.2));
        p->drawPath(path);
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& i) const override {
        return QStyledItemDelegate::sizeHint(o, i).expandedTo(QSize(90, 18));
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

QVector<double> closes_of(const QJsonObject& row) {
    return screener::number_list(row.value(QStringLiteral("closes")));
}

QString signed_pct_html(const std::optional<double>& v) {
    if (!v)
        return ui::formatting::placeholder();
    const QString c = *v > 0 ? ui::colors::GREEN() : (*v < 0 ? ui::colors::RED() : ui::colors::TEXT_PRIMARY());
    return QStringLiteral("<span style='color:%1'>%2</span>")
        .arg(c, ui::formatting::format_percent(*v * 100.0, 1, true));
}

const QStringList kBaseHeaders = {"#", "Symbol", "Trend 6M", "Name", "Sector", "Price", "1D", "Mkt cap"};
const QStringList kBaseKeys = {"price", "chg_1d", "mcap"};
constexpr int kSymbolCol = 1;

} // namespace

ScreenerScreen::ScreenerScreen(QWidget* parent) : QWidget(parent) {
    result_.meta_only = true; // nothing scanned yet
    build_ui();
}

void ScreenerScreen::build_ui() {
    setStyleSheet(ou::table_stylesheet() +
                  QString("QListWidget{background:%1;color:%2;border:1px solid %3;outline:0;}"
                          "QListWidget::item{padding:7px 8px;border-bottom:1px solid %3;}"
                          "QListWidget::item:selected{background:%4;color:%5;}"
                          "QTextBrowser{background:%1;color:%2;border:1px solid %3;}"
                          "QComboBox,QLineEdit,QDoubleSpinBox{background:%6;color:%2;border:1px solid %3;"
                          "padding:2px 6px;}"
                          "QPushButton{background:%6;color:%2;border:1px solid %3;padding:3px 12px;"
                          "font-weight:700;}"
                          "QPushButton:hover{background:%4;}"
                          "QPushButton:disabled{color:%7;}"
                          "QProgressBar{background:%6;border:1px solid %3;height:6px;text-align:center;}"
                          "QProgressBar::chunk{background:%5;}"
                          "QSplitter::handle{background:%3;}")
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
    refresh_btn_->setToolTip(
        QStringLiteral("Re-run every screen (prices re-download once stale: after a new close, or after "
                       "15 minutes in session)"));
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

    // ── progress: visible while a scan runs ─────────────────────────────────
    progress_row_ = new QWidget;
    auto* pr = new QHBoxLayout(progress_row_);
    pr->setContentsMargins(0, 0, 0, 0);
    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    progress_->setRange(0, 0); // busy until the first PROGRESS line
    progress_lbl_ = new QLabel;
    progress_lbl_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    pr->addWidget(progress_, 1);
    pr->addWidget(progress_lbl_);
    progress_row_->hide();
    root->addWidget(progress_row_);

    // ── body: strategies | names | record + charts ──────────────────────────
    split_ = new QSplitter(Qt::Horizontal);
    split_->setChildrenCollapsible(false);
    split_->setHandleWidth(3);

    list_ = new QListWidget;
    list_->setMinimumWidth(200);
    split_->addWidget(list_);

    centre_ = new QStackedWidget;
    empty_lbl_ = new QLabel;
    empty_lbl_->setWordWrap(true);
    empty_lbl_->setAlignment(Qt::AlignCenter);
    empty_lbl_->setTextFormat(Qt::RichText);
    empty_lbl_->setStyleSheet(QString("color:%1;padding:40px;").arg(ui::colors::TEXT_SECONDARY()));
    centre_->addWidget(empty_lbl_);

    auto* mid = new QWidget;
    auto* ml = new QVBoxLayout(mid);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(4);
    table_title_ = new QLabel;
    table_title_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::AMBER()));
    table_title_->setWordWrap(true);
    ml->addWidget(table_title_);
    auto* tools = new QHBoxLayout;
    filter_ = new QLineEdit;
    filter_->setPlaceholderText(QStringLiteral("Filter by symbol, name or sector…"));
    filter_->setClearButtonEnabled(true);
    tools->addWidget(filter_, 1);
    plan_box_ = new QWidget;
    auto* pl = new QHBoxLayout(plan_box_);
    pl->setContentsMargins(8, 0, 0, 0);
    auto* alab = new QLabel(QStringLiteral("Account"));
    alab->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    pl->addWidget(alab);
    account_ = new QDoubleSpinBox;
    account_->setRange(1'000, 1e9);
    account_->setDecimals(0);
    account_->setSingleStep(10'000);
    account_->setPrefix(QStringLiteral("$"));
    account_->setGroupSeparatorShown(true);
    account_->setValue(account_value_);
    account_->setToolTip(QStringLiteral("Your account size: Shares and Position size the trade so the stop costs the risk below"));
    pl->addWidget(account_);
    auto* rlab = new QLabel(QStringLiteral("Risk / trade"));
    rlab->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    pl->addWidget(rlab);
    risk_ = new QComboBox;
    risk_->addItem(QStringLiteral("0.25%"), 0.0025);
    risk_->addItem(QStringLiteral("0.5% (his)"), 0.005);
    risk_->addItem(QStringLiteral("1%"), 0.01);
    risk_->setCurrentIndex(1);
    pl->addWidget(risk_);
    plan_box_->hide();
    tools->addWidget(plan_box_);
    ml->addLayout(tools);
    table_ = ou::make_table(kBaseHeaders);
    table_->setToolTip(QStringLiteral("Click a row for its chart; double-click to open it in Equity Research"));
    table_->setItemDelegateForColumn(2, new SparkDelegate(table_));
    ml->addWidget(table_, 1);
    centre_->addWidget(mid);
    split_->addWidget(centre_);

    auto* right = new QSplitter(Qt::Vertical);
    right->setChildrenCollapsible(false);
    right->setHandleWidth(3);
    card_ = new QTextBrowser;
    card_->setOpenExternalLinks(false);
    card_->setMinimumHeight(140);
    right->addWidget(card_);
    equity_chart_ = new ScreenerChart;
    equity_chart_->set_log_scale(true);
    right->addWidget(equity_chart_);
    price_chart_ = new ScreenerChart;
    price_chart_->set_placeholder(QStringLiteral("Select a stock to see its chart"));
    right->addWidget(price_chart_);
    right->setStretchFactor(0, 4);
    right->setStretchFactor(1, 3);
    right->setStretchFactor(2, 3);
    right->setMinimumWidth(320);
    split_->addWidget(right);
    split_->setStretchFactor(0, 2);
    split_->setStretchFactor(1, 5);
    split_->setStretchFactor(2, 3);
    root->addWidget(split_, 1);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= list_->count())
            return;
        show_strategy(list_->item(row)->data(Qt::UserRole).toString());
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (auto* it = row >= 0 ? table_->item(row, kSymbolCol) : nullptr)
            show_stock(it->text());
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* it = table_->item(row, kSymbolCol))
            emit navigate_to_screen(QStringLiteral("equity_research"), it->text());
    });
    connect(filter_, &QLineEdit::textChanged, this, [this](const QString&) { apply_filter(); });
    // Only Shares and Position depend on these: updated in place, so the
    // reader's sort and selection survive every keystroke.
    connect(account_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        account_value_ = v;
        update_plan_cells();
    });
    connect(risk_, &QComboBox::currentIndexChanged, this, [this](int) {
        risk_frac_ = risk_->currentData().toDouble();
        update_plan_cells();
    });
    connect(refresh_btn_, &QPushButton::clicked, this, [this]() { run_scan(); });
    connect(universe_, &QComboBox::currentIndexChanged, this, [this](int) {
        // Bring this universe current, showing its last result meanwhile —
        // or the strategies alone: the previous universe's lists must not
        // stay up under this universe's name.
        run_scan();
        const QJsonDocument doc = services::util::DiskCache(QStringLiteral("screener")).load(cache_name());
        // the generation that WROTE this cache, so a later-finishing older scan
        // cannot replace it
        displayed_gen_ = saved_gen_.value(cache_name(), -1);
        if (doc.isObject()) {
            apply_result(screener::parse_scan(doc.object()), true);
        } else {
            result_.rows.clear();
            result_.meta_only = true;
            for (auto& s : result_.strategies)
                s.symbols.clear();
            market_->clear();
            populate_strategies();
        }
    });
}

double ScreenerScreen::min_mcap() const {
    return universe_->currentData().toDouble();
}

QString ScreenerScreen::cache_name() const {
    return QStringLiteral("scan_v2_%1.json").arg(qint64(min_mcap()));
}

void ScreenerScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    even_out_panes();
    if (shown_once_)
        return;
    shown_once_ = true;
    run_scan();
    const QJsonDocument doc = services::util::DiskCache(QStringLiteral("screener")).load(cache_name());
    if (doc.isObject())
        apply_result(screener::parse_scan(doc.object()), true);
    else
        load_meta(); // no lists yet: the strategies and their records, at once
}

void ScreenerScreen::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    even_out_panes();
}

void ScreenerScreen::even_out_panes() {
    // Once, at the first real width: strategies ~20%, names ~47%, record and
    // charts ~33%. After that the reader's own drag wins.
    if (panes_set_ || !split_ || split_->width() < 900)
        return;
    panes_set_ = true;
    const int w = split_->width();
    const int left = std::max(220, int(w * 0.20));
    const int right = std::max(340, int(w * 0.33));
    split_->setSizes({left, w - left - right, right});
}

void ScreenerScreen::load_meta() {
    QPointer<ScreenerScreen> self = this;
    python::PythonRunner::instance().run_interactive(
        QStringLiteral("screener_data.py"), {QStringLiteral("meta")},
        [self](python::PythonResult res) {
            if (!self || self->has_lists() || !res.success)
                return;
            const QJsonDocument doc = QJsonDocument::fromJson(python::extract_json(res.output).toUtf8());
            auto r = screener::parse_scan(doc.object());
            if (!doc.isObject() || !r.error.isEmpty() || r.strategies.isEmpty())
                return;
            r.meta_only = true;
            self->result_ = r;
            self->populate_strategies();
        },
        20'000);
}

void ScreenerScreen::set_status(const QString& text, bool error) {
    status_->setText(text);
    status_->setStyleSheet(
        QString("color:%1;").arg(error ? ui::colors::RED() : ui::colors::TEXT_SECONDARY()));
}

void ScreenerScreen::set_progress(int done, int total, const QString& text) {
    if (total > 0) {
        progress_->setRange(0, total);
        progress_->setValue(done);
    } else {
        progress_->setRange(0, 0);
    }
    progress_lbl_->setText(text);
    if (!has_lists())
        update_empty_label(); // not show_strategy(): re-rendering the card on every line
                              // scrolled the rules the reader was reading back to the top
}

void ScreenerScreen::run_scan() {
    const int gen = ++scan_gen_;
    running_ = true;
    refresh_btn_->setEnabled(false);
    progress_row_->show();
    set_progress(0, 0, QStringLiteral("Starting…"));
    set_status(QStringLiteral("Running screens…"));
    const double mcap = min_mcap();
    const QString name = cache_name();
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"min_mcap", mcap}}).toJson(QJsonDocument::Compact));
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
                self->progress_row_->hide();
            }
            if (!res.success) {
                if (!current)
                    return;
                const QString err = res.error.isEmpty() ? QStringLiteral("screener scan failed")
                                                        : res.error.left(200);
                LOG_WARN(TAG, "scan failed: " + err);
                self->set_status(QStringLiteral("Scan failed — ") + err, true);
                if (!self->has_lists())
                    self->show_strategy(self->current_id_);
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
                if (!self->has_lists())
                    self->show_strategy(self->current_id_);
                return;
            }
            // A superseded universe's result is still that universe's latest:
            // keep it for the next time it is selected, just don't show it.
            if (gen >= self->saved_gen_.value(name, -1)) {
                self->saved_gen_.insert(name, gen);
                services::util::DiskCache(QStringLiteral("screener")).save(name, doc);
            }
            // Show it if it is this scan, or an older scan for the universe
            // selected again meanwhile that is newer than what is on screen.
            if (current || (name == self->cache_name() && gen > self->displayed_gen_)) {
                self->displayed_gen_ = gen;
                self->apply_result(r, !current);
            }
        },
        [self, gen](QString line, bool is_stderr) {
            if (!self || gen != self->scan_gen_ || !is_stderr || !line.startsWith(QLatin1String("PROGRESS ")))
                return;
            // "PROGRESS <done> <total> <text>"
            const int done = line.section(QLatin1Char(' '), 1, 1).toInt();
            const int total = line.section(QLatin1Char(' '), 2, 2).toInt();
            const QString text = line.section(QLatin1Char(' '), 3);
            self->set_progress(done, total, text); // the bar's own label; the corner keeps "Running…"
        },
        kScanTimeoutMs);
}

void ScreenerScreen::apply_result(const screener::ScanResult& r, bool provisional) {
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
    if (provisional && running_)
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
            "made every screen worse over 2010-2026 (see the evidence)."));
    } else {
        market_->clear();
    }
    populate_strategies();
}

void ScreenerScreen::populate_strategies() {
    const QSignalBlocker block(list_);
    list_->clear();
    const bool lists = has_lists();
    auto count_text = [&](int n) {
        return lists ? QStringLiteral("%1 names").arg(n) : QStringLiteral("loading…");
    };
    auto* ov = new QListWidgetItem(QStringLiteral("★  Overview\nOn 2+ screens · %1")
                                       .arg(count_text(int(screener::overview(result_).size()))));
    ov->setData(Qt::UserRole, kOverview);
    ov->setToolTip(QStringLiteral("Stocks that several independent screens pick today"));
    list_->addItem(ov);
    int select = 0;
    for (int i = 0; i < result_.strategies.size(); ++i) {
        const auto& s = result_.strategies[i];
        auto* it = new QListWidgetItem(
            QStringLiteral("%1\n%2 · %3").arg(s.name, s.family, count_text(int(s.symbols.size()))));
        it->setData(Qt::UserRole, s.id);
        it->setToolTip(s.summary);
        list_->addItem(it);
        if (s.id == current_id_)
            select = i + 1;
    }
    list_->setCurrentRow(select);
    show_strategy(list_->item(select)->data(Qt::UserRole).toString());
}

void ScreenerScreen::show_strategy(const QString& id) {
    current_id_ = id;
    plan_box_->setVisible(id == QLatin1String("luk_pullback"));
    fill_card();
    fill_equity_chart();
    if (!has_lists()) {
        update_empty_label();
        centre_->setCurrentIndex(0);
        price_chart_->clear_all();
        return;
    }
    centre_->setCurrentIndex(1);
    fill_table();
}

void ScreenerScreen::update_empty_label() {
    if (!running_) {
        // the scan ended without lists: it failed — say so, and how to retry
        empty_lbl_->setText(
            QStringLiteral("<div style='font-size:15px;color:%1;font-weight:700'>Couldn't build the lists</div>"
                           "<p style='color:%2'>%3</p><p>Check the connection and press REFRESH. The "
                           "strategies and their records on the left and right don't need it.</p>")
                .arg(ui::colors::RED(), ui::colors::TEXT_PRIMARY(), status_->text().toHtmlEscaped()));
        return;
    }
    empty_lbl_->setText(
        QStringLiteral("<div style='font-size:15px;color:%1;font-weight:700'>Building today's lists…</div>"
                       "<p>The first run downloads two years of daily prices for every stock in the "
                       "universe (about a minute for ~1,700). After that a refresh takes seconds.</p>"
                       "<p>The strategies are on the left and each one's rules and measured record are on "
                       "the right already.</p><p style='color:%2'>%3</p>")
            .arg(ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(), progress_lbl_->text().toHtmlEscaped()));
}

void ScreenerScreen::update_plan_cells() {
    if (current_id_ != QLatin1String("luk_pullback") || shares_col_ < 0 || position_col_ < 0)
        return;
    const bool sorting = table_->isSortingEnabled();
    table_->setSortingEnabled(false); // a changed sort key must not reorder rows mid-loop
    for (int r = 0; r < table_->rowCount(); ++r) {
        const auto* sym = table_->item(r, kSymbolCol);
        if (!sym)
            continue;
        const QJsonObject o = result_.rows.value(sym->text());
        const auto entry = screener::num(o, QStringLiteral("entry"));
        const auto shares = screener::position_shares(account_value_, risk_frac_, entry,
                                                      screener::num(o, QStringLiteral("stop")));
        const auto pos = shares && entry ? std::optional<double>(*shares * *entry) : std::nullopt;
        table_->setItem(r, shares_col_, metric_item(Kind::Int, shares));
        table_->setItem(r, position_col_, metric_item(Kind::Compact, pos));
    }
    table_->setSortingEnabled(sorting);
}

void ScreenerScreen::fill_table() {
    const bool ov = current_id_ == kOverview;
    const bool plan = current_id_ == QLatin1String("luk_pullback");
    const screener::Strategy* s = ov ? nullptr : result_.strategy(current_id_);
    if (!ov && !s)
        return;

    QStringList symbols;
    QHash<QString, QStringList> picked_by;
    if (ov) {
        for (const auto& row : screener::overview(result_)) {
            symbols << row.symbol;
            picked_by.insert(row.symbol, row.by);
        }
    } else {
        symbols = s->symbols;
    }

    // One order for every view: who it is and how it trades first, then what
    // THIS strategy is about (and, for the pullback, the trade plan), then the
    // context. Name and sector at the end: wide, and not why you are here.
    enum class Col { Rank, Symbol, Trend, Metric, Shares, Position, PickedBy, Name, Sector };
    struct Spec {
        Col col;
        const screener::ColumnDef* def = nullptr;
        QString header;
    };
    QVector<Spec> cols = {{Col::Rank, nullptr, "#"}, {Col::Symbol, nullptr, "Symbol"}, {Col::Trend, nullptr, "Trend 6M"}};
    auto metric = [&](const char* key) {
        if (const auto* d = screener::column_def(QString::fromLatin1(key)))
            cols.push_back({Col::Metric, d, QString::fromLatin1(d->header)});
    };
    metric("price");
    metric("chg_1d");
    if (ov) {
        metric("screens");
        cols.push_back({Col::PickedBy, nullptr, QStringLiteral("Picked by")});
        metric("rs");
        metric("r3m");
    } else {
        for (const QString& key : s->columns) {
            if (key == QLatin1String("price") || key == QLatin1String("chg_1d"))
                continue;
            metric(key.toLatin1().constData());
            // the plan reads entry, stop, risk -> how many shares, how much money
            if (plan && key == QLatin1String("risk_pct")) {
                cols.push_back({Col::Shares, nullptr, QStringLiteral("Shares")});
                cols.push_back({Col::Position, nullptr, QStringLiteral("Position")});
            }
        }
    }
    cols.push_back({Col::Name, nullptr, QStringLiteral("Name")});
    cols.push_back({Col::Sector, nullptr, QStringLiteral("Sector")});
    metric("mcap");

    const QString keep = table_->currentRow() >= 0 && table_->item(table_->currentRow(), kSymbolCol)
                             ? table_->item(table_->currentRow(), kSymbolCol)->text()
                             : QString();
    table_->setSortingEnabled(false);
    table_->clear();
    table_->setColumnCount(cols.size());
    QStringList headers;
    for (const auto& c : cols)
        headers << c.header;
    table_->setHorizontalHeaderLabels(headers);
    shares_col_ = position_col_ = -1;
    for (int c = 0; c < cols.size(); ++c) {
        if (cols[c].col == Col::Shares)
            shares_col_ = c;
        if (cols[c].col == Col::Position)
            position_col_ = c;
        if (cols[c].col == Col::Name)
            name_col_ = c;
        if (cols[c].col == Col::Sector)
            sector_col_ = c;
        if (auto* h = table_->horizontalHeaderItem(c)) {
            if (cols[c].def)
                h->setToolTip(QString::fromUtf8(cols[c].def->tip));
            else if (cols[c].col == Col::Shares)
                h->setToolTip(QStringLiteral("Shares so that a stop-out costs your chosen risk of the account"));
            else if (cols[c].col == Col::Position)
                h->setToolTip(QStringLiteral("Position value at the entry price"));
        }
    }
    table_->setRowCount(symbols.size());
    for (int row = 0; row < symbols.size(); ++row) {
        const QString& sym = symbols[row];
        const QJsonObject o = result_.rows.value(sym);
        const auto entry = screener::num(o, QStringLiteral("entry"));
        const auto shares = plan ? screener::position_shares(account_value_, risk_frac_, entry,
                                                             screener::num(o, QStringLiteral("stop")))
                                 : std::nullopt;
        for (int c = 0; c < cols.size(); ++c) {
            QTableWidgetItem* it = nullptr;
            switch (cols[c].col) {
                case Col::Rank:
                    it = metric_item(Kind::Int, double(row + 1));
                    break;
                case Col::Symbol:
                    it = text_item(sym, ui::colors::AMBER());
                    break;
                case Col::Trend: {
                    it = metric_item(Kind::PctSigned, screener::num(o, QStringLiteral("r6m")));
                    it->setText(QString());
                    const QVector<double> cl = closes_of(o);
                    const qsizetype n = std::min<qsizetype>(cl.size(), kSparkBars);
                    it->setData(kSparkRole, QVariant::fromValue(QList<double>(cl.end() - n, cl.end())));
                    it->setToolTip(QStringLiteral("6-month closes; sorts by 6-month return"));
                    break;
                }
                case Col::Metric: {
                    const QString key = QString::fromLatin1(cols[c].def->key);
                    const auto v = key == QLatin1String("screens")
                                       ? std::optional<double>(picked_by.value(sym).size())
                                       : screener::num(o, key);
                    it = metric_item(cols[c].def->kind, v);
                    break;
                }
                case Col::Shares:
                    it = metric_item(Kind::Int, shares);
                    break;
                case Col::Position:
                    it = metric_item(Kind::Compact, shares && entry ? std::optional<double>(*shares * *entry)
                                                                    : std::nullopt);
                    break;
                case Col::PickedBy:
                    it = text_item(picked_by.value(sym).join(QStringLiteral(", ")));
                    it->setToolTip(picked_by.value(sym).join(QLatin1Char('\n')));
                    break;
                case Col::Name:
                    it = text_item(o.value(QStringLiteral("name")).toString());
                    break;
                case Col::Sector:
                    it = text_item(o.value(QStringLiteral("sector")).toString());
                    break;
            }
            table_->setItem(row, c, it);
        }
    }
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(2, 96);
    for (int c = 0; c < cols.size(); ++c) {
        const int cap = cols[c].col == Col::Name ? 190 : cols[c].col == Col::Sector ? 140
                      : cols[c].col == Col::PickedBy ? 300 : 0;
        if (cap && table_->columnWidth(c) > cap)
            table_->setColumnWidth(c, cap);
    }

    const QString name = ov ? QStringLiteral("Overview") : s->name;
    table_title_->setText(
        symbols.isEmpty()
            ? QStringLiteral("%1 — no stock passes today").arg(name)
            : ov ? QStringLiteral("Overview — %1 names picked by two or more screens, most-picked first")
                       .arg(symbols.size())
                 : QStringLiteral("%1 — %2 names, ranked by %3%4")
                       .arg(name)
                       .arg(symbols.size())
                       .arg(s->rank_by.toLower(),
                            plan && !result_.plan_asof.isEmpty()
                                ? QStringLiteral(" · plans from the %1 close (a buy stop for the next session)")
                                      .arg(result_.plan_asof)
                                : QString()));
    apply_filter();
    // keep the reader's stock selected across a refill, else the top row
    int sel = -1;
    for (int r = 0; r < table_->rowCount() && !keep.isEmpty(); ++r)
        if (table_->item(r, kSymbolCol)->text() == keep && !table_->isRowHidden(r))
            sel = r;
    for (int r = 0; r < table_->rowCount() && sel < 0; ++r)
        if (!table_->isRowHidden(r))
            sel = r;
    if (sel >= 0)
        table_->setCurrentCell(sel, kSymbolCol);
    else
        price_chart_->clear_all();
}

void ScreenerScreen::apply_filter() {
    const QString q = filter_->text().trimmed();
    for (int r = 0; r < table_->rowCount(); ++r) {
        bool show = q.isEmpty();
        for (int c : {kSymbolCol, name_col_, sector_col_})
            if (!show && table_->item(r, c) && table_->item(r, c)->text().contains(q, Qt::CaseInsensitive))
                show = true;
        table_->setRowHidden(r, !show);
    }
}

void ScreenerScreen::fill_card() {
    namespace fmtn = ui::formatting;
    const QString dim = ui::colors::TEXT_SECONDARY();
    const QString amber = ui::colors::AMBER();
    QString h;
    if (current_id_ == kOverview) {
        h += QStringLiteral("<div style='font-size:13px;font-weight:700;color:%1'>Overview</div>").arg(amber);
        h += QStringLiteral(
            "<p>Stocks picked by two or more of the screens today — where independent methods (trend, "
            "momentum, quality, value, setups) agree. RS Leaders is nested in the Trend Template and counts "
            "once.</p><p>Pick a strategy on the left for its rules, its list and its measured record. Click "
            "a stock for its chart; double-click to open it in Equity Research.</p>"
            "<div style='font-weight:700;color:%1;margin-top:8px'>HOW TO READ THE RECORDS</div>"
            "<p>Every screen was backtested on the S&amp;P 500 as it stood on each date, 2010-2026. "
            "<b>None beat the equal-weighted index with statistical significance</b> (|t| &lt; 2 is "
            "indistinguishable from luck). The chart shows each screen's growth of $1 (grey) against the "
            "index: they bunch around it. A list here is where a method would look, not a forecast.</p>")
                 .arg(amber);
        if (!result_.basis.isEmpty())
            h += QStringLiteral("<p style='color:%1;font-size:11px'>Basis: %2.</p>").arg(dim, result_.basis.toHtmlEscaped());
        card_->setHtml(h);
        return;
    }
    const auto* s = result_.strategy(current_id_);
    if (!s) {
        card_->clear();
        return;
    }
    h += QStringLiteral("<div style='font-size:13px;font-weight:700;color:%1'>%2</div>").arg(amber, s->name.toHtmlEscaped());
    h += QStringLiteral("<div style='color:%1;margin-top:2px'>%2</div>").arg(dim, s->source.toHtmlEscaped());
    h += QStringLiteral("<p>%1</p>").arg(s->summary.toHtmlEscaped());
    h += QStringLiteral("<div style='font-weight:700;color:%1'>RULES</div><ul style='margin-left:-20px'>").arg(amber);
    for (const QString& r : s->rules)
        h += QStringLiteral("<li>%1</li>").arg(r.toHtmlEscaped());
    h += QStringLiteral("</ul><div style='color:%1'>Ranked by: %2</div>").arg(dim, s->rank_by.toHtmlEscaped());

    h += QStringLiteral("<div style='font-weight:700;color:%1;margin-top:10px'>MEASURED RECORD</div>").arg(amber);
    const auto& ev = s->evidence;
    if (ev.tested) {
        const bool significant = ev.t && std::abs(*ev.t) >= 2.0;
        QString verdict;
        if (ev.t && ev.excess)
            verdict = significant ? (*ev.excess > 0 ? QStringLiteral(" (beat the index)")
                                                    : QStringLiteral(" (reliably WORSE than the index)"))
                                  : QStringLiteral(" (noise)");
        h += QStringLiteral("<table cellspacing='0' cellpadding='2'>"
                            "<tr><td style='color:%1'>Excess / yr vs index</td><td align='right'>%2</td></tr>"
                            "<tr><td style='color:%1'>t-stat</td><td align='right'>%3</td></tr>"
                            "<tr><td style='color:%1'>Max drawdown</td><td align='right'>%4</td></tr>")
                 .arg(dim, signed_pct_html(ev.excess),
                      ev.t ? QString::number(*ev.t, 'f', 2) + verdict : fmtn::placeholder(),
                      ev.max_dd ? fmtn::format_percent(*ev.max_dd * 100.0, 1) : fmtn::placeholder());
        if (ev.trades)
            h += QStringLiteral("<tr><td style='color:%1'>Trades / win rate / avg</td><td align='right'>%2 / %3 / %4R</td></tr>")
                     .arg(dim)
                     .arg(*ev.trades)
                     .arg(ev.win_rate ? fmtn::format_percent(*ev.win_rate * 100.0, 0) : fmtn::placeholder())
                     .arg(ev.avg_R ? QString::number(*ev.avg_R, 'f', 2) : fmtn::placeholder());
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
        h += QStringLiteral("<p style='color:%1;font-size:11px'>Basis: %2.</p>").arg(dim, result_.basis.toHtmlEscaped());
    card_->setHtml(h);
}

void ScreenerScreen::fill_equity_chart() {
    using Series = ScreenerChart::Series;
    const QStringList& m = result_.curve_months;
    const QStringList xl = m.isEmpty() ? QStringList{}
                                       : QStringList{m.first(), m.value(m.size() / 2), m.last()};
    equity_chart_->set_x_labels(xl);
    equity_chart_->set_levels({{1.0, ui::colors::BORDER_MED(), QString()}});
    QVector<Series> series;
    if (current_id_ == kOverview) {
        for (const auto& s : result_.strategies)
            if (!s.evidence.curve.isEmpty())
                series.push_back({s.evidence.curve, ui::colors::TEXT_TERTIARY(), QString(), 1.0, false});
        series.push_back({result_.curve_spy, ui::colors::TEXT_SECONDARY(), QStringLiteral("SPY"), 1.4, true});
        series.push_back({result_.curve_ew, ui::colors::AMBER(), QStringLiteral("Index (EW)"), 2.0, false});
        equity_chart_->set_title(QStringLiteral("Growth of $1 — every screen (grey) vs the index"));
        equity_chart_->set_placeholder(QStringLiteral("Backtest curves unavailable"));
    } else if (const auto* s = result_.strategy(current_id_); s && !s->evidence.curve.isEmpty()) {
        series.push_back({result_.curve_spy, ui::colors::TEXT_TERTIARY(), QStringLiteral("SPY"), 1.2, true});
        series.push_back({result_.curve_ew, ui::colors::TEXT_SECONDARY(), QStringLiteral("Index (EW)"), 1.4, false});
        series.push_back({s->evidence.curve, ui::colors::AMBER(), QStringLiteral("This screen"), 2.0, false});
        equity_chart_->set_title(QStringLiteral("Growth of $1 — backtest, log scale"));
        equity_chart_->set_placeholder(QStringLiteral("Backtest curve unavailable"));
    } else {
        equity_chart_->set_title(QStringLiteral("Growth of $1"));
        equity_chart_->set_placeholder(QStringLiteral("Not backtested — no record to draw"));
    }
    equity_chart_->set_series(series);
}

void ScreenerScreen::show_stock(const QString& symbol) {
    using Series = ScreenerChart::Series;
    const QJsonObject o = result_.rows.value(symbol);
    const QVector<double> cl = closes_of(o);
    if (cl.size() < 2) {
        price_chart_->clear_all();
        return;
    }
    // EMAs over all the bars sent, drawn over the same window
    QVector<Series> series;
    series.push_back({screener::ema(cl, 50), ui::colors::CYAN(), QStringLiteral("EMA 50"), 1.1, false});
    series.push_back({screener::ema(cl, 21), ui::colors::AMBER(), QStringLiteral("EMA 21"), 1.1, false});
    series.push_back({cl, ui::colors::TEXT_PRIMARY(), symbol, 1.5, false});
    QVector<ScreenerChart::Level> levels;
    if (current_id_ == QLatin1String("luk_pullback")) {
        const auto e = screener::num(o, QStringLiteral("entry"));
        const auto st = screener::num(o, QStringLiteral("stop"));
        const auto t3 = screener::num(o, QStringLiteral("target3"));
        if (e)
            levels.push_back({*e, ui::colors::INFO(), QStringLiteral("Entry %1").arg(*e, 0, 'f', 2)});
        if (st)
            levels.push_back({*st, ui::colors::RED(), QStringLiteral("Stop %1").arg(*st, 0, 'f', 2), true});
        if (t3)
            levels.push_back({*t3, ui::colors::GREEN(), QStringLiteral("3R %1").arg(*t3, 0, 'f', 2)});
    }
    price_chart_->set_log_scale(false);
    price_chart_->set_title(QStringLiteral("%1 — %2").arg(symbol, o.value(QStringLiteral("name")).toString()));
    price_chart_->set_x_labels({QStringLiteral("%1 sessions ago").arg(cl.size()), QString(), QStringLiteral("latest")});
    price_chart_->set_levels(levels);
    price_chart_->set_series(series);
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
    if (const double a = state.value(QStringLiteral("account")).toDouble(); a > 0) {
        const QSignalBlocker block(account_);
        account_->setValue(a);
        account_value_ = account_->value(); // the spin box clamps; size on what it shows
    }
    if (const double rf = state.value(QStringLiteral("risk")).toDouble(); rf > 0) {
        const int idx = risk_->findData(rf);
        if (idx >= 0) {
            const QSignalBlocker block(risk_);
            risk_->setCurrentIndex(idx);
            risk_frac_ = rf;
        }
    }
}

QVariantMap ScreenerScreen::save_state() const {
    return {{QStringLiteral("strategy"), current_id_},
            {QStringLiteral("min_mcap"), min_mcap()},
            {QStringLiteral("account"), account_value_},
            {QStringLiteral("risk"), risk_frac_}};
}

} // namespace fincept::screens

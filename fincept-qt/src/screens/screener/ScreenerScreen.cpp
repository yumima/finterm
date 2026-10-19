#include "screens/screener/ScreenerScreen.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "screens/ownership/OwnershipUi.h"
#include "services/util/DiskCache.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGuiApplication>
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
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

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
    market_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    market_->setMinimumWidth(60);
    head->addSpacing(12);
    head->addWidget(market_, 1);
    root->addLayout(head);

    // ── status line: as-of / universe, and the scan's progress while it runs.
    // Its own row: the header must not set the screener's minimum width —
    // beside Equity Research the pane is half the window, and an unshrinkable
    // status pushed the whole screen past its edge (band, buttons, table
    // clipped), then collided with the market label. Labels give way; the
    // tooltip keeps the full text.
    auto* srow = new QHBoxLayout;
    srow->setSpacing(8);
    status_ = new QLabel;
    status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status_->setMinimumWidth(60);
    srow->addWidget(status_, 1);
    progress_row_ = new QWidget;
    auto* pr = new QHBoxLayout(progress_row_);
    pr->setContentsMargins(0, 0, 0, 0);
    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    progress_->setFixedWidth(180);
    progress_->setRange(0, 0); // busy until the first PROGRESS line
    progress_lbl_ = new QLabel;
    progress_lbl_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    pr->addWidget(progress_);
    pr->addWidget(progress_lbl_);
    progress_row_->hide();
    srow->addWidget(progress_row_);
    root->addLayout(srow);

    // ── body: strategy rail | (strategy band + its names) ───────────────────
    split_ = new QSplitter(Qt::Horizontal);
    split_->setChildrenCollapsible(false);
    split_->setHandleWidth(3);

    list_ = new QListWidget;
    list_->setMinimumWidth(200);
    split_->addWidget(list_);

    auto* main = new QWidget;
    auto* mainl = new QVBoxLayout(main);
    mainl->setContentsMargins(0, 0, 0, 0);
    mainl->setSpacing(6);

    // The strategy card, always open above its list: what it is, its exact
    // rules and its measured record, every word shown (nothing behind a
    // toggle). Rules and record sit side by side; when Equity Research beside
    // halves the width they stack. A card taller than 75% of the pane scrolls
    // inside its frame rather than clip (render_band).
    band_ = new QTextBrowser;
    band_->setOpenLinks(false);
    band_->setFrameShape(QFrame::NoFrame);
    band_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    band_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    band_->setStyleSheet(QString("QTextBrowser{background:%1;color:%2;border:1px solid %3;padding:4px 8px;}")
                             .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
    band_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // its height is its text's
    mainl->addWidget(band_);
    // the text area's width decides the layout, the pane's height the cap;
    // the viewport, not band_: its resize lands after band_'s own event
    band_->viewport()->installEventFilter(this);
    main->installEventFilter(this);

    centre_ = new QStackedWidget;
    empty_lbl_ = new QLabel;
    empty_lbl_->setWordWrap(true);
    empty_lbl_->setAlignment(Qt::AlignCenter);
    empty_lbl_->setTextFormat(Qt::RichText);
    empty_lbl_->setStyleSheet(QString("color:%1;padding:16px 40px;").arg(ui::colors::TEXT_SECONDARY()));
    // The stack's minimum is its largest page's: this wrapped text must not
    // set it, or the card above is squeezed into its own words. It scrolls
    // instead when the pane is short, so its retry advice is never cut off.
    empty_lbl_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* empty_scroll = new QScrollArea;
    empty_scroll->setWidget(empty_lbl_);
    empty_scroll->setWidgetResizable(true);
    empty_scroll->setFrameShape(QFrame::NoFrame);
    empty_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    empty_scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    centre_->addWidget(empty_scroll);

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
    table_->setToolTip(QStringLiteral("Click a stock to open it in Equity Research beside the list"));
    table_->setItemDelegateForColumn(2, new SparkDelegate(table_));
    ml->addWidget(table_, 1);
    centre_->addWidget(mid);
    mainl->addWidget(centre_, 1);
    split_->addWidget(main);
    split_->setStretchFactor(0, 0);
    split_->setStretchFactor(1, 1);
    root->addWidget(split_, 1);

    follow_timer_ = new QTimer(this);
    follow_timer_->setSingleShot(true);
    follow_timer_->setInterval(180);
    connect(follow_timer_, &QTimer::timeout, this, [this]() {
        auto* it = table_->currentRow() >= 0 ? table_->item(table_->currentRow(), kSymbolCol) : nullptr;
        if (!it || it->text() == last_research_)
            return; // the current STOCK did not change (a sort just moved its row)
        last_research_ = it->text();
        emit research_requested(it->text(), false);
    });

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= list_->count())
            return;
        show_strategy(list_->item(row)->data(Qt::UserRole).toString());
    });
    // A click opens the stock in Equity Research beside the list. Arrow keys
    // follow (debounced, so holding ↓ does not load every name) — but only
    // into an ER that is already open; browsing alone never pops one up.
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row, int, int prev, int) {
        // a mouse press is the click path's to handle (cellClicked) — a press
        // held past the debounce otherwise loaded the stock twice
        if (!filling_ && row >= 0 && row != prev && QGuiApplication::mouseButtons() == Qt::NoButton)
            follow_timer_->start();
    });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        follow_timer_->stop();
        auto* it = table_->item(row, kSymbolCol);
        if (!it)
            return;
        last_research_ = it->text();
        // Queued, then focus back to the table: opening ER inside the mouse
        // release splits the dock area (reparenting this screen, which drops
        // focus) and builds ER (which may take it) — and then ↓ did nothing.
        const QString sym = it->text();
        QTimer::singleShot(0, this, [this, sym]() {
            emit research_requested(sym, true);
            table_->setFocus(Qt::OtherFocusReason);
        });
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

bool ScreenerScreen::eventFilter(QObject* o, QEvent* e) {
    if (band_ && e->type() == QEvent::Resize && (o == band_->viewport() || o == band_->parentWidget()))
        render_band(); // unchanged width and cap are a no-op there: no loop
    return QWidget::eventFilter(o, e);
}

void ScreenerScreen::render_band(bool force) {
    if (!band_)
        return;
    const int w = band_->viewport()->width();
    // The table keeps at least a quarter of the pane (5+ rows beside Equity
    // Research at 1080p, where the Overview card fits): a card taller than that
    // (the Leader Pullback beside Equity Research) scrolls inside its frame
    // rather than clip its words or push the list off screen.
    const int cap = std::max(160, int(band_->parentWidget()->height() * 0.75));
    if (!force && w == band_width_ && cap == band_cap_)
        return;
    band_cap_ = cap;
    band_width_ = w;
    // Side by side the rules wrap to ~60% of the card; under ~640 px (Equity
    // Research beside) that is too narrow, so the record goes below.
    QString body;
    if (w >= 640)
        body = QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0'><tr>"
                              "<td width='58%' valign='top' style='padding-right:18px'>%1</td>"
                              "<td valign='top'>%2</td></tr></table>")
                   .arg(band_rules_, band_record_);
    else
        body = band_rules_ + QStringLiteral("<div style='margin-top:8px'></div>") + band_record_;
    const QString html = band_head_ + body + band_basis_;
    // Re-parse only when the words or their arrangement change: a splitter
    // drag or a finished scan must not throw a reader of a capped card back
    // to its top.
    if (force || html != band_html_) {
        const int at = force ? 0 : band_->verticalScrollBar()->value();
        band_html_ = html;
        band_->setHtml(html);
        band_->verticalScrollBar()->setValue(at);
    }
    // Every word shown: the card is as tall as its document laid out at this
    // width, so the table below gets the rest and nothing is clipped.
    auto* doc = band_->document();
    doc->setTextWidth(w);
    const int chrome = band_->height() - band_->viewport()->height(); // border + padding
    int need = int(std::ceil(doc->size().height())) + chrome;
    // a run that cannot wrap at this width would be cut off on the right: scroll instead
    const bool too_wide = doc->idealWidth() > w + 1;
    band_->setHorizontalScrollBarPolicy(too_wide ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    if (too_wide)
        need += band_->horizontalScrollBar()->sizeHint().height();
    band_->setVerticalScrollBarPolicy(need > cap ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    band_->setFixedHeight(std::min(need, cap));
}

void ScreenerScreen::even_out_panes() {
    // Once, at the first real width: a slim strategy rail (~17%, 190-280 px)
    // and the rest for the band + table. Equity Research takes the right of
    // the window when a stock is clicked. After that the reader's drag wins.
    if (panes_set_ || !split_ || split_->width() < 600)
        return;
    panes_set_ = true;
    const int w = split_->width();
    const int left = std::clamp(int(w * 0.18), 210, 260);
    split_->setSizes({left, w - left});
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
    status_->setToolTip(text);
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
        if (r.intraday && r.live_applied > 0) {
            // say exactly what today's bar is: whose quote, how delayed, from when
            const QString delay = !r.live_delay_min       ? QStringLiteral(", delay unknown")
                                  : *r.live_delay_min > 0 ? QStringLiteral(", %1-min delayed").arg(*r.live_delay_min)
                                                          : QStringLiteral(", real-time");
            QString line = QStringLiteral("Intraday %1 · %2 names on TradingView quotes%3%4")
                               .arg(r.asof)
                               .arg(r.live_applied)
                               .arg(delay, r.live_price_time.isEmpty()
                                               ? QString()
                                               : QStringLiteral(" (prices ≈%1)").arg(r.live_price_time));
            if (r.live_on_last_close > 0)
                line += QStringLiteral(" · %1 on their last close").arg(r.live_on_last_close);
            bits << line;
        }

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
    fill_band();
    render_band(true);
    if (!has_lists()) {
        update_empty_label();
        centre_->setCurrentIndex(0);
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
                           "strategies and their rules and records above don't need it.</p>")
                .arg(ui::colors::RED(), ui::colors::TEXT_PRIMARY(), status_->text().toHtmlEscaped()));
        return;
    }
    empty_lbl_->setText(
        QStringLiteral("<div style='font-size:15px;color:%1;font-weight:700'>Building today's lists…</div>"
                       "<p>The first run downloads two years of daily prices for every stock in the "
                       "universe (about a minute for ~1,700). After that a refresh takes seconds.</p>"
                       "<p>The strategies are on the left and each one's rules and measured record are "
                       "above already.</p><p style='color:%2'>%3</p>")
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
    // The whole refill is programmatic — clear(), the re-sort when sorting is
    // re-enabled, the final setCurrentCell all move the current cell — and
    // none of it is the reader's pick.
    filling_ = true;
    follow_timer_->stop();
    const bool ov = current_id_ == kOverview;
    const bool plan = current_id_ == QLatin1String("luk_pullback");
    const screener::Strategy* s = ov ? nullptr : result_.strategy(current_id_);
    if (!ov && !s) {
        filling_ = false;
        return;
    }

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
        table_->setCurrentCell(sel, kSymbolCol); // keep the place, but do not load it into ER
    follow_timer_->stop();
    filling_ = false;
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

void ScreenerScreen::fill_band() {
    namespace fmtn = ui::formatting;
    const QString dim = ui::colors::TEXT_SECONDARY();
    const QString amber = ui::colors::AMBER();
    const auto head = [&](const QString& t) {
        return QStringLiteral("<div style='font-weight:700;color:%1'>%2</div>").arg(amber, t);
    };
    // "beat the index" only at |t| >= 2; below that the excess is noise
    const auto verdict_of = [](const screener::Evidence& ev) -> QString {
        if (!ev.tested || !ev.t || !ev.excess)
            return {};
        if (std::abs(*ev.t) < 2.0)
            return QStringLiteral("noise");
        return *ev.excess > 0 ? QStringLiteral("beat the index") : QStringLiteral("reliably WORSE");
    };
    const auto t_text = [&](const screener::Evidence& ev) {
        const QString v = verdict_of(ev);
        return ev.t ? QString::number(*ev.t, 'f', 2) + (v.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(v))
                    : fmtn::placeholder();
    };
    band_basis_ = result_.basis.isEmpty()
                      ? QString()
                      : QStringLiteral("<div style='color:%1;font-size:11px;margin-top:6px'>Basis: %2.</div>")
                            .arg(dim, result_.basis.toHtmlEscaped());
    // strategy strings are text, never markup: every one is escaped
    const auto make_head = [&](const QString& name, const QString& sub, const QString& summary) {
        return QStringLiteral("<div style='color:%1;font-weight:700;font-size:14px'>%2</div>"
                              "<div style='color:%3'>%4</div><div style='margin:3px 0 6px 0'>%5</div>")
            .arg(amber, name.toHtmlEscaped(), dim, sub.toHtmlEscaped(), summary.toHtmlEscaped());
    };

    if (current_id_ == kOverview) {
        band_head_ = make_head(
            QStringLiteral("Overview"),
            QStringLiteral("Names on two or more screens (RS Leaders, nested in the Trend Template, counts once)"),
            QStringLiteral("Where independent methods agree today. Pick a strategy on the left for its own list "
                           "and rules; click any stock to open it in Equity Research beside the list."));
        band_rules_ = (
            head(QStringLiteral("HOW TO READ THE RECORDS")) +
            QStringLiteral("<p style='margin-top:2px'>Every screen was backtested on the S&amp;P 500 as it stood "
                           "on each date, 2010-2026, monthly, after costs (the Leader Pullback as individual "
                           "trades). <b>None beat the equal-weighted index with statistical significance</b>: "
                           "|t| &lt; 2 is indistinguishable from luck. A list here is where a method would look, "
                           "not a forecast.</p>"));
        // every screen's record side by side: the big picture in one table
        QString t = head(QStringLiteral("EVERY SCREEN'S RECORD &nbsp;<span style='font-weight:400;color:%1'>"
                                        "excess / yr vs index &nbsp;·&nbsp; t-stat</span>")
                             .arg(dim)) +
                    QStringLiteral("<table cellspacing='0' cellpadding='1'>");
        for (const auto& st : result_.strategies) {
            const auto& ev = st.evidence;
            t += QStringLiteral("<tr><td>%1</td><td align='right'>&nbsp;%2</td><td align='right'>&nbsp;&nbsp;%3</td></tr>")
                     .arg(st.name.toHtmlEscaped(),
                          ev.tested ? signed_pct_html(ev.excess) : QStringLiteral("<span style='color:%1'>—</span>").arg(dim),
                          ev.tested ? t_text(ev).toHtmlEscaped()
                                    : QStringLiteral("<span style='color:%1'>not backtested</span>").arg(dim));
        }
        band_record_ = t + QStringLiteral("</table>");
        return;
    }
    const auto* s = result_.strategy(current_id_);
    if (!s) {
        band_head_.clear();
        band_rules_.clear();
        band_record_.clear();
        return;
    }
    band_head_ = make_head(s->name, QStringLiteral("%1 · %2").arg(s->family, s->source), s->summary);

    QString h = head(QStringLiteral("RULES")) + QStringLiteral("<ul style='margin-left:-24px;margin-top:2px'>");
    for (const QString& r : s->rules)
        h += QStringLiteral("<li>%1</li>").arg(r.toHtmlEscaped());
    h += QStringLiteral("</ul><div style='color:%1'>Ranked by: %2</div>").arg(dim, s->rank_by.toHtmlEscaped());
    band_rules_ = h;

    const auto& ev = s->evidence;
    QString r = head(QStringLiteral("MEASURED RECORD"));
    if (ev.tested) {
        r += QStringLiteral("<table cellspacing='0' cellpadding='1'>"
                            "<tr><td style='color:%1'>Excess / yr vs index</td><td align='right'>&nbsp;%2</td></tr>"
                            "<tr><td style='color:%1'>t-stat</td><td align='right'>&nbsp;%3</td></tr>"
                            "<tr><td style='color:%1'>Max drawdown</td><td align='right'>&nbsp;%4</td></tr>")
                 .arg(dim, signed_pct_html(ev.excess), t_text(ev).toHtmlEscaped(),
                      ev.max_dd ? fmtn::format_percent(*ev.max_dd * 100.0, 1) : fmtn::placeholder());
        if (ev.trades)
            r += QStringLiteral("<tr><td style='color:%1'>Trades / win rate / avg</td><td align='right'>&nbsp;%2 / %3 / %4R</td></tr>")
                     .arg(dim)
                     .arg(*ev.trades)
                     .arg(ev.win_rate ? fmtn::format_percent(*ev.win_rate * 100.0, 0) : fmtn::placeholder())
                     .arg(ev.avg_R ? QString::number(*ev.avg_R, 'f', 2) : fmtn::placeholder());
        for (int i = 0; i < ev.subperiods.size() && i < result_.subperiods.size(); ++i)
            r += QStringLiteral("<tr><td style='color:%1'>&nbsp;&nbsp;%2</td><td align='right'>&nbsp;%3</td></tr>")
                     .arg(dim, result_.subperiods[i].toHtmlEscaped(), signed_pct_html(ev.subperiods[i]));
        r += QStringLiteral("</table>");
    } else {
        r += QStringLiteral("<div style='color:%1'>Not backtested.</div>").arg(dim);
    }
    if (!ev.note.isEmpty())
        r += QStringLiteral("<p style='margin-top:4px'>%1</p>").arg(ev.note.toHtmlEscaped());
    band_record_ = r;
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

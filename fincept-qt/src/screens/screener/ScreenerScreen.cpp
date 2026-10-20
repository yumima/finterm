#include "screens/screener/ScreenerScreen.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "screens/ownership/OwnershipUi.h"
#include "services/markets/MarketDataService.h"
#include "services/portfolio/PortfolioService.h"
#include "services/util/DiskCache.h"
#include "storage/repositories/PortfolioRepository.h"
#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
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
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

namespace {

constexpr const char* TAG = "Screener";
namespace ou = ownership_ui;
using screener::Kind;

const QString kOverview = QStringLiteral("__overview__");
const QString kWatchPortfolio = QStringLiteral("Screener Watch");

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
    // long names ("Breakout Leaders (Kullamaggie-style)") wrap, not elide
    list_->setWordWrap(true);
    list_->setTextElideMode(Qt::ElideNone);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    split_->addWidget(list_);

    auto* main = new QWidget;
    auto* mainl = new QVBoxLayout(main);
    mainl->setContentsMargins(0, 0, 0, 0);
    mainl->setSpacing(6);

    // The strategy's summary above its list — what it is, where it comes
    // from, its record in one line — every word shown, sized to its text
    // (render_band). The full write-up is the right-hand pane (details_).
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
    // Passing today | one rule away: what already qualifies, and what is
    // closest to qualifying next (only screens with a tested setting-up list)
    view_box_ = new QWidget;
    auto* vl = new QHBoxLayout(view_box_);
    vl->setContentsMargins(0, 0, 8, 0);
    vl->setSpacing(0);
    passing_btn_ = new QPushButton;
    setup_btn_ = new QPushButton;
    for (QPushButton* b : {passing_btn_, setup_btn_}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString("QPushButton:checked{background:%1;color:%2;border-color:%1;}")
                             .arg(ui::colors::AMBER(), ui::colors::BG_BASE()));
        vl->addWidget(b);
    }
    passing_btn_->setToolTip(QStringLiteral("The names that pass every rule today"));
    setup_btn_->setToolTip(QStringLiteral(
        "Setting up: the names next in line, with the rule each misses and how far it is — the "
        "closest to qualifying next. See the write-up for how such a list did."));
    auto* views = new QButtonGroup(this);
    views->setExclusive(true);
    views->addButton(passing_btn_);
    views->addButton(setup_btn_);
    passing_btn_->setChecked(true);
    view_box_->hide();
    tools->addWidget(view_box_);
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
    table_->setToolTip(QStringLiteral("Click a stock to open it in Equity Research beside the list; "
                                      "right-click to add it to a portfolio"));
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setItemDelegateForColumn(2, new SparkDelegate(table_));
    ml->addWidget(table_, 1);
    centre_->addWidget(mid);
    // 2 = the write-up in place of the list, for a window too narrow for the
    // right-hand pane (show_details_inline); a link at its top goes back
    inline_ = new QTextBrowser;
    inline_->setOpenLinks(false);
    inline_->setStyleSheet(QString("QTextBrowser{background:%1;color:%2;border:1px solid %3;padding:8px 14px;}")
                               .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
    connect(inline_, &QTextBrowser::anchorClicked, this, [this](const QUrl& u) {
        if (!open_reading(u))
            leave_inline(); // the "Back to the list" link
    });
    inline_->viewport()->installEventFilter(this);
    centre_->addWidget(inline_);
    mainl->addWidget(centre_, 1);
    split_->addWidget(main);
    split_->setStretchFactor(0, 0);
    split_->setStretchFactor(1, 1);
    // The right half: the selected strategy's full write-up — its idea,
    // rules, how its author uses it, the columns, the measured record and how
    // it was measured — a reference to learn from. Clicking a stock puts
    // Equity Research there instead (beside the screener, which then fills
    // its half and hides this); clicking a strategy brings this back.
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(10);
    body->addWidget(split_, 1);
    details_pane_ = new QWidget;
    auto* dpl = new QVBoxLayout(details_pane_);
    dpl->setContentsMargins(0, 0, 0, 0);
    dpl->setSpacing(4);
    // Layout: Auto (tiles when the pane is wide enough, one column when not)
    // | Tiles | Column — the reader's choice, kept with the screen's state
    auto* lay_row = new QHBoxLayout;
    lay_row->setSpacing(0);
    auto* lay_lbl = new QLabel(QStringLiteral("Layout  "));
    lay_lbl->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    lay_row->addStretch(1);
    lay_row->addWidget(lay_lbl);
    auto* lay_group = new QButtonGroup(this);
    lay_group->setExclusive(true);
    const QStringList lay_names = {QStringLiteral("Auto"), QStringLiteral("Tiles"), QStringLiteral("Column")};
    const QStringList lay_tips = {QStringLiteral("Tiles on a wide pane, one column on a narrow one"),
                                  QStringLiteral("The three parts side by side: least scrolling on a wide screen"),
                                  QStringLiteral("One column: easiest to read on a laptop")};
    for (int i = 0; i < lay_names.size(); ++i) {
        auto* b = new QPushButton(lay_names[i]);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(lay_tips[i]);
        b->setStyleSheet(QString("QPushButton{padding:1px 10px;}"
                                 "QPushButton:checked{background:%1;color:%2;border-color:%1;}")
                             .arg(ui::colors::AMBER(), ui::colors::BG_BASE()));
        lay_group->addButton(b, i);
        lay_row->addWidget(b);
    }
    layout_group_ = lay_group;
    lay_group->button(0)->setChecked(true);
    connect(lay_group, &QButtonGroup::idClicked, this, [this](int id) {
        details_layout_ = id;
        render_details(true);
        if (inline_on_)
            render_inline();
    });
    dpl->addLayout(lay_row);
    details_ = new QTextBrowser;
    details_->setOpenLinks(false);
    details_->setStyleSheet(QString("QTextBrowser{background:%1;color:%2;border:1px solid %3;padding:8px 14px;}")
                                .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
    connect(details_, &QTextBrowser::anchorClicked, this, [this](const QUrl& u) { open_reading(u); });
    // its width picks the tile count
    details_->viewport()->installEventFilter(this);
    dpl->addWidget(details_, 1);
    details_pane_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    details_pane_->hide();
    body->addWidget(details_pane_, 1);
    root->addLayout(body, 1);

    flash_timer_ = new QTimer(this);
    flash_timer_->setSingleShot(true);
    flash_timer_->setInterval(8000);
    connect(flash_timer_, &QTimer::timeout, this, [this]() { set_status(flash_prev_, flash_prev_error_); });
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
    // A click on a strategy — the current one too — is a request to read
    // about it: its write-up takes the right half again, in place of an
    // Equity Research opened beside. Arrowing through the rail only switches.
    // Click and Enter both ask; a style that activates on single click sends
    // both for one click, so they coalesce into one request.
    const auto ask = [this](QListWidgetItem*) {
        if (details_pending_)
            return;
        details_pending_ = true;
        QTimer::singleShot(0, this, [this]() {
            details_pending_ = false;
            emit details_requested();
        });
    };
    connect(list_, &QListWidget::itemClicked, this, ask);
    connect(list_, &QListWidget::itemActivated, this, ask);
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
        if (auto* it = table_->item(row, kSymbolCol))
            open_research(it->text());
    });
    connect(table_, &QTableWidget::customContextMenuRequested, this, &ScreenerScreen::show_row_menu);
    connect(filter_, &QLineEdit::textChanged, this, [this](const QString&) { apply_filter(); });
    connect(setup_btn_, &QPushButton::toggled, this, [this](bool on) {
        setting_up_ = on; // no early return: a restored state may disagree with the button
        if (has_lists())
            fill_table();
    });
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
    return QStringLiteral("scan_v5_%1.json").arg(qint64(min_mcap())); // v5: setting up for every screen
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
    update_details_pane();
    even_out_panes();
}

void ScreenerScreen::render_details(bool force) {
    if (!details_)
        return;
    const QString html = compose_details(details_columns(layout_width(details_)));
    if (!force && html == details_shown_)
        return;
    // the same strategy re-rendered (a finished scan, a new width): keep the place
    const int at = details_id_ == current_id_ ? details_->verticalScrollBar()->value() : 0;
    details_->setHtml(html);
    details_shown_ = html;
    if (at > 0)
        details_->verticalScrollBar()->setValue(at);
    details_id_ = current_id_;
    // Repaint it all: the new document's layout (and the scroll bar it may
    // bring in) can leave the old pixels on screen otherwise — a write-up
    // showed its header twice, ~90 px apart, until the next full repaint.
    details_->viewport()->update();
}

int ScreenerScreen::layout_width(const QTextBrowser* b) {
    // The width to choose tiles by, as if the scroll bar were not there: the
    // bar comes and goes WITH the tile count (3 columns -> taller -> a bar ->
    // narrower -> 2 columns -> shorter -> no bar -> 3 ...), so choosing by the
    // viewport alone re-rendered without end at some sizes.
    const auto* bar = b->verticalScrollBar();
    return b->viewport()->width() + (bar && bar->isVisible() ? bar->width() : 0);
}

bool ScreenerScreen::open_reading(const QUrl& u) {
    // only the write-up's own web links; anything else is not ours to open
    if (u.scheme() != QLatin1String("https"))
        return false;
    QDesktopServices::openUrl(u);
    return true;
}

void ScreenerScreen::update_details_pane() {
    // Most of a wide window = Equity Research is not beside: the write-up
    // takes the right half. Beside ER the screener is about half the window
    // and fills its pane. Its own width decides, never the pane's
    // visibility: no feedback loop.
    const QWidget* win = window();
    const bool alone = win && width() >= 1100 && width() > win->width() * 0.7;
    if (details_pane_ && details_pane_->isVisible() != alone) {
        details_pane_->setVisible(alone);
        if (alone)
            leave_inline(); // the pane on the right has it now
        render_band(true); // its pointer to the write-up depends on this
    }
}

bool ScreenerScreen::eventFilter(QObject* o, QEvent* e) {
    if (band_ && e->type() == QEvent::Resize && (o == band_->viewport() || o == band_->parentWidget()))
        render_band(); // unchanged width and cap are a no-op there: no loop
    // The tile count follows the width — after the resize has finished: a new
    // document set from inside the scroll area's own resize handling (the
    // scroll bar appearing) is laid out mid-geometry-change. Coalesced.
    if (e->type() == QEvent::Resize && ((details_ && o == details_->viewport()) ||
                                        (inline_ && inline_on_ && o == inline_->viewport()))) {
        if (!relayout_pending_) {
            relayout_pending_ = true;
            QTimer::singleShot(0, this, [this]() {
                relayout_pending_ = false;
                render_details(); // same html is a no-op
                if (inline_on_)
                    render_inline();
            });
        }
    }
    return QWidget::eventFilter(o, e);
}

void ScreenerScreen::render_band(bool force) {
    if (!band_)
        return;
    const int w = band_->viewport()->width();
    // The table keeps at least a quarter of the pane: a card taller than that
    // (a tiny window) scrolls inside its frame rather than clip its words or
    // push the list off screen.
    const int cap = std::max(160, int(band_->parentWidget()->height() * 0.75));
    if (!force && w == band_width_ && cap == band_cap_)
        return;
    band_cap_ = cap;
    band_width_ = w;
    // with the write-up hidden (Equity Research beside), say where it went
    const QString html = band_html_ +
                         (details_pane_ && !details_pane_->isVisible() && !band_html_.isEmpty()
                              ? QStringLiteral("<div style='color:%1;margin-top:4px'>Click the strategy on the "
                                               "left for its full write-up.</div>")
                                    .arg(ui::colors::TEXT_TERTIARY())
                              : QString());
    // Re-parse only when the words change: a splitter drag or a finished
    // scan must not throw a reader of a capped card back to its top.
    if (force || html != band_shown_) {
        const int at = band_->verticalScrollBar()->value();
        band_shown_ = html;
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
    // Once, at the first real width: a strategy rail (~24% of the screener,
    // 230-280 px, so a name wraps over two lines rather than eliding)
    // and the rest for the band + table. Equity Research takes the right of
    // the window when a stock is clicked. After that the reader's drag wins.
    if (panes_set_ || !split_ || split_->width() < 600)
        return;
    panes_set_ = true;
    const int w = split_->width();
    const int left = std::clamp(int(w * 0.24), 230, 280);
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
    if (flash_timer_ && flash_timer_->isActive()) {
        // the scan wrote while a message is flashed: that is what returns after it
        flash_prev_ = text;
        flash_prev_error_ = error;
        return;
    }
    status_error_ = error;
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
            "made every screen it was tried on worse over 2010-2026 (see Overview)."));
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
    update_view_toggle();
    fill_band();
    render_band(true);
    render_details();
    if (inline_on_)
        render_inline();
    if (!has_lists()) {
        update_empty_label();
        if (!inline_on_)
            centre_->setCurrentIndex(0);
        return;
    }
    if (!inline_on_)
        centre_->setCurrentIndex(1);
    fill_table();
}

bool ScreenerScreen::setting_up() const {
    const auto* s = result_.strategy(current_id_);
    return setting_up_ && s && !s->setups_from.isEmpty();
}

void ScreenerScreen::update_view_toggle() {
    const auto* s = current_id_ == kOverview ? nullptr : result_.strategy(current_id_);
    const bool show = s && !s->setups_from.isEmpty() && has_lists();
    view_box_->setVisible(show);
    if (!show)
        return;
    passing_btn_->setText(QStringLiteral("Passing · %1").arg(s->symbols.size()));
    setup_btn_->setText(QStringLiteral("Setting up · %1").arg(result_.setups.value(s->setups_from).size()));
    setup_btn_->setToolTip(s->setups_from == current_id_
                               ? QStringLiteral("Next in line: %1 — with the rule each misses and how far it is")
                                     .arg(s->setup_desc)
                               : QStringLiteral("Names one rule from the Trend Template (whose top 25 by RS this "
                                                "list is), with the rule and how far they are"));
    const QSignalBlocker b1(passing_btn_), b2(setup_btn_);
    (setting_up_ ? setup_btn_ : passing_btn_)->setChecked(true); // the group unchecks the other
}

QString ScreenerScreen::inline_html() const {
    return QStringLiteral("<p><a href='back' style='color:%1'>◂ Back to the list</a></p>")
               .arg(ui::colors::AMBER()) +
           compose_details(details_columns(layout_width(inline_)));
}

void ScreenerScreen::render_inline() {
    const QString html = inline_html();
    if (html == inline_shown_)
        return;
    // the same strategy re-rendered (a finished scan): keep the reader's place
    const int at = inline_id_ == current_id_ ? inline_->verticalScrollBar()->value() : 0;
    inline_->setHtml(html);
    inline_shown_ = html;
    inline_id_ = current_id_;
    if (at > 0)
        inline_->verticalScrollBar()->setValue(at);
    inline_->viewport()->update(); // see render_details
}

void ScreenerScreen::show_details_inline() {
    if (details_pane_->isVisible() || details_html_.isEmpty())
        return; // the right-hand pane has it
    inline_on_ = true;
    render_inline();
    centre_->setCurrentIndex(2);
}

void ScreenerScreen::leave_inline() {
    if (!inline_on_)
        return;
    inline_on_ = false;
    centre_->setCurrentIndex(has_lists() ? 1 : 0);
}

void ScreenerScreen::update_empty_label() {
    if (!running_) {
        // the scan ended without lists: it failed — say so, and how to retry
        empty_lbl_->setText(
            QStringLiteral("<div style='font-size:15px;color:%1;font-weight:700'>Couldn't build the lists</div>"
                           "<p style='color:%2'>%3</p><p>Check the connection and press REFRESH. The "
                           "strategies' write-ups and records don't need it.</p>")
                .arg(ui::colors::RED(), ui::colors::TEXT_PRIMARY(), status_->text().toHtmlEscaped()));
        return;
    }
    empty_lbl_->setText(
        QStringLiteral("<div style='font-size:15px;color:%1;font-weight:700'>Building today's lists…</div>"
                       "<p>The first run downloads two years of daily prices for every stock in the "
                       "universe (about a minute for ~1,700). After that a refresh takes seconds.</p>"
                       "<p>The strategies are on the left and each one's write-up and measured record are "
                       "ready already — click a strategy to read it.</p><p style='color:%2'>%3</p>")
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
    } else if (setting_up()) {
        for (const auto& su : result_.setups.value(s->setups_from))
            symbols << su.symbol;
    } else {
        symbols = s->symbols;
    }
    const bool su_view = !ov && setting_up();
    const bool tt_setups = su_view && s->setups_from == QLatin1String("trend_template"); // ranked by RS
    QHash<QString, screener::ScanResult::Setup> setup_of;
    if (su_view)
        for (const auto& su : result_.setups.value(s->setups_from))
            setup_of.insert(su.symbol, su);

    // One order for every view: who it is and how it trades first, then what
    // THIS strategy is about (and, for the pullback, the trade plan), then the
    // context. Name and sector at the end: wide, and not why you are here.
    enum class Col { Rank, Symbol, Trend, Metric, Shares, Position, PickedBy, Misses, Gap, Name, Sector };
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
    } else if (su_view) {
        // what it misses and how far, then the strategy's own columns
        cols.push_back({Col::Misses, nullptr, QStringLiteral("Misses")});
        cols.push_back({Col::Gap, nullptr, QStringLiteral("To pass")});
        if (tt_setups) {
            metric("rs");
            metric("off_high");
            metric("r3m");
        } else {
            for (const QString& key : s->columns)
                if (key != QLatin1String("price") && key != QLatin1String("chg_1d") &&
                    key != QLatin1String("entry") && key != QLatin1String("stop") &&
                    key != QLatin1String("risk_pct") && key != QLatin1String("target3"))
                    metric(key.toLatin1().constData()); // no trade plan for a setup not yet formed
        }
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

    // the column the list is ranked by: marked in its header, its values bold
    const QString rank_key = ov ? QStringLiteral("screens") : tt_setups ? QStringLiteral("rs") : s->rank_key;
    const bool rank_asc = !ov && !tt_setups && s->rank_asc;
    const QString arrow = rank_asc ? QStringLiteral(" ▲") : QStringLiteral(" ▼");
    for (auto& c : cols) {
        if (c.def && QLatin1String(c.def->key) == rank_key)
            c.header += arrow;

    }

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
            const bool ranks = cols[c].def ? QLatin1String(cols[c].def->key) == rank_key
                                           : cols[c].col == Col::Rank && rank_key.isEmpty();
            if (ranks)
                h->setToolTip(QStringLiteral("Ranks the list (%1). %2")
                                  .arg(ov ? QStringLiteral("most-picked first, then RS")
                                          : tt_setups ? QStringLiteral("RS, highest first") : s->rank_by,
                                       cols[c].def ? QString::fromUtf8(cols[c].def->tip) : QString()));
            else if (cols[c].def)
                h->setToolTip(QString::fromUtf8(cols[c].def->tip));
            else if (cols[c].col == Col::Shares)
                h->setToolTip(QStringLiteral("Shares so that a stop-out costs your chosen risk of the account"));
            else if (cols[c].col == Col::Position)
                h->setToolTip(QStringLiteral("Position value at the entry price"));
            else if (cols[c].col == Col::Misses)
                h->setToolTip(QStringLiteral("The one rule it fails today"));
            else if (cols[c].col == Col::Gap)
                h->setToolTip(QStringLiteral(
                    "How far that rule's measure must still move. A close is one day's move; a slow average "
                    "rising takes weeks — sort within the same rule"));
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
                    if (key == rank_key) {
                        QFont f = it->font();
                        f.setBold(true);
                        it->setFont(f);
                    }
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
                case Col::Misses: {
                    const auto& su = setup_of.value(sym);
                    it = text_item(su.short_rule.isEmpty() ? su.rule : su.short_rule);
                    it->setToolTip(su.rule);
                    break;
                }
                case Col::Gap: {
                    // the move still needed; the sentence on hover
                    // sorts by the number; reads in the list's own unit (%, pts, ranks, tests)
                    it = metric_item(Kind::Pct, setup_of.value(sym).gap);
                    if (!setup_of.value(sym).gap_short.isEmpty())
                        it->setText(setup_of.value(sym).gap_short);
                    it->setToolTip(setup_of.value(sym).gap_text);
                    break;
                }
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
                      : cols[c].col == Col::PickedBy ? 300 : cols[c].col == Col::Misses ? 260 : 0;
        if (cap && table_->columnWidth(c) > cap)
            table_->setColumnWidth(c, cap);
    }

    const QString name = ov ? QStringLiteral("Overview") : s->name;
    // RS Leaders shows the Trend Template's setting-up list: say whose it is
    const auto* target = su_view ? result_.strategy(s->setups_from) : nullptr;
    const QString su_target = !su_view ? QString()
                              : s->setups_from == current_id_ || !target ? QStringLiteral("passing")
                                                                          : QStringLiteral("the ") + target->name;
    table_title_->setText(
        su_view ? (symbols.isEmpty()
                       ? (tt_setups ? QStringLiteral("%1 — no name is one rule from %2 today").arg(name, su_target)
                                    : QStringLiteral("%1 — no name is next in line today").arg(name))
                       : tt_setups ? QStringLiteral("%1 — setting up: %2 names one rule from %3, ranked by RS")
                                         .arg(name)
                                         .arg(symbols.size())
                                         .arg(su_target)
                                   : QStringLiteral("%1 — setting up: %2 names next in line, in the list's own order")
                                         .arg(name)
                                         .arg(symbols.size()))
        : symbols.isEmpty()
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

void ScreenerScreen::open_research(const QString& sym) {
    last_research_ = sym;
    // Queued, then focus back to the table: opening ER inside the mouse
    // release splits the dock area (reparenting this screen, which drops
    // focus) and builds ER (which may take it) — and then ↓ did nothing.
    QTimer::singleShot(0, this, [this, sym]() {
        emit research_requested(sym, true);
        table_->setFocus(Qt::OtherFocusReason);
    });
}

void ScreenerScreen::flash_status(const QString& text, bool error) {
    if (!flash_timer_->isActive()) { // a flash over a flash keeps the status beneath both
        flash_prev_ = status_->text();
        flash_prev_error_ = status_error_;
    }
    flash_timer_->stop(); // so set_status shows it rather than holding it for after
    set_status(text, error);
    flash_timer_->start();
}

void ScreenerScreen::show_row_menu(const QPoint& pos) {
    const int row = table_->rowAt(pos.y());
    auto* it = row >= 0 ? table_->item(row, kSymbolCol) : nullptr;
    if (!it)
        return;
    follow_timer_->stop(); // the right-press moved the current row: not a pick to follow
    const QString sym = it->text();
    QMenu menu(this);
    menu.addAction(QStringLiteral("Open %1 in Equity Research").arg(sym), this, [this, sym]() { open_research(sym); });
    menu.addAction(QStringLiteral("Add %1 to a portfolio…").arg(sym), this, [this, sym]() { add_to_portfolio(sym); });
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

// A position at the current price: the screener's price shows at once, then a
// fresh quote replaces it unless the reader has typed their own. The dialog is
// the confirmation — nothing is recorded until Add.
void ScreenerScreen::add_to_portfolio(const QString& sym) {
    const auto listed = screener::num(result_.rows.value(sym), QStringLiteral("price"));

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Add %1 to a portfolio").arg(sym));
    auto* form = new QFormLayout(&dlg);
    auto* pf = new QComboBox;
    pf->setEditable(true);
    pf->setToolTip(QStringLiteral("An existing portfolio, or type a name to create one"));
    // Each item carries its portfolio's id: two portfolios may share a name.
    int watch = -1;
    if (const auto all = PortfolioRepository::instance().list_portfolios(); all.is_ok())
        for (const auto& p : all.value()) {
            if (watch < 0 && p.name.trimmed().compare(kWatchPortfolio, Qt::CaseInsensitive) == 0)
                watch = pf->count();
            pf->addItem(p.name.trimmed(), p.id);
        }
    if (watch < 0) {
        pf->insertItem(0, kWatchPortfolio); // no id: created on Add
        watch = 0;
    }
    pf->setCurrentIndex(watch);
    form->addRow(QStringLiteral("Portfolio"), pf);
    auto* qty = new QDoubleSpinBox;
    qty->setRange(0.01, 1e9);
    qty->setDecimals(2);
    qty->setValue(1);
    form->addRow(QStringLiteral("Shares"), qty);
    auto* px = new QDoubleSpinBox;
    px->setRange(0, 1e7);
    px->setDecimals(2);
    px->setPrefix(QStringLiteral("$"));
    px->setGroupSeparatorShown(true);
    form->addRow(QStringLiteral("Price"), px);
    auto* note = new QLabel;
    note->setWordWrap(true);
    note->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    const auto can_add = [=]() {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(px->value() > 0 && !pf->currentText().trimmed().isEmpty());
    };
    bool typed = false; // the reader's own price is never replaced by the quote
    bool setting = false;
    const auto set_price = [&setting, px](double v) {
        setting = true;
        px->setDecimals(v < 1 ? 4 : 2); // cents would round a sub-dollar price's cost basis
        px->setValue(v);
        setting = false;
    };
    connect(px, &QDoubleSpinBox::valueChanged, &dlg, [&typed, &setting, note, can_add](double) {
        if (setting)
            return;
        typed = true;
        note->setText(QStringLiteral("Your price."));
        can_add();
    });
    connect(pf, &QComboBox::currentTextChanged, &dlg, [can_add](const QString&) { can_add(); });
    if (listed && *listed > 0) {
        set_price(*listed);
        note->setText(QStringLiteral("The screener's price — fetching a quote…"));
    } else {
        note->setText(QStringLiteral("Fetching a quote…"));
    }
    can_add();

    QPointer<QDialog> alive(&dlg);
    auto& mds = services::MarketDataService::instance();
    mds.invalidate_quotes({sym}); // "current" means now, not a cached quote
    mds.fetch_quotes({sym}, [alive, sym, &typed, set_price, note, can_add, listed](bool ok,
                                                                                    QVector<services::QuoteData> q) {
        if (!alive || typed) // closed already (the captured locals are gone), or the reader's price stands
            return;
        for (const auto& d : q) {
            if (d.symbol.compare(sym, Qt::CaseInsensitive) == 0 && d.price > 0) {
                set_price(d.price);
                // the fetch time, not the price's: out of hours the latest
                // quote is the last close
                note->setText(QStringLiteral("Latest quote, fetched %1.").arg(QTime::currentTime().toString("HH:mm")));
                can_add();
                return;
            }
        }
        note->setText(listed && *listed > 0
                          ? QStringLiteral("No quote%1 — the screener's price.").arg(ok ? "" : " (fetch failed)")
                          : QStringLiteral("No price available — enter one."));
    });

    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString name = pf->currentText().trimmed();
    const double shares = qty->value(), price = px->value();
    auto& ps = services::PortfolioService::instance();
    // the picked item's own id; a typed name finds or creates by name
    QString id = pf->currentIndex() >= 0 && pf->itemText(pf->currentIndex()) == pf->currentText()
                     ? pf->currentData().toString()
                     : QString();
    if (id.isEmpty())
        id = ps.find_or_create_portfolio(name);
    if (id.isEmpty()) {
        flash_status(QStringLiteral("Could not open the portfolio \"%1\" — see the log").arg(name), true);
        return;
    }
    // The universe is NASDAQ/NYSE/AMEX listings, so USD is known — filled in
    // only where no lookup has answered yet, never over one.
    if (services::PortfolioService::cached_symbol_currency(sym).isEmpty())
        ps.note_symbol_currency(sym, QStringLiteral("USD"));
    if (!ps.add_asset(id, sym, shares, price)) {
        flash_status(QStringLiteral("Could not add %1 to %2 — see the log").arg(sym, name), true);
        return;
    }
    flash_status(QStringLiteral("Added %1 %2 at $%3 to %4")
                     .arg(QLocale().toString(shares, 'g', 10), sym,
                          QLocale().toString(price, 'f', price < 1 ? 4 : 2), name),
                 false);
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
    // strategy strings are text, never markup: every one is escaped
    const auto head = [&](const QString& t) {
        return QStringLiteral("<div style='font-weight:700;color:%1;margin-top:12px'>%2</div>").arg(amber, t);
    };
    const auto para = [](const QString& text) {
        return QStringLiteral("<p style='margin-top:2px'>%1</p>").arg(text.toHtmlEscaped());
    };
    // a section only when the document carries it (a scan cached before a
    // field existed lacks it until the next scan)
    const auto section = [&](const QString& title, const QString& text) {
        return text.isEmpty() ? QString() : head(title) + para(text);
    };
    const auto bullets = [](const QStringList& items) {
        QString h = QStringLiteral("<ul style='margin-left:-24px;margin-top:2px'>");
        for (const QString& r : items)
            h += QStringLiteral("<li>%1</li>").arg(r.toHtmlEscaped());
        return h + QStringLiteral("</ul>");
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
    const auto card_head = [&](const QString& name, const QString& sub, const QString& summary) {
        return QStringLiteral("<div style='color:%1;font-weight:700;font-size:14px'>%2</div>"
                              "<div style='color:%3'>%4</div><div style='margin-top:3px'>%5</div>")
            .arg(amber, name.toHtmlEscaped(), dim, sub.toHtmlEscaped(), summary.toHtmlEscaped());
    };
    // what each column of the list means: the table's own definitions
    const auto columns = [&](const QStringList& keys, const QString& rank_key, bool asc) {
        QString h = head(QStringLiteral("THE COLUMNS")) + QStringLiteral("<table cellspacing='0' cellpadding='1'>");
        const auto row = [&](const QString& name, const QString& tip, bool ranks = false) {
            // the ranking column stands out, as it does in the table
            h += ranks ? QStringLiteral("<tr><td valign='top' style='color:%1;font-weight:700'>%2 %3&nbsp;&nbsp;</td>"
                                        "<td><b style='color:%1'>Ranks the list.</b> %4</td></tr>")
                             .arg(amber, name.toHtmlEscaped(),
                                  // "#" keeps the table's own sort arrow, so no second one here
                                  name == QLatin1String("#") ? QString()
                                  : asc                      ? QStringLiteral("▲")
                                                             : QStringLiteral("▼"),
                                  tip.toHtmlEscaped())
                       : QStringLiteral("<tr><td valign='top' style='color:%1'>%2&nbsp;&nbsp;</td><td>%3</td></tr>")
                             .arg(dim, name.toHtmlEscaped(), tip.toHtmlEscaped());
        };
        if (rank_key.isEmpty())
            row(QStringLiteral("#"), QStringLiteral("The rank: by a combined score the table does not show (see "
                                                    "Ranked by)"),
                true);
        else
            row(QStringLiteral("#"), QStringLiteral("The rank, by the column marked below"));
        row(QStringLiteral("Trend 6M"), QStringLiteral("The last 126 daily closes, green if up over the span"));
        for (const QString& k : keys) {
            if (k == QLatin1String("picked_by"))
                row(QStringLiteral("Picked by"), QStringLiteral("The screens that pick this stock today"));
            else if (k == QLatin1String("shares"))
                row(QStringLiteral("Shares"), QStringLiteral("How many shares so that a fill at Entry stopped at Stop "
                                                             "loses your chosen risk of the account"));
            else if (k == QLatin1String("position"))
                row(QStringLiteral("Position"), QStringLiteral("Shares × Entry: the money the trade ties up"));
            else if (const auto* d = screener::column_def(k))
                row(QString::fromUtf8(d->header), QString::fromUtf8(d->tip), k == rank_key);
        }
        row(QStringLiteral("Name, Sector"), QStringLiteral("The company and its TradingView sector"));
        return h + QStringLiteral("</table>");
    };
    const QString basis = result_.basis.isEmpty()
                              ? QString()
                              : QStringLiteral("<div style='color:%1;font-size:11px;margin-top:12px'>Basis: %2.</div>")
                                    .arg(dim, result_.basis.toHtmlEscaped());
    // the write-up is three parts, each opened by a band: side by side as
    // tiles or stacked as one column (compose_details)
    const auto part = [&](const QString& t) {
        return QStringLiteral("<table width='100%' cellspacing='0' cellpadding='4' style='margin-top:12px'>"
                              "<tr><td style='background:%1;color:%2;font-size:13px;font-weight:700'>%3</td>"
                              "</tr></table>")
            .arg(ui::colors::AMBER(), ui::colors::BG_BASE(), t);
    };
    // readings link out: papers to their DOI, books to an Open Library search
    const auto readings = [&](const QVector<QPair<QString, QString>>& items) {
        QString h = QStringLiteral("<ul style='margin-left:-24px;margin-top:2px'>");
        for (const auto& [text, url] : items)
            h += url.startsWith(QLatin1String("https://"))
                     // double quotes: Qt decodes &amp; only there (toHtmlEscaped escapes ")
                     ? QStringLiteral("<li><a href=\"%1\" style='color:%2'>%3</a></li>")
                           .arg(url.toHtmlEscaped(), amber, text.toHtmlEscaped())
                     : QStringLiteral("<li>%1</li>").arg(text.toHtmlEscaped());
        return h + QStringLiteral("</ul>");
    };
    details_parts_.clear();
    details_tail_ = basis;

    if (current_id_ == kOverview) {
        band_html_ =
            card_head(QStringLiteral("Overview"),
                      QStringLiteral("Names on two or more screens (RS Leaders, nested in the Trend Template, counts "
                                     "once)"),
                      QStringLiteral("Where independent methods agree today. Pick a strategy on the left for its "
                                     "own list; click any stock to open it in Equity Research.")) +
            QStringLiteral("<div style='margin-top:4px'><span style='color:%1'>RECORD</span>&nbsp; No screen beat "
                           "the equal-weighted S&amp;P 500 with significance, 2010-2026.</div>")
                .arg(dim);

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
        t += QStringLiteral("</table>");
        details_head_ =
            card_head(QStringLiteral("Overview"), QStringLiteral("How to read the screens and their records"),
                      QStringLiteral("The screens come from practitioners — US Investing Champions Minervini and "
                                     "Luk, O'Neil's CAN SLIM (with which David Ryan won), Kullamägi's "
                                     "self-reported method — and from the academic literature; Quality + "
                                     "Momentum is this terminal's combination of two published factors. Most "
                                     "code only the SELECTION rules (the Leader Pullback also codes entry, stop, "
                                     "size and exits); what the practitioners add — entries, stops, sizing, "
                                     "judgment — is described under each strategy and is not in the list."));
        details_parts_ << part(QStringLiteral("1 · HOW TO READ THE RECORDS")) +
            QStringLiteral("<p style='margin-top:2px'>Every screen but CAN SLIM was backtested on the S&amp;P 500 "
                           "as it stood on each date, 2010-2026, monthly, after costs (the Leader Pullback as "
                           "individual trades). <b>None beat the equal-weighted index with statistical significance</b>. The "
                           "t-stat measures how far an average excess is from zero relative to its month-to-month "
                           "noise: |t| &lt; 2 is indistinguishable from luck. A list here is where a method would "
                           "look, not a forecast.</p>") +
            t;
        details_parts_ << part(QStringLiteral("2 · HOW THEY WERE MEASURED")) +
            section(QStringLiteral("THE TEST"),
                    QStringLiteral("Universe: the S&P 500's actual members on each month-end from 2010-01 to "
                                   "2026-08 (200 rebalances), those with at least 260 days of prices. Holding: equal "
                                   "weight for one month, 10 bp cost per side on every buy and sell. Fundamentals "
                                   "come from SEC filings and count only from 120 days after their period end "
                                   "(market cap from the filed public float, from 270 days). The SEC frames carry "
                                   "each period's latest-filed value, so a later restatement can leak back.")) +
            section(QStringLiteral("WHAT THE TEST CANNOT SEE"),
                    QStringLiteral("Survivorship: price history exists for 65.5% of the 2010 members, rising to "
                                   "98.3% in 2026; the missing ones are mostly companies that were delisted, so "
                                   "absolute returns read high. The excess is measured against the same surviving "
                                   "pool and is far less affected. Large caps only: small and mid caps, where "
                                   "several of these methods were born, are not in the 16-year test. And none of "
                                   "the practitioners' discretion is in it.")) +
            section(QStringLiteral("TIMING THE MARKET"),
                    QStringLiteral("CAN SLIM's 'M' — hold cash when SPY is below its 200-day average — made "
                                   "every strategy it was tried on worse: the plain index (-4.7%/yr), Momentum "
                                   "12-1, the Trend Template, its top 25 and the RS composite. The 2010-26 "
                                   "sell-offs recovered faster than a month-end 200-day rule gets back in. The header still "
                                   "shows SPY against its 50- and 200-day as context."));
        details_parts_ << part(QStringLiteral("3 · THE LIST")) +
            columns({QStringLiteral("price"), QStringLiteral("chg_1d"), QStringLiteral("screens"),
                     QStringLiteral("picked_by"), QStringLiteral("rs"), QStringLiteral("r3m"),
                     QStringLiteral("mcap")}, QStringLiteral("screens"), false);
        details_html_ = compose_details(1);
        return;
    }
    const auto* s = result_.strategy(current_id_);
    if (!s) {
        band_html_.clear();
        details_head_.clear();
        details_tail_.clear();
        details_html_.clear();
        return;
    }
    const auto& ev = s->evidence;
    const QString sub = QStringLiteral("%1 · %2").arg(s->family, s->source);

    // the summary card: what it is and its record in one line
    QString line = QStringLiteral("<div style='margin-top:4px'><span style='color:%1'>RECORD</span>&nbsp; ").arg(dim);
    if (ev.tested) {
        line += QStringLiteral("%1 / yr vs index &nbsp;·&nbsp; t %2 &nbsp;·&nbsp; max drawdown %3")
                    .arg(signed_pct_html(ev.excess), t_text(ev).toHtmlEscaped(),
                         ev.max_dd ? fmtn::format_percent(*ev.max_dd * 100.0, 0) : fmtn::placeholder());
        if (ev.trades && ev.avg_R)
            line += QStringLiteral(" &nbsp;·&nbsp; %1R / trade").arg(QString::number(*ev.avg_R, 'f', 2));
    } else {
        line += QStringLiteral("<span style='color:%1'>not backtested</span>").arg(dim);
    }
    band_html_ = card_head(s->name, sub, s->summary) + line + QStringLiteral("</div>");

    // the write-up, in three parts: how it works, what it did, today
    details_head_ = card_head(s->name, sub, s->summary);
    QString d = part(QStringLiteral("1 · HOW IT WORKS"));
    d += section(QStringLiteral("THE IDEA"), s->idea);
    d += head(QStringLiteral("RULES")) + bullets(s->rules) +
         QStringLiteral("<div style='color:%1'>Ranked by: %2</div>").arg(dim, s->rank_by.toHtmlEscaped());
    d += section(QStringLiteral("HOW IT'S USED"), s->use);
    QStringList keys = {QStringLiteral("price"), QStringLiteral("chg_1d")};
    for (const QString& k : s->columns) {
        if (!keys.contains(k))
            keys << k;
        if (k == QLatin1String("risk_pct")) // the plan's sized columns follow the risk, as in the table
            keys << QStringLiteral("shares") << QStringLiteral("position");
    }
    keys << QStringLiteral("mcap");
    d += columns(keys, s->rank_key, s->rank_asc);
    if (s->setups_from == QLatin1String("trend_template"))
        d += QStringLiteral("<div style='color:%1'>Setting up is ranked by RS.</div>").arg(dim);
    else if (!s->setups_from.isEmpty())
        d += QStringLiteral("<div style='color:%1'>Setting up is ranked in the list's own order.</div>")
                 .arg(dim);

    details_parts_ << d;
    d = part(QStringLiteral("2 · WHAT IT DID"));
    d += head(QStringLiteral("MEASURED RECORD"));
    if (ev.tested) {
        d += QStringLiteral("<table cellspacing='0' cellpadding='1'>"
                            "<tr><td style='color:%1'>Excess / yr vs index</td><td align='right'>&nbsp;%2</td></tr>"
                            "<tr><td style='color:%1'>t-stat</td><td align='right'>&nbsp;%3</td></tr>"
                            "<tr><td style='color:%1'>Max drawdown</td><td align='right'>&nbsp;%4</td></tr>")
                 .arg(dim, signed_pct_html(ev.excess), t_text(ev).toHtmlEscaped(),
                      ev.max_dd ? fmtn::format_percent(*ev.max_dd * 100.0, 1) : fmtn::placeholder());
        if (ev.trades)
            d += QStringLiteral("<tr><td style='color:%1'>Trades / win rate / avg</td><td align='right'>&nbsp;%2 / %3 / %4R</td></tr>")
                     .arg(dim)
                     .arg(*ev.trades)
                     .arg(ev.win_rate ? fmtn::format_percent(*ev.win_rate * 100.0, 0) : fmtn::placeholder())
                     .arg(ev.avg_R ? QString::number(*ev.avg_R, 'f', 2) : fmtn::placeholder());
        for (int i = 0; i < ev.subperiods.size() && i < result_.subperiods.size(); ++i)
            d += QStringLiteral("<tr><td style='color:%1'>&nbsp;&nbsp;%2</td><td align='right'>&nbsp;%3</td></tr>")
                     .arg(dim, result_.subperiods[i].toHtmlEscaped(), signed_pct_html(ev.subperiods[i]));
        d += QStringLiteral("</table>");
    } else {
        d += QStringLiteral("<div style='color:%1'>Not backtested.</div>").arg(dim);
    }
    if (!ev.note.isEmpty())
        d += para(ev.note);
    d += section(QStringLiteral("HOW IT WAS TESTED"), s->tested);

    details_parts_ << d;
    d = part(QStringLiteral("3 · TODAY"));
    // the rules on real stocks, with the scan's own numbers
    const auto example = [&](const QString& title, const screener::Example& e) {
        if (e.empty())
            return QString();
        QString h = head(title.arg(e.symbol.toHtmlEscaped())) + QStringLiteral("<table cellspacing='0' cellpadding='1'>");
        for (const auto& c : e.checks)
            h += QStringLiteral("<tr><td valign='top' style='color:%1'>%2&nbsp;</td><td valign='top'>%3&nbsp;&nbsp;</td>"
                                "<td valign='top' style='color:%4'>%5</td></tr>")
                     .arg(c.ok ? ui::colors::GREEN() : ui::colors::RED(), c.ok ? QStringLiteral("✓") : QStringLiteral("✗"),
                          c.rule.toHtmlEscaped(), dim, c.value.toHtmlEscaped());
        return h + QStringLiteral("</table>");
    };
    if (s->pick.empty() && s->miss.empty()) {
        d += para(has_lists() ? QStringLiteral("No stock passes today.")
                              : QStringLiteral("The worked example appears when today's lists are built."));
    } else {
        if (s->pick.empty())
            d += para(QStringLiteral("No stock passes today."));
        d += example(QStringLiteral("WORKED EXAMPLE · %1, TODAY'S FIRST PICK"), s->pick);
        d += example(QStringLiteral("THE FIRST NAME THAT MISSED · %1"), s->miss);
    }
    if (!s->setups_from.isEmpty()) {
        // the forward-looking list, with what its record says and does not
        const QJsonObject rec = result_.setups_record.value(s->setups_from).toObject();
        const QJsonObject trap = result_.setups_record.value(QStringLiteral("rs_trap")).toObject();
        const auto pc = [&](const QJsonObject& o, const char* k, bool sign) {
            const auto v = screener::num(o, QString::fromLatin1(k));
            return v ? fmtn::format_percent(*v * 100.0, 1, sign) : fmtn::placeholder();
        };
        const auto tv = [&](const QJsonObject& o) {
            const auto v = screener::num(o, QStringLiteral("t"));
            return v ? QString::number(*v, 'f', 2) : fmtn::placeholder();
        };
        const int n_su = result_.setups.value(s->setups_from).size();
        d += head(has_lists() ? QStringLiteral("SETTING UP · %1 NAMES NEXT IN LINE").arg(n_su)
                              : QStringLiteral("SETTING UP"));
        d += para(QStringLiteral("The Setting up view above the list: %1, with the rule it misses and how far "
                                 "it is. The closest thing here to \"what could come next\".")
                      .arg(s->setup_desc));
        const bool daily = s->setups_from == QLatin1String("luk_pullback");
        if (rec.isEmpty())
            d += para(QStringLiteral("Not tested: there is no backtest of this list, so nothing is claimed for "
                                     "it."));
        else
            d += para(QStringLiteral(
                         "What it did, 2010-2026 on the S&P 500: %1 of these names were on %7 %2, against %3 "
                         "of %8. Held as a list it returned %4 / yr against the index, t %5 (%6). Being next in "
                         "line is not the same as rising next.")
                         .arg(pc(rec, "qualify_next", false),
                              daily ? QStringLiteral("within the next 10 sessions") : QStringLiteral("a month later"),
                              pc(rec, "qualify_base", false), pc(rec, "excess", true), tv(rec),
                              [&]() {
                                  const auto t = screener::num(rec, QStringLiteral("t"));
                                  return !t ? QStringLiteral("untested") : std::abs(*t) < 2.0
                                                                              ? QStringLiteral("noise")
                                                                          : *t > 0 ? QStringLiteral("beat the index")
                                                                                   : QStringLiteral("reliably worse");
                              }(),
                              // whose list was measured (RS Leaders shows the Trend Template's)
                              s->setups_from == current_id_ || !result_.strategy(s->setups_from)
                                  ? QStringLiteral("the screen itself")
                                  : QStringLiteral("the ") + result_.strategy(s->setups_from)->name,
                              // the Trend Template's base counts every non-passer (setups_backtest.py)
                              s->setups_from == QLatin1String("trend_template") ? QStringLiteral("all non-passers")
                                                                                 : QStringLiteral("every other name")));
        if (s->setups_from == QLatin1String("trend_template") && !trap.isEmpty())
            d += para(QStringLiteral(
                         "Left out on purpose: names whose price trend passes all seven rules but whose RS is "
                         "below 70. Held as a list they were reliably worse than the index, %1 / yr, t %2, in "
                         "every sub-period — one of the two significant results in this research, both "
                         "negative (the other: buying the names nearest their 52-week high).")
                         .arg(pc(trap, "excess", true), tv(trap)));
    }
    d += section(QStringLiteral("PRACTISE IT · PAPER FIRST"), s->practice);
    if (!s->reading.isEmpty())
        d += head(QStringLiteral("FURTHER READING")) + readings(s->reading);
    details_parts_ << d;
    details_html_ = compose_details(1);
}

int ScreenerScreen::details_columns(int width) const {
    // Tiles: the three parts side by side on a wide pane, two on a medium
    // one; Column: stacked, for a laptop. Auto picks by width, so a column
    // never gets narrower than ~400 px of text.
    if (details_layout_ == 2)
        return 1;
    if (width >= 1250)
        return 3;
    if (width >= 820 || details_layout_ == 1)
        return 2;
    return 1;
}

QString ScreenerScreen::compose_details(int n) const {
    if (details_parts_.isEmpty())
        return details_head_ + details_tail_;
    if (n <= 1)
        return details_head_ + details_parts_.join(QString()) + details_tail_;
    QStringList cells;
    if (n == 2 && details_parts_.size() >= 3) // the long first part beside the other two
        cells = {details_parts_[0], details_parts_.mid(1).join(QString())};
    else
        cells = details_parts_;
    const int pct = 100 / cells.size();
    QString row;
    for (int i = 0; i < cells.size(); ++i)
        row += QStringLiteral("<td width='%1%' valign='top' style='%2'>%3</td>")
                   .arg(pct)
                   .arg(i + 1 < cells.size() ? QStringLiteral("padding-right:18px") : QString())
                   .arg(cells[i]);
    return details_head_ + QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0'><tr>") + row +
           QStringLiteral("</tr></table>") + details_tail_;
}

void ScreenerScreen::restore_state(const QVariantMap& state) {
    const QString id = state.value(QStringLiteral("strategy")).toString();
    if (!id.isEmpty())
        current_id_ = id;
    setting_up_ = state.value(QStringLiteral("setting_up")).toBool();
    details_layout_ = std::clamp(state.value(QStringLiteral("details_layout")).toInt(), 0, 2);
    if (auto* b = layout_group_->button(details_layout_))
        b->setChecked(true);
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
            {QStringLiteral("setting_up"), setting_up_},
            {QStringLiteral("details_layout"), details_layout_},
            {QStringLiteral("min_mcap"), min_mcap()},
            {QStringLiteral("account"), account_value_},
            {QStringLiteral("risk"), risk_frac_}};
}

} // namespace fincept::screens

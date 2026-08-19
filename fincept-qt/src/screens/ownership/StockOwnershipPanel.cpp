#include "screens/ownership/StockOwnershipPanel.h"

#include "screens/ownership/OwnershipSignals.h"
#include "screens/ownership/HoldersChart.h"
#include "screens/ownership/SmartMoneyPanel.h"
#include "services/ownership/OwnershipService.h"
#include "storage/repositories/PortfolioRepository.h"
#include "screens/ownership/Form4Dialog.h"
#include "screens/ownership/ReadThroughStrip.h"
#include "ui/components/ExternalLink.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QGridLayout>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

namespace {

/// A titled tile. The screen is a grid of these rather than one long scroll:
/// every number is on screen at once, and each tile owns its own overflow so a
/// long table scrolls inside its box instead of pushing everything else down.
QWidget* tile(const QString& title, QWidget* content, const QString& hint = {}) {
    auto* box = new QFrame;
    box->setFrameShape(QFrame::StyledPanel);
    box->setStyleSheet(QString("QFrame{background:%1;border:1px solid %2;border-radius:4px;}")
                           .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(8, 6, 8, 8);
    v->setSpacing(4);

    auto* head = new QLabel(title);
    // 12px and a real weight: the old 11px dim captions were the hardest thing
    // on the screen to read, and a tile title has to be findable at a glance.
    head->setStyleSheet(QString("color:%1;font-weight:700;font-size:12px;letter-spacing:1px;"
                                "border:none;background:transparent;")
                            .arg(ui::colors::AMBER()));
    head->setToolTip(hint);
    v->addWidget(head);
    content->setStyleSheet(content->styleSheet() + QStringLiteral("border:none;"));
    v->addWidget(content, 1);
    return box;
}

QTableWidget* make_table(const QStringList& headers) {
    auto* t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    // Interactive + stretch-last is the Qt default shape for a data table and
    // leaves the user free to size columns. Nothing is pinned to a fixed width.
    // Deliberately NOT ResizeToContents: that mode re-measures on every layout
    // pass for the life of the table. Callers size once after filling, which
    // gives identical widths without the standing cost.
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setSectionsMovable(true);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}


/// Compact, not exact. A position value is read for its magnitude, and
/// format_money renders 732786778636 in full — twelve digits nobody parses,
/// pushing every other column off the pane.
QString money_or_placeholder(const std::optional<double>& v) {
    return v ? fmt::format_compact(*v) : fmt::placeholder();
}

QString compact_or_placeholder(const std::optional<double>& v) {
    return v ? fmt::format_compact(*v) : fmt::placeholder();
}

QString pct_or_placeholder(const std::optional<double>& fraction) {
    return fraction ? fmt::format_percent(*fraction * 100.0) : fmt::placeholder();
}

QTableWidgetItem* cell(const QString& text, const QString& colour = {}) {
    auto* it = new QTableWidgetItem(text);
    if (!colour.isEmpty())
        it->setForeground(QColor(colour));
    return it;
}

} // namespace

StockOwnershipPanel::StockOwnershipPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
    apply_theme();

    connect(&services::OwnershipService::instance(),
            &services::OwnershipService::snapshot_updated, this, [this](const QString& sym) {
                if (sym.compare(symbol_, Qt::CaseInsensitive) == 0)
                    render();
            });
    connect(&services::OwnershipService::instance(),
            &services::OwnershipService::load_finished, this, [this](const QString& sym) {
                if (sym.compare(symbol_, Qt::CaseInsensitive) == 0)
                    render();
            });

    // Follow the shared symbol so arriving from another screen lands on the
    // same company rather than on whatever was last typed here.
    connect(&services::OwnershipService::instance(),
            &services::OwnershipService::index_changed, this,
            [this](const QString& msg) { refresh_index_ui(msg); });
    refresh_index_ui({});
    reload_portfolio();
    // Asked once on construction rather than polled: SEC publishes quarterly.
    services::OwnershipService::instance().check_for_newer_quarter();

    // NO shared-symbol subscription. This followed symbol group A when it was
    // the OWNERSHIP SCREEN, where following the shell was right. Embedded — in
    // Equity Research's tab, and in the ownership screen's detail pane — it is
    // told what to show by its host, and following the group fights that host.
    //
    // Concretely: clicking a holding switches the detail stack to this panel,
    // which makes it VISIBLE for the first time, which fired showEvent, which
    // re-synced to whatever symbol Equity Research had last published. Click
    // Meta in Capital World's book and the panel loaded Microsoft, because
    // Microsoft was what the rest of the shell was pointed at. The click was
    // not lost; it was overwritten a moment later.
    //
    // Group linking still works where it belongs: Equity Research implements
    // IGroupLinked and routes group changes through load_symbol, which calls
    // set_symbol here.
}

void StockOwnershipPanel::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);

    // ── Top bar ─────────────────────────────────────────────────────────────
    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    title_ = new QLabel(QStringLiteral("OWNERSHIP"));
    title_->setStyleSheet(QString("font-weight:700;letter-spacing:2px;color:%1;")
                              .arg(ui::colors::AMBER()));
    bar->addWidget(title_);

    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("Ticker — e.g. AAPL"));
    search_->setMaximumWidth(180);
    connect(search_, &QLineEdit::returnPressed, this,
            [this]() { load(search_->text()); });
    bar->addWidget(search_);

    // Your own holdings as a dropdown. The overwhelmingly common question is
    // "who else owns what I own", and making that require typing a ticker you
    // already told the app about is a pointless keystroke tax.
    portfolio_ = new QComboBox;
    portfolio_->setMinimumWidth(150);
    portfolio_->setToolTip(QStringLiteral("Jump to one of your portfolio holdings."));
    connect(portfolio_, &QComboBox::activated, this, [this](int i) {
        const QString sym = portfolio_->itemData(i).toString();
        // Snap back to the label. Picking a holding is an ACTION, not a state:
        // leaving the ticker showing made this look like a second search box
        // sitting next to the first one with the same value in it.
        portfolio_->setCurrentIndex(0);
        if (!sym.isEmpty())
            load(sym);
    });
    bar->addWidget(portfolio_);

    // Drill-through to the deep per-ticker screen. Without this the
    // navigate_to_screen signal — and MainWindow's handler for it — was dead
    // code wired to nothing.
    er_btn_ = new QPushButton(QStringLiteral("EQUITY RESEARCH \u2192"));
    auto* er_btn = er_btn_;
    er_btn->setToolTip(QStringLiteral("Open this ticker in Equity Research"));
    connect(er_btn, &QPushButton::clicked, this, [this]() {
        if (!symbol_.isEmpty())
            emit navigate_to_screen(QStringLiteral("equity_research"), symbol_);
    });

    refresh_btn_ = new QPushButton(QStringLiteral("REFRESH"));
    connect(refresh_btn_, &QPushButton::clicked, this, [this]() {
        if (symbol_.isEmpty())
            return;
        services::OwnershipService::instance().refresh(symbol_);
        render();
    });
    bar->addWidget(refresh_btn_);
    bar->addWidget(er_btn);

    status_ = new QLabel;
    bar->addWidget(status_, 1);

    // The index controls live on the top bar, not inside a panel: without an
    // index every holder view is empty, so "how do I fix that" must be the most
    // findable thing on the screen rather than something to hunt for.
    index_btn_ = new QPushButton;
    connect(index_btn_, &QPushButton::clicked, this, [this]() {
        auto& svc = services::OwnershipService::instance();
        if (!svc.index_ready())
            svc.build_index();
        else
            svc.resolve_symbols(2000);
    });
    bar->addWidget(index_btn_);
    root->addLayout(bar);

    index_lbl_ = new QLabel;
    index_lbl_->setVisible(false);   // shown only when it has something to add
    index_lbl_->setWordWrap(true);
    index_lbl_->setStyleSheet(QString("color:%1;font-size:12px;")
                                  .arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(index_lbl_);

    // ── Tiled body ──────────────────────────────────────────────────────────
    // A grid, not a scroll. Every tile is visible at once at a normal window
    // size; each one scrolls internally if its own content is long.
    // Three resizable columns rather than a fixed grid. A firm like Capital
    // World files 613 positions and Berkshire 29 — a tile sized for the second
    // shows six rows of the first. The two list-heavy panes get their own
    // full-height column and the rest tile beside them, and every divider is a
    // splitter handle so the reader can decide.
    auto* cols = new QSplitter(Qt::Horizontal);
    cols->setChildrenCollapsible(false);
    auto* side = new QSplitter(Qt::Vertical);
    side->setChildrenCollapsible(false);
    // A shim so the tile() helper's addWidget calls still have somewhere to go
    // while the three columns are assembled below.
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);

    // Row 0 — what it means, and who is buying. The two things a reader wants
    // first, side by side.
    reads_strip_ = new ReadThroughStrip;
    auto* reads_scroll = new QScrollArea;
    reads_scroll->setWidgetResizable(true);
    reads_scroll->setFrameShape(QFrame::NoFrame);
    reads_scroll->setWidget(reads_strip_);
    // A scroll area's size hint is its viewport's, not its content's, so the
    // splitter would happily crush this to one clipped line next to tiles with
    // tall tables. The floor is roughly two read-through cards.
    reads_scroll->setMinimumHeight(80);
    side->addWidget(tile(QStringLiteral("READ-THROUGH"), reads_scroll,
                         QStringLiteral("What the register implies, with the number and the "
                                        "rule behind each line.")));

    insider_timeline_ = new EventTimeline;
    insider_timeline_->set_empty_text(QStringLiteral("No Form 4 activity in the window."));
    auto* ins_box = new QWidget;
    auto* ins_v = new QVBoxLayout(ins_box);
    ins_v->setContentsMargins(0, 0, 0, 0);
    ins_v->setSpacing(4);
    ins_v->addWidget(insider_timeline_, 1);
    insiders_tbl_ = make_table({QStringLiteral("Date"), QStringLiteral("Insider"),
                                QStringLiteral("Action"), QStringLiteral("Shares"),
                                QStringLiteral("Value"), QStringLiteral("Pattern")});
    connect(insiders_tbl_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* it = insiders_tbl_->item(row, 0);
        if (!it)
            return;
        // The filing's URL is raw XML — opening it in a browser shows markup,
        // not a filing. Every transaction on that accession is already parsed
        // and in the snapshot, so render it here and keep EDGAR one click away.
        const QString url = it->data(Qt::UserRole).toString();
        if (url.isEmpty())
            return;
        const auto snap = services::OwnershipService::instance().snapshot(symbol_);
        QVector<ownership::InsiderTransaction> filing;
        for (const auto& t : snap.transactions) {
            if (t.source_url == url)
                filing.push_back(t);
        }
        if (filing.isEmpty())
            return;
        Form4Dialog dlg(filing, snap.company.isEmpty() ? symbol_ : snap.company, this);
        dlg.exec();
    });
    ins_v->addWidget(insiders_tbl_, 2);
    coverage_ = new QLabel;
    coverage_->setWordWrap(true);
    ins_v->addWidget(coverage_);
    auto* insiders_tile = tile(QStringLiteral("INSIDERS — FORM 4"), ins_box,
                               QStringLiteral("Open-market buys above the line, sells below, "
                                              "sized by value. Double-click a row to open the "
                                              "filing."));

    // Row 1 — the institutional register and the short side.
    smart_money_ = new SmartMoneyPanel;
    smart_money_->set_chrome_visible(false);
    side->addWidget(tile(QStringLiteral("INSTITUTIONAL HOLDERS"), smart_money_,
                         QStringLiteral("Every discretionary 13F filer holding this, on "
                                        "conviction against direction.")));

    auto* right = new QWidget;
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(4);
    ownership_mix_ = new RankedBarChart;
    ownership_mix_->set_empty_text(QStringLiteral("No ownership breakdown reported."));
    rv->addWidget(ownership_mix_, 1);
    short_lbl_ = new QLabel;
    short_lbl_->setWordWrap(true);
    short_lbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rv->addWidget(short_lbl_);
    side->addWidget(tile(QStringLiteral("FLOAT & SHORT INTEREST"), right,
                         QStringLiteral("Who is sitting on the shares, how much of the float "
                                        "is sold short, and today's traded short volume.")));

    // Row 2 — 5% stakes and the firm-level browser.
    stakes_tbl_ = make_table({QStringLiteral("Filed"), QStringLiteral("Filer"),
                              QStringLiteral("Form"), QStringLiteral("Intent")});
    connect(stakes_tbl_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* it = stakes_tbl_->item(row, 0))
            ui::open_external_link(it->data(Qt::UserRole).toString());
    });
    // Coverage line for the stakes list. It is filtered — schedules this
    // company filed ON OTHERS are excluded, and so is anything whose header
    // could not be read — and a filtered list has to say so or it reads as
    // the whole truth.
    stakes_note_ = new QLabel;
    stakes_note_->setWordWrap(true);
    stakes_note_->hide();
    auto* stakes_box = new QWidget;
    auto* stakes_v = new QVBoxLayout(stakes_box);
    stakes_v->setContentsMargins(0, 0, 0, 0);
    stakes_v->setSpacing(3);
    stakes_v->addWidget(stakes_tbl_, 1);
    stakes_v->addWidget(stakes_note_);
    side->addWidget(tile(QStringLiteral("5% STAKES — 13D / 13G"), stakes_box,
                         QStringLiteral("13D declares intent to influence; 13G is passive. "
                                        "Double-click to open the filing.")));

    // ── Empty state ─────────────────────────────────────────────────────────
    // Six empty tiles, each repeating "no 13F index", is five messages too
    // many and no clear action. Without an index the screen has exactly one
    // thing to say and one button to offer, so it says it once, in the middle,
    // and swaps to the grid the moment there is data.
    auto* empty = new QWidget;
    auto* ev = new QVBoxLayout(empty);
    ev->addStretch(1);
    auto* etitle = new QLabel(QStringLiteral("No 13F ownership index yet"));
    etitle->setAlignment(Qt::AlignCenter);
    etitle->setStyleSheet(QString("color:%1;font-size:20px;font-weight:700;")
                              .arg(ui::colors::TEXT_PRIMARY()));
    ev->addWidget(etitle);
    auto* ebody = new QLabel(QStringLiteral(
        "Downloads two quarterly SEC 13F data sets — around 10,600 filers and 2.4 million "
        "positions each — and indexes them locally. Every ticker then gets its complete "
        "institutional holder list and its quarter-over-quarter changes, answered in "
        "milliseconds with no network.\n\nRuns once, takes a couple of minutes."));
    ebody->setAlignment(Qt::AlignCenter);
    ebody->setWordWrap(true);
    ebody->setMaximumWidth(620);
    ebody->setStyleSheet(QString("color:%1;font-size:13px;").arg(ui::colors::TEXT_SECONDARY()));
    auto* ebrow = new QHBoxLayout;
    ebrow->addStretch(1);
    ebrow->addWidget(ebody);
    ebrow->addStretch(1);
    ev->addLayout(ebrow);
    auto* ebtn = new QPushButton(QStringLiteral("BUILD 13F INDEX"));
    ebtn->setMinimumWidth(220);
    ebtn->setMinimumHeight(34);
    connect(ebtn, &QPushButton::clicked, this,
            [this]() { services::OwnershipService::instance().build_index(); refresh_index_ui({}); });
    auto* ebtnrow = new QHBoxLayout;
    ebtnrow->addStretch(1);
    ebtnrow->addWidget(ebtn);
    ebtnrow->addStretch(1);
    ev->addSpacing(14);
    ev->addLayout(ebtnrow);
    ev->addStretch(2);
    empty_page_ = empty;

    cols->addWidget(insiders_tile);   // 0 — long filing lists, full height
    cols->addWidget(side);            // 1 — everything else, tiled

    // Initial proportions only. Qt's own splitter behaviour is what the user
    // expects, so nothing is pinned and nothing intercepts the drag — these
    // are a starting point, not a constraint.
    cols->setStretchFactor(0, 3);
    cols->setStretchFactor(1, 5);
    side->setStretchFactor(0, 3);   // read-through
    side->setStretchFactor(1, 4);   // holders + quadrant
    side->setStretchFactor(2, 2);   // float and short
    side->setStretchFactor(3, 2);   // stakes
    // Stretch factors only divide space left over once every widget has its
    // size hint, and the hints here are wildly uneven (a scroll area reports
    // almost nothing, a populated table reports a lot). Seed the proportions
    // directly so the column opens balanced; the user can drag from there.
    side->setSizes({150, 520, 220, 180});

    auto* grid_host = new QWidget;
    auto* host_layout = new QVBoxLayout(grid_host);
    host_layout->setContentsMargins(0, 0, 0, 0);
    host_layout->addWidget(cols);
    delete grid;
    body_ = new QStackedWidget;
    body_->addWidget(grid_host);   // 0
    body_->addWidget(empty_page_); // 1
    root->addWidget(body_, 1);
}

void StockOwnershipPanel::apply_theme() {
    setStyleSheet(QString("QWidget{background:%1;color:%2;}"
                          "QTableWidget{background:%1;gridline-color:%3;"
                          "alternate-background-color:%6;color:%2;}"
                          "QTableWidget::item{color:%2;}"
                          "QHeaderView::section{background:%4;color:%5;padding:4px;border:0;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                           ui::colors::BORDER_DIM(), ui::colors::BG_RAISED(),
                           ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE()));
}

void StockOwnershipPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // Deliberately does NOT re-sync to the shared symbol group. Becoming
    // visible is not new information about which security the reader wants —
    // the host already said, and the host is the only thing that knows whether
    // this panel is showing a stock the user clicked or a tab they opened.
}

void StockOwnershipPanel::load(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty())
        return;
    symbol_ = sym;
    if (search_->text().toUpper() != sym)
        search_->setText(sym);
    services::OwnershipService::instance().load(sym);
    // Point the panel at the symbol but do not auto-fetch: reading every
    // tracked manager's 13F is minutes of EDGAR round-trips and must be the
    // user's choice, not a side effect of typing a ticker.
    if (smart_money_)
        smart_money_->set_symbol(sym);
    render();
}

void StockOwnershipPanel::render() {
    if (symbol_.isEmpty()) {
        status_->setText(QStringLiteral("Enter a ticker to load its ownership register."));
        return;
    }
    auto& svc = services::OwnershipService::instance();
    const auto snap = svc.snapshot(symbol_);
    const bool loading = svc.is_loading(symbol_);

    QStringList bits;
    bits << (snap.company.isEmpty() ? symbol_ : QString("%1 — %2").arg(symbol_, snap.company));
    if (loading)
        bits << QStringLiteral("loading…");
    // Errors are elided and the full text goes to the tooltip. An unbounded
    // message — a script path, a stack fragment — pushes the toolbar wider than
    // the window and shoves the controls off the edge.
    auto brief = [](const QString& e) {
        return e.length() > 90 ? e.left(87) + QStringLiteral("…") : e;
    };
    QStringList full;
    if (!snap.edgar_error.isEmpty()) {
        bits << QStringLiteral("EDGAR: ") + brief(snap.edgar_error);
        full << QStringLiteral("EDGAR: ") + snap.edgar_error;
    }
    if (!snap.market_error.isEmpty()) {
        bits << QStringLiteral("Holders: ") + brief(snap.market_error);
        full << QStringLiteral("Holders: ") + snap.market_error;
    }
    status_->setText(bits.join(QStringLiteral("   ·   ")));
    status_->setToolTip(full.join(QStringLiteral("\n")));

    render_reads(snap);
    render_insiders(snap);
    render_stakes(snap);
    render_holders(snap);
    render_short(snap);
}

void StockOwnershipPanel::render_reads(const OwnershipSnapshot& s) {
    const auto reads = derive_reads(s);
    reads_strip_->set_empty_text(
        services::OwnershipService::instance().is_loading(s.symbol)
            ? QStringLiteral("Reading the register…")
            : QStringLiteral("Nothing in this register crosses a stated threshold. That is "
                             "itself a read: ordinary ownership, ordinary short interest, no "
                             "activist and no insider cluster."));
    reads_strip_->set_reads(reads);
}

void StockOwnershipPanel::render_insiders(const OwnershipSnapshot& s) {
    QHash<QString, InsiderProfile> by_name;
    for (const auto& p : s.insiders)
        by_name.insert(p.insider, p);

    // ── Timeline: open-market decisions only ────────────────────────────────
    // Grants, option exercises and tax withholding vest on a calendar. Plotting
    // them alongside purchases is what makes a naive insider chart look like
    // constant activity, so only P and S reach the timeline.
    double biggest = 0.0;
    for (const auto& t : s.transactions)
        if (t.open_market && t.value)
            biggest = std::max(biggest, std::abs(*t.value));

    QVector<TimelineEvent> events;
    for (const auto& t : s.transactions) {
        if (!t.open_market || !t.date.isValid())
            continue;
        TimelineEvent e;
        e.date = t.date;
        e.positive = t.acquired;
        e.magnitude = (biggest > 0 && t.value) ? std::abs(*t.value) / biggest : 0.3;
        e.tooltip = QStringLiteral("%1\n%2 — %3\n%4 shares at %5")
                        .arg(t.date.toString(QStringLiteral("d MMM yyyy")), t.insider,
                             t.acquired ? QStringLiteral("bought") : QStringLiteral("sold"),
                             t.shares ? fmt::format_compact(*t.shares) : fmt::placeholder(),
                             t.price ? fmt::format_money(*t.price) : fmt::placeholder());
        events.push_back(e);
    }
    insider_timeline_->set_events(events);
    insider_timeline_->set_empty_text(
        s.transactions.isEmpty()
            ? QStringLiteral("No Form 4 filings in the window.")
            : QStringLiteral("Form 4 filings exist, but none are open-market buys or sells — "
                             "all of it is grants, option exercises or tax withholding."));

    // ── Table ───────────────────────────────────────────────────────────────
    insiders_tbl_->setRowCount(s.transactions.size());
    insiders_tbl_->setUpdatesEnabled(false);
    for (int i = 0; i < s.transactions.size(); ++i) {
        const auto& t = s.transactions[i];
        // Only real decisions carry a direction colour. Colouring a grant green
        // is what makes every insider screen look like relentless buying.
        QString colour;
        if (t.open_market)
            colour = t.acquired ? ui::colors::GREEN() : ui::colors::RED();

        auto* date_item = cell(t.date.toString(QStringLiteral("yyyy-MM-dd")), colour);
        date_item->setData(Qt::UserRole, t.source_url);
        date_item->setToolTip(QStringLiteral("Double-click to open the filing on EDGAR"));
        insiders_tbl_->setItem(i, 0, date_item);

        auto* who = cell(t.insider);
        if (!t.roles.isEmpty())
            who->setToolTip(t.roles.join(QStringLiteral(", ")));
        insiders_tbl_->setItem(i, 1, who);

        insiders_tbl_->setItem(
            i, 2, cell(t.derivative ? t.code_label + QStringLiteral(" (deriv)") : t.code_label,
                       colour));
        insiders_tbl_->setItem(i, 3, cell(compact_or_placeholder(t.shares), colour));
        insiders_tbl_->setItem(i, 4, cell(money_or_placeholder(t.value), colour));

        const bool have_profile = by_name.contains(t.insider);
        const auto p = by_name.value(t.insider);
        QString pat = fmt::placeholder();
        if (p.pattern == Pattern::Routine)
            pat = QStringLiteral("routine");
        else if (p.pattern == Pattern::Opportunistic)
            pat = QStringLiteral("opportunistic");
        auto* pat_item = cell(pat, p.pattern == Pattern::Opportunistic ? ui::colors::AMBER()
                                                                      : QString());
        if (!have_profile)
            pat_item->setToolTip(QStringLiteral("No filing history matched to this name"));
        else if (p.pattern == Pattern::Unclassified && !p.reason.isEmpty())
            pat_item->setToolTip(QStringLiteral("Unclassified — ") + p.reason);
        else
            pat_item->setToolTip(QStringLiteral("%1 trades over %2 year(s) of this insider's "
                                                "own filing history")
                                     .arg(p.trades).arg(p.years_observed));
        insiders_tbl_->setItem(i, 5, pat_item);
    }
    insiders_tbl_->setUpdatesEnabled(true);
    insiders_tbl_->resizeColumnsToContents();

    QStringList notes;
    if (s.filings_found > 0) {
        notes << QStringLiteral("%1 filings in %2 months, %3 about this company")
                     .arg(s.filings_found).arg(s.window_months).arg(s.filings_parsed);
        if (s.filings_truncated > 0)
            notes << QStringLiteral("%1 older filings not fetched — this is the most recent "
                                    "slice, not the whole window").arg(s.filings_truncated);
        // A holding company files Form 4s about the issuers it owns 10% of.
        // Those are real filings and they are not insider trades here, so say
        // where they went rather than leaving an unexplained empty table.
        if (s.insider_rows_filed_as_owner > 0) {
            QString where = s.insider_other_issuers.mid(0, 3).join(QStringLiteral(", "));
            if (s.insider_other_issuers.size() > 3)
                where += QStringLiteral(" and %1 more")
                             .arg(s.insider_other_issuers.size() - 3);
            notes << QStringLiteral("%1 rows filed BY this company about %2 — shown on those "
                                    "companies, not here")
                         .arg(s.insider_rows_filed_as_owner)
                         .arg(where.isEmpty() ? QStringLiteral("other issuers") : where);
        }
    }
    coverage_->setText(notes.join(QStringLiteral(" · ")));
    coverage_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
}

void StockOwnershipPanel::render_stakes(const OwnershipSnapshot& s) {
    stakes_tbl_->setRowCount(s.stakes.size());
    stakes_tbl_->setUpdatesEnabled(false);
    for (int i = 0; i < s.stakes.size(); ++i) {
        const auto& b = s.stakes[i];
        auto* d = cell(b.filed_date.toString(QStringLiteral("yyyy-MM-dd")));
        d->setData(Qt::UserRole, b.url);
        d->setToolTip(QStringLiteral("Double-click to open the filing on EDGAR"));
        stakes_tbl_->setItem(i, 0, d);
        // Who filed it. The percentage owned is inside the filing body, which
        // is free-form for most filers — extracting it with a regex over prose
        // would produce an authoritative-looking number that is sometimes
        // wrong, so the name is shown and the document is a double-click away.
        auto* who = cell(b.filer.isEmpty() ? QStringLiteral("—") : b.filer);
        who->setToolTip(b.filer.isEmpty()
                            ? QStringLiteral("Filer not named in the submission header")
                            : QStringLiteral("%1 — double-click to open the filing and read the %")
                                  .arg(b.filer));
        stakes_tbl_->setItem(i, 1, who);
        stakes_tbl_->setItem(i, 2, cell(b.amendment ? b.form + QStringLiteral(" (amended)")
                                                    : b.form));
        stakes_tbl_->setItem(
            i, 3,
            cell(b.activist ? QStringLiteral("activist") : QStringLiteral("passive"),
                 b.activist ? ui::colors::AMBER() : QString()));
    }
    stakes_tbl_->setUpdatesEnabled(true);
    stakes_tbl_->resizeColumnsToContents();

    QStringList notes;
    if (s.stakes_filed_by_this_cik > 0)
        notes << QStringLiteral("%1 schedule(s) this company filed on OTHERS — not stakes in it")
                     .arg(s.stakes_filed_by_this_cik);
    if (s.stakes_unverified > 0)
        notes << QStringLiteral("%1 older filing(s) could not be checked in time — left out "
                                "rather than shown without knowing whose side they are on")
                     .arg(s.stakes_unverified);
    if (s.stakes_truncated > 0)
        notes << QStringLiteral("%1 older not fetched").arg(s.stakes_truncated);
    stakes_note_->setText(notes.join(QStringLiteral(" · ")));
    stakes_note_->setVisible(!notes.isEmpty());
    stakes_note_->setStyleSheet(
        QString("color:%1;font-size:11px;").arg(ui::colors::TEXT_SECONDARY()));
}

void StockOwnershipPanel::render_holders(const OwnershipSnapshot& s) {
    // Who is sitting on the shares, as bars rather than a numbers column —
    // the comparison between insider, institutional and free float is the
    // point, and a bar makes it in one glance.
    QVector<RankedBar> bars;
    const auto& si = s.shorts;
    auto add = [&bars](const QString& label, const std::optional<double>& frac,
                       const QString& colour, const QString& tip) {
        if (!frac || *frac < 0.0 || *frac > 1.0)
            return;  // absent or incoherent: show nothing rather than a guess
        RankedBar b;
        b.label = label;
        b.value_text = fmt::format_percent(*frac * 100.0, 1);
        b.fraction = *frac;
        b.colour = QColor(colour);
        b.tooltip = tip;
        bars.push_back(b);
    };
    add(QStringLiteral("Institutions"), si.held_pct_institutions, ui::colors::CYAN(),
        QStringLiteral("Share of shares outstanding held by 13F filers."));
    add(QStringLiteral("Insiders"), si.held_pct_insiders, ui::colors::AMBER(),
        QStringLiteral("Officers, directors and 10% owners."));
    add(QStringLiteral("Short interest"), si.pct_float, ui::colors::RED(),
        QStringLiteral("Percent of the float sold short."));
    ownership_mix_->set_bars(bars);
}

void StockOwnershipPanel::render_short(const OwnershipSnapshot& s) {
    const auto& si = s.shorts;
    if (!si.shares_short && !si.short_ratio && !si.pct_float && !s.short_volume.has_data()) {
        short_lbl_->setText(QStringLiteral("No short-interest figures reported for %1.")
                                .arg(symbol_));
        short_lbl_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        return;
    }
    QStringList rows;
    auto row = [&rows](const QString& k, const QString& v) {
        rows << QStringLiteral("%1: <b>%2</b>").arg(k, v);
    };
    row(QStringLiteral("Shares short"), compact_or_placeholder(si.shares_short));
    row(QStringLiteral("Prior settlement"), compact_or_placeholder(si.shares_short_prior));
    row(QStringLiteral("Days to cover"),
        si.short_ratio ? QString::number(*si.short_ratio, 'f', 1) : fmt::placeholder());
    row(QStringLiteral("% of float"), pct_or_placeholder(si.pct_float));
    row(QStringLiteral("Float"), compact_or_placeholder(si.float_shares));
    row(QStringLiteral("Held by institutions"), pct_or_placeholder(si.held_pct_institutions));
    row(QStringLiteral("Held by insiders"), pct_or_placeholder(si.held_pct_insiders));

    // The daily series, above the fortnightly figures. Short interest is a
    // settled position reported twice a month; short volume is intraday flow
    // reported every morning. Different numbers, so they are labelled
    // separately rather than merged into one "short" line.
    const auto& sv = s.short_volume;
    QString daily;
    if (sv.has_data()) {
        const QString dir = sv.latest > sv.avg_20 ? QStringLiteral("above")
                                                  : QStringLiteral("below");
        const QString col = sv.latest > sv.avg_20 ? ui::colors::RED() : ui::colors::GREEN();
        daily = QStringLiteral(
                    "<b>Short volume %1</b> of %2 volume &nbsp;·&nbsp; "
                    "<span style='color:%3;'>%4 its 20-day average of %5</span> &nbsp;·&nbsp; "
                    "%6 of its own %7-day range"
                    "<br><span style='color:%8;font-size:12px;'>Daily from FINRA. This is "
                    "traded short volume, not short interest — much of it is market-maker "
                    "inventory that is flat by the close, so the trend is the signal, not "
                    "the level.</span><br><br>")
                    .arg(fmt::format_percent(sv.latest * 100.0, 1),
                         sv.as_of.toString(QStringLiteral("d MMM")), col, dir,
                         fmt::format_percent(sv.avg_20 * 100.0, 1))
                    .arg(QStringLiteral("%1th").arg(qRound(sv.percentile * 100.0)))
                    .arg(sv.days)
                    .arg(ui::colors::TEXT_SECONDARY());
    } else if (!sv.error.isEmpty()) {
        daily = QStringLiteral("<span style='color:%1;'>Daily short volume: %2</span><br><br>")
                    .arg(ui::colors::TEXT_SECONDARY(), sv.error.left(90));
    }

    QString as_of;
    if (si.as_of.isValid()) {
        // Short interest is a twice-monthly settlement snapshot published a few
        // days later, so the reading date matters as much as the number.
        as_of = QStringLiteral("<br><span style='color:%1;font-size:12px;'>"
                               "Settlement date %2. Exchanges report twice a month and publish a "
                               "few days later, so this is a fortnightly reading, not a live "
                               "figure.</span>")
                    .arg(ui::colors::TEXT_SECONDARY(),
                         si.as_of.toString(QStringLiteral("d MMM yyyy")));
    }
    short_lbl_->setText(daily + rows.join(QStringLiteral(" &nbsp;·&nbsp; ")) + as_of);
    short_lbl_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
}


void StockOwnershipPanel::reload_portfolio() {
    portfolio_->clear();
    portfolio_->addItem(QStringLiteral("My holdings…"), QString());
    auto& repo = fincept::PortfolioRepository::instance();
    const auto pf = repo.list_portfolios();
    if (!pf.is_ok())
        return;
    QSet<QString> seen;
    for (const auto& p : pf.value()) {
        const auto assets = repo.get_assets(p.id);
        if (!assets.is_ok())
            continue;
        for (const auto& a : assets.value()) {
            const QString sym = a.symbol.trimmed().toUpper();
            if (sym.isEmpty() || seen.contains(sym))
                continue;
            seen.insert(sym);
            portfolio_->addItem(sym, sym);
        }
    }
    portfolio_->setEnabled(portfolio_->count() > 1);
}

void StockOwnershipPanel::set_symbol(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty() || sym == symbol_)
        return;
    if (search_)
        search_->setText(sym);
    load(sym);
}

void StockOwnershipPanel::set_chrome_visible(bool on) {
    chrome_ = on;
    // Hosted inside Equity Research (or the ownership screen's detail pane) the
    // symbol is already named above this widget, and a button offering to open
    // Equity Research from inside Equity Research is a dead end.
    if (title_)
        title_->setVisible(on);
    if (er_btn_)
        er_btn_->setVisible(on);
    // The index controls belong to whichever screen owns the index, not to a
    // panel embedded three levels down: in the ownership screen's detail pane
    // this was a second MAP MORE SYMBOLS beside the screen's own. The empty
    // state still offers to build the index when there isn't one.
    refresh_index_ui({});   // one place decides index-control visibility
    if (search_)
        search_->setVisible(on);
    if (portfolio_)
        portfolio_->setVisible(on);
    if (refresh_btn_)
        refresh_btn_->setVisible(on);
}

void StockOwnershipPanel::refresh_index_ui(const QString& msg) {
    auto& svc = services::OwnershipService::instance();
    const bool ready = svc.index_ready();
    if (body_)
        body_->setCurrentIndex(ready ? 0 : 1);
    // The toolbar control is redundant while the empty page owns the action.
    // chrome_ gates these too: embedded, the host screen owns the index
    // controls, and showing a second MAP MORE SYMBOLS beside its own is a
    // question about which one is in charge.
    index_btn_->setVisible(chrome_ && ready);
    index_lbl_->setVisible(chrome_ && ready && !msg.isEmpty());
    index_btn_->setText(ready ? QStringLiteral("MAP MORE SYMBOLS")
                              : QStringLiteral("BUILD 13F INDEX"));
    index_btn_->setToolTip(
        ready ? QStringLiteral("Resolve more CUSIPs to tickers via OpenFIGI so more securities "
                               "are searchable by symbol.")
              : QStringLiteral("Download one quarterly SEC 13F data set — every filer, every "
                               "position — and index it locally. About 100 MB, runs once."));
    index_btn_->setEnabled(!svc.index_busy());
    index_lbl_->setText(msg.isEmpty() ? svc.index_status_text() : msg);
    if (!symbol_.isEmpty() && ready)
        svc.load_smart_money(symbol_);
}



} // namespace fincept::screens

#include "screens/ownership/OwnershipScreen.h"

#include "core/symbol/SymbolContext.h"
#include "screens/ownership/OwnershipSignals.h"
#include "screens/ownership/SmartMoneyPanel.h"
#include "services/ownership/OwnershipService.h"
#include "ui/components/ExternalLink.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

namespace {

/// A section heading with a rule under it, matching the other research screens.
QLabel* section_label(const QString& text) {
    auto* l = new QLabel(text);
    l->setStyleSheet(QString("color:%1;font-weight:700;letter-spacing:1px;"
                             "padding:6px 2px 4px 2px;border-bottom:1px solid %2;")
                         .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM()));
    return l;
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
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

QString weight_colour(Weight w) {
    switch (w) {
        case Weight::Elevated: return ui::colors::AMBER();
        case Weight::Notable:  return ui::colors::CYAN();
        case Weight::Context:  break;
    }
    return ui::colors::TEXT_SECONDARY();
}

QString money_or_placeholder(const std::optional<double>& v) {
    return v ? fmt::format_money(*v) : fmt::placeholder();
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

OwnershipScreen::OwnershipScreen(QWidget* parent) : QWidget(parent) {
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
    connect(&SymbolContext::instance(), &SymbolContext::group_symbol_changed, this,
            [this](SymbolGroup group, const SymbolRef& ref) {
                if (group == SymbolGroup::A && !ref.symbol.isEmpty() && isVisible())
                    load(ref.symbol);
            });
}

void OwnershipScreen::build_ui() {
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
    search_->setMaximumWidth(220);
    connect(search_, &QLineEdit::returnPressed, this,
            [this]() { load(search_->text()); });
    bar->addWidget(search_);

    // Drill-through to the deep per-ticker screen. Without this the
    // navigate_to_screen signal — and MainWindow's handler for it — was dead
    // code wired to nothing.
    auto* er_btn = new QPushButton(QStringLiteral("EQUITY RESEARCH \u2192"));
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
    root->addLayout(bar);

    // ── Scrollable body ─────────────────────────────────────────────────────
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(10);

    // The interpretation leads. Everything below it is the evidence.
    bl->addWidget(section_label(QStringLiteral("READ-THROUGH")));
    reads_host_ = new QWidget;
    reads_layout_ = new QVBoxLayout(reads_host_);
    reads_layout_->setContentsMargins(0, 0, 0, 0);
    reads_layout_->setSpacing(6);
    bl->addWidget(reads_host_);

    coverage_ = new QLabel;
    coverage_->setWordWrap(true);
    bl->addWidget(coverage_);

    bl->addWidget(section_label(QStringLiteral("INSIDER TRANSACTIONS — FORM 4")));
    insiders_tbl_ = make_table({QStringLiteral("Date"), QStringLiteral("Insider"),
                                QStringLiteral("Role"), QStringLiteral("Action"),
                                QStringLiteral("Shares"), QStringLiteral("Price"),
                                QStringLiteral("Value"), QStringLiteral("Pattern")});
    insiders_tbl_->setMinimumHeight(220);
    connect(insiders_tbl_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* it = insiders_tbl_->item(row, 0))
            ui::open_external_link(it->data(Qt::UserRole).toString());
    });
    bl->addWidget(insiders_tbl_);

    bl->addWidget(section_label(QStringLiteral("5% STAKES — SCHEDULE 13D / 13G")));
    stakes_tbl_ = make_table({QStringLiteral("Filed"), QStringLiteral("Form"),
                              QStringLiteral("Intent"), QStringLiteral("Document")});
    stakes_tbl_->setMinimumHeight(120);
    connect(stakes_tbl_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* it = stakes_tbl_->item(row, 0))
            ui::open_external_link(it->data(Qt::UserRole).toString());
    });
    bl->addWidget(stakes_tbl_);

    // The stock-perspective view of the 13F index: which tracked managers hold
    // this and at what weight in THEIR book. Sits above the flat holder table
    // because "22% of Berkshire's book" is a decision and "8% of shares
    // outstanding held by BlackRock" mostly is not.
    bl->addWidget(section_label(QStringLiteral("TRACKED MANAGERS — POSITION IN THEIR BOOK")));
    smart_money_ = new SmartMoneyPanel;
    bl->addWidget(smart_money_);

    bl->addWidget(section_label(QStringLiteral("INSTITUTIONAL HOLDERS — 13F")));
    holders_tbl_ = make_table({QStringLiteral("Holder"), QStringLiteral("% out"),
                               QStringLiteral("Shares"), QStringLiteral("Value"),
                               QStringLiteral("As of")});
    holders_tbl_->setMinimumHeight(180);
    bl->addWidget(holders_tbl_);

    bl->addWidget(section_label(QStringLiteral("SHORT INTEREST")));
    short_lbl_ = new QLabel;
    short_lbl_->setWordWrap(true);
    short_lbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bl->addWidget(short_lbl_);

    bl->addStretch(1);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);
}

void OwnershipScreen::apply_theme() {
    setStyleSheet(QString("QWidget{background:%1;color:%2;}"
                          "QTableWidget{background:%1;gridline-color:%3;}"
                          "QHeaderView::section{background:%4;color:%5;padding:4px;border:0;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                           ui::colors::BORDER_DIM(), ui::colors::BG_RAISED(),
                           ui::colors::TEXT_SECONDARY()));
}

void OwnershipScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // Re-sync on EVERY show, not only the first. The group_symbol_changed
    // handler requires isVisible(), so a symbol change made while this tab was
    // hidden never reached it — and the old guard (symbol_.isEmpty()) then
    // skipped the catch-up, leaving OWNERSHIP on AAPL while the rest of the
    // shell had moved to MSFT.
    const auto ref = SymbolContext::instance().group_symbol(SymbolGroup::A);
    if (!ref.symbol.isEmpty() && ref.symbol.compare(symbol_, Qt::CaseInsensitive) != 0)
        load(ref.symbol);
}

void OwnershipScreen::load(const QString& symbol) {
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

void OwnershipScreen::render() {
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
    if (!snap.edgar_error.isEmpty())
        bits << QStringLiteral("EDGAR: ") + snap.edgar_error;
    if (!snap.market_error.isEmpty())
        bits << QStringLiteral("Holders: ") + snap.market_error;
    status_->setText(bits.join(QStringLiteral("   ·   ")));

    render_reads(snap);
    render_insiders(snap);
    render_stakes(snap);
    render_holders(snap);
    render_short(snap);
}

void OwnershipScreen::render_reads(const OwnershipSnapshot& s) {
    while (QLayoutItem* item = reads_layout_->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const auto reads = derive_reads(s);
    if (reads.isEmpty()) {
        auto* none = new QLabel(
            services::OwnershipService::instance().is_loading(s.symbol)
                ? QStringLiteral("Reading the register…")
                : QStringLiteral("Nothing in this register crosses a stated threshold. "
                                 "That is itself a read: ordinary ownership, ordinary "
                                 "short interest, no activist and no insider cluster."));
        none->setWordWrap(true);
        none->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        reads_layout_->addWidget(none);
        return;
    }

    auto add_group = [&](Lens lens, const QString& heading) {
        const auto group = reads_for(reads, lens);
        if (group.isEmpty())
            return;
        auto* h = new QLabel(heading);
        h->setStyleSheet(QString("color:%1;font-weight:700;padding-top:4px;")
                             .arg(ui::colors::TEXT_SECONDARY()));
        reads_layout_->addWidget(h);
        for (const auto& r : group) {
            auto* card = new QLabel;
            card->setWordWrap(true);
            card->setTextInteractionFlags(Qt::TextSelectableByMouse);
            // Headline, then the sentence, then the rule that produced it in a
            // dimmer tone — the user can audit any line without leaving it.
            card->setText(QStringLiteral(
                              "<div style='margin:2px 0 2px 0;'>"
                              "<span style='color:%1;font-weight:700;'>%2</span><br>"
                              "<span style='color:%3;'>%4</span><br>"
                              "<span style='color:%5;font-size:11px;'>%6</span></div>")
                              .arg(weight_colour(r.weight), r.headline.toHtmlEscaped(),
                                   ui::colors::TEXT_PRIMARY(), r.detail.toHtmlEscaped(),
                                   ui::colors::TEXT_DIM(), r.basis.toHtmlEscaped()));
            card->setStyleSheet(QString("border-left:2px solid %1;padding-left:8px;")
                                    .arg(weight_colour(r.weight)));
            reads_layout_->addWidget(card);
        }
    };
    add_group(Lens::Stock, QStringLiteral("WHAT IT MEANS FOR THE STOCK"));
    add_group(Lens::Flows, QStringLiteral("WHAT IT MEANS FOR HOW IT TRADES"));
}

void OwnershipScreen::render_insiders(const OwnershipSnapshot& s) {
    // Pattern is per-insider, so index it once rather than scanning per row.
    QHash<QString, InsiderProfile> by_name;
    for (const auto& p : s.insiders)
        by_name.insert(p.insider, p);

    insiders_tbl_->setRowCount(s.transactions.size());
    for (int i = 0; i < s.transactions.size(); ++i) {
        const auto& t = s.transactions[i];
        // Only open-market decisions get a directional colour. Grants and tax
        // withholding are neutral events and colouring them green/red is what
        // makes a naive insider screen read as constant buying.
        QString colour;
        if (t.open_market)
            colour = t.acquired ? ui::colors::GREEN() : ui::colors::RED();

        auto* date_item = cell(t.date.toString(QStringLiteral("yyyy-MM-dd")));
        date_item->setData(Qt::UserRole, t.source_url);
        date_item->setToolTip(QStringLiteral("Double-click to open the filing on EDGAR"));
        insiders_tbl_->setItem(i, 0, date_item);
        insiders_tbl_->setItem(i, 1, cell(t.insider));
        insiders_tbl_->setItem(i, 2, cell(t.roles.join(QStringLiteral(", "))));
        insiders_tbl_->setItem(
            i, 3, cell(t.derivative ? t.code_label + QStringLiteral(" (deriv)") : t.code_label,
                       colour));
        insiders_tbl_->setItem(i, 4, cell(compact_or_placeholder(t.shares)));
        insiders_tbl_->setItem(i, 5, cell(money_or_placeholder(t.price)));
        insiders_tbl_->setItem(i, 6, cell(money_or_placeholder(t.value)));

        const bool have_profile = by_name.contains(t.insider);
        const auto p = by_name.value(t.insider);
        QString pat = QStringLiteral("—");
        switch (p.pattern) {
            case Pattern::Routine:
                pat = QStringLiteral("routine");
                break;
            case Pattern::Opportunistic:
                pat = QStringLiteral("opportunistic");
                break;
            case Pattern::Unclassified:
                break;
        }
        auto* pat_item = cell(pat, p.pattern == Pattern::Opportunistic ? ui::colors::AMBER()
                                                                       : QString());
        // A default-constructed profile would otherwise claim "0 trades over 0
        // year(s)" for a row that is itself proof of at least one trade.
        if (!have_profile)
            pat_item->setToolTip(QStringLiteral("No filing history matched to this name"));
        else if (p.pattern == Pattern::Unclassified && !p.reason.isEmpty())
            pat_item->setToolTip(QStringLiteral("Unclassified — ") + p.reason);
        else
            pat_item->setToolTip(QStringLiteral("%1 trades over %2 year(s) of this insider's "
                                                "own filing history")
                                     .arg(p.trades).arg(p.years_observed));
        insiders_tbl_->setItem(i, 7, pat_item);
    }

    QStringList notes;
    if (s.filings_found > 0) {
        notes << QStringLiteral("%1 Form 4 filings in the last %2 months; %3 parsed")
                     .arg(s.filings_found).arg(s.window_months).arg(s.filings_parsed);
        if (s.filings_truncated > 0)
            notes << QStringLiteral("%1 older filings not fetched — the table is the most "
                                    "recent slice, not the whole window")
                         .arg(s.filings_truncated);
    }
    coverage_->setText(notes.join(QStringLiteral(" · ")));
    coverage_->setStyleSheet(QString("color:%1;font-size:11px;").arg(ui::colors::TEXT_DIM()));
}

void OwnershipScreen::render_stakes(const OwnershipSnapshot& s) {
    stakes_tbl_->setRowCount(s.stakes.size());
    for (int i = 0; i < s.stakes.size(); ++i) {
        const auto& b = s.stakes[i];
        auto* d = cell(b.filed_date.toString(QStringLiteral("yyyy-MM-dd")));
        d->setData(Qt::UserRole, b.url);
        d->setToolTip(QStringLiteral("Double-click to open the filing on EDGAR"));
        stakes_tbl_->setItem(i, 0, d);
        stakes_tbl_->setItem(i, 1, cell(b.form));
        stakes_tbl_->setItem(
            i, 2,
            cell(b.activist ? QStringLiteral("activist") : QStringLiteral("passive"),
                 b.activist ? ui::colors::AMBER() : QString()));
        // The percentage owned is inside the filing body, which is free-form
        // for most filers. Extracting it with a regex over prose would produce
        // an authoritative-looking number that is sometimes wrong, so the
        // document is offered instead.
        stakes_tbl_->setItem(i, 3, cell(b.amendment ? QStringLiteral("amendment — open to read %")
                                                    : QStringLiteral("open to read %")));
    }
}

void OwnershipScreen::render_holders(const OwnershipSnapshot& s) {
    holders_tbl_->setRowCount(s.holders.size());
    for (int i = 0; i < s.holders.size(); ++i) {
        const auto& h = s.holders[i];
        holders_tbl_->setItem(i, 0, cell(h.holder, is_index_complex(h.holder)
                                                       ? ui::colors::CYAN()
                                                       : QString()));
        holders_tbl_->setItem(i, 1, cell(pct_or_placeholder(h.pct)));
        holders_tbl_->setItem(i, 2, cell(compact_or_placeholder(h.shares)));
        holders_tbl_->setItem(i, 3, cell(money_or_placeholder(h.value)));
        holders_tbl_->setItem(i, 4, cell(h.as_of.isValid()
                                             ? h.as_of.toString(QStringLiteral("yyyy-MM-dd"))
                                             : fmt::placeholder()));
    }
}

void OwnershipScreen::render_short(const OwnershipSnapshot& s) {
    const auto& si = s.shorts;
    if (!si.shares_short && !si.short_ratio && !si.pct_float) {
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

    QString as_of;
    if (si.as_of.isValid()) {
        // Short interest is a twice-monthly settlement snapshot published a few
        // days later, so the reading date matters as much as the number.
        as_of = QStringLiteral("<br><span style='color:%1;font-size:11px;'>"
                               "Settlement date %2. Exchanges report twice a month and publish a "
                               "few days later, so this is a fortnightly reading, not a live "
                               "figure.</span>")
                    .arg(ui::colors::TEXT_DIM(),
                         si.as_of.toString(QStringLiteral("d MMM yyyy")));
    }
    short_lbl_->setText(rows.join(QStringLiteral(" &nbsp;·&nbsp; ")) + as_of);
    short_lbl_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
}

void OwnershipScreen::restore_state(const QVariantMap& state) {
    const QString sym = state.value(QStringLiteral("symbol")).toString();
    if (!sym.isEmpty())
        load(sym);
}

QVariantMap OwnershipScreen::save_state() const {
    QVariantMap m;
    if (!symbol_.isEmpty())
        m.insert(QStringLiteral("symbol"), symbol_);
    return m;
}

} // namespace fincept::screens

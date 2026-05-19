// src/screens/equity_research/EquityPeersTab.cpp
#include "screens/equity_research/EquityPeersTab.h"

#include "services/equity/EquityResearchService.h"
#include "ui/theme/Theme.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>

namespace fincept::screens {

EquityPeersTab::EquityPeersTab(QWidget* parent) : QWidget(parent) {
    build_ui();
    // Subscribe per-(symbol, basket) in on_load_clicked() via QueryStore.
}

void EquityPeersTab::set_symbol(const QString& symbol) {
    if (symbol == current_symbol_)
        return;
    current_symbol_ = symbol;
    auto peers = default_peers(symbol);
    peers_edit_->setText(peers.join(", "));
    loading_overlay_->show_loading("LOADING PEERS…");
    on_load_clicked();
}

void EquityPeersTab::build_ui() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    loading_overlay_ = new ui::LoadingOverlay(this);
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(10);

    // ── Controls row ──────────────────────────────────────────────────────────
    auto* ctrl = new QFrame;
    ctrl->setStyleSheet(QString("QFrame { background:%1; border:1px solid %2; border-radius:4px; }")
                            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hl = new QHBoxLayout(ctrl);
    hl->setContentsMargins(12, 8, 12, 8);
    hl->setSpacing(10);

    auto* lbl = new QLabel("PEERS (comma-separated):");
    lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px; "
                               "background:transparent; border:0;")
                           .arg(ui::colors::TEXT_SECONDARY()));
    hl->addWidget(lbl);

    peers_edit_ = new QLineEdit;
    peers_edit_->setStyleSheet(
        QString("QLineEdit { background:%1; color:%2; border:1px solid %3; "
                "border-radius:3px; padding:4px 10px; font-size:12px; }"
                "QLineEdit:focus { border-color:%4; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER()));
    hl->addWidget(peers_edit_, 1);

    auto* load_btn = new QPushButton("LOAD");
    load_btn->setStyleSheet(QString("QPushButton { background:%1; color:%2; border:0; border-radius:3px; "
                                    "padding:5px 18px; font-size:12px; font-weight:700; }"
                                    "QPushButton:hover { background:#b45309; }")
                                .arg(ui::colors::AMBER(), ui::colors::BG_BASE()));
    hl->addWidget(load_btn);
    connect(load_btn, &QPushButton::clicked, this, &EquityPeersTab::on_load_clicked);
    vl->addWidget(ctrl);

    // ── Status ────────────────────────────────────────────────────────────────
    status_label_ = new QLabel("Loading peer data…");
    status_label_->setStyleSheet(
        QString("color:%1; font-size:12px; padding:4px; background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    status_label_->hide();
    vl->addWidget(status_label_);

    // ── Table ─────────────────────────────────────────────────────────────────
    peer_table_ = new QTableWidget;
    peer_table_->setStyleSheet(QString(R"(
        QTableWidget {
            background:%1; alternate-background-color:%2;
            gridline-color:%3; color:%4; border:0; font-size:12px;
        }
        QHeaderView::section {
            background:%5; color:%6; font-size:12px; font-weight:700;
            padding:5px 4px; border:0; border-bottom:1px solid %3;
            letter-spacing:1px;
        }
        QTableWidget::item { padding:2px 6px; }
    )")
                                   .arg(ui::colors::BG_SURFACE(), ui::colors::BG_BASE(), ui::colors::BORDER_DIM(),
                                        ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY()));
    peer_table_->setAlternatingRowColors(true);
    peer_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    peer_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    peer_table_->verticalHeader()->setDefaultSectionSize(26);
    peer_table_->verticalHeader()->hide();
    peer_table_->horizontalHeader()->setStretchLastSection(false);
    peer_table_->setSortingEnabled(true);
    // The COMP column (last) hosts QCheckBox widgets via setCellWidget,
    // not inline ItemIsUserCheckable items — explicit boxes are easier
    // to see and click than the indicator glyph the item flag draws.
    // setCellWidget tradeoff: widgets do NOT move with sort, so we
    // re-sync check states against active_comp_set_ + current SYMBOL
    // cell on every sortIndicatorChanged. Also: sorting by COMP itself
    // is meaningless (the widget isn't an item), so bounce that header
    // click back to SYMBOL (col 0).
    connect(peer_table_->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, [this](int col, Qt::SortOrder order) {
                const int comp_col = peer_table_->columnCount() - 1;
                if (col == comp_col) {
                    // Bounce back to whatever column WAS sorted before
                    // this click — flipping unrelated columns (e.g.
                    // SYMBOL desc → asc) would be surprising. We track
                    // last_sort_col_/last_sort_order_ on every accepted
                    // sort below.
                    peer_table_->horizontalHeader()->setSortIndicator(
                        last_sort_col_ < 0 ? 0 : last_sort_col_,
                        last_sort_col_ < 0 ? Qt::AscendingOrder : last_sort_order_);
                    return;
                }
                last_sort_col_ = col;
                last_sort_order_ = order;
                // QTableWidget sorts items synchronously before this
                // signal fires, so SYMBOL cells already reflect the new
                // row order — refresh_check_column will read them.
                refresh_check_column();
            });
    vl->addWidget(peer_table_, 1);

    // ── Legend row ────────────────────────────────────────────────────────────
    auto* legend = new QWidget(this);
    auto* leg_hl = new QHBoxLayout(legend);
    leg_hl->setContentsMargins(0, 4, 0, 0);
    leg_hl->setSpacing(16);
    auto make_leg = [&](const QString& color, const QString& text) {
        auto* dot = new QLabel("●");
        dot->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;").arg(color));
        auto* t = new QLabel(text);
        t->setStyleSheet(
            QString("color:%1; font-size:12px; background:transparent; border:0;").arg(ui::colors::TEXT_SECONDARY()));
        leg_hl->addWidget(dot);
        leg_hl->addWidget(t);
    };
    make_leg(ui::colors::POSITIVE, "Positive / Good");
    make_leg(ui::colors::NEGATIVE, "Negative / High Risk");
    make_leg("#eab308", "Neutral");
    make_leg("#22d3ee", "Symbol / Info");
    leg_hl->addStretch();
    vl->addWidget(legend);
}

void EquityPeersTab::set_active_comparisons(const QStringList& symbols) {
    QSet<QString> next;
    next.reserve(symbols.size());
    for (const auto& s : symbols) next.insert(s.toUpper());
    if (next == active_comp_set_) return;     // no-op when unchanged
    active_comp_set_ = std::move(next);
    refresh_check_column();
}

void EquityPeersTab::refresh_check_column() {
    if (!peer_table_) return;
    const int comp_col = peer_table_->columnCount() - 1;
    if (comp_col < 0) return;
    for (int r = 0; r < peer_table_->rowCount(); ++r) {
        auto* wrap = peer_table_->cellWidget(r, comp_col);
        auto* sym_item = peer_table_->item(r, 0);
        if (!wrap || !sym_item) continue;
        auto* cb = wrap->findChild<QCheckBox*>();
        if (!cb) continue;
        const QString sym = sym_item->text().trimmed().toUpper();
        const bool should_check = active_comp_set_.contains(sym);
        if (cb->isChecked() == should_check) continue;
        // QCheckBox::clicked only fires for actual user clicks (not
        // setChecked), so no signal blocking is needed — clicked-vs-
        // toggled is the entire reason we connected to clicked above.
        cb->setChecked(should_check);
    }
}

void EquityPeersTab::on_load_clicked() {
    if (current_symbol_.isEmpty())
        return;
    QString text = peers_edit_->text().trimmed();
    QStringList peers;
    for (const auto& p : text.split(','))
        if (!p.trimmed().isEmpty())
            peers.append(p.trimmed().toUpper());
    if (peers.isEmpty())
        return;
    status_label_->setText("Loading peer data…");
    status_label_->show();

    // Rebind the subscription — different basket = different key, so the
    // unsubscribe_all here drops any prior basket's subscription before the
    // new one binds. The QueryStore basket key (sorted peers) keeps multiple
    // baskets independently cached.
    auto& store = services::query::QueryStore::instance();
    store.unsubscribe_all(this);
    services::equity::EquityResearchService::instance().subscribe_peers(
        this, current_symbol_, peers,
        [this](const services::query::QueryStore::State& s) { apply_peers_state(s); });
}

void EquityPeersTab::apply_peers_state(const services::query::QueryStore::State& s) {
    if (!s.error.isEmpty()) {
        status_label_->hide();
        if (loading_overlay_) loading_overlay_->hide_loading();
        return;
    }
    if (!s.data.isValid() || s.data.isNull()) return;
    const auto peers = s.data.value<QVector<services::equity::PeerData>>();
    status_label_->hide();
    loading_overlay_->hide_loading();
    populate_table(peers);
}

void EquityPeersTab::populate_table(const QVector<services::equity::PeerData>& peers) {
    // COMP is the LAST column (after BETA) — explicit per UX: leftmost
    // columns are content (SYMBOL → BETA), rightmost is the action.
    static const QStringList headers = {"SYMBOL",    "PRICE", "P/E",       "FWD P/E",   "P/B",     "P/S",
                                        "PEG",       "ROE",   "ROA",       "GROSS MGN", "NET MGN", "OP MGN",
                                        "REV GRWTH", "D/E",   "DIV YIELD", "BETA",      "COMP"};
    const int comp_col = headers.size() - 1;

    peer_table_->setSortingEnabled(false);
    peer_table_->setColumnCount(headers.size());
    peer_table_->setRowCount(peers.size());
    peer_table_->setHorizontalHeaderLabels(headers);

    // Color helpers
    auto color_ratio = [](double v, double good, double warn) -> QColor {
        if (v <= 0.0)
            return QColor("#6b7280");
        if (v <= good)
            return QColor(ui::colors::POSITIVE());
        if (v <= warn)
            return QColor("#eab308");
        return QColor(ui::colors::NEGATIVE());
    };
    auto color_pct_pos = [](double v) -> QColor {
        if (v == 0.0)
            return QColor("#6b7280");
        if (v > 0.0)
            return QColor(ui::colors::POSITIVE());
        return QColor(ui::colors::NEGATIVE());
    };

    auto set_cell = [&](int row, int col, const QString& text, const QColor& fg = QColor(),
                        Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter) {
        auto* item = new QTableWidgetItem(text);
        item->setTextAlignment(align);
        if (fg.isValid())
            item->setForeground(fg);
        peer_table_->setItem(row, col, item);
    };

    auto fmt = [](double v, int dec = 2) -> QString { return v != 0.0 ? QString::number(v, 'f', dec) : "—"; };
    auto fmt_pct = [](double v) -> QString { return v != 0.0 ? QString("%1%").arg(v * 100.0, 0, 'f', 1) : "—"; };

    for (int r = 0; r < peers.size(); ++r) {
        const auto& p = peers[r];
        const bool is_primary = (p.symbol == current_symbol_);

        // Symbol — highlight current symbol in amber, peers in cyan.
        auto* sym_item = new QTableWidgetItem(p.symbol);
        sym_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        sym_item->setForeground(is_primary ? QColor(ui::colors::AMBER()) : QColor("#22d3ee"));
        sym_item->setFont([&] {
            QFont f;
            f.setBold(true);
            return f;
        }());
        if (is_primary) sym_item->setToolTip(QStringLiteral("(current symbol)"));
        peer_table_->setItem(r, 0, sym_item);

        set_cell(r, 1, fmt(p.price, 2), QColor("#22d3ee"));
        set_cell(r, 2, fmt(p.pe_ratio, 1), color_ratio(p.pe_ratio, 15, 30));
        set_cell(r, 3, fmt(p.forward_pe, 1), color_ratio(p.forward_pe, 15, 30));
        set_cell(r, 4, fmt(p.price_to_book, 2), color_ratio(p.price_to_book, 2, 5));
        set_cell(r, 5, fmt(p.price_to_sales, 2), color_ratio(p.price_to_sales, 3, 8));
        set_cell(r, 6, fmt(p.peg_ratio, 2), color_ratio(p.peg_ratio, 1, 2));
        set_cell(r, 7, fmt_pct(p.roe), color_pct_pos(p.roe));
        set_cell(r, 8, fmt_pct(p.roa), color_pct_pos(p.roa));
        set_cell(r, 9, fmt_pct(p.gross_margin), color_pct_pos(p.gross_margin));
        set_cell(r, 10, fmt_pct(p.profit_margin), color_pct_pos(p.profit_margin));
        set_cell(r, 11, fmt_pct(p.operating_margin), color_pct_pos(p.operating_margin));
        set_cell(r, 12, fmt_pct(p.revenue_growth), color_pct_pos(p.revenue_growth));
        set_cell(r, 13, fmt(p.debt_to_equity, 2), color_ratio(p.debt_to_equity, 0.5, 2.0));
        set_cell(r, 14, fmt_pct(p.dividend_yield),
                 p.dividend_yield > 0 ? QColor(ui::colors::POSITIVE()) : QColor("#6b7280"));
        set_cell(r, 15, fmt(p.beta, 2),
                 p.beta >= 0 && p.beta <= 1.5 ? QColor(ui::colors::POSITIVE()) : QColor(ui::colors::NEGATIVE()));

        // COMP — explicit QCheckBox widget (last column). For the primary
        // row it's shown but disabled (self-comparison is meaningless).
        // For peer rows, the click handler walks the table to find this
        // widget's current visual row (cellWidgets don't move with sort)
        // and reads the SYMBOL cell text — which IS sort-aware via the
        // underlying QTableWidgetItem moves — so we always emit for the
        // ticker currently shown in this row.
        // Wrap QCheckBox in a centered layout so the indicator sits in
        // the middle of the cell rather than hugging the left edge.
        // findChild<QCheckBox*>() recovers the box from the wrapper in
        // refresh_check_column without needing extra bookkeeping.
        auto* wrap = new QWidget;
        wrap->setStyleSheet(QStringLiteral("background:transparent;"));
        auto* wl = new QHBoxLayout(wrap);
        wl->setContentsMargins(0, 0, 0, 0);
        auto* cb = new QCheckBox;
        cb->setChecked(active_comp_set_.contains(p.symbol));
        cb->setStyleSheet(QStringLiteral(
            "QCheckBox{background:transparent;margin:0;padding:0;}"
            "QCheckBox::indicator{width:14px;height:14px;border:1px solid #6b7280;"
            "border-radius:2px;background:#0a0a0a;}"
            "QCheckBox::indicator:hover{border-color:#d97706;}"
            "QCheckBox::indicator:checked{background:#d97706;border-color:#d97706;}"));
        if (is_primary) {
            cb->setEnabled(false);
            cb->setToolTip(QStringLiteral("(current symbol — primary, not a comparison)"));
        } else {
            cb->setToolTip(QStringLiteral("Toggle on chart comparison overlay"));
            connect(cb, &QCheckBox::clicked, this, [this, wrap](bool checked) {
                // cellWidget identity is fixed; the SYMBOL item at the
                // same visual row may have shifted via sort. Walking the
                // grid finds wrap's current row, then we read SYMBOL.
                const int cc = peer_table_->columnCount() - 1;
                for (int rr = 0; rr < peer_table_->rowCount(); ++rr) {
                    if (peer_table_->cellWidget(rr, cc) != wrap) continue;
                    auto* sit = peer_table_->item(rr, 0);
                    if (!sit) return;
                    const QString sym = sit->text().trimmed().toUpper();
                    if (sym.isEmpty() || sym == current_symbol_) return;
                    if (checked) {
                        active_comp_set_.insert(sym);
                        emit add_to_comparison_requested(sym);
                    } else {
                        active_comp_set_.remove(sym);
                        emit remove_from_comparison_requested(sym);
                    }
                    return;
                }
            });
        }
        wl->addStretch();
        wl->addWidget(cb);
        wl->addStretch();
        peer_table_->setCellWidget(r, comp_col, wrap);
    }

    peer_table_->resizeColumnsToContents();
    // Reserve a comfortable width for the COMP column header + checkbox.
    peer_table_->setColumnWidth(comp_col, 60);
    peer_table_->setSortingEnabled(true);
    // Re-enabling sort applies the current sort indicator immediately —
    // QTableWidgetItems move, but cellWidgets (the checkbox wrappers)
    // do not. Resync check states against the SYMBOL now sitting in each
    // visual row so we don't show a leftover "checked AVGO" box next to
    // an unchecked GOOG row, etc.
    refresh_check_column();
}

QStringList EquityPeersTab::default_peers(const QString& symbol) const {
    static const QHash<QString, QStringList> kPeers = {
        {"AAPL", {"MSFT", "GOOGL", "META", "AMZN", "NVDA"}},
        {"MSFT", {"AAPL", "GOOGL", "AMZN", "ORCL", "CRM"}},
        {"GOOGL", {"MSFT", "AAPL", "META", "AMZN", "NFLX"}},
        {"GOOG", {"MSFT", "AAPL", "META", "AMZN", "NFLX"}},
        {"AMZN", {"MSFT", "GOOGL", "AAPL", "WMT", "TGT"}},
        {"NVDA", {"AMD", "INTC", "QCOM", "TSM", "AVGO"}},
        {"META", {"GOOGL", "SNAP", "PINS", "MSFT", "AMZN"}},
        {"TSLA", {"F", "GM", "NIO", "RIVN", "LCID"}},
        {"JPM", {"BAC", "WFC", "GS", "MS", "C"}},
        {"JNJ", {"PFE", "ABT", "MRK", "BMY", "ABBV"}},
        {"XOM", {"CVX", "BP", "SHEL", "COP", "EOG"}},
        {"RELIANCE.NS", {"TCS.NS", "INFY.NS", "HDFCBANK.NS", "ITC.NS", "HINDUNILVR.NS"}},
        {"TCS.NS", {"INFY.NS", "WIPRO.NS", "HCLTECH.NS", "TECHM.NS", "RELIANCE.NS"}},
        // Quantum computing
        {"IONQ", {"RGTI", "QBTS", "QUBT", "IBM", "ARQQ"}},
        {"RGTI", {"IONQ", "QBTS", "QUBT", "IBM", "ARQQ"}},
        {"QBTS", {"IONQ", "RGTI", "QUBT", "IBM", "ARQQ"}},
        // Semis — chip design / AI infra
        {"ARM",  {"NVDA", "AVGO", "QCOM", "AMD", "INTC"}},
        {"AVGO", {"NVDA", "AMD", "QCOM", "TSM", "MRVL"}},
        {"AMD",  {"NVDA", "INTC", "QCOM", "AVGO", "TSM"}},
        // Enterprise data / AI software
        {"PLTR", {"SNOW", "CRM", "NOW", "MDB", "AI"}},
        // Diversified financials (Berkshire has no true peer; this is the closest set)
        {"BRK.B", {"JPM", "BLK", "MS", "V", "MA"}},
        {"BRK-B", {"JPM", "BLK", "MS", "V", "MA"}},
        // ── More common stocks ────────────────────────────────────────────────
        // Semis (standalone keys)
        {"TSM",  {"NVDA", "AVGO", "INTC", "AMD", "ASML"}},
        {"INTC", {"AMD", "NVDA", "TSM", "QCOM", "AVGO"}},
        {"QCOM", {"NVDA", "AVGO", "AMD", "INTC", "MRVL"}},
        // Enterprise software
        {"ORCL", {"MSFT", "CRM", "SAP", "IBM", "NOW"}},
        {"CRM",  {"ORCL", "NOW", "MSFT", "ADBE", "WDAY"}},
        {"NOW",  {"CRM", "MSFT", "ORCL", "SNOW", "DDOG"}},
        {"SNOW", {"DDOG", "MDB", "NET", "PLTR", "CRM"}},
        {"ADBE", {"CRM", "MSFT", "ORCL", "INTU", "AUTODESK"}},
        // Media / streaming
        {"NFLX", {"DIS", "CMCSA", "WBD", "ROKU", "SPOT"}},
        {"DIS",  {"NFLX", "CMCSA", "PARA", "WBD", "T"}},
        // Retail
        {"WMT",  {"COST", "TGT", "AMZN", "KR", "BJ"}},
        {"COST", {"WMT", "BJ", "TGT", "KR", "DG"}},
        {"HD",   {"LOW", "FND", "BLDR", "TSCO", "AZO"}},
        {"TGT",  {"WMT", "COST", "DG", "DLTR", "BJ"}},
        // Consumer staples
        {"KO",   {"PEP", "MNST", "KDP", "FIZZ", "CELH"}},
        {"PEP",  {"KO", "MNST", "KDP", "CELH", "STZ"}},
        {"PG",   {"KMB", "CL", "CHD", "UL", "EL"}},
        // Payments / fintech
        {"V",    {"MA", "AXP", "PYPL", "FIS", "FISV"}},
        {"MA",   {"V", "AXP", "PYPL", "FIS", "FISV"}},
        {"AXP",  {"V", "MA", "COF", "DFS", "SYF"}},
        {"PYPL", {"V", "MA", "SQ", "ADYEN", "AFRM"}},
        // Banks (extended)
        {"BAC",  {"JPM", "WFC", "C", "USB", "PNC"}},
        {"WFC",  {"JPM", "BAC", "C", "USB", "PNC"}},
        {"GS",   {"MS", "JPM", "BAC", "BLK", "C"}},
        {"MS",   {"GS", "JPM", "BAC", "BLK", "SCHW"}},
        // Healthcare
        {"LLY",  {"NVO", "MRK", "PFE", "AZN", "ABBV"}},
        {"UNH",  {"CVS", "ELV", "CI", "HUM", "CNC"}},
        {"PFE",  {"MRK", "JNJ", "BMY", "ABBV", "LLY"}},
        // Aerospace / defense
        {"BA",   {"LMT", "NOC", "RTX", "GD", "HII"}},
        {"LMT",  {"NOC", "RTX", "GD", "BA", "HII"}},
        {"RTX",  {"LMT", "NOC", "GD", "BA", "HII"}},
        // Industrials
        {"HON",  {"MMM", "GE", "EMR", "ROK", "ETN"}},
        {"CAT",  {"DE", "CMI", "PCAR", "AGCO", "OSK"}},
        {"GE",   {"HON", "MMM", "RTX", "EMR", "ROK"}},
        // Telecom
        {"T",    {"VZ", "TMUS", "CMCSA", "CHTR", "LUMN"}},
        {"VZ",   {"T", "TMUS", "CMCSA", "CHTR", "LUMN"}},
        {"TMUS", {"T", "VZ", "CMCSA", "CHTR", "LUMN"}},
        // Energy (extended)
        {"CVX",  {"XOM", "BP", "SHEL", "COP", "EOG"}},
        {"COP",  {"XOM", "CVX", "EOG", "OXY", "PXD"}},
        // Crypto / fintech retail
        {"COIN", {"HOOD", "SOFI", "MARA", "RIOT", "CLSK"}},
        {"HOOD", {"COIN", "SOFI", "IBKR", "SCHW", "LPLA"}},
        // EV / auto (extended beyond TSLA)
        {"F",    {"GM", "TSLA", "STLA", "TM", "HMC"}},
        {"GM",   {"F", "TSLA", "STLA", "TM", "HMC"}},

        // ── Index & broad-market ETFs ────────────────────────────────────────
        {"SPY",  {"IVV", "VOO", "VTI", "SCHX", "SPLG"}},       // S&P 500
        {"VOO",  {"IVV", "SPY", "VTI", "SCHX", "SPLG"}},
        {"IVV",  {"SPY", "VOO", "VTI", "SCHX", "SPLG"}},
        {"QQQ",  {"QQQM", "ONEQ", "SPYG", "VUG", "IVW"}},      // Nasdaq-100
        {"QQQM", {"QQQ", "ONEQ", "SPYG", "VUG", "IVW"}},
        {"DIA",  {"SPY", "IVV", "OEF", "IWB", "RSP"}},         // Dow
        {"VTI",  {"ITOT", "SCHB", "SWTSX", "FZROX", "VTSAX"}}, // total US
        {"IWM",  {"VTWO", "SCHA", "VBR", "VB", "IJR"}},        // small cap
        {"EFA",  {"VEA", "IEFA", "SCHF", "SPDW", "VXUS"}},     // developed ex-US
        {"VWO",  {"IEMG", "EEM", "SCHE", "SPEM", "VXUS"}},     // emerging
        {"AGG",  {"BND", "SCHZ", "IUSB", "FXNAX", "VBTLX"}},   // total bond

        // ── Sector / thematic ETFs ───────────────────────────────────────────
        {"SMH",  {"SOXX", "FTXL", "XSD", "PSI", "SOXQ"}},      // semiconductor
        {"XLK",  {"VGT", "IYW", "FTEC", "FXL", "IGM"}},        // broad tech
        {"SLX",  {"XME", "PICK", "COPX", "REMX", "GDX"}},      // steel
        {"AIRR", {"XLI", "IYJ", "VIS", "FXR", "ITA"}},         // US industrials
        {"GLD",  {"IAU", "GLDM", "SGOL", "BAR", "AAAU"}},      // gold bullion
        {"XME",  {"PICK", "SLX", "COPX", "REMX", "GDX"}},      // metals & mining
        {"XLE",  {"VDE", "IYE", "FENY", "IXC", "AMLP"}},       // energy
        {"XLF",  {"VFH", "IYF", "FNCL", "KBE", "KRE"}},        // financials
        {"XLV",  {"VHT", "IYH", "FHLC", "IBB", "XBI"}},        // healthcare
        {"XLY",  {"VCR", "IYC", "FDIS", "RXI", "RTH"}},        // consumer disc.
        {"XLP",  {"VDC", "IYK", "FSTA", "KXI", "PSL"}},        // consumer staples
        {"XLI",  {"VIS", "IYJ", "FIDU", "FXR", "EXI"}},        // industrials
        {"XLU",  {"VPU", "IDU", "FUTY", "JXI", "RYU"}},        // utilities
        {"XLB",  {"VAW", "IYM", "FMAT", "MXI", "RTM"}},        // materials
        {"XLRE", {"VNQ", "IYR", "USRT", "REET", "RWR"}},       // real estate
        {"VNQ",  {"XLRE", "IYR", "USRT", "REET", "RWR"}},
        {"GDX",  {"GDXJ", "RING", "GOAU", "SGDM", "PICK"}},    // gold miners
        {"ARKK", {"ARKW", "ARKG", "ARKQ", "ARKF", "TSLA"}},    // disruptive innov.

        // ── Mutual funds (your portfolio holdings) ───────────────────────────
        // Index funds — large-cap blend
        {"FNILX", {"FXAIX", "FZROX", "VFIAX", "SWPPX", "VTSAX"}},
        // Short-term bond
        {"VBIPX", {"VFSUX", "VBIRX", "VSCSX", "VTAPX", "VFICX"}},
        // Active large-cap growth
        {"FTQGX", {"FBGRX", "FCNTX", "VPMCX", "FOCPX", "VWUAX"}},
        // Sector — semiconductors
        {"FSELX", {"FELC", "FSPTX", "SMH", "SOXX", "PSI"}},
        // EKWAX / GRHAX — exact strategy not verified; default to large-cap
        // blend & balanced peers respectively. Edit if your funds differ.
        {"EKWAX", {"FXAIX", "VFIAX", "FCNTX", "VPMCX", "FBGRX"}},
        {"GRHAX", {"VWELX", "FBALX", "VTMFX", "VBIAX", "AOR"}},
    };
    return kPeers.value(symbol.toUpper(), {"SPY", "QQQ", "DIA"});
}

} // namespace fincept::screens

// src/screens/power_trader/TradesFeedPanel.cpp
#include "screens/power_trader/TradesFeedPanel.h"

#include "ui/components/EstTooltip.h"
#include "ui/components/LayoutHelpers.h"
#include "ui/components/SignalTooltip.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

namespace fincept::screens {

static const QStringList kFeedCols = {
    "DISCLOSED", "MEMBER", "PTY", "CHB", "CMTE", "TICKER", "B/S", "AMOUNT", "LAG", "SIG", "SOURCE"
};

static constexpr const char* kPartyD = "#3b82f6";
static constexpr const char* kPartyR = "#ef4444";
static constexpr const char* kPartyI = "#eab308";

static const char* party_color(const QString& p) {
    if (p == QStringLiteral("D")) return kPartyD;
    if (p == QStringLiteral("R")) return kPartyR;
    return kPartyI;
}

static QString combo_style() {
    return QString(
        "QComboBox { background:%1; color:%2; border:1px solid %3;"
        "  border-radius:3px; padding:4px 8px; font-size:12px; }"
        "QComboBox::drop-down { border:none; width:18px; }"
        "QComboBox QAbstractItemView { background:%1; color:%2; border:1px solid %3; "
        "  selection-background-color:%4; }")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_MED(), ui::colors::AMBER_DIM());
}

static QString lineedit_style() {
    return QString(
        "QLineEdit { background:%1; color:%2; border:1px solid %3;"
        "  border-radius:3px; padding:4px 10px; font-size:12px; }"
        "QLineEdit:focus { border:1px solid %4; }")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_MED(), ui::colors::AMBER());
}

static QString dateedit_style() {
    return QString(
        "QDateEdit { background:%1; color:%2; border:1px solid %3;"
        "  border-radius:3px; padding:4px 8px; font-size:12px; }"
        "QDateEdit::drop-down { border:none; width:18px; }")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_MED());
}

TradesFeedPanel::TradesFeedPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void TradesFeedPanel::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Section header ────────────────────────────────────────────────────────
    auto* hdr_label = new QLabel(QStringLiteral("TRADE DISCLOSURES"), this);
    hdr_label->setStyleSheet(
        QString("QLabel { background:%1; color:%2; font-size:12px; font-weight:700;"
                " letter-spacing:0.5px; padding:6px 12px; border-bottom:1px solid %3; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED()));
    layout->addWidget(hdr_label);

    // ── Filter bar ────────────────────────────────────────────────────────────
    auto* filter_row = new QWidget(this);
    filter_row->setStyleSheet(
        QString("QWidget { background:%1; border-bottom:1px solid %2; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* filter_layout = new QHBoxLayout(filter_row);
    filter_layout->setContentsMargins(8, 6, 8, 6);
    filter_layout->setSpacing(8);

    ticker_filter_ = new QLineEdit(this);
    ticker_filter_->setPlaceholderText(QStringLiteral("Filter ticker…"));
    ticker_filter_->setMaximumWidth(120);
    ticker_filter_->setStyleSheet(lineedit_style());
    filter_layout->addWidget(ticker_filter_);

    party_filter_ = new QComboBox(this);
    party_filter_->addItems({QStringLiteral("All Parties"),
                              QStringLiteral("Democrat (D)"),
                              QStringLiteral("Republican (R)")});
    party_filter_->setStyleSheet(combo_style());
    filter_layout->addWidget(party_filter_);

    chamber_filter_ = new QComboBox(this);
    chamber_filter_->addItems({QStringLiteral("All Chambers"),
                                QStringLiteral("Senate"),
                                QStringLiteral("House")});
    chamber_filter_->setStyleSheet(combo_style());
    filter_layout->addWidget(chamber_filter_);

    committee_filter_ = new QComboBox(this);
    committee_filter_->addItem(QStringLiteral("All Committees"));
    committee_filter_->setMinimumWidth(160);
    committee_filter_->setStyleSheet(combo_style());
    connect(committee_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TradesFeedPanel::apply_filters);
    filter_layout->addWidget(committee_filter_);

    auto* from_label = new QLabel(QStringLiteral("From"), this);
    from_label->setStyleSheet(QString("QLabel { color:%1; font-size:12px; background:transparent; }").arg(ui::colors::TEXT_SECONDARY()));
    filter_layout->addWidget(from_label);

    date_from_ = new QDateEdit(QDate::currentDate().addDays(-90), this);
    date_from_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    date_from_->setCalendarPopup(true);
    date_from_->setStyleSheet(dateedit_style());
    filter_layout->addWidget(date_from_);

    auto* to_label = new QLabel(QStringLiteral("To"), this);
    to_label->setStyleSheet(QString("QLabel { color:%1; font-size:12px; background:transparent; }").arg(ui::colors::TEXT_SECONDARY()));
    filter_layout->addWidget(to_label);

    date_to_ = new QDateEdit(QDate::currentDate(), this);
    date_to_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    date_to_->setCalendarPopup(true);
    date_to_->setStyleSheet(dateedit_style());
    filter_layout->addWidget(date_to_);

    filter_layout->addStretch();
    layout->addWidget(filter_row);

    // Connect filter controls
    connect(ticker_filter_,  &QLineEdit::textChanged,    this, &TradesFeedPanel::apply_filters);
    connect(party_filter_,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TradesFeedPanel::apply_filters);
    connect(chamber_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TradesFeedPanel::apply_filters);
    connect(date_from_, &QDateEdit::dateChanged, this, &TradesFeedPanel::apply_filters);
    connect(date_to_,   &QDateEdit::dateChanged, this, &TradesFeedPanel::apply_filters);

    // ── Table ─────────────────────────────────────────────────────────────────
    table_ = new QTableWidget(this);
    table_->setColumnCount(kFeedCols.size());
    table_->setHorizontalHeaderLabels(kFeedCols);
    if (auto* a = table_->horizontalHeaderItem(7)) a->setToolTip(fincept::ui::est::amount_tooltip());
    if (auto* l = table_->horizontalHeaderItem(8)) l->setToolTip(fincept::ui::est::disclosure_lag_tooltip());
    fincept::ui::ensure_header_fits(table_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    table_->setFocusPolicy(Qt::NoFocus);

    auto* h = table_->horizontalHeader();
    h->setMinimumSectionSize(20);
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Fixed);    // DISCLOSED
    h->resizeSection(0, 92);
    h->setSectionResizeMode(1, QHeaderView::Interactive); h->resizeSection(1, 200); h->setStretchLastSection(true);
    h->setSectionResizeMode(2, QHeaderView::Fixed);    // PTY
    h->resizeSection(2, 38);
    h->setSectionResizeMode(3, QHeaderView::Fixed);    // CHB
    h->resizeSection(3, 44);
    h->setSectionResizeMode(4, QHeaderView::Interactive); h->resizeSection(4, 160);
    h->setSectionResizeMode(5, QHeaderView::Fixed);    // TICKER
    h->resizeSection(5, 62);
    h->setSectionResizeMode(6, QHeaderView::Fixed);    // B/S
    h->resizeSection(6, 54);
    h->setSectionResizeMode(7, QHeaderView::Fixed);    // AMOUNT
    h->resizeSection(7, 160);
    h->setSectionResizeMode(8, QHeaderView::Fixed);    // LAG
    h->resizeSection(8, 40);
    h->setSectionResizeMode(9, QHeaderView::Fixed);    // SIG
    h->resizeSection(9, 40);
    h->setSectionResizeMode(10, QHeaderView::Fixed);   // SOURCE
    h->resizeSection(10, 88);

    table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:12px; font-family:Consolas,monospace;"
                "  gridline-color:transparent; }"
                "QTableWidget::item { padding:4px 8px; border-bottom:1px solid %3; }"
                "QTableWidget::item:selected { background:rgba(217,119,6,0.18); color:%2; }"
                "QTableWidget::item:hover { background:%4; }"
                "QScrollBar:vertical { width:4px; background:%1; }"
                "QScrollBar::handle:vertical { background:%3; min-height:16px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BORDER_DIM(), ui::colors::BG_HOVER()));

    h->setStyleSheet(
        QString("QHeaderView::section { background:%1; color:%2; border:none;"
                "  border-bottom:2px solid %3; border-right:1px solid %4;"
                "  padding:4px 6px; font-size:12px; font-weight:700; letter-spacing:0.5px; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::AMBER(), ui::colors::BORDER_DIM()));

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= table_->rowCount())
            return;
        auto* item = table_->item(row, 1);
        if (!item)
            return;
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            emit member_selected(id);
    });

    layout->addWidget(table_);
}

void TradesFeedPanel::set_trades(const QVector<power_trader::PoliticalTrade>& trades) {
    trades_ = trades;
    apply_filters();
}

void TradesFeedPanel::set_selected_member(const QString& member_id) {
    selected_member_id_ = member_id;
}

void TradesFeedPanel::set_available_committees(const QStringList& committees) {
    committee_filter_->blockSignals(true);
    const QString current = committee_filter_->currentIndex() > 0
                            ? committee_filter_->currentText() : QString();
    committee_filter_->clear();
    committee_filter_->addItem(QStringLiteral("All Committees"));
    for (const auto& c : committees)
        committee_filter_->addItem(c);
    if (!current.isEmpty()) {
        const int idx = committee_filter_->findText(current);
        if (idx >= 0) committee_filter_->setCurrentIndex(idx);
    }
    committee_filter_->blockSignals(false);
}

void TradesFeedPanel::apply_filters() {
    const QString ticker_text = ticker_filter_->text().trimmed().toUpper();
    const int party_idx    = party_filter_->currentIndex();    // 0=All,1=D,2=R
    const int chamber_idx  = chamber_filter_->currentIndex();  // 0=All,1=Senate,2=House
    const int cmte_idx     = committee_filter_->currentIndex();// 0=All,1+= specific
    const QString cmte_sel = cmte_idx > 0 ? committee_filter_->currentText() : QString();
    const QDate from_date  = date_from_->date();
    const QDate to_date    = date_to_->date();

    QVector<power_trader::PoliticalTrade> visible;
    visible.reserve(trades_.size());

    for (const auto& t : trades_) {
        if (!ticker_text.isEmpty() && !t.ticker.toUpper().contains(ticker_text))
            continue;
        if (party_idx == 1 && t.party != QStringLiteral("D"))   continue;
        if (party_idx == 2 && t.party != QStringLiteral("R"))   continue;
        if (chamber_idx == 1 && t.chamber != power_trader::MemberChamber::Senate) continue;
        if (chamber_idx == 2 && t.chamber != power_trader::MemberChamber::House)  continue;
        if (!cmte_sel.isEmpty()
            && !t.committee_relevance.contains(cmte_sel, Qt::CaseInsensitive))
            continue;
        if (t.disclosure_date < from_date || t.disclosure_date > to_date)
            continue;
        visible.append(t);
    }

    populate_table(visible);
}

void TradesFeedPanel::populate_table(const QVector<power_trader::PoliticalTrade>& visible) {
    table_->setRowCount(visible.size());

    for (int r = 0; r < visible.size(); ++r) {
        const auto& t = visible[r];
        table_->setRowHeight(r, 30);

        auto mk = [&](int col, const QString& text, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(r, col, item);
        };

        // 0 DISCLOSED
        mk(0, t.disclosure_date.toString(QStringLiteral("yyyy-MM-dd")),
           ui::colors::TEXT_SECONDARY);

        // 1 MEMBER — store member_id in UserRole
        auto* name_item = new QTableWidgetItem(t.member_name);
        name_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        name_item->setForeground(QColor(ui::colors::CYAN()));
        name_item->setData(Qt::UserRole, t.member_id);
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(r, 1, name_item);

        // 2 PTY
        mk(2, t.party, party_color(t.party), Qt::AlignCenter);

        // 3 CHB
        mk(3, t.chamber == power_trader::MemberChamber::Senate ? "SEN" : "HSE",
           ui::colors::TEXT_TERTIARY, Qt::AlignCenter);

        // 4 CMTE — amber if non-empty (committee-relevant trade)
        mk(4, t.committee_relevance.isEmpty() ? QStringLiteral("—") : t.committee_relevance,
           t.committee_relevance.isEmpty() ? ui::colors::TEXT_TERTIARY : ui::colors::AMBER);

        // 5 TICKER
        mk(5, t.ticker);

        // 6 B/S — color coded
        const bool is_buy  = t.direction == power_trader::TradeDirection::Buy;
        const bool is_sell = t.direction == power_trader::TradeDirection::Sell;
        const char* dir_color = is_buy  ? ui::colors::POSITIVE
                               : is_sell ? ui::colors::NEGATIVE
                               : ui::colors::TEXT_SECONDARY;
        mk(6, power_trader::direction_label(t.direction), dir_color, Qt::AlignCenter);

        // 7 AMOUNT
        mk(7, t.amount_range_label, ui::colors::TEXT_SECONDARY);

        // 8 LAG
        {
            auto* lag_item = new QTableWidgetItem(QString::number(t.disclosure_lag_days));
            lag_item->setTextAlignment(Qt::AlignCenter);
            lag_item->setForeground(QColor(t.disclosure_lag_days > 30
                                           ? ui::colors::WARNING : ui::colors::TEXT_SECONDARY()));
            lag_item->setFlags(lag_item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(r, 8, lag_item);
        }

        // 9 SIG — tooltip breaks the score back down into its components
        {
            auto* sig_item = new QTableWidgetItem(QString::number(t.signal_score, 'f', 0));
            sig_item->setTextAlignment(Qt::AlignCenter);
            sig_item->setForeground(QColor(t.signal_score >= 60
                                           ? ui::colors::AMBER : ui::colors::TEXT_SECONDARY()));
            sig_item->setFlags(sig_item->flags() & ~Qt::ItemIsEditable);
            sig_item->setToolTip(fincept::ui::tooltip_for_trade_signal(t));
            table_->setItem(r, 9, sig_item);
        }

        // 10 SOURCE — data_source is not in PoliticalTrade; infer from chamber
        mk(10, t.chamber == power_trader::MemberChamber::Senate
               ? QStringLiteral("Senate eFTS") : QStringLiteral("House FDS"),
           ui::colors::TEXT_TERTIARY);
    }
}

} // namespace fincept::screens

// src/screens/power_trader/CommitteePanel.cpp
#include "screens/power_trader/CommitteePanel.h"

#include "screens/power_trader/PowerTraderService.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QVBoxLayout>

namespace fincept::screens {

static QString section_header_style() {
    return QString("QLabel{background:%1;color:%2;font-size:11px;font-weight:700;"
                   "letter-spacing:0.5px;padding:6px 12px;border-bottom:1px solid %3;}")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED());
}

static QString table_style() {
    return QString("QTableWidget{background:%1;color:%2;border:none;font-size:12px;"
                   "font-family:Consolas,monospace;gridline-color:transparent;}"
                   "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %3;}"
                   "QTableWidget::item:selected{background:rgba(217,119,6,0.18);color:%2;}"
                   "QTableWidget::item:hover{background:%4;}"
                   "QScrollBar:vertical{width:5px;background:%1;}"
                   "QScrollBar::handle:vertical{background:%3;min-height:20px;}")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_MED(), ui::colors::BG_HOVER());
}

static QString header_style() {
    return QString("QHeaderView::section{background:%1;color:%2;border:none;"
                   "border-bottom:2px solid %3;border-right:1px solid %4;"
                   "padding:5px 8px;font-size:10px;font-weight:700;letter-spacing:0.5px;}")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::AMBER(), ui::colors::BORDER_MED());
}

CommitteePanel::CommitteePanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void CommitteePanel::build_ui() {
    setStyleSheet(QString("background:%1;color:%2;").arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* hdr = new QLabel(QStringLiteral("COMMITTEE INTELLIGENCE"));
    hdr->setStyleSheet(section_header_style());
    root->addWidget(hdr);

    // ── Horizontal splitter: committee list | detail ──────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setStyleSheet(QString("QSplitter::handle{background:%1;}")
                                .arg(ui::colors::BORDER_DIM()));

    // ── Left: committee list ──────────────────────────────────────────────────
    auto* left = new QWidget;
    {
        auto* ll = new QVBoxLayout(left);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(0);

        auto* lhdr = new QLabel(QStringLiteral("COMMITTEES"));
        lhdr->setStyleSheet(section_header_style());
        ll->addWidget(lhdr);

        committee_list_ = new QTableWidget;
        committee_list_->setColumnCount(3);
        committee_list_->setHorizontalHeaderLabels({"COMMITTEE", "TRADES", "CORR %"});
        committee_list_->setSelectionBehavior(QAbstractItemView::SelectRows);
        committee_list_->setSelectionMode(QAbstractItemView::SingleSelection);
        committee_list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        committee_list_->setShowGrid(false);
        committee_list_->verticalHeader()->setVisible(false);
        committee_list_->setFocusPolicy(Qt::NoFocus);

        auto* ch = committee_list_->horizontalHeader();
        ch->setSectionResizeMode(0, QHeaderView::Interactive); ch->resizeSection(0, 240); ch->setStretchLastSection(true);
        ch->setSectionResizeMode(1, QHeaderView::Fixed);
        ch->resizeSection(1, 52);
        ch->setSectionResizeMode(2, QHeaderView::Fixed);
        ch->resizeSection(2, 56);
        ch->setStyleSheet(header_style());
        committee_list_->setStyleSheet(table_style());

        connect(committee_list_, &QTableWidget::currentCellChanged,
                this, [this](int row, int, int, int) { on_committee_selected(row); });
        ll->addWidget(committee_list_, 1);
    }

    // ── Right: detail pane ────────────────────────────────────────────────────
    auto* right = new QWidget;
    right->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    {
        auto* rl = new QVBoxLayout(right);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);

        detail_title_ = new QLabel(QStringLiteral("Select a committee"));
        detail_title_->setStyleSheet(
            QString("color:%1;font-size:13px;font-weight:700;padding:10px 14px;"
                    "border-bottom:1px solid %2;background:%3;")
                .arg(ui::colors::AMBER(), ui::colors::BORDER_DIM(), ui::colors::BG_SURFACE()));
        rl->addWidget(detail_title_);

        // Stats row
        detail_stats_ = new QLabel;
        detail_stats_->setStyleSheet(
            QString("color:%1;font-size:11px;padding:6px 14px;"
                    "background:%2;border-bottom:1px solid %3;")
                .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        detail_stats_->setWordWrap(true);
        rl->addWidget(detail_stats_);

        // Top tickers + members
        auto* info_row = new QWidget;
        info_row->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                                    .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        auto* ir = new QHBoxLayout(info_row);
        ir->setContentsMargins(14, 6, 14, 6);

        detail_tickers_ = new QLabel;
        detail_tickers_->setStyleSheet(QString("color:%1;font-size:11px;").arg(ui::colors::CYAN()));
        ir->addWidget(detail_tickers_);
        ir->addStretch();
        detail_members_ = new QLabel;
        detail_members_->setStyleSheet(
            QString("color:%1;font-size:11px;").arg(ui::colors::TEXT_TERTIARY()));
        ir->addWidget(detail_members_);
        rl->addWidget(info_row);

        // Trade table
        auto* thdr = new QLabel(QStringLiteral("COMMITTEE-RELEVANT TRADES"));
        thdr->setStyleSheet(section_header_style());
        rl->addWidget(thdr);

        trade_table_ = new QTableWidget;
        trade_table_->setColumnCount(8);
        trade_table_->setHorizontalHeaderLabels(
            {"MEMBER", "PTY", "TICKER", "ASSET", "B/S", "AMOUNT", "LAG", "SIG"});
        trade_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        trade_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        trade_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        trade_table_->setShowGrid(false);
        trade_table_->verticalHeader()->setVisible(false);
        trade_table_->setFocusPolicy(Qt::NoFocus);

        auto* th = trade_table_->horizontalHeader();
        th->setSectionResizeMode(0, QHeaderView::Interactive); th->resizeSection(0, 180);
        th->setSectionResizeMode(1, QHeaderView::Fixed); th->resizeSection(1, 30);
        th->setSectionResizeMode(2, QHeaderView::Fixed); th->resizeSection(2, 60);
        th->setSectionResizeMode(3, QHeaderView::Interactive); th->resizeSection(3, 200); th->setStretchLastSection(true);
        th->setSectionResizeMode(4, QHeaderView::Fixed); th->resizeSection(4, 44);
        th->setSectionResizeMode(5, QHeaderView::Fixed); th->resizeSection(5, 130);
        th->setSectionResizeMode(6, QHeaderView::Fixed); th->resizeSection(6, 40);
        th->setSectionResizeMode(7, QHeaderView::Fixed); th->resizeSection(7, 40);
        th->setStyleSheet(header_style());
        trade_table_->setStyleSheet(table_style());

        connect(trade_table_, &QTableWidget::cellClicked, this, [this](int row, int) {
            auto* item = trade_table_->item(row, 0);
            if (item) emit member_selected(item->data(Qt::UserRole).toString());
        });
        rl->addWidget(trade_table_, 1);
    }

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setSizes({300, 700});

    root->addWidget(splitter, 1);
}

void CommitteePanel::set_data(const power_trader::PowerTraderSummary& summary,
                               const QVector<power_trader::CommitteeGroup>& groups) {
    summary_ = summary;
    groups_  = groups;
    populate_committee_list();
    if (!groups_.isEmpty())
        on_committee_selected(0);
}

void CommitteePanel::populate_committee_list() {
    committee_list_->setRowCount(groups_.size());
    for (int r = 0; r < groups_.size(); ++r) {
        const auto& g = groups_[r];
        committee_list_->setRowHeight(r, 32);

        auto* name_item = new QTableWidgetItem(g.committee);
        name_item->setForeground(QColor(ui::colors::TEXT_PRIMARY()));
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        committee_list_->setItem(r, 0, name_item);

        auto* cnt_item = new QTableWidgetItem(QString::number(g.trade_count));
        cnt_item->setTextAlignment(Qt::AlignCenter);
        cnt_item->setForeground(QColor(g.trade_count > 0 ? ui::colors::AMBER() : ui::colors::TEXT_TERTIARY()));
        cnt_item->setFlags(cnt_item->flags() & ~Qt::ItemIsEditable);
        committee_list_->setItem(r, 1, cnt_item);

        auto* corr_item = new QTableWidgetItem(
            QString::number(g.correlation_pct, 'f', 0) + "%");
        corr_item->setTextAlignment(Qt::AlignCenter);
        const char* corr_color = g.correlation_pct > 40 ? ui::colors::WARNING
                               : g.correlation_pct > 20 ? ui::colors::AMBER
                               : ui::colors::TEXT_TERTIARY;
        corr_item->setForeground(QColor(corr_color));
        corr_item->setFlags(corr_item->flags() & ~Qt::ItemIsEditable);
        committee_list_->setItem(r, 2, corr_item);
    }
}

void CommitteePanel::on_committee_selected(int row) {
    if (row < 0 || row >= groups_.size()) return;
    selected_row_ = row;
    committee_list_->selectRow(row);
    show_committee(groups_[row]);
}

void CommitteePanel::show_committee(const power_trader::CommitteeGroup& g) {
    detail_title_->setText(g.committee);

    detail_stats_->setText(
        QString("%1 members  ·  %2 committee-relevant trades  ·  "
                "~$%3  ·  avg signal %4  ·  %5% insider correlation")
            .arg(g.member_count)
            .arg(g.trade_count)
            .arg(g.total_est_amount >= 1e6
                 ? QString::number(g.total_est_amount/1e6,'f',1)+"M"
                 : QString::number(g.total_est_amount/1e3,'f',0)+"K")
            .arg(g.avg_signal_score, 0, 'f', 0)
            .arg(g.correlation_pct,  0, 'f', 0));

    detail_tickers_->setText("Top tickers: " +
        (g.top_tickers.isEmpty() ? "—" : g.top_tickers.join("  ")));

    // Members on this committee
    QStringList member_names;
    for (const auto& mid : g.member_ids) {
        for (const auto& m : summary_.members)
            if (m.id == mid) { member_names.append(m.full_name); break; }
    }
    detail_members_->setText("Members: " +
        (member_names.isEmpty() ? "—" : member_names.mid(0,6).join(", ")
         + (member_names.size()>6 ? QString(" +%1 more").arg(member_names.size()-6) : "")));

    // Filter trades for this committee
    const auto trades = power_trader::PowerTraderService::instance()
                            .trades_by_committee(g.committee);
    trade_table_->setRowCount(trades.size());

    for (int r = 0; r < trades.size(); ++r) {
        const auto& t = trades[r];
        trade_table_->setRowHeight(r, 30);

        auto* name_item = new QTableWidgetItem(t.member_name);
        name_item->setForeground(QColor(ui::colors::CYAN()));
        name_item->setData(Qt::UserRole, t.member_id);
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        trade_table_->setItem(r, 0, name_item);

        const char* pc = t.party == QStringLiteral("D") ? "#3b82f6"
                        : t.party == QStringLiteral("R") ? "#ef4444"
                        : ui::colors::AMBER;

        auto mk = [&](int col, const QString& txt, const char* col_color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* i = new QTableWidgetItem(txt);
            i->setTextAlignment(align);
            i->setForeground(QColor(col_color ? col_color : ui::colors::TEXT_PRIMARY()));
            i->setFlags(i->flags() & ~Qt::ItemIsEditable);
            trade_table_->setItem(r, col, i);
        };

        mk(1, t.party, pc, Qt::AlignCenter);
        mk(2, t.ticker);
        mk(3, t.asset_name);
        const bool buy = t.direction == power_trader::TradeDirection::Buy;
        mk(4, power_trader::direction_label(t.direction),
           buy ? ui::colors::POSITIVE : ui::colors::NEGATIVE, Qt::AlignCenter);
        mk(5, t.amount_range_label, ui::colors::TEXT_SECONDARY);

        auto* lag_i = new QTableWidgetItem(QString::number(t.disclosure_lag_days));
        lag_i->setTextAlignment(Qt::AlignCenter);
        lag_i->setForeground(QColor(t.disclosure_lag_days > 30
                                    ? ui::colors::WARNING : ui::colors::TEXT_TERTIARY()));
        lag_i->setFlags(lag_i->flags() & ~Qt::ItemIsEditable);
        trade_table_->setItem(r, 6, lag_i);

        auto* sig_i = new QTableWidgetItem(QString::number(t.signal_score,'f',0));
        sig_i->setTextAlignment(Qt::AlignCenter);
        sig_i->setForeground(QColor(t.signal_score >= 60
                                    ? ui::colors::AMBER : ui::colors::TEXT_TERTIARY()));
        sig_i->setFlags(sig_i->flags() & ~Qt::ItemIsEditable);
        trade_table_->setItem(r, 7, sig_i);
    }
}

} // namespace fincept::screens

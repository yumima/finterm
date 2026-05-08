// src/screens/power_trader/PartyPanel.cpp
#include "screens/power_trader/PartyPanel.h"

#include "screens/power_trader/PowerTraderService.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens {

static constexpr const char* kBlue = "#3b82f6";
static constexpr const char* kRed  = "#ef4444";

static QWidget* make_divider(const QString& label) {
    auto* w = new QWidget;
    w->setFixedHeight(30);
    w->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                         .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED()));
    auto* l = new QLabel(label, w);
    l->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;letter-spacing:0.5px;padding:0 12px;")
            .arg(ui::colors::TEXT_SECONDARY()));
    auto* ll = new QHBoxLayout(w);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(l);
    return w;
}

static QTableWidget* make_table(const QStringList& cols) {
    auto* t = new QTableWidget;
    t->setColumnCount(cols.size());
    t->setHorizontalHeaderLabels(cols);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setShowGrid(false);
    t->verticalHeader()->setVisible(false);
    t->setFocusPolicy(Qt::NoFocus);
    t->setStyleSheet(
        QString("QTableWidget{background:%1;color:%2;border:none;font-size:12px;"
                "font-family:Consolas,monospace;gridline-color:transparent;}"
                "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %3;}"
                "QTableWidget::item:selected{background:rgba(217,119,6,0.12);}"
                "QScrollBar:vertical{width:5px;background:%1;}"
                "QScrollBar::handle:vertical{background:%3;}")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED()));
    t->horizontalHeader()->setStyleSheet(
        QString("QHeaderView::section{background:%1;color:%2;border:none;"
                "border-bottom:2px solid %3;padding:5px 8px;font-size:12px;font-weight:700;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::AMBER()));
    return t;
}

PartyPanel::PartyPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void PartyPanel::build_ui() {
    setStyleSheet(QString("background:%1;color:%2;")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header ────────────────────────────────────────────────────────────────
    auto* hdr = new QLabel(QStringLiteral("PARTY INTELLIGENCE  ·  DEMOCRAT vs REPUBLICAN TRADING COMPARISON"));
    hdr->setStyleSheet(
        QString("background:%1;color:%2;font-size:12px;font-weight:700;"
                "letter-spacing:1.5px;padding:5px 10px;border-bottom:1px solid %3;")
            .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM()));
    root->addWidget(hdr);

    // ── Comparison bar ────────────────────────────────────────────────────────
    compare_bar_ = new QLabel(QStringLiteral("Loading…"));
    compare_bar_->setAlignment(Qt::AlignCenter);
    compare_bar_->setStyleSheet(
        QString("background:%1;color:%2;font-size:12px;padding:6px 14px;"
                "border-bottom:1px solid %3;")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM()));
    root->addWidget(compare_bar_);

    // ── Two-column scroll layout ──────────────────────────────────────────────
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QString("QScrollArea{background:%1;border:none;}"
                                  "QScrollBar:vertical{width:4px;background:%1;}"
                                  "QScrollBar::handle:vertical{background:%2;}")
                              .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));

    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* cols = new QHBoxLayout(body);
    cols->setContentsMargins(0, 0, 0, 0);
    cols->setSpacing(0);

    // Democrat column
    auto* d_col = new QWidget;
    d_col->setStyleSheet(QString("background:%1;border-right:1px solid %2;")
                             .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
    auto* d_layout = new QVBoxLayout(d_col);
    d_layout->setContentsMargins(0, 0, 0, 0);
    d_layout->setSpacing(0);

    d_title_ = new QLabel(QStringLiteral("DEMOCRAT (D)"));
    d_title_->setStyleSheet(
        QString("background:%1;color:%2;font-size:13px;font-weight:700;"
                "padding:10px 14px;border-bottom:1px solid %3;")
            .arg(ui::colors::BG_SURFACE(), kBlue, ui::colors::BORDER_DIM()));
    d_layout->addWidget(d_title_);

    d_stats_ = new QLabel;
    d_stats_->setWordWrap(true);
    d_stats_->setStyleSheet(
        QString("color:%1;font-size:12px;padding:8px 14px;"
                "background:%2;border-bottom:1px solid %3;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    d_layout->addWidget(d_stats_);

    d_layout->addWidget(make_divider(QStringLiteral("TOP SECTORS  (90 days)")));
    d_sectors_ = make_table({"SECTOR", "$AMOUNT", "TRADES"});
    d_sectors_->setMinimumHeight(160);
    d_sectors_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive); d_sectors_->horizontalHeader()->resizeSection(0, 120); d_sectors_->horizontalHeader()->setStretchLastSection(true);
    d_sectors_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    d_sectors_->horizontalHeader()->resizeSection(1, 90);
    d_sectors_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    d_sectors_->horizontalHeader()->resizeSection(2, 50);
    d_layout->addWidget(d_sectors_);

    d_layout->addWidget(make_divider(QStringLiteral("TOP TICKERS  (90 days)")));
    d_tickers_ = make_table({"TICKER", "NET FLOW", "# TRADES"});
    d_tickers_->setMinimumHeight(160);
    d_tickers_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    d_tickers_->horizontalHeader()->resizeSection(0, 70);
    d_tickers_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive); d_tickers_->horizontalHeader()->resizeSection(1, 80); d_tickers_->horizontalHeader()->setStretchLastSection(true);
    d_tickers_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    d_tickers_->horizontalHeader()->resizeSection(2, 60);
    d_layout->addWidget(d_tickers_);
    d_layout->addStretch();

    // Republican column
    auto* r_col = new QWidget;
    r_col->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* r_layout = new QVBoxLayout(r_col);
    r_layout->setContentsMargins(0, 0, 0, 0);
    r_layout->setSpacing(0);

    r_title_ = new QLabel(QStringLiteral("REPUBLICAN (R)"));
    r_title_->setStyleSheet(
        QString("background:%1;color:%2;font-size:13px;font-weight:700;"
                "padding:10px 14px;border-bottom:1px solid %3;")
            .arg(ui::colors::BG_SURFACE(), kRed, ui::colors::BORDER_DIM()));
    r_layout->addWidget(r_title_);

    r_stats_ = new QLabel;
    r_stats_->setWordWrap(true);
    r_stats_->setStyleSheet(
        QString("color:%1;font-size:12px;padding:8px 14px;"
                "background:%2;border-bottom:1px solid %3;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    r_layout->addWidget(r_stats_);

    r_layout->addWidget(make_divider(QStringLiteral("TOP SECTORS  (90 days)")));
    r_sectors_ = make_table({"SECTOR", "$AMOUNT", "TRADES"});
    r_sectors_->setMinimumHeight(160);
    r_sectors_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive); r_sectors_->horizontalHeader()->resizeSection(0, 120); r_sectors_->horizontalHeader()->setStretchLastSection(true);
    r_sectors_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    r_sectors_->horizontalHeader()->resizeSection(1, 90);
    r_sectors_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    r_sectors_->horizontalHeader()->resizeSection(2, 50);
    r_layout->addWidget(r_sectors_);

    r_layout->addWidget(make_divider(QStringLiteral("TOP TICKERS  (90 days)")));
    r_tickers_ = make_table({"TICKER", "NET FLOW", "# TRADES"});
    r_tickers_->setMinimumHeight(160);
    r_tickers_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    r_tickers_->horizontalHeader()->resizeSection(0, 70);
    r_tickers_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive); r_tickers_->horizontalHeader()->resizeSection(1, 80); r_tickers_->horizontalHeader()->setStretchLastSection(true);
    r_tickers_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    r_tickers_->horizontalHeader()->resizeSection(2, 60);
    r_layout->addWidget(r_tickers_);
    r_layout->addStretch();

    cols->addWidget(d_col, 1);
    cols->addWidget(r_col, 1);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);
}

void PartyPanel::set_data(const power_trader::PowerTraderSummary& summary) {
    auto& svc = power_trader::PowerTraderService::instance();
    const auto d_stats = svc.party_stats(QStringLiteral("D"));
    const auto r_stats = svc.party_stats(QStringLiteral("R"));

    // ── Comparison bar ────────────────────────────────────────────────────────
    const double d_flow = d_stats.net_bought_90d;
    const double r_flow = r_stats.net_bought_90d;
    auto fmt_flow = [](double v) -> QString {
        const QString sign = v >= 0 ? "+" : "";
        if (qAbs(v) >= 1e6) return sign + QString::number(v/1e6,'f',1)+"M net";
        return sign + QString::number(v/1e3,'f',0)+"K net";
    };
    compare_bar_->setText(
        QString("D: %1 members  %2 trades  %3   ·   "
                "R: %4 members  %5 trades  %6   ·   "
                "Avg signal  D:%7  R:%8")
            .arg(d_stats.member_count).arg(d_stats.trade_count_90d).arg(fmt_flow(d_flow))
            .arg(r_stats.member_count).arg(r_stats.trade_count_90d).arg(fmt_flow(r_flow))
            .arg(d_stats.avg_signal, 0,'f',0)
            .arg(r_stats.avg_signal, 0,'f',0));

    // ── Helper: fill a column ─────────────────────────────────────────────────
    auto fill_col = [&](QLabel* stats_lbl, QTableWidget* sectors_tbl,
                        QTableWidget* tickers_tbl,
                        const power_trader::PartyStats& ps,
                        const char* party_color) {
        stats_lbl->setText(
            QString("%1 members  ·  %2 trades (90d)  ·  ~$%3 total\n"
                    "Avg signal: %4 / 100  ·  Avg lag: %5d  ·  "
                    "Net buyers: %6  Net sellers: %7")
                .arg(ps.member_count)
                .arg(ps.trade_count_90d)
                .arg(ps.total_disc_90d >= 1e6
                     ? QString::number(ps.total_disc_90d/1e6,'f',1)+"M"
                     : QString::number(ps.total_disc_90d/1e3,'f',0)+"K")
                .arg(ps.avg_signal, 0,'f',0)
                .arg(ps.avg_lag, 0,'f',0)
                .arg(ps.net_buyer_count)
                .arg(ps.net_seller_count));

        // Sectors table
        sectors_tbl->setRowCount(ps.top_sectors.size());
        for (int r = 0; r < ps.top_sectors.size(); ++r) {
            const auto& s = ps.top_sectors[r];
            sectors_tbl->setRowHeight(r, 32);
            auto mk = [&](int col, const QString& txt, Qt::Alignment align = Qt::AlignLeft|Qt::AlignVCenter) {
                auto* i = new QTableWidgetItem(txt);
                i->setTextAlignment(align);
                i->setForeground(QColor(col == 0 ? party_color : ui::colors::TEXT_PRIMARY()));
                i->setFlags(i->flags() & ~Qt::ItemIsEditable);
                sectors_tbl->setItem(r, col, i);
            };
            mk(0, s.sector);
            mk(1, s.total_est_amount >= 1e6
               ? "$"+QString::number(s.total_est_amount/1e6,'f',1)+"M"
               : "$"+QString::number(s.total_est_amount/1e3,'f',0)+"K",
               Qt::AlignRight | Qt::AlignVCenter);
            mk(2, QString::number(s.trade_count), Qt::AlignCenter);
        }

        // Tickers table
        tickers_tbl->setRowCount(ps.top_tickers.size());
        // Compute per-ticker net flow from summary trades
        QHash<QString, double> ticker_net;
        QHash<QString, int>    ticker_cnt;
        for (const auto& t : summary.recent_trades) {
            if (t.party != ps.party) continue;
            const double mid = (t.amount_low + t.amount_high) / 2.0;
            const double delta = t.direction == power_trader::TradeDirection::Buy ? mid : -mid;
            ticker_net[t.ticker] += delta;
            ticker_cnt[t.ticker]++;
        }
        for (int r = 0; r < ps.top_tickers.size(); ++r) {
            const QString& tk = ps.top_tickers[r];
            tickers_tbl->setRowHeight(r, 32);
            auto mk = [&](int col, const QString& txt, const char* cc = nullptr,
                          Qt::Alignment align = Qt::AlignLeft|Qt::AlignVCenter) {
                auto* i = new QTableWidgetItem(txt);
                i->setTextAlignment(align);
                i->setForeground(QColor(cc ? cc : ui::colors::TEXT_PRIMARY()));
                i->setFlags(i->flags() & ~Qt::ItemIsEditable);
                tickers_tbl->setItem(r, col, i);
            };
            mk(0, tk, party_color);
            const double net = ticker_net.value(tk, 0);
            const char* nc  = net >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
            mk(1, (net >= 0 ? "+" : "") +
               (qAbs(net) >= 1e6 ? QString::number(net/1e6,'f',1)+"M"
                                  : QString::number(net/1e3,'f',0)+"K"),
               nc, Qt::AlignRight | Qt::AlignVCenter);
            mk(2, QString::number(ticker_cnt.value(tk, 0)), nullptr, Qt::AlignCenter);
        }
    };

    fill_col(d_stats_, d_sectors_, d_tickers_, d_stats, kBlue);
    fill_col(r_stats_, r_sectors_, r_tickers_, r_stats, kRed);
}

} // namespace fincept::screens

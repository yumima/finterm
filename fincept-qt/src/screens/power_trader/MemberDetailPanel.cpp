// src/screens/power_trader/MemberDetailPanel.cpp
#include "screens/power_trader/MemberDetailPanel.h"

#include "ui/theme/Theme.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::screens {

static constexpr const char* kPartyD = "#3b82f6";
static constexpr const char* kPartyR = "#ef4444";
static constexpr const char* kPartyI = "#eab308";

static const char* party_color(const QString& p) {
    if (p == QStringLiteral("D")) return kPartyD;
    if (p == QStringLiteral("R")) return kPartyR;
    return kPartyI;
}

static const QStringList kHistCols = {
    "DATE", "TICKER", "B/S", "AMOUNT", "LAG", "SIG"
};

static QLabel* make_stat_label(QWidget* parent, const QString& caption) {
    auto* w = new QWidget(parent);
    w->setStyleSheet(QString("QWidget { background:%1; border-radius:4px; padding:4px; }")
                         .arg(ui::colors::BG_RAISED()));
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(8, 6, 8, 6);
    vl->setSpacing(2);

    auto* cap = new QLabel(caption, w);
    cap->setStyleSheet(QString("QLabel { color:%1; font-size:9px; font-weight:700;"
                               " letter-spacing:0.8px; background:transparent; }")
                           .arg(ui::colors::TEXT_TERTIARY()));
    vl->addWidget(cap);

    auto* val = new QLabel(QStringLiteral("—"), w);
    val->setStyleSheet(QString("QLabel { color:%1; font-size:13px; font-family:Consolas,monospace;"
                               " font-weight:600; background:transparent; }")
                           .arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(val);
    return val;
}

MemberDetailPanel::MemberDetailPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
    show_placeholder();
}

void MemberDetailPanel::build_ui() {
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    // ── Section header ────────────────────────────────────────────────────────
    auto* hdr_label = new QLabel(QStringLiteral("MEMBER DETAIL"), this);
    hdr_label->setStyleSheet(
        QString("QLabel { background:%1; color:%2; font-size:9px; font-weight:700;"
                " letter-spacing:1.5px; padding:6px 10px; border-bottom:1px solid %3; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_TERTIARY(), ui::colors::BORDER_DIM()));
    root_layout->addWidget(hdr_label);

    // ── Placeholder ───────────────────────────────────────────────────────────
    placeholder_ = new QWidget(this);
    {
        auto* pl = new QVBoxLayout(placeholder_);
        pl->setAlignment(Qt::AlignCenter);
        auto* msg = new QLabel(QStringLiteral("Select a member\nto view details"), placeholder_);
        msg->setAlignment(Qt::AlignCenter);
        msg->setStyleSheet(QString("QLabel { color:%1; font-size:13px; }").arg(ui::colors::TEXT_TERTIARY()));
        pl->addWidget(msg);
    }
    root_layout->addWidget(placeholder_);

    // ── Detail widget (inside a scroll area) ──────────────────────────────────
    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setStyleSheet(
        QString("QScrollArea { background:%1; border:none; }"
                "QScrollBar:vertical { width:4px; background:%1; }"
                "QScrollBar::handle:vertical { background:%2; min-height:16px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));

    detail_widget_ = new QWidget(this);
    detail_widget_->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    scroll_area_->setWidget(detail_widget_);

    auto* dv = new QVBoxLayout(detail_widget_);
    dv->setContentsMargins(10, 10, 10, 10);
    dv->setSpacing(10);

    // Name + party badge + meta row
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        row->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        name_label_ = new QLabel(QStringLiteral("—"), detail_widget_);
        name_label_->setStyleSheet(
            QString("QLabel { color:%1; font-size:16px; font-weight:700; background:transparent; }")
                .arg(ui::colors::TEXT_PRIMARY()));
        row->addWidget(name_label_);

        party_badge_ = new QLabel(QStringLiteral("—"), detail_widget_);
        party_badge_->setFixedSize(28, 20);
        party_badge_->setAlignment(Qt::AlignCenter);
        party_badge_->setStyleSheet(
            QString("QLabel { background:%1; color:white; font-size:10px; font-weight:700;"
                    " border-radius:3px; }")
                .arg(ui::colors::BG_RAISED()));
        row->addWidget(party_badge_);

        row->addStretch();
        dv->addLayout(row);

        meta_label_ = new QLabel(QStringLiteral("—"), detail_widget_);
        meta_label_->setStyleSheet(
            QString("QLabel { color:%1; font-size:11px; background:transparent; }")
                .arg(ui::colors::TEXT_SECONDARY()));
        dv->addWidget(meta_label_);
    }

    // Divider
    {
        auto* line = new QFrame(detail_widget_);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QString("QFrame { color:%1; }").arg(ui::colors::BORDER_DIM()));
        dv->addWidget(line);
    }

    // Stats row — 4 tiles in a grid
    {
        auto* stats_row = new QWidget(detail_widget_);
        stats_row->setStyleSheet("QWidget { background:transparent; }");
        auto* grid = new QGridLayout(stats_row);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(6);

        alpha_label_    = make_stat_label(stats_row, "ALPHA YTD");
        return_label_   = make_stat_label(stats_row, "RETURN YTD");
        trades_label_   = make_stat_label(stats_row, "TRADES YTD");
        net_worth_label_= make_stat_label(stats_row, "EST. NET WORTH");

        // Place the value labels' parent widgets (walk up one level)
        grid->addWidget(alpha_label_->parentWidget(),     0, 0);
        grid->addWidget(return_label_->parentWidget(),    0, 1);
        grid->addWidget(trades_label_->parentWidget(),    1, 0);
        grid->addWidget(net_worth_label_->parentWidget(), 1, 1);
        dv->addWidget(stats_row);
    }

    // Committees section
    {
        auto* cap = new QLabel(QStringLiteral("COMMITTEES"), detail_widget_);
        cap->setStyleSheet(
            QString("QLabel { color:%1; font-size:9px; font-weight:700;"
                    " letter-spacing:1.2px; background:transparent; margin-top:4px; }")
                .arg(ui::colors::TEXT_TERTIARY()));
        dv->addWidget(cap);

        committees_label_ = new QLabel(QStringLiteral("—"), detail_widget_);
        committees_label_->setWordWrap(true);
        committees_label_->setStyleSheet(
            QString("QLabel { color:%1; font-size:11px; background:transparent; }")
                .arg(ui::colors::TEXT_SECONDARY()));
        dv->addWidget(committees_label_);
    }

    // Trade history header
    {
        auto* line = new QFrame(detail_widget_);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QString("QFrame { color:%1; }").arg(ui::colors::BORDER_DIM()));
        dv->addWidget(line);

        auto* cap = new QLabel(QStringLiteral("TRADE HISTORY (LAST 20)"), detail_widget_);
        cap->setStyleSheet(
            QString("QLabel { color:%1; font-size:9px; font-weight:700;"
                    " letter-spacing:1.2px; background:transparent; }")
                .arg(ui::colors::TEXT_TERTIARY()));
        dv->addWidget(cap);
    }

    // Trade history table
    hist_table_ = new QTableWidget(detail_widget_);
    hist_table_->setColumnCount(kHistCols.size());
    hist_table_->setHorizontalHeaderLabels(kHistCols);
    hist_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    hist_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    hist_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    hist_table_->setShowGrid(false);
    hist_table_->setAlternatingRowColors(false);
    hist_table_->verticalHeader()->setVisible(false);
    hist_table_->setFocusPolicy(Qt::NoFocus);

    auto* hh = hist_table_->horizontalHeader();
    hh->setMinimumSectionSize(20);
    hh->setStretchLastSection(false);
    hh->setSectionResizeMode(0, QHeaderView::Fixed);  // DATE
    hh->resizeSection(0, 84);
    hh->setSectionResizeMode(1, QHeaderView::Stretch); // TICKER
    hh->setSectionResizeMode(2, QHeaderView::Fixed);  // B/S
    hh->resizeSection(2, 44);
    hh->setSectionResizeMode(3, QHeaderView::Fixed);  // AMOUNT
    hh->resizeSection(3, 110);
    hh->setSectionResizeMode(4, QHeaderView::Fixed);  // LAG
    hh->resizeSection(4, 36);
    hh->setSectionResizeMode(5, QHeaderView::Fixed);  // SIG
    hh->resizeSection(5, 36);

    hist_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:11px; font-family:Consolas,monospace;"
                "  gridline-color:transparent; }"
                "QTableWidget::item { padding:2px 5px; border-bottom:1px solid %3; }"
                "QTableWidget::item:selected { background:rgba(217,119,6,0.18); color:%2; }"
                "QScrollBar:vertical { width:4px; background:%1; }"
                "QScrollBar::handle:vertical { background:%3; min-height:16px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));

    hh->setStyleSheet(
        QString("QHeaderView::section { background:%1; color:%2; border:none;"
                "  border-bottom:2px solid %3; border-right:1px solid %4;"
                "  padding:3px 5px; font-size:9px; font-weight:700; letter-spacing:0.5px; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::AMBER(), ui::colors::BORDER_DIM()));

    hist_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    dv->addWidget(hist_table_);
    dv->addStretch();

    scroll_area_->setVisible(false);
    root_layout->addWidget(scroll_area_, 1);
}

void MemberDetailPanel::set_member(const power_trader::CongressMember& member,
                                    const QVector<power_trader::PoliticalTrade>& trades) {
    placeholder_->setVisible(false);
    scroll_area_->setVisible(true);
    populate(member, trades);
}

void MemberDetailPanel::clear() {
    show_placeholder();
}

void MemberDetailPanel::show_placeholder() {
    placeholder_->setVisible(true);
    scroll_area_->setVisible(false);
}

void MemberDetailPanel::populate(const power_trader::CongressMember& member,
                                  const QVector<power_trader::PoliticalTrade>& trades) {
    // Name
    name_label_->setText(member.full_name);

    // Party badge
    party_badge_->setText(member.party);
    party_badge_->setStyleSheet(
        QString("QLabel { background:%1; color:white; font-size:10px; font-weight:700;"
                " border-radius:3px; }")
            .arg(party_color(member.party)));

    // Meta
    QString meta = power_trader::chamber_label(member.chamber) + QStringLiteral(" · ") + member.state;
    if (!member.district.isEmpty())
        meta += QStringLiteral(" · ") + member.district;
    meta_label_->setText(meta);

    // Stats
    const bool alpha_pos = member.alpha_ytd >= 0;
    alpha_label_->setText(
        QString("%1%2%").arg(alpha_pos ? "+" : "").arg(member.alpha_ytd, 0, 'f', 1));
    alpha_label_->setStyleSheet(
        QString("QLabel { color:%1; font-size:13px; font-family:Consolas,monospace;"
                " font-weight:600; background:transparent; }")
            .arg(alpha_pos ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));

    const bool ret_pos = member.portfolio_return_ytd >= 0;
    return_label_->setText(
        QString("%1%2%").arg(ret_pos ? "+" : "").arg(member.portfolio_return_ytd, 0, 'f', 1));
    return_label_->setStyleSheet(
        QString("QLabel { color:%1; font-size:13px; font-family:Consolas,monospace;"
                " font-weight:600; background:transparent; }")
            .arg(ret_pos ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));

    trades_label_->setText(QString::number(member.trade_count_ytd));

    auto fmt_worth = [](double v) -> QString {
        if (v >= 1'000'000'000)
            return QString("$%1B").arg(v / 1e9, 0, 'f', 1);
        if (v >= 1'000'000)
            return QString("$%1M").arg(v / 1e6, 0, 'f', 1);
        if (v >= 1'000)
            return QString("$%1K").arg(v / 1e3, 0, 'f', 0);
        return QString("$%1").arg(v, 0, 'f', 0);
    };
    net_worth_label_->setText(fmt_worth(member.estimated_net_worth));

    // Committees
    committees_label_->setText(member.committees.isEmpty()
                                    ? QStringLiteral("—")
                                    : member.committees.join(QStringLiteral(", ")));

    // Trade history — last 20, sorted by disclosure date desc
    QVector<power_trader::PoliticalTrade> hist = trades;
    std::stable_sort(hist.begin(), hist.end(),
                     [](const power_trader::PoliticalTrade& a, const power_trader::PoliticalTrade& b) {
                         return a.disclosure_date > b.disclosure_date;
                     });
    if (hist.size() > 20)
        hist = hist.mid(0, 20);

    hist_table_->setRowCount(hist.size());
    for (int r = 0; r < hist.size(); ++r) {
        const auto& t = hist[r];
        hist_table_->setRowHeight(r, 24);

        auto set_item = [&](int col, const QString& text, const char* color = nullptr,
                            Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            hist_table_->setItem(r, col, item);
        };

        set_item(0, t.disclosure_date.toString(QStringLiteral("yyyy-MM-dd")),
                 ui::colors::TEXT_SECONDARY);
        set_item(1, t.ticker);

        const bool is_buy  = t.direction == power_trader::TradeDirection::Buy;
        const bool is_sell = t.direction == power_trader::TradeDirection::Sell;
        const char* dir_color = is_buy  ? ui::colors::POSITIVE
                               : is_sell ? ui::colors::NEGATIVE
                               : ui::colors::TEXT_SECONDARY;
        set_item(2, power_trader::direction_label(t.direction), dir_color, Qt::AlignCenter);

        set_item(3, t.amount_range_label, ui::colors::TEXT_SECONDARY);

        {
            auto* item = new QTableWidgetItem(QString::number(t.disclosure_lag_days));
            item->setTextAlignment(Qt::AlignCenter);
            item->setForeground(
                QColor(t.disclosure_lag_days > 30 ? ui::colors::WARNING() : ui::colors::TEXT_SECONDARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            hist_table_->setItem(r, 4, item);
        }

        {
            auto* item = new QTableWidgetItem(QString::number(t.signal_score, 'f', 0));
            item->setTextAlignment(Qt::AlignCenter);
            item->setForeground(
                QColor(t.signal_score >= 60 ? ui::colors::AMBER() : ui::colors::TEXT_TERTIARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            hist_table_->setItem(r, 5, item);
        }
    }
}

} // namespace fincept::screens

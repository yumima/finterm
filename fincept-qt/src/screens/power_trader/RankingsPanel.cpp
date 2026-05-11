// src/screens/power_trader/RankingsPanel.cpp
#include "screens/power_trader/RankingsPanel.h"

#include "screens/power_trader/PowerTraderService.h"
#include "ui/components/EstTooltip.h"
#include "ui/components/LayoutHelpers.h"
#include "ui/components/SectionHeader.h"
#include "ui/components/SignalTooltip.h"
#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace fincept::screens {

using power_trader::RankingDimension;

// ── Party color constants (semantic political identities, not theme tokens) ───
static constexpr const char* kPartyD = "#3b82f6";
static constexpr const char* kPartyR = "#ef4444";
static constexpr const char* kPartyI = "#eab308";

static const char* party_color(const QString& p) {
    if (p == QLatin1String("D")) return kPartyD;
    if (p == QLatin1String("R")) return kPartyR;
    return kPartyI;
}

// ── Dimension metadata table ──────────────────────────────────────────────────
struct DimInfo {
    RankingDimension dim;
    const char* label;
};

static const DimInfo kDims[] = {
    { RankingDimension::Alpha,         "ALPHA"          },
    { RankingDimension::Return,        "YTD RETURN"     },
    { RankingDimension::NetWorth,      "NET WORTH"      },
    { RankingDimension::NetBuyer,      "NET BUYER"      },
    { RankingDimension::Frequency,     "FREQUENCY"      },
    { RankingDimension::AvgSignal,     "AVG SIGNAL"     },
    { RankingDimension::DisclosureLag, "DISCLOSURE LAG" },
    { RankingDimension::BestPick,      "BEST PICK"      },
};
static constexpr int kDimCount = static_cast<int>(std::size(kDims));

// ── Value formatter ───────────────────────────────────────────────────────────

QString RankingsPanel::format_value(double v, RankingDimension dim) {
    switch (dim) {
        case RankingDimension::Alpha:
        case RankingDimension::Return:
        case RankingDimension::BestPick:
            return QStringLiteral("%1%2%")
                .arg(v >= 0 ? "+" : "")
                .arg(v, 0, 'f', 1);

        case RankingDimension::NetWorth:
        case RankingDimension::NetBuyer: {
            double av = qAbs(v);
            QString prefix = v < 0 ? QStringLiteral("-") : QStringLiteral("");
            if (av >= 1e9) return prefix + QStringLiteral("$%1B").arg(av / 1e9, 0, 'f', 1);
            if (av >= 1e6) return prefix + QStringLiteral("$%1M").arg(av / 1e6, 0, 'f', 1);
            if (av >= 1e3) return prefix + QStringLiteral("$%1K").arg(av / 1e3, 0, 'f', 0);
            return prefix + QStringLiteral("$%1").arg(av, 0, 'f', 0);
        }

        case RankingDimension::Frequency:
            return QString::number(static_cast<int>(v));

        case RankingDimension::AvgSignal:
            return QString::number(v, 'f', 1);

        case RankingDimension::DisclosureLag:
            return QStringLiteral("%1d").arg(static_cast<int>(v));
    }
    return QString::number(v, 'f', 1);
}

// ── Value color for the VALUE column ─────────────────────────────────────────
static const char* value_color(double v, RankingDimension dim) {
    switch (dim) {
        case RankingDimension::Alpha:
        case RankingDimension::Return:
        case RankingDimension::BestPick:
        case RankingDimension::NetBuyer:
            return v >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;

        case RankingDimension::NetWorth:
            return ui::colors::AMBER;

        case RankingDimension::AvgSignal:
            return ui::colors::AMBER;

        case RankingDimension::Frequency:
            return ui::colors::TEXT_PRIMARY;

        case RankingDimension::DisclosureLag:
            // Shorter lag = better; color as warning if very high
            return v > 45 ? ui::colors::WARNING : ui::colors::TEXT_PRIMARY;
    }
    return ui::colors::TEXT_PRIMARY;
}

// ── Bar color ─────────────────────────────────────────────────────────────────
static QColor bar_color(double v, RankingDimension dim) {
    switch (dim) {
        case RankingDimension::Alpha:
        case RankingDimension::Return:
        case RankingDimension::BestPick:
        case RankingDimension::NetBuyer:
            return v >= 0 ? QColor(ui::colors::POSITIVE()) : QColor(ui::colors::NEGATIVE());

        case RankingDimension::NetWorth:
        case RankingDimension::AvgSignal:
            return QColor(ui::colors::AMBER());

        default:
            return QColor(ui::colors::TEXT_SECONDARY());
    }
}

// ── Inline progress bar widget ─────────────────────────────────────────────────
// Used as a cell widget in the BAR column; avoids QProgressBar stylesheet hacks.
class InlineBar : public QWidget {
  public:
    InlineBar(double ratio, const QColor& color, QWidget* parent = nullptr)
        : QWidget(parent), ratio_(qBound(0.0, ratio, 1.0)), color_(color) {
        setFixedHeight(28);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const int w  = width();
        const int h  = height();
        const int bh = 8;
        const int by = (h - bh) / 2;
        const int pad = 6;
        const int bar_w = w - pad * 2;

        // Background track
        QColor bg = color_;
        bg.setAlpha(25);
        p.fillRect(QRect(pad, by, bar_w, bh), bg);

        // Fill
        int fill = static_cast<int>(bar_w * ratio_);
        if (fill > 0)
            p.fillRect(QRect(pad, by, fill, bh), color_);
    }

  private:
    double ratio_;
    QColor color_;
};

// ── Constructor ───────────────────────────────────────────────────────────────

RankingsPanel::RankingsPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

// ── UI construction ───────────────────────────────────────────────────────────

void RankingsPanel::build_ui() {
    setStyleSheet(QString("QWidget { background:%1; color:%2; }")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QHBoxLayout(this);   // horizontal: table LEFT | card RIGHT
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── LEFT: section header + pill bar + table + footer ─────────────────────
    auto* left = new QWidget(this);
    left->setStyleSheet(QString("QWidget{background:%1;border-right:1px solid %2;}")
                            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
    auto* lvl = new QVBoxLayout(left);
    lvl->setContentsMargins(0, 0, 0, 0);
    lvl->setSpacing(0);

    // ── Section header ────────────────────────────────────────────────────────
    lvl->addWidget(fincept::ui::make_section_header(QStringLiteral("RANKINGS"), left));

    // ── Dimension pill bar (horizontally scrollable) ──────────────────────────
    auto* pill_scroll = new QScrollArea(this);
    pill_scroll->setFixedHeight(44);
    pill_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pill_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pill_scroll->setFrameShape(QFrame::NoFrame);
    pill_scroll->setWidgetResizable(false);
    pill_scroll->setStyleSheet(
        QString("QScrollArea { background:%1; border:none; border-bottom:1px solid %2; }"
                "QScrollBar:horizontal { height:0px; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));

    auto* pill_container = new QWidget(pill_scroll);
    pill_container->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_SURFACE()));
    auto* pill_layout = new QHBoxLayout(pill_container);
    pill_layout->setContentsMargins(8, 6, 8, 6);
    pill_layout->setSpacing(6);

    btn_group_ = new QButtonGroup(this);
    btn_group_->setExclusive(true);

    QList<QPushButton*> pill_buttons;
    for (int i = 0; i < kDimCount; ++i) {
        auto* btn = new QPushButton(QLatin1String(kDims[i].label), pill_container);
        btn->setCheckable(true);
        btn->setFixedHeight(28);
        btn->setCursor(Qt::PointingHandCursor);

        const RankingDimension dim = kDims[i].dim;
        connect(btn, &QPushButton::clicked, this, [this, dim]() {
            on_dimension_changed(dim);
        });

        btn_group_->addButton(btn, i);
        pill_layout->addWidget(btn);
        dim_buttons_.append(btn);
        pill_buttons.append(btn);

        if (i == 0)
            btn->setChecked(true);
    }

    // Equal-width pills distributed across the row — uses the longest label's
    // width as the floor so "DISCLOSURE LAG" doesn't truncate.
    QFont pill_font = pill_container->font();
    pill_font.setBold(true);
    fincept::ui::equalize_and_distribute(pill_buttons, pill_font);

    // No addStretch — Expanding policy in the helper absorbs the slack itself.
    pill_scroll->setWidgetResizable(true);
    pill_scroll->setWidget(pill_container);
    lvl->addWidget(pill_scroll);

    // Apply pill styles
    apply_pill_styles();

    // ── Rankings table ────────────────────────────────────────────────────────
    static const QStringList kCols = { "#", "NAME", "PARTY", "VALUE", "BAR" };

    table_ = new QTableWidget(this);
    table_->setColumnCount(kCols.size());
    table_->setHorizontalHeaderLabels(kCols);
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
    h->setSectionResizeMode(0, QHeaderView::Fixed);    // RANK
    h->resizeSection(0, 30);
    h->setSectionResizeMode(1, QHeaderView::Interactive); h->resizeSection(1, 180); h->setStretchLastSection(true);
    h->setSectionResizeMode(2, QHeaderView::Fixed);    // PARTY
    h->resizeSection(2, 28);
    h->setSectionResizeMode(3, QHeaderView::Fixed);    // VALUE
    h->resizeSection(3, 100);
    h->setSectionResizeMode(4, QHeaderView::Fixed);    // BAR
    h->resizeSection(4, 160);

    table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:12px; font-family:Consolas,monospace;"
                "  gridline-color:transparent; }"
                "QTableWidget::item { padding:0px 4px; border-bottom:1px solid %3; }"
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
        if (row < 0 || row >= table_->rowCount()) return;
        auto* item = table_->item(row, 1);
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty()) {
            emit member_selected(id);
            populate_detail_card(id);
        }
    });

    lvl->addWidget(table_, 1);

    footer_label_ = new QLabel(
        QStringLiteral("Estimated from public STOCK Act disclosures."), left);
    footer_label_->setWordWrap(true);
    footer_label_->setStyleSheet(
        QString("QLabel { color:%1; font-size:12px; padding:5px 10px;"
                " border-top:1px solid %2; background:%3; }")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED(), ui::colors::BG_SURFACE()));
    lvl->addWidget(footer_label_);

    root->addWidget(left, 1);

    // ── RIGHT: member detail card (narrow, taller than wide = correct) ────────
    auto* card = new QWidget(this);
    card->setFixedWidth(240);
    card->setStyleSheet(
        QString("QWidget{background:%1;}").arg(ui::colors::BG_SURFACE()));
    build_detail_card(card, nullptr);
    root->addWidget(card);
}

void RankingsPanel::build_detail_card(QWidget* card, QVBoxLayout*) {
    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    vl->addWidget(fincept::ui::make_section_header(QStringLiteral("MEMBER STATS"), card));

    card_name_ = new QLabel(QStringLiteral("Select a member"), card);
    card_name_->setWordWrap(true);
    card_name_->setStyleSheet(
        QString("color:%1;font-size:13px;font-weight:700;padding:10px 12px 4px 12px;"
                "background:transparent;")
            .arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(card_name_);

    card_meta_ = new QLabel(card);
    card_meta_->setWordWrap(true);
    card_meta_->setStyleSheet(
        QString("color:%1;font-size:12px;padding:0 12px 8px 12px;"
                "border-bottom:1px solid %2;background:transparent;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM()));
    vl->addWidget(card_meta_);

    // Stat rows — each narrow enough that height > width per row
    const QString row_ss =
        QString("QWidget{background:transparent;border-bottom:1px solid %1;}")
            .arg(ui::colors::BORDER_DIM());
    const QString lbl_ss =
        QString("color:%1;font-size:12px;background:transparent;")
            .arg(ui::colors::TEXT_SECONDARY());
    const QString val_ss =
        QString("color:%1;font-size:13px;font-weight:700;"
                "font-family:Consolas,monospace;background:transparent;")
            .arg(ui::colors::TEXT_PRIMARY());

    auto add_row = [&](const QString& label, QLabel*& out,
                       const QString& est_tip = QString()) {
        auto* row = new QWidget(card);
        row->setStyleSheet(row_ss);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(12, 7, 12, 7);
        hl->setSpacing(4);
        const QString shown = est_tip.isEmpty() ? label : fincept::ui::est::with_marker(label);
        auto* lbl = new QLabel(shown, row);
        lbl->setStyleSheet(lbl_ss);
        lbl->setTextFormat(Qt::RichText);
        if (!est_tip.isEmpty())
            lbl->setToolTip(est_tip);
        hl->addWidget(lbl);
        hl->addStretch();
        out = new QLabel(QStringLiteral("—"), row);
        out->setStyleSheet(val_ss);
        out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (!est_tip.isEmpty())
            out->setToolTip(est_tip);
        hl->addWidget(out);
        vl->addWidget(row);
    };

    add_row(QStringLiteral("Alpha vs SPY"),  card_alpha_,  fincept::ui::est::alpha_tooltip());
    add_row(QStringLiteral("YTD Return"),    card_return_, fincept::ui::est::return_tooltip());
    add_row(QStringLiteral("Avg Signal"),    card_signal_);
    add_row(QStringLiteral("Avg Lag"),       card_lag_);
    add_row(QStringLiteral("Trades YTD"),    card_trades_);
    add_row(QStringLiteral("Net Worth"),     card_nw_,     fincept::ui::est::net_worth_tooltip());

    auto* cmte_hdr = new QLabel(QStringLiteral("COMMITTEES"), card);
    cmte_hdr->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;padding:8px 12px 4px 12px;"
                "background:transparent;border-top:1px solid %2;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED()));
    vl->addWidget(cmte_hdr);

    card_cmtes_ = new QLabel(card);
    card_cmtes_->setWordWrap(true);
    card_cmtes_->setStyleSheet(
        QString("color:%1;font-size:12px;padding:0 12px 10px 12px;background:transparent;")
            .arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(card_cmtes_);
    vl->addStretch();
}

void RankingsPanel::populate_detail_card(const QString& member_id) {
    if (!has_data_) return;
    const auto& svc = power_trader::PowerTraderService::instance();

    power_trader::CongressMember member;
    for (const auto& m : summary_.members)
        if (m.id == member_id) { member = m; break; }
    if (member.id.isEmpty()) return;

    card_name_->setText(member.full_name);
    card_meta_->setText(
        QString("%1  ·  %2  ·  %3")
            .arg(member.party == QStringLiteral("D") ? "Democrat" :
                 member.party == QStringLiteral("R") ? "Republican" : "Independent")
            .arg(member.chamber == power_trader::MemberChamber::Senate ? "Senate" : "House")
            .arg(member.state));

    auto fmt_pct = [](double v) {
        return (v >= 0 ? "+" : "") + QString::number(v, 'f', 1) + "%";
    };
    card_alpha_ ->setText(fmt_pct(member.alpha_ytd));
    card_return_->setText(fmt_pct(member.portfolio_return_ytd));
    card_signal_->setText(QString::number(svc.avg_signal_score(member_id), 'f', 0) + "/100");
    card_signal_->setToolTip(fincept::ui::tooltip_for_aggregate_signal(
        svc.trades_for_member(member_id).size()));
    card_lag_   ->setText(QString::number(svc.avg_disclosure_lag(member_id), 'f', 0) + "d");
    card_trades_->setText(QString::number(member.trade_count_ytd));

    const double nw = member.estimated_net_worth;
    card_nw_->setText(nw >= 1e9 ? "$"+QString::number(nw/1e9,'f',1)+"B" :
                      nw >= 1e6 ? "$"+QString::number(nw/1e6,'f',1)+"M" : "—");

    const auto cmtes = member.committees;
    card_cmtes_->setText(cmtes.isEmpty() ? "—" : cmtes.join("\n"));
}

// ── apply_pill_styles ─────────────────────────────────────────────────────────

void RankingsPanel::apply_pill_styles() {
    for (auto* btn : dim_buttons_) {
        if (btn->isChecked()) {
            btn->setStyleSheet(
                QString("QPushButton {"
                        "  background:%1; color:%2; border:none;"
                        "  border-radius:4px; padding:0 12px;"
                        "  font-size:12px; font-weight:700; letter-spacing:0.5px; }"
                        "QPushButton:hover { background:%3; }")
                    .arg(ui::colors::AMBER(), ui::colors::BG_BASE(),
                         ui::colors::AMBER()));
        } else {
            btn->setStyleSheet(
                QString("QPushButton {"
                        "  background:transparent; color:%1; border:1px solid %2;"
                        "  border-radius:4px; padding:0 12px;"
                        "  font-size:12px; font-weight:600; letter-spacing:0.5px; }"
                        "QPushButton:hover { background:%3; color:%4; border-color:%4; }")
                    .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                         ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY()));
        }
    }
}

// ── set_data ──────────────────────────────────────────────────────────────────

void RankingsPanel::set_data(const power_trader::PowerTraderSummary& summary) {
    summary_ = summary;
    has_data_ = summary.loaded;
    on_dimension_changed(current_dim_);
    // Auto-populate detail card with top-ranked member
    if (has_data_ && !summary.members.isEmpty()) {
        const auto ranked = power_trader::PowerTraderService::instance()
                                .ranked_members(current_dim_);
        if (!ranked.isEmpty())
            populate_detail_card(ranked.first().member.id);
    }
}

void RankingsPanel::refresh_theme() {
    apply_pill_styles();
    if (has_data_)
        on_dimension_changed(current_dim_);
}

// ── on_dimension_changed ──────────────────────────────────────────────────────

void RankingsPanel::on_dimension_changed(RankingDimension dim) {
    current_dim_ = dim;

    // Update button checked state and re-style
    for (int i = 0; i < kDimCount; ++i) {
        const bool active = (kDims[i].dim == dim);
        dim_buttons_[i]->blockSignals(true);
        dim_buttons_[i]->setChecked(active);
        dim_buttons_[i]->blockSignals(false);
    }
    apply_pill_styles();

    // Fetch ranked members from service
    const auto ranked = power_trader::PowerTraderService::instance().ranked_members(dim);
    populate_table(ranked);

    // Update footer
    footer_label_->setText(
        QStringLiteral("Showing %1 members ranked by %2. "
                        "Data: estimated from public STOCK Act disclosures.")
            .arg(ranked.size())
            .arg(power_trader::ranking_label(dim)));
}

// ── populate_table ────────────────────────────────────────────────────────────

void RankingsPanel::populate_table(const QVector<power_trader::RankedMember>& ranked) {
    // Clamp to 20 rows
    const int count = qMin(ranked.size(), 20);

    table_->setRowCount(count);

    // Find max abs value for bar scaling
    double max_abs = 0.0;
    for (int i = 0; i < count; ++i)
        max_abs = qMax(max_abs, qAbs(ranked[i].rank_value));
    if (max_abs < 1e-9) max_abs = 1.0;

    for (int r = 0; r < count; ++r) {
        const auto& rm = ranked[r];
        table_->setRowHeight(r, 28);

        // ── RANK (col 0) ──────────────────────────────────────────────────────
        auto* rank_item = new QTableWidgetItem(QString::number(rm.rank));
        rank_item->setTextAlignment(Qt::AlignCenter);
        // Top 3 = amber, rest = tertiary
        rank_item->setForeground(
            QColor(rm.rank <= 3 ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY()));
        QFont rf = rank_item->font();
        rf.setBold(true);
        rank_item->setFont(rf);
        rank_item->setFlags(rank_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(r, 0, rank_item);

        // ── NAME (col 1) — store member_id in UserRole ────────────────────────
        auto* name_item = new QTableWidgetItem(rm.member.full_name);
        name_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        name_item->setForeground(QColor(ui::colors::CYAN()));
        name_item->setData(Qt::UserRole, rm.member.id);
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(r, 1, name_item);

        // ── PARTY (col 2) — colored pill ─────────────────────────────────────
        auto* party_item = new QTableWidgetItem(rm.member.party);
        party_item->setTextAlignment(Qt::AlignCenter);
        party_item->setForeground(QColor(party_color(rm.member.party)));
        QFont pf = party_item->font();
        pf.setBold(true);
        party_item->setFont(pf);
        party_item->setFlags(party_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(r, 2, party_item);

        // ── VALUE (col 3) ─────────────────────────────────────────────────────
        const QString val_str = format_value(rm.rank_value, current_dim_);
        auto* val_item = new QTableWidgetItem(val_str);
        val_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val_item->setForeground(QColor(value_color(rm.rank_value, current_dim_)));
        QFont vf = val_item->font();
        vf.setBold(true);
        val_item->setFont(vf);
        val_item->setFlags(val_item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(r, 3, val_item);

        // ── BAR (col 4) — custom InlineBar cell widget ────────────────────────
        const double ratio = qAbs(rm.rank_value) / max_abs;
        const QColor bc    = bar_color(rm.rank_value, current_dim_);
        auto* bar          = new InlineBar(ratio, bc, table_->viewport());
        table_->setCellWidget(r, 4, bar);
    }
}

} // namespace fincept::screens

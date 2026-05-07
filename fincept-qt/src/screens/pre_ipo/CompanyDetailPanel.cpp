// src/screens/pre_ipo/CompanyDetailPanel.cpp
#include "screens/pre_ipo/CompanyDetailPanel.h"

#include "ui/theme/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

// ── Constructor ───────────────────────────────────────────────────────────────

CompanyDetailPanel::CompanyDetailPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

// ── Public API ────────────────────────────────────────────────────────────────

void CompanyDetailPanel::set_company(const PrivateCompany& company) {
    populate(company);
    stack_->setCurrentWidget(detail_scroll_);
}

void CompanyDetailPanel::clear() {
    stack_->setCurrentWidget(placeholder_);
}

// ── Build UI ─────────────────────────────────────────────────────────────────

void CompanyDetailPanel::build_ui() {
    setObjectName("companyDetailPanel");
    setStyleSheet(
        QString("#companyDetailPanel { background:%1; }").arg(colors::BG_BASE()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    stack_ = new QStackedWidget;

    placeholder_ = build_placeholder();
    stack_->addWidget(placeholder_);

    detail_view_   = build_detail_view();
    detail_scroll_ = new QScrollArea;
    detail_scroll_->setWidgetResizable(true);
    detail_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detail_scroll_->setStyleSheet(
        QString("QScrollArea { border:none; background:%1; }"
                "QScrollBar:vertical { width:5px; background:transparent; }"
                "QScrollBar::handle:vertical { background:%2; border-radius:2px; min-height:20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
            .arg(colors::BG_BASE(), colors::BORDER_MED()));
    detail_scroll_->setWidget(detail_view_);
    stack_->addWidget(detail_scroll_);

    root->addWidget(stack_, 1);
    stack_->setCurrentWidget(placeholder_);
}

QWidget* CompanyDetailPanel::build_placeholder() {
    auto* w = new QWidget;
    w->setStyleSheet(
        QString("background:%1;").arg(colors::BG_BASE()));
    auto* vl = new QVBoxLayout(w);
    vl->setAlignment(Qt::AlignCenter);
    auto* lbl = new QLabel("Select a company to view details");
    lbl->setStyleSheet(
        QString("color:%1; font-size:13px;").arg(colors::TEXT_SECONDARY()));
    lbl->setAlignment(Qt::AlignCenter);
    vl->addWidget(lbl);
    return w;
}

QWidget* CompanyDetailPanel::build_detail_view() {
    auto* view = new QWidget;
    view->setStyleSheet(
        QString("background:%1;").arg(colors::BG_BASE()));
    auto* vl = new QVBoxLayout(view);
    vl->setContentsMargins(20, 16, 20, 20);
    vl->setSpacing(0);

    // ── Header ────────────────────────────────────────────────────────────────
    auto* header = new QWidget;
    header->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2; padding-bottom:12px;")
            .arg(colors::BG_BASE(), colors::BORDER_DIM()));
    auto* hdr_layout = new QVBoxLayout(header);
    hdr_layout->setContentsMargins(0, 0, 0, 12);
    hdr_layout->setSpacing(4);

    name_lbl_ = new QLabel;
    name_lbl_->setStyleSheet(
        QString("color:%1; font-size:22px; font-weight:700; background:transparent;")
            .arg(colors::AMBER()));

    sector_lbl_ = new QLabel;
    sector_lbl_->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(colors::TEXT_SECONDARY()));

    meta_lbl_ = new QLabel;
    meta_lbl_->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(colors::TEXT_SECONDARY()));

    hdr_layout->addWidget(name_lbl_);
    hdr_layout->addWidget(sector_lbl_);
    hdr_layout->addWidget(meta_lbl_);
    vl->addWidget(header);
    vl->addSpacing(12);

    // ── Key metrics tiles row ─────────────────────────────────────────────────
    auto* metrics_row = new QHBoxLayout;
    metrics_row->setSpacing(8);
    metrics_row->setContentsMargins(0, 0, 0, 0);

    tile_val_   = make_metric_tile("LAST VALUATION", "—", colors::AMBER());
    tile_round_ = make_metric_tile("LAST ROUND",     "—");
    tile_rev_   = make_metric_tile("REVENUE EST.",   "—");
    tile_emp_   = make_metric_tile("EMPLOYEES",      "—");

    metrics_row->addWidget(tile_val_,   1);
    metrics_row->addWidget(tile_round_, 1);
    metrics_row->addWidget(tile_rev_,   1);
    metrics_row->addWidget(tile_emp_,   1);
    vl->addLayout(metrics_row);
    vl->addSpacing(14);

    // ── Divider ───────────────────────────────────────────────────────────────
    auto make_divider = [&]() -> QWidget* {
        auto* d = new QWidget;
        d->setFixedHeight(1);
        d->setStyleSheet(QString("background:%1;").arg(colors::BORDER_DIM()));
        return d;
    };

    // ── IPO Status section ────────────────────────────────────────────────────
    auto* ipo_section = new QWidget;
    ipo_section->setStyleSheet(
        QString("background:%1; border:1px solid %2; border-radius:4px;")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
    auto* ipo_layout = new QVBoxLayout(ipo_section);
    ipo_layout->setContentsMargins(12, 10, 12, 10);
    ipo_layout->setSpacing(6);

    auto* ipo_header = new QLabel("IPO STATUS");
    ipo_header->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px; background:transparent;")
            .arg(colors::TEXT_SECONDARY()));
    ipo_layout->addWidget(ipo_header);

    auto* ipo_row = new QHBoxLayout;
    ipo_row->setSpacing(10);

    status_badge_ = new QLabel;
    status_badge_->setStyleSheet("font-size:12px; font-weight:700; border-radius:4px; padding:3px 10px;");

    window_lbl_ = new QLabel;
    window_lbl_->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(colors::TEXT_PRIMARY()));

    s1_date_lbl_ = new QLabel;
    s1_date_lbl_->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(colors::TEXT_SECONDARY()));

    ipo_row->addWidget(status_badge_);
    ipo_row->addWidget(window_lbl_);
    ipo_row->addStretch();
    ipo_row->addWidget(s1_date_lbl_);
    ipo_layout->addLayout(ipo_row);

    vl->addWidget(ipo_section);
    vl->addSpacing(16);

    // ── Funding rounds table ──────────────────────────────────────────────────
    auto* rounds_header_lbl = new QLabel("FUNDING ROUNDS");
    rounds_header_lbl->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;")
            .arg(colors::TEXT_SECONDARY()));
    vl->addWidget(rounds_header_lbl);
    vl->addSpacing(6);

    rounds_table_ = new QTableWidget;
    rounds_table_->setColumnCount(4);
    rounds_table_->setHorizontalHeaderLabels({"Date", "Round", "Amount", "Lead Investors"});
    rounds_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rounds_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    rounds_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rounds_table_->setShowGrid(false);
    rounds_table_->setAlternatingRowColors(false);
    rounds_table_->verticalHeader()->setVisible(false);
    rounds_table_->setFocusPolicy(Qt::NoFocus);
    rounds_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rounds_table_->setMinimumHeight(120);

    auto* hdr = rounds_table_->horizontalHeader();
    hdr->setStretchLastSection(true);
    hdr->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(3, QHeaderView::Interactive); hdr->resizeSection(3, 180);

    rounds_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:1px solid %3;"
                "  font-size:12px; gridline-color:transparent; }"
                "QTableWidget::item { padding:4px 8px; border-bottom:1px solid %3; }"
                "QTableWidget::item:selected { background:rgba(217,119,6,0.15); color:%2; }"
                "QScrollBar:vertical { width:4px; background:%1; }"
                "QScrollBar::handle:vertical { background:%3; }")
            .arg(colors::BG_SURFACE(), colors::TEXT_PRIMARY(), colors::BORDER_DIM()));

    hdr->setStyleSheet(
        QString("QHeaderView::section { background:%1; color:%2; border:none;"
                "  border-bottom:2px solid %3; padding:4px 8px; font-size:12px; font-weight:700; }")
            .arg(colors::BG_RAISED(), colors::TEXT_PRIMARY(), colors::AMBER()));

    vl->addWidget(rounds_table_);
    vl->addSpacing(16);
    vl->addWidget(make_divider());
    vl->addSpacing(12);

    // ── Key Investors ─────────────────────────────────────────────────────────
    auto* inv_header = new QLabel("KEY INVESTORS");
    inv_header->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;")
            .arg(colors::TEXT_SECONDARY()));
    vl->addWidget(inv_header);
    vl->addSpacing(6);

    investors_container_ = new QWidget;
    investors_layout_ = new QVBoxLayout(investors_container_);
    investors_layout_->setContentsMargins(0, 0, 0, 0);
    investors_layout_->setSpacing(3);
    vl->addWidget(investors_container_);
    vl->addSpacing(14);
    vl->addWidget(make_divider());
    vl->addSpacing(12);

    // ── Public Comparables ────────────────────────────────────────────────────
    auto* comps_header = new QLabel("PUBLIC COMPARABLES");
    comps_header->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;")
            .arg(colors::TEXT_SECONDARY()));
    vl->addWidget(comps_header);
    vl->addSpacing(6);

    comps_container_ = new QWidget;
    comps_layout_ = new QHBoxLayout(comps_container_);
    comps_layout_->setContentsMargins(0, 0, 0, 0);
    comps_layout_->setSpacing(6);
    comps_layout_->addStretch();
    vl->addWidget(comps_container_);
    vl->addSpacing(14);
    vl->addWidget(make_divider());
    vl->addSpacing(12);

    // ── Tags ──────────────────────────────────────────────────────────────────
    auto* tags_header = new QLabel("TAGS");
    tags_header->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;")
            .arg(colors::TEXT_SECONDARY()));
    vl->addWidget(tags_header);
    vl->addSpacing(6);

    tags_container_ = new QWidget;
    tags_layout_ = new QHBoxLayout(tags_container_);
    tags_layout_->setContentsMargins(0, 0, 0, 0);
    tags_layout_->setSpacing(6);
    tags_layout_->addStretch();
    vl->addWidget(tags_container_);
    vl->addSpacing(14);
    vl->addWidget(make_divider());
    vl->addSpacing(12);

    // ── Description ───────────────────────────────────────────────────────────
    auto* desc_header = new QLabel("ABOUT");
    desc_header->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;")
            .arg(colors::TEXT_SECONDARY()));
    vl->addWidget(desc_header);
    vl->addSpacing(6);

    desc_lbl_ = new QLabel;
    desc_lbl_->setWordWrap(true);
    desc_lbl_->setStyleSheet(
        QString("color:%1; font-size:12px; line-height:1.5; background:transparent;")
            .arg(colors::TEXT_PRIMARY()));
    vl->addWidget(desc_lbl_);

    vl->addStretch();
    return view;
}

// ── Populate ──────────────────────────────────────────────────────────────────

void CompanyDetailPanel::populate(const PrivateCompany& c) {
    name_lbl_->setText(c.name);
    sector_lbl_->setText(c.sector + (c.sub_sector.isEmpty() ? "" : " · " + c.sub_sector));

    QString meta;
    if (c.founded.isValid())
        meta += "Founded " + QString::number(c.founded.year());
    if (!c.hq_city.isEmpty()) {
        if (!meta.isEmpty()) meta += "  ·  ";
        meta += c.hq_city + ", " + c.hq_country;
    }
    meta_lbl_->setText(meta);

    // ── Update metric tiles ───────────────────────────────────────────────────
    auto update_tile = [](QWidget* tile, const QString& val) {
        if (!tile) return;
        // The value label is the second child (index 1) in the tile's VBoxLayout
        auto* vl = qobject_cast<QVBoxLayout*>(tile->layout());
        if (!vl || vl->count() < 2) return;
        if (auto* lbl = qobject_cast<QLabel*>(vl->itemAt(1)->widget()))
            lbl->setText(val);
    };

    const QString val_str = c.last_valuation_usd > 0
        ? QString("$%1B").arg(c.last_valuation_usd, 0, 'f', c.last_valuation_usd >= 10 ? 0 : 1)
        : "—";
    update_tile(tile_val_, val_str);

    const QString round_str = c.last_round_name.isEmpty() ? "—"
        : c.last_round_name + (c.last_round_date.isValid()
            ? "\n" + c.last_round_date.toString("MMM yyyy") : "");
    update_tile(tile_round_, round_str);

    const QString rev_str = c.revenue_est_usd > 0
        ? "$" + QString::number(qRound(c.revenue_est_usd)) + "M est."
        : "—";
    update_tile(tile_rev_, rev_str);

    const QString emp_str = c.employee_count > 0
        ? QString::number(c.employee_count) + "+"
        : "—";
    update_tile(tile_emp_, emp_str);

    // ── IPO status ────────────────────────────────────────────────────────────
    const QString sl = ipo_status_label(c.ipo_status);
    QString badge_bg, badge_fg;
    switch (c.ipo_status) {
        case IpoStatus::Filed:
            badge_bg = "rgba(217,119,6,0.2)"; badge_fg = colors::AMBER(); break;
        case IpoStatus::Rumored:
            badge_bg = "rgba(107,114,128,0.2)"; badge_fg = colors::TEXT_TERTIARY(); break;
        case IpoStatus::Priced:
        case IpoStatus::Listed:
            badge_bg = "rgba(22,163,74,0.2)"; badge_fg = colors::POSITIVE(); break;
        case IpoStatus::Acquired:
            badge_bg = "rgba(139,92,246,0.2)"; badge_fg = "#a78bfa"; break;
        default:
            badge_bg = "rgba(107,114,128,0.1)"; badge_fg = colors::TEXT_TERTIARY(); break;
    }
    status_badge_->setText(sl);
    status_badge_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; background:%2;"
                "  border-radius:4px; padding:3px 10px;")
            .arg(badge_fg, badge_bg));

    window_lbl_->setText(c.ipo_expected_window.isEmpty() ? "No announced plans"
                                                          : c.ipo_expected_window);

    if (c.s1_filed_date.isValid()) {
        s1_date_lbl_->setText("S-1 filed: " + c.s1_filed_date.toString("MMM d, yyyy"));
        s1_date_lbl_->setVisible(true);
    } else {
        s1_date_lbl_->setVisible(false);
    }

    // ── Funding rounds table ──────────────────────────────────────────────────
    rebuild_rounds_table(c.rounds);

    // ── Investors ─────────────────────────────────────────────────────────────
    rebuild_investors(c.key_investors);

    // ── Comps ─────────────────────────────────────────────────────────────────
    rebuild_comps_chips(c.public_comps);

    // ── Tags ──────────────────────────────────────────────────────────────────
    rebuild_tags(c.tags);

    // ── Description ──────────────────────────────────────────────────────────
    desc_lbl_->setText(c.description);
}

// ── Rebuild helpers ──────────────────────────────────────────────────────────

void CompanyDetailPanel::rebuild_rounds_table(const QVector<FundingRound>& rounds) {
    rounds_table_->setRowCount(0);
    rounds_table_->setRowCount(rounds.size());

    for (int i = 0; i < rounds.size(); ++i) {
        const auto& r = rounds[i];

        auto make_item = [](const QString& text) {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return it;
        };

        rounds_table_->setItem(i, 0, make_item(r.date.isValid() ? r.date.toString("MMM yyyy") : "—"));
        rounds_table_->setItem(i, 1, make_item(r.round_name));

        const QString amt = r.amount_usd > 0
            ? "$" + QString::number(qRound(r.amount_usd)) + "M"
            : "—";
        rounds_table_->setItem(i, 2, make_item(amt));
        rounds_table_->setItem(i, 3, make_item(r.lead_investors.join(", ")));
    }

    // Adjust height based on row count
    const int rows = rounds.size();
    const int row_h = 28;
    const int header_h = 30;
    rounds_table_->setFixedHeight(qMin(200, header_h + rows * row_h + 2));
}

void CompanyDetailPanel::rebuild_investors(const QStringList& investors) {
    while (QLayoutItem* item = investors_layout_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (const auto& inv : investors) {
        auto* lbl = new QLabel("• " + inv);
        lbl->setStyleSheet(
            QString("color:%1; font-size:12px; background:transparent;")
                .arg(colors::TEXT_PRIMARY()));
        investors_layout_->addWidget(lbl);
    }
}

void CompanyDetailPanel::rebuild_comps_chips(const QStringList& tickers) {
    while (QLayoutItem* item = comps_layout_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (const auto& ticker : tickers) {
        auto* btn = new QPushButton(ticker);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { background:%1; color:%2; border:1px solid %3;"
                    "  border-radius:3px; padding:3px 10px; font-size:12px; font-weight:600; }"
                    "QPushButton:hover { background:%4; border-color:%5; color:%6; }")
                .arg(colors::BG_SURFACE(), colors::CYAN(), colors::BORDER_DIM(),
                     colors::BG_RAISED(), colors::CYAN(), colors::TEXT_PRIMARY()));
        const QString t = ticker;
        connect(btn, &QPushButton::clicked, this, [this, t]() {
            emit navigate_to_markets(t);
        });
        comps_layout_->addWidget(btn);
    }
    comps_layout_->addStretch();
}

void CompanyDetailPanel::rebuild_tags(const QStringList& tags) {
    while (QLayoutItem* item = tags_layout_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (const auto& tag : tags) {
        auto* lbl = new QLabel(tag);
        lbl->setStyleSheet(
            QString("color:%1; font-size:12px; background:rgba(107,114,128,0.2);"
                    "  border-radius:3px; padding:2px 8px;")
                .arg(colors::TEXT_SECONDARY()));
        tags_layout_->addWidget(lbl);
    }
    tags_layout_->addStretch();
}

// ── Metric tile ───────────────────────────────────────────────────────────────

QWidget* CompanyDetailPanel::make_metric_tile(const QString& label, const QString& value,
                                              const QString& color) const {
    auto* tile = new QWidget;
    tile->setStyleSheet(
        QString("background:%1; border:1px solid %2; border-radius:4px;")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
    tile->setFixedHeight(60);

    auto* vl = new QVBoxLayout(tile);
    vl->setContentsMargins(10, 8, 10, 8);
    vl->setSpacing(3);

    auto* lbl = new QLabel(label);
    lbl->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:0.5px; background:transparent;")
            .arg(colors::TEXT_SECONDARY()));

    auto* val_lbl = new QLabel(value);
    const QString val_color = color.isEmpty() ? colors::TEXT_PRIMARY() : color;
    val_lbl->setStyleSheet(
        QString("color:%1; font-size:13px; font-weight:700; background:transparent;")
            .arg(val_color));
    val_lbl->setWordWrap(true);

    vl->addWidget(lbl);
    vl->addWidget(val_lbl);
    return tile;
}

} // namespace fincept::screens

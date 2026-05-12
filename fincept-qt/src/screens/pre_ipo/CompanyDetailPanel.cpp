// src/screens/pre_ipo/CompanyDetailPanel.cpp
#include "screens/pre_ipo/CompanyDetailPanel.h"

#include "ui/theme/Theme.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSet>

#include <algorithm>
#include <cmath>

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
    view->setStyleSheet(QString("background:%1;").arg(colors::BG_BASE()));
    auto* vl = new QVBoxLayout(view);
    vl->setContentsMargins(16, 12, 16, 16);
    vl->setSpacing(0);

    // ── Header: name + status badge + sector ──────────────────────────────────
    {
        auto* h = new QWidget;
        h->setStyleSheet(
            QString("background:%1;border-bottom:1px solid %2;")
                .arg(colors::BG_BASE(), colors::BORDER_DIM()));
        auto* hl = new QVBoxLayout(h);
        hl->setContentsMargins(0, 0, 0, 10);
        hl->setSpacing(3);

        auto* row1 = new QHBoxLayout;
        name_lbl_ = new QLabel;
        name_lbl_->setStyleSheet(
            QString("color:%1;font-size:20px;font-weight:700;background:transparent;")
                .arg(colors::AMBER()));
        row1->addWidget(name_lbl_);
        // Note: status_badge_ is created in the IPO STATUS section below —
        // do NOT construct it here to avoid a double-construction leak.
        row1->addStretch();
        hl->addLayout(row1);

        sector_lbl_ = new QLabel;
        sector_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
        hl->addWidget(sector_lbl_);

        meta_lbl_ = new QLabel;  // founded · HQ · IPO window
        meta_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
        hl->addWidget(meta_lbl_);

        vl->addWidget(h);
        vl->addSpacing(8);
    }

    // ── Key metrics: compact 2-column label:value (no tile borders) ──────────
    {
        const QString sep =
            QString("QWidget{background:transparent;border-bottom:1px solid %1;}")
                .arg(colors::BORDER_DIM());
        const QString lbl_ss =
            QString("color:%1;font-size:12px;font-weight:600;background:transparent;")
                .arg(colors::TEXT_SECONDARY());
        const QString val_amber =
            QString("color:%1;font-size:13px;font-weight:700;"
                    "font-family:Consolas,monospace;background:transparent;")
                .arg(colors::AMBER());
        const QString val_white =
            QString("color:%1;font-size:13px;font-weight:700;"
                    "font-family:Consolas,monospace;background:transparent;")
                .arg(colors::TEXT_PRIMARY());

        // 2 rows × 2 cols — no boxes, just spaced label:value
        auto make_kv = [&](const QString& label, QLabel*& out,
                           bool amber = false) -> QWidget* {
            auto* row = new QWidget;
            row->setStyleSheet(sep);
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 6, 0, 6);
            rl->setSpacing(8);
            auto* ll = new QLabel(label, row);
            ll->setStyleSheet(lbl_ss);
            rl->addWidget(ll);
            rl->addStretch();
            out = new QLabel(QStringLiteral("—"), row);
            out->setStyleSheet(amber ? val_amber : val_white);
            out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rl->addWidget(out);
            return row;
        };

        auto* g = new QGridLayout;
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(0);
        g->setColumnStretch(0, 1);
        g->setColumnStretch(1, 1);
        g->setHorizontalSpacing(16);

        g->addWidget(make_kv("Valuation",    val_lbl_,   true), 0, 0);
        g->addWidget(make_kv("Last Round",   round_lbl_),       0, 1);
        g->addWidget(make_kv("Revenue Est.", rev_lbl_),         1, 0);
        g->addWidget(make_kv("Employees",    emp_lbl_),         1, 1);

        auto* gw = new QWidget;
        gw->setStyleSheet("background:transparent;");
        gw->setLayout(g);
        vl->addWidget(gw);
        vl->addSpacing(8);
    }

    // ── IPO status + S-1 (inline, no box border) ─────────────────────────────
    {
        auto* row = new QWidget;
        row->setStyleSheet(
            QString("QWidget{background:%1;border-radius:4px;}")
                .arg(colors::BG_SURFACE()));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(10, 6, 10, 6);
        rl->setSpacing(10);

        auto* ipo_lbl = new QLabel("IPO STATUS", row);
        ipo_lbl->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                .arg(colors::TEXT_SECONDARY()));
        rl->addWidget(ipo_lbl);

        status_badge_ = new QLabel(row);
        status_badge_->setStyleSheet("font-size:12px;font-weight:700;border-radius:3px;padding:2px 8px;");
        rl->addWidget(status_badge_);

        window_lbl_ = new QLabel(row);
        window_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_PRIMARY()));
        rl->addWidget(window_lbl_);

        rl->addStretch();

        s1_date_lbl_ = new QLabel(row);
        s1_date_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
        rl->addWidget(s1_date_lbl_);

        vl->addWidget(row);
        vl->addSpacing(10);
    }

    // ── Share price section ───────────────────────────────────────────────────
    {
        price_section_ = new QWidget;
        price_section_->setStyleSheet(
            QString("QWidget{background:%1;border-radius:4px;border:1px solid %2;}")
                .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
        auto* ps = new QVBoxLayout(price_section_);
        ps->setContentsMargins(10, 8, 10, 8);
        ps->setSpacing(4);

        auto* ph = new QLabel("SHARE PRICE  ·  Private Market Estimates", price_section_);
        ph->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                .arg(colors::TEXT_SECONDARY()));
        ps->addWidget(ph);

        const QString price_val_ss =
            QString("color:%1;font-size:13px;font-weight:700;"
                    "font-family:Consolas,monospace;background:transparent;")
                .arg(colors::POSITIVE());
        const QString lbl2_ss =
            QString("color:%1;font-size:12px;background:transparent;")
                .arg(colors::TEXT_SECONDARY());

        auto make_price_row = [&](const QString& label, QLabel*& out) {
            auto* row = new QHBoxLayout;
            auto* ll = new QLabel(label, price_section_);
            ll->setStyleSheet(lbl2_ss);
            row->addWidget(ll);
            row->addStretch();
            out = new QLabel("—", price_section_);
            out->setStyleSheet(price_val_ss);
            row->addWidget(out);
            ps->addLayout(row);
        };

        make_price_row("Secondary Market:", sec_price_lbl_);
        make_price_row("Implied (last round):", implied_price_lbl_);
        make_price_row("Form D implied:", formd_price_lbl_);

        auto* delta_row = new QHBoxLayout;
        auto* dl = new QLabel("Δ vs last round:", price_section_);
        dl->setStyleSheet(lbl2_ss);
        delta_row->addWidget(dl);
        delta_row->addStretch();
        price_delta_lbl_ = new QLabel("—", price_section_);
        price_delta_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                .arg(colors::TEXT_SECONDARY()));
        delta_row->addWidget(price_delta_lbl_);
        ps->addLayout(delta_row);

        vl->addWidget(price_section_);
        vl->addSpacing(10);
    }

    // ── Funding rounds table (no outer border, with $/share column) ───────────
    {
        auto* rh = new QLabel("FUNDING ROUNDS", view);
        rh->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;letter-spacing:1px;"
                    "padding:4px 0;")
                .arg(colors::TEXT_SECONDARY()));
        vl->addWidget(rh);
        vl->addSpacing(4);

        rounds_table_ = new QTableWidget;
        rounds_table_->setColumnCount(5);
        rounds_table_->setHorizontalHeaderLabels(
            {"Date", "Round", "Amount", "$/Share", "Lead Investors"});
        rounds_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        rounds_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        rounds_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        rounds_table_->setShowGrid(false);
        rounds_table_->setAlternatingRowColors(false);
        rounds_table_->verticalHeader()->setVisible(false);
        rounds_table_->setFocusPolicy(Qt::NoFocus);
        rounds_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* hdr = rounds_table_->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Stretch);  // equal distribution
        hdr->setSectionResizeMode(0, QHeaderView::Fixed);  hdr->resizeSection(0, 80);  // Date
        hdr->setSectionResizeMode(1, QHeaderView::Fixed);  hdr->resizeSection(1, 80);  // Round
        hdr->setSectionResizeMode(2, QHeaderView::Fixed);  hdr->resizeSection(2, 72);  // Amount
        hdr->setSectionResizeMode(3, QHeaderView::Fixed);  hdr->resizeSection(3, 72);  // $/Share

        rounds_table_->setStyleSheet(
            QString("QTableWidget{background:%1;color:%2;border:none;"
                    "  font-size:12px;font-family:Consolas,monospace;"
                    "  gridline-color:transparent;}"
                    "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %3;}"
                    "QTableWidget::item:selected{background:rgba(217,119,6,0.15);color:%2;}"
                    "QScrollBar:vertical{width:4px;background:%1;}"
                    "QScrollBar::handle:vertical{background:%3;}")
                .arg(colors::BG_BASE(), colors::TEXT_PRIMARY(), colors::BORDER_DIM()));

        hdr->setStyleSheet(
            QString("QHeaderView::section{background:%1;color:%2;border:none;"
                    "  border-bottom:2px solid %3;padding:4px 8px;"
                    "  font-size:12px;font-weight:700;}")
                .arg(colors::BG_SURFACE(), colors::TEXT_PRIMARY(), colors::AMBER()));

        vl->addWidget(rounds_table_);
        vl->addSpacing(12);
    }

    auto make_divider = [&]() -> QWidget* {
        auto* d = new QWidget;
        d->setFixedHeight(1);
        d->setStyleSheet(QString("background:%1;").arg(colors::BORDER_DIM()));
        return d;
    };
    Q_UNUSED(make_divider);

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

    // ── ANALYTICS / KPI band ──────────────────────────────────────────────────
    {
        kpi_section_ = new QWidget;
        kpi_section_->setStyleSheet(
            QString("QWidget{background:%1;border-radius:4px;border:1px solid %2;}")
                .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
        auto* kg = new QGridLayout(kpi_section_);
        kg->setContentsMargins(10, 8, 10, 8);
        kg->setHorizontalSpacing(18);
        kg->setVerticalSpacing(4);

        auto add_kpi = [&](int col, const QString& label, QLabel*& out, const QString& color) {
            auto* l = new QLabel(label, kpi_section_);
            l->setStyleSheet(
                QString("color:%1;font-size:12px;font-weight:700;letter-spacing:1px;background:transparent;")
                    .arg(colors::TEXT_SECONDARY()));
            kg->addWidget(l, 0, col);
            out = new QLabel(QStringLiteral("—"), kpi_section_);
            out->setStyleSheet(
                QString("color:%1;font-size:14px;font-weight:700;font-family:Consolas,monospace;background:transparent;")
                    .arg(color));
            kg->addWidget(out, 1, col);
        };

        add_kpi(0, "READINESS",     kpi_readiness_lbl_, colors::AMBER());
        add_kpi(1, "MARK DRIFT",    kpi_drift_lbl_,     colors::POSITIVE());
        add_kpi(2, "HIIVE PREM.",   kpi_premium_lbl_,   colors::CYAN());
        add_kpi(3, "DAYS→PRICE",    kpi_days_lbl_,      colors::TEXT_PRIMARY());
        add_kpi(4, "CUM. RAISED",   kpi_raised_lbl_,    colors::AMBER());

        vl->addWidget(kpi_section_);
        vl->addSpacing(10);
    }

    // ── FUND MARKS section ───────────────────────────────────────────────────
    {
        marks_section_ = new QWidget;
        marks_section_->setStyleSheet(
            QString("QWidget{background:%1;border-radius:4px;border:1px solid %2;}")
                .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
        auto* mvl = new QVBoxLayout(marks_section_);
        mvl->setContentsMargins(10, 8, 10, 8);
        mvl->setSpacing(4);

        auto* hdr = new QLabel("MUTUAL FUND MARKS  ·  Consensus from N-PORT-P filings", marks_section_);
        hdr->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                .arg(colors::TEXT_SECONDARY()));
        mvl->addWidget(hdr);

        auto* row = new QHBoxLayout;
        row->setSpacing(16);
        auto make_metric = [&](const QString& label, QLabel*& out, const QString& color) {
            auto* l = new QLabel(label, marks_section_);
            l->setStyleSheet(
                QString("color:%1;font-size:12px;background:transparent;")
                    .arg(colors::TEXT_SECONDARY()));
            row->addWidget(l);
            out = new QLabel("—", marks_section_);
            out->setStyleSheet(
                QString("color:%1;font-size:12px;font-weight:700;font-family:Consolas,monospace;background:transparent;")
                    .arg(color));
            row->addWidget(out);
        };
        make_metric("Consensus:",  consensus_lbl_,   colors::AMBER());
        make_metric("Dispersion:", dispersion_lbl_,  colors::TEXT_PRIMARY());
        make_metric("Funds:",      smart_money_lbl_, colors::CYAN());
        row->addStretch();
        mvl->addLayout(row);

        marks_table_ = new QTableWidget(marks_section_);
        marks_table_->setColumnCount(4);
        marks_table_->setHorizontalHeaderLabels({"Fund", "As of", "Shares", "Mark $/sh"});
        marks_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        marks_table_->setSelectionMode(QAbstractItemView::NoSelection);
        marks_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        marks_table_->setShowGrid(false);
        marks_table_->setAlternatingRowColors(false);
        marks_table_->verticalHeader()->setVisible(false);
        marks_table_->setFocusPolicy(Qt::NoFocus);
        marks_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        marks_table_->horizontalHeader()->setStretchLastSection(false);
        marks_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        marks_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        marks_table_->horizontalHeader()->resizeSection(1, 80);
        marks_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        marks_table_->horizontalHeader()->resizeSection(2, 90);
        marks_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        marks_table_->horizontalHeader()->resizeSection(3, 90);
        marks_table_->setStyleSheet(
            QString("QTableWidget{background:transparent;color:%1;border:none;"
                    "  font-size:12px;font-family:Consolas,monospace;}"
                    "QTableWidget::item{padding:3px 6px;border-bottom:1px solid %2;}")
                .arg(colors::TEXT_PRIMARY(), colors::BORDER_DIM()));
        marks_table_->horizontalHeader()->setStyleSheet(
            QString("QHeaderView::section{background:%1;color:%2;border:none;"
                    "  border-bottom:1px solid %3;padding:3px 6px;font-size:12px;font-weight:700;}")
                .arg(colors::BG_RAISED(), colors::TEXT_SECONDARY(), colors::AMBER()));
        mvl->addWidget(marks_table_);

        vl->addWidget(marks_section_);
        vl->addSpacing(10);
    }

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
    // Show city if known; fall back to state (Form D only provides state)
    if (!c.hq_city.isEmpty()) {
        if (!meta.isEmpty()) meta += "  ·  ";
        meta += c.hq_city;
        if (!c.hq_state.isEmpty())
            meta += ", " + c.hq_state;
        else if (!c.hq_country.isEmpty())
            meta += ", " + c.hq_country;
    } else if (!c.hq_state.isEmpty()) {
        if (!meta.isEmpty()) meta += "  ·  ";
        meta += c.hq_state;
    }
    if (!c.ipo_expected_window.isEmpty()) {
        if (!meta.isEmpty()) meta += "  ·  ";
        meta += "IPO: " + c.ipo_expected_window;
    }
    meta_lbl_->setText(meta);

    // ── Key metrics (direct label updates) ───────────────────────────────────
    if (val_lbl_)
        val_lbl_->setText(c.last_valuation_usd > 0
            ? QString("$%1B").arg(c.last_valuation_usd, 0, 'f',
                                  c.last_valuation_usd >= 10 ? 0 : 1)
            : QStringLiteral("—"));

    if (round_lbl_)
        round_lbl_->setText(c.last_round_name.isEmpty() ? "—"
            : c.last_round_name + (c.last_round_date.isValid()
                ? "  " + c.last_round_date.toString("MMM yyyy") : ""));

    if (rev_lbl_)
        rev_lbl_->setText(c.revenue_est_usd > 0
            ? "$" + QString::number(qRound(c.revenue_est_usd)) + "M est."
            : QStringLiteral("—"));

    if (emp_lbl_)
        emp_lbl_->setText(c.employee_count_est > 0
            ? QString::number(c.employee_count_est) + "+"
            : QStringLiteral("—"));

    // ── IPO status ────────────────────────────────────────────────────────────
    const QString sl = ipo_status_label(c.ipo_status);
    QString badge_bg, badge_fg;
    switch (c.ipo_status) {
        case IpoStatus::Filed:
            badge_bg = "rgba(217,119,6,0.2)"; badge_fg = colors::AMBER(); break;
        case IpoStatus::Rumored:
            badge_bg = "rgba(107,114,128,0.2)"; badge_fg = colors::TEXT_SECONDARY(); break;
        case IpoStatus::Priced:
        case IpoStatus::Listed:
            badge_bg = "rgba(22,163,74,0.2)"; badge_fg = colors::POSITIVE(); break;
        case IpoStatus::Acquired:
            badge_bg = "rgba(139,92,246,0.2)"; badge_fg = "#a78bfa"; break;
        default:
            badge_bg = "rgba(107,114,128,0.1)"; badge_fg = colors::TEXT_SECONDARY(); break;
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

    // ── Share price ───────────────────────────────────────────────────────────
    if (price_section_) {
        // Show price section if we have a secondary/Hiive quote, fund-mark consensus,
        // or any legacy implied price.
        const bool has_price = c.secondary_market_price > 0
                            || c.implied_share_price > 0
                            || c.form_d_implied_price > 0
                            || c.analytics.consensus_mark_pps > 0;
        price_section_->setVisible(has_price);

        auto fmt_price = [](double p) -> QString {
            return p > 0 ? QString("$%1").arg(p, 0, 'f', 2) : QStringLiteral("—");
        };

        if (sec_price_lbl_) {
            const QString src = c.secondary_market_source.isEmpty()
                ? "" : "  (" + c.secondary_market_source + ")";
            const QString dt  = c.secondary_market_date.isValid()
                ? "  " + c.secondary_market_date.toString("MMM yyyy") : "";
            sec_price_lbl_->setText(fmt_price(c.secondary_market_price) + src + dt);
        }
        if (implied_price_lbl_) {
            // Prefer consensus mark from fund N-PORTs when available; fall back to legacy implied.
            const double v = c.analytics.consensus_mark_pps > 0
                ? c.analytics.consensus_mark_pps : c.implied_share_price;
            implied_price_lbl_->setText(fmt_price(v));
        }
        if (formd_price_lbl_)
            formd_price_lbl_->setText(fmt_price(c.form_d_implied_price));

        // Delta: secondary vs latest fund-mark consensus (replaces old PPS lookup).
        if (price_delta_lbl_) {
            const double base_price = c.analytics.consensus_mark_pps;
            if (base_price > 0 && c.secondary_market_price > 0) {
                const double delta = (c.secondary_market_price - base_price) / base_price * 100.0;
                const QString sign = delta >= 0 ? "+" : "";
                price_delta_lbl_->setText(sign + QString::number(delta, 'f', 1) + "% vs consensus mark");
                price_delta_lbl_->setStyleSheet(
                    QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                        .arg(delta >= 0
                             ? colors::POSITIVE() : colors::NEGATIVE()));
            } else {
                price_delta_lbl_->setText("—");
            }
        }
    }

    // ── Funding rounds table ──────────────────────────────────────────────────
    rebuild_rounds_table(c.rounds);

    // ── Investors ─────────────────────────────────────────────────────────────
    QStringList investor_names = c.key_investors;
    if (investor_names.isEmpty()) {
        QSet<QString> seen;
        for (const auto& r : c.rounds) {
            for (const auto& rp : r.related_persons) {
                if (seen.contains(rp.name)) continue;
                seen.insert(rp.name);
                investor_names << rp.name;
                if (investor_names.size() >= 10) break;
            }
            if (investor_names.size() >= 10) break;
        }
    }
    rebuild_investors(investor_names);

    // ── Comps ─────────────────────────────────────────────────────────────────
    rebuild_comps_chips(c.public_comps);

    // ── Tags ──────────────────────────────────────────────────────────────────
    rebuild_tags(c.tags);

    // ── Description ──────────────────────────────────────────────────────────
    desc_lbl_->setText(c.description);

    // ── Fund marks & analytics ───────────────────────────────────────────────
    rebuild_fund_marks(c.fund_marks);
    rebuild_analytics(c);
}

// ── Rebuild helpers ──────────────────────────────────────────────────────────

void CompanyDetailPanel::rebuild_rounds_table(const QVector<PrimaryRound>& rounds) {
    rounds_table_->setRowCount(0);
    rounds_table_->setRowCount(rounds.size());

    for (int i = 0; i < rounds.size(); ++i) {
        const auto& r = rounds[i];

        auto make_item = [](const QString& text,
                            Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter,
                            const QString& color = {}) {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            it->setTextAlignment(align);
            if (!color.isEmpty()) it->setForeground(QColor(color));
            return it;
        };

        rounds_table_->setItem(i, 0,
            make_item(r.filed_date.isValid() ? r.filed_date.toString("MMM yyyy") : "—"));

        const QString round_label = !r.round_name_inferred.isEmpty()
            ? r.round_name_inferred
            : (r.exemption.isEmpty() ? "Form D" : "Form D · " + r.exemption);
        rounds_table_->setItem(i, 1,
            make_item(round_label, Qt::AlignLeft | Qt::AlignVCenter, colors::AMBER()));

        const QString amt = r.amount_sold_m > 0
            ? "$" + QString::number(qRound(r.amount_sold_m)) + "M" : "—";
        rounds_table_->setItem(i, 2,
            make_item(amt, Qt::AlignRight | Qt::AlignVCenter));

        // Form D doesn't disclose PPS; we keep the column for future enrichment.
        rounds_table_->setItem(i, 3,
            make_item("—", Qt::AlignRight | Qt::AlignVCenter));

        QStringList persons;
        for (const auto& rp : r.related_persons) persons << rp.name;
        rounds_table_->setItem(i, 4, make_item(persons.join(", ")));
    }

    // Adjust height based on row count
    const int rows = rounds.size();
    const int row_h = 28;
    const int header_h = 30;
    rounds_table_->setFixedHeight(qMin(200, header_h + rows * row_h + 2));
}

// ── Fund-mark consensus + per-fund rows ──────────────────────────────────────

void CompanyDetailPanel::rebuild_fund_marks(const QVector<FundMark>& marks) {
    if (!marks_section_) return;
    if (marks.isEmpty()) {
        marks_section_->setVisible(false);
        return;
    }
    marks_section_->setVisible(true);

    // Aggregate latest period (within 120 days of newest as_of).
    QDate newest;
    for (const auto& m : marks)
        if (newest.isNull() || m.as_of > newest) newest = m.as_of;
    double sum_w = 0, sum_wx = 0;
    QVector<double> recent_pps;
    for (const auto& m : marks) {
        if (m.mark_pps <= 0) continue;
        if (newest.isValid() && m.as_of.daysTo(newest) > 120) continue;
        const double w = std::max(m.shares_held, 1.0);
        sum_w  += w;
        sum_wx += w * m.mark_pps;
        recent_pps.append(m.mark_pps);
    }
    const double consensus = (sum_w > 0) ? sum_wx / sum_w : 0;
    double dispersion = 0;
    if (recent_pps.size() >= 2 && consensus > 0) {
        double var = 0;
        for (double v : recent_pps) { const double d = v - consensus; var += d * d; }
        dispersion = std::sqrt(var / recent_pps.size()) / consensus * 100.0;
    }
    consensus_lbl_->setText(consensus > 0 ? QString("$%1").arg(consensus, 0, 'f', 2) : "—");
    dispersion_lbl_->setText(dispersion > 0 ? QString("±%1%").arg(dispersion, 0, 'f', 1) : "—");
    smart_money_lbl_->setText(QString::number(recent_pps.size()));

    // Per-fund rows (newest as_of first).
    QVector<FundMark> sorted = marks;
    std::sort(sorted.begin(), sorted.end(),
              [](const FundMark& a, const FundMark& b) { return a.as_of > b.as_of; });
    marks_table_->setRowCount(sorted.size());
    for (int i = 0; i < sorted.size(); ++i) {
        const auto& m = sorted[i];
        auto cell = [](const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(Qt::ItemIsEnabled);
            it->setTextAlignment(align);
            return it;
        };
        marks_table_->setItem(i, 0, cell(m.fund_name));
        marks_table_->setItem(i, 1, cell(m.as_of.isValid() ? m.as_of.toString("yyyy-MM") : "—",
                                         Qt::AlignCenter));
        marks_table_->setItem(i, 2, cell(
            m.shares_held > 0 ? QString::number(qRound(m.shares_held))
                              : "—",
            Qt::AlignRight | Qt::AlignVCenter));
        marks_table_->setItem(i, 3, cell(
            m.mark_pps > 0 ? QString("$%1").arg(m.mark_pps, 0, 'f', 2) : "—",
            Qt::AlignRight | Qt::AlignVCenter));
    }
    const int row_h = 24;
    const int header_h = 26;
    marks_table_->setFixedHeight(qMin(200, header_h + sorted.size() * row_h + 4));
}

// ── Analytics KPI band ───────────────────────────────────────────────────────

void CompanyDetailPanel::rebuild_analytics(const PrivateCompany& c) {
    if (!kpi_section_) return;
    const auto& a = c.analytics;
    if (kpi_readiness_lbl_)
        kpi_readiness_lbl_->setText(QString::number(a.ipo_readiness_score) + "/100");
    if (kpi_drift_lbl_) {
        if (a.mark_drift_vs_last_round_pct != 0) {
            const double v = a.mark_drift_vs_last_round_pct;
            kpi_drift_lbl_->setText((v >= 0 ? "+" : "") + QString::number(v, 'f', 1) + "%");
            kpi_drift_lbl_->setStyleSheet(
                QString("color:%1;font-size:14px;font-weight:700;font-family:Consolas,monospace;background:transparent;")
                    .arg(v >= 0 ? colors::POSITIVE() : colors::NEGATIVE()));
        } else {
            kpi_drift_lbl_->setText("—");
        }
    }
    if (kpi_premium_lbl_)
        kpi_premium_lbl_->setText(a.hiive_premium_pct != 0
            ? QString::number(a.hiive_premium_pct, 'f', 1) + "%" : "—");
    if (kpi_days_lbl_)
        kpi_days_lbl_->setText(a.days_to_price_est > 0 ? QString("~%1d").arg(a.days_to_price_est) : "—");
    if (kpi_raised_lbl_)
        kpi_raised_lbl_->setText(c.cumulative_raised_m > 0
            ? "$" + QString::number(qRound(c.cumulative_raised_m)) + "M" : "—");
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

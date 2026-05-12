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
    stack_->setCurrentWidget(detail_view_);
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

    // detail_view_ is added directly — no outer QScrollArea. Cards size
    // themselves to content (with internal scroll on data-heavy tiles), so
    // a populated dossier fits the pane vertically without a page-level
    // scrollbar.
    detail_view_ = build_detail_view();
    stack_->addWidget(detail_view_);

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
    vl->setContentsMargins(8, 6, 8, 6);
    vl->setSpacing(0);

    // Reusable thin-scrollbar styling for the internal scroll wrappers
    // (investors, description). Page-level scroll is gone — these are the
    // only scrollbars in the panel, and they're 4px ghost bars.
    const QString thin_scroll_ss =
        QString("QScrollArea{border:none;background:transparent;}"
                "QScrollBar:vertical{width:4px;background:transparent;}"
                "QScrollBar::handle:vertical{background:%1;border-radius:2px;min-height:20px;}"
                "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}")
            .arg(colors::BORDER_MED());

    const QString section_header_ss =
        QString("color:%1;font-size:12px;font-weight:700;letter-spacing:1px;")
            .arg(colors::TEXT_SECONDARY());
    const QString surface_card_ss =
        QString("QWidget#detailCard{background:%1;border-radius:4px;border:1px solid %2;}")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM());

    auto make_card = [&]() -> QWidget* {
        auto* w = new QWidget;
        w->setObjectName("detailCard");
        w->setStyleSheet(surface_card_ss);
        return w;
    };

    auto make_card_title = [&](QWidget* parent, const QString& title) -> QLabel* {
        auto* l = new QLabel(title, parent);
        l->setStyleSheet(section_header_ss + QString("background:transparent;"));
        return l;
    };

    // ── Header: name + sector + meta (full width) ──────────────────────────
    {
        auto* h = new QWidget;
        h->setStyleSheet(
            QString("background:%1;border-bottom:1px solid %2;")
                .arg(colors::BG_BASE(), colors::BORDER_DIM()));
        auto* hl = new QVBoxLayout(h);
        hl->setContentsMargins(0, 0, 0, 6);
        hl->setSpacing(2);

        name_lbl_ = new QLabel;
        name_lbl_->setStyleSheet(
            QString("color:%1;font-size:18px;font-weight:700;background:transparent;")
                .arg(colors::AMBER()));
        hl->addWidget(name_lbl_);

        sector_lbl_ = new QLabel;
        sector_lbl_->setStyleSheet(
            QString("color:%1;font-size:11px;background:transparent;").arg(colors::TEXT_SECONDARY()));
        hl->addWidget(sector_lbl_);

        meta_lbl_ = new QLabel;
        meta_lbl_->setStyleSheet(
            QString("color:%1;font-size:11px;background:transparent;").arg(colors::TEXT_SECONDARY()));
        hl->addWidget(meta_lbl_);

        vl->addWidget(h);
        vl->addSpacing(6);
    }

    // ── Section builders ────────────────────────────────────────────────────
    // Each returns a self-contained card; layout below composes them into
    // 3-col (top band) + 2-col (mid bands) + full-width (bottom).

    // KEY FACTS — 2×2 label:value grid inside a card.
    auto build_facts_card = [&]() -> QWidget* {
        auto* card = make_card();
        auto* outer = new QVBoxLayout(card);
        outer->setContentsMargins(8, 6, 8, 6);
        outer->setSpacing(4);
        outer->addWidget(make_card_title(card, "KEY FACTS"));

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

        auto make_kv = [&](const QString& initial_label,
                           QLabel*& label_out, QLabel*& value_out,
                           bool amber = false) -> QWidget* {
            auto* row = new QWidget;
            row->setStyleSheet(sep);
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 3, 0, 3);
            rl->setSpacing(6);
            label_out = new QLabel(initial_label, row);
            label_out->setStyleSheet(lbl_ss);
            rl->addWidget(label_out);
            rl->addStretch();
            value_out = new QLabel(QStringLiteral("—"), row);
            value_out->setStyleSheet(amber ? val_amber : val_white);
            value_out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rl->addWidget(value_out);
            return row;
        };

        auto* g = new QGridLayout;
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(0);
        g->setColumnStretch(0, 1);
        g->setColumnStretch(1, 1);
        g->setHorizontalSpacing(14);

        g->addWidget(make_kv("Valuation",    val_lbl_label_,   val_lbl_,   true), 0, 0);
        g->addWidget(make_kv("Last Round",   round_lbl_label_, round_lbl_),       0, 1);
        g->addWidget(make_kv("Revenue Est.", rev_lbl_label_,   rev_lbl_),         1, 0);
        g->addWidget(make_kv("Employees",    emp_lbl_label_,   emp_lbl_),         1, 1);
        outer->addLayout(g);
        outer->addStretch();
        return card;
    };

    // IPO STATUS — stacked: badge / window / S-1 date.
    auto build_ipo_card = [&]() -> QWidget* {
        auto* card = make_card();
        auto* il = new QVBoxLayout(card);
        il->setContentsMargins(8, 6, 8, 6);
        il->setSpacing(6);
        il->addWidget(make_card_title(card, "IPO STATUS"));

        status_badge_ = new QLabel(card);
        status_badge_->setStyleSheet("font-size:12px;font-weight:700;border-radius:3px;padding:3px 10px;");
        status_badge_->setAlignment(Qt::AlignCenter);
        // Wrap badge in a row with addStretch so it shrinks to content width.
        auto* badge_row = new QHBoxLayout;
        badge_row->setContentsMargins(0, 0, 0, 0);
        badge_row->addWidget(status_badge_);
        badge_row->addStretch();
        il->addLayout(badge_row);

        window_lbl_ = new QLabel(card);
        window_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_PRIMARY()));
        window_lbl_->setWordWrap(true);
        il->addWidget(window_lbl_);

        s1_date_lbl_ = new QLabel(card);
        s1_date_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
        s1_date_lbl_->setWordWrap(true);
        il->addWidget(s1_date_lbl_);

        il->addStretch();
        return card;
    };

    // SHARE PRICE — label/value rows.
    auto build_price_card = [&]() -> QWidget* {
        price_section_ = make_card();
        auto* ps = new QVBoxLayout(price_section_);
        ps->setContentsMargins(8, 6, 8, 6);
        ps->setSpacing(4);
        ps->addWidget(make_card_title(price_section_, "SHARE PRICE"));

        const QString price_val_ss =
            QString("color:%1;font-size:13px;font-weight:700;"
                    "font-family:Consolas,monospace;background:transparent;")
                .arg(colors::POSITIVE());
        const QString lbl2_ss =
            QString("color:%1;font-size:12px;background:transparent;")
                .arg(colors::TEXT_SECONDARY());

        auto make_price_row = [&](const QString& label, QLabel*& out) {
            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            auto* ll = new QLabel(label, price_section_);
            ll->setStyleSheet(lbl2_ss);
            row->addWidget(ll);
            row->addStretch();
            out = new QLabel("—", price_section_);
            out->setStyleSheet(price_val_ss);
            row->addWidget(out);
            ps->addLayout(row);
        };

        make_price_row("Secondary:", sec_price_lbl_);
        make_price_row("Implied:",   implied_price_lbl_);
        make_price_row("Form D:",    formd_price_lbl_);

        auto* delta_row = new QHBoxLayout;
        delta_row->setContentsMargins(0, 0, 0, 0);
        auto* dl = new QLabel("Δ vs mark:", price_section_);
        dl->setStyleSheet(lbl2_ss);
        delta_row->addWidget(dl);
        delta_row->addStretch();
        price_delta_lbl_ = new QLabel("—", price_section_);
        price_delta_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                .arg(colors::TEXT_SECONDARY()));
        delta_row->addWidget(price_delta_lbl_);
        ps->addLayout(delta_row);
        ps->addStretch();
        return price_section_;
    };

    // FUNDING ROUNDS — table inside a card.
    auto build_rounds_card = [&]() -> QWidget* {
        auto* card = make_card();
        auto* outer = new QVBoxLayout(card);
        outer->setContentsMargins(8, 6, 8, 6);
        outer->setSpacing(4);
        outer->addWidget(make_card_title(card, "FUNDING ROUNDS"));

        rounds_table_ = new QTableWidget(card);
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
        // Table sizes to its actual row count via setFixedHeight in
        // rebuild_rounds_table() — no inflation, no blank rows below data.
        // Internal vertical scrollbar appears only when row count would
        // exceed the cap (rebuild_rounds_table chooses the cap).
        rounds_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        rounds_table_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        auto* hdr = rounds_table_->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Stretch);
        hdr->setSectionResizeMode(0, QHeaderView::Fixed); hdr->resizeSection(0, 76);
        hdr->setSectionResizeMode(1, QHeaderView::Fixed); hdr->resizeSection(1, 76);
        hdr->setSectionResizeMode(2, QHeaderView::Fixed); hdr->resizeSection(2, 68);
        hdr->setSectionResizeMode(3, QHeaderView::Fixed); hdr->resizeSection(3, 68);

        rounds_table_->setStyleSheet(
            QString("QTableWidget{background:transparent;color:%1;border:none;"
                    "  font-size:12px;font-family:Consolas,monospace;"
                    "  gridline-color:transparent;}"
                    "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %2;}"
                    "QTableWidget::item:selected{background:rgba(217,119,6,0.15);color:%1;}"
                    "QScrollBar:vertical{width:4px;background:transparent;}"
                    "QScrollBar::handle:vertical{background:%2;}")
                .arg(colors::TEXT_PRIMARY(), colors::BORDER_DIM()));
        hdr->setStyleSheet(
            QString("QHeaderView::section{background:%1;color:%2;border:none;"
                    "  border-bottom:2px solid %3;padding:4px 8px;"
                    "  font-size:12px;font-weight:700;}")
                .arg(colors::BG_RAISED(), colors::TEXT_PRIMARY(), colors::AMBER()));
        outer->addWidget(rounds_table_);
        return card;
    };

    // KEY INVESTORS — bullet list inside a card. Internal scroll on the list
    // so the card height stays bounded — a 30-investor company doesn't push
    // the rest of the dossier off-screen.
    auto build_investors_card = [&]() -> QWidget* {
        investors_card_ = make_card();
        auto* outer = new QVBoxLayout(investors_card_);
        outer->setContentsMargins(8, 6, 8, 6);
        outer->setSpacing(4);
        outer->addWidget(make_card_title(investors_card_, "KEY INVESTORS"));

        investors_container_ = new QWidget;
        investors_container_->setStyleSheet("background:transparent;");
        investors_layout_ = new QVBoxLayout(investors_container_);
        investors_layout_->setContentsMargins(0, 0, 0, 0);
        investors_layout_->setSpacing(3);

        investors_scroll_ = new QScrollArea(investors_card_);
        investors_scroll_->setWidget(investors_container_);
        investors_scroll_->setWidgetResizable(true);
        investors_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        investors_scroll_->setStyleSheet(thin_scroll_ss);
        investors_scroll_->setFrameShape(QFrame::NoFrame);
        // Scroll height is set in rebuild_investors() once the actual name
        // count is known — see there for the cap.
        investors_scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        outer->addWidget(investors_scroll_);
        return investors_card_;
    };

    // ANALYTICS KPI band — 3×2 grid inside a card so it stacks neatly in
    // a 2-column row (was 1×5 originally; 5-wide doesn't fit half-width).
    auto build_analytics_card = [&]() -> QWidget* {
        kpi_section_ = make_card();
        auto* outer = new QVBoxLayout(kpi_section_);
        outer->setContentsMargins(8, 6, 8, 6);
        outer->setSpacing(4);
        outer->addWidget(make_card_title(kpi_section_, "ANALYTICS"));

        auto* kg = new QGridLayout;
        kg->setContentsMargins(0, 0, 0, 0);
        kg->setHorizontalSpacing(14);
        kg->setVerticalSpacing(4);

        auto add_kpi = [&](int row, int col, const QString& label,
                           QLabel*& out, const QString& color) {
            auto* l = new QLabel(label, kpi_section_);
            l->setStyleSheet(
                QString("color:%1;font-size:11px;font-weight:700;letter-spacing:1px;background:transparent;")
                    .arg(colors::TEXT_SECONDARY()));
            kg->addWidget(l, row * 2, col);
            out = new QLabel(QStringLiteral("—"), kpi_section_);
            out->setStyleSheet(
                QString("color:%1;font-size:14px;font-weight:700;"
                        "font-family:Consolas,monospace;background:transparent;")
                    .arg(color));
            kg->addWidget(out, row * 2 + 1, col);
        };

        // 3 columns × 2 rows so the band stays readable at half width.
        add_kpi(0, 0, "READINESS",   kpi_readiness_lbl_, colors::AMBER());
        add_kpi(0, 1, "MARK DRIFT",  kpi_drift_lbl_,     colors::POSITIVE());
        add_kpi(0, 2, "HIIVE PREM.", kpi_premium_lbl_,   colors::CYAN());
        add_kpi(1, 0, "DAYS→PRICE",  kpi_days_lbl_,      colors::TEXT_PRIMARY());
        add_kpi(1, 1, "CUM. RAISED", kpi_raised_lbl_,    colors::AMBER());
        outer->addLayout(kg);
        outer->addStretch();
        return kpi_section_;
    };

    // FUND MARKS — consensus row + table inside a card.
    auto build_marks_card = [&]() -> QWidget* {
        marks_section_ = make_card();
        auto* mvl = new QVBoxLayout(marks_section_);
        mvl->setContentsMargins(8, 6, 8, 6);
        mvl->setSpacing(4);
        mvl->addWidget(make_card_title(marks_section_, "FUND MARKS"));

        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(12);
        auto make_metric = [&](const QString& label, QLabel*& out, const QString& color) {
            auto* l = new QLabel(label, marks_section_);
            l->setStyleSheet(
                QString("color:%1;font-size:11px;background:transparent;")
                    .arg(colors::TEXT_SECONDARY()));
            row->addWidget(l);
            out = new QLabel("—", marks_section_);
            out->setStyleSheet(
                QString("color:%1;font-size:12px;font-weight:700;"
                        "font-family:Consolas,monospace;background:transparent;")
                    .arg(color));
            row->addWidget(out);
        };
        make_metric("Cons:",  consensus_lbl_,   colors::AMBER());
        make_metric("Disp:",  dispersion_lbl_,  colors::TEXT_PRIMARY());
        make_metric("Funds:", smart_money_lbl_, colors::CYAN());
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
        marks_table_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        auto* mhdr = marks_table_->horizontalHeader();
        mhdr->setStretchLastSection(false);
        mhdr->setSectionResizeMode(0, QHeaderView::Stretch);
        mhdr->setSectionResizeMode(1, QHeaderView::Fixed); mhdr->resizeSection(1, 70);
        mhdr->setSectionResizeMode(2, QHeaderView::Fixed); mhdr->resizeSection(2, 80);
        mhdr->setSectionResizeMode(3, QHeaderView::Fixed); mhdr->resizeSection(3, 80);
        marks_table_->setStyleSheet(
            QString("QTableWidget{background:transparent;color:%1;border:none;"
                    "  font-size:12px;font-family:Consolas,monospace;}"
                    "QTableWidget::item{padding:3px 6px;border-bottom:1px solid %2;}")
                .arg(colors::TEXT_PRIMARY(), colors::BORDER_DIM()));
        mhdr->setStyleSheet(
            QString("QHeaderView::section{background:%1;color:%2;border:none;"
                    "  border-bottom:1px solid %3;padding:3px 6px;"
                    "  font-size:12px;font-weight:700;}")
                .arg(colors::BG_RAISED(), colors::TEXT_SECONDARY(), colors::AMBER()));
        mvl->addWidget(marks_table_);
        return marks_section_;
    };

    // Layout strategy — each section hugs its content (no inflation):
    //   header        — natural height
    //   top band      — natural height (KEY FACTS / IPO STATUS / SHARE PRICE)
    //   mid 1         — natural height (max of ROUNDS, INVESTORS card heights)
    //   mid 2         — natural height (max of ANALYTICS, FUND MARKS heights)
    //   tail sections — natural height (COMPS / TAGS / ABOUT; each hides empty)
    //   vl->addStretch() at the very end — leftover pane vertical sinks to
    //     the bottom instead of inflating cards.
    //
    // Data-heavy tiles (rounds_table, marks_table, investors scroll,
    // description scroll) each set their own setFixedHeight / setMaximumHeight
    // = min(cap, row_count × row_h + header_pad). They are *exactly* as tall
    // as their data, capped, with an internal scrollbar that activates only
    // when the cap is exceeded. A sparse company (1 investor, 0 rounds) lands
    // at ~480px total; a maxed-out company (20 rounds + 12 investors + 8
    // marks) lands at ~720px with three internal scrollbars.
    auto add_row = [&](std::initializer_list<std::pair<QWidget*, int>> cards) {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        for (const auto& [w, s] : cards)
            row->addWidget(w, s);
        vl->addLayout(row);
        vl->addSpacing(4);
    };

    // ── 3-column top band: KEY FACTS · IPO STATUS · SHARE PRICE ────────────
    add_row({{build_facts_card(), 2}, {build_ipo_card(), 1}, {build_price_card(), 1}});
    // ── 2-column mid 1: FUNDING ROUNDS · KEY INVESTORS ─────────────────────
    add_row({{build_rounds_card(), 3}, {build_investors_card(), 2}});
    // ── 2-column mid 2: ANALYTICS · FUND MARKS ─────────────────────────────
    add_row({{build_analytics_card(), 2}, {build_marks_card(), 3}});

    // ── Full-width tail: COMPS · TAGS · ABOUT ──────────────────────────────
    // Each section lives in a wrapper widget (*_section_) so it can be hidden
    // wholesale when the company has no data — keeps sparse companies tight
    // instead of showing three label-only stubs.
    auto make_chip_section = [&](const QString& title, QWidget*& section_out,
                                 QWidget*& container_out, QHBoxLayout*& layout_out) {
        section_out = new QWidget;
        section_out->setStyleSheet("background:transparent;");
        auto* sl = new QVBoxLayout(section_out);
        sl->setContentsMargins(0, 0, 0, 0);
        sl->setSpacing(4);
        auto* hdr = new QLabel(title, section_out);
        hdr->setStyleSheet(section_header_ss);
        sl->addWidget(hdr);
        container_out = new QWidget(section_out);
        container_out->setStyleSheet("background:transparent;");
        layout_out = new QHBoxLayout(container_out);
        layout_out->setContentsMargins(0, 0, 0, 0);
        layout_out->setSpacing(6);
        layout_out->addStretch();
        sl->addWidget(container_out);
    };
    make_chip_section("PUBLIC COMPARABLES", comps_section_, comps_container_, comps_layout_);
    vl->addWidget(comps_section_);
    vl->addSpacing(4);
    make_chip_section("TAGS", tags_section_, tags_container_, tags_layout_);
    vl->addWidget(tags_section_);
    vl->addSpacing(4);

    desc_section_ = new QWidget;
    desc_section_->setStyleSheet("background:transparent;");
    {
        auto* dl = new QVBoxLayout(desc_section_);
        dl->setContentsMargins(0, 0, 0, 0);
        dl->setSpacing(4);
        auto* desc_header = new QLabel("ABOUT", desc_section_);
        desc_header->setStyleSheet(section_header_ss);
        dl->addWidget(desc_header);

        desc_lbl_ = new QLabel;
        desc_lbl_->setWordWrap(true);
        desc_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;line-height:1.5;background:transparent;")
                .arg(colors::TEXT_PRIMARY()));
        desc_lbl_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        desc_lbl_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        // Wrap description in a height-bounded scroll so multi-paragraph
        // bios don't push the dossier off-screen. Companies without a
        // description hide the whole section instead of showing an
        // empty scroll viewport.
        auto* desc_scroll = new QScrollArea(desc_section_);
        desc_scroll->setWidget(desc_lbl_);
        desc_scroll->setWidgetResizable(true);
        desc_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        desc_scroll->setFrameShape(QFrame::NoFrame);
        desc_scroll->setStyleSheet(thin_scroll_ss);
        desc_scroll->setMaximumHeight(56);
        dl->addWidget(desc_scroll);
    }
    vl->addWidget(desc_section_);

    // Push any leftover pane vertical to the bottom instead of inflating
    // cards. With every card sized to its content, a populated dossier in a
    // tall pane will show empty space below "ABOUT"; that's the right place
    // for whitespace, not inside the data tiles.
    vl->addStretch(1);
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

    // ── Key metrics — adaptive per data source ───────────────────────────────
    // The 4 tiles relabel themselves based on what we know:
    //   * Form D companies      → Valuation / Last Round / Cum. Raised / Last Filing
    //   * N-PORT-only companies → Consensus Mark / Latest Mark / Funds / Mark Drift
    // The previous static labels ("Valuation", "Employees", etc.) showed
    // em-dashes for OpenAI/Anthropic-class names because N-PORT marks don't
    // disclose valuation or employee count.
    const bool has_marks  = !c.fund_marks.isEmpty();
    const bool has_rounds = !c.rounds.isEmpty()
                            || c.cumulative_raised_m > 0
                            || c.last_round_date.isValid();

    auto set_kv = [](QLabel* label, QLabel* value, const QString& l, const QString& v) {
        if (label) label->setText(l);
        if (value) value->setText(v.isEmpty() ? QStringLiteral("—") : v);
    };

    if (has_rounds) {
        // Form D path
        set_kv(val_lbl_label_, val_lbl_, "Valuation",
               c.last_valuation_usd > 0
                   ? QString("$%1B").arg(c.last_valuation_usd, 0, 'f',
                                         c.last_valuation_usd >= 10 ? 0 : 1)
                   : QString());
        set_kv(round_lbl_label_, round_lbl_, "Last Round",
               c.last_round_name.isEmpty()
                   ? QString()
                   : c.last_round_name + (c.last_round_date.isValid()
                         ? "  " + c.last_round_date.toString("MMM yyyy") : ""));
        set_kv(rev_lbl_label_, rev_lbl_,
               c.fin.revenue_m > 0 ? "Revenue Est." : "Cum. Raised",
               c.fin.revenue_m > 0
                   ? "$" + QString::number(qRound(c.fin.revenue_m)) + "M est."
                   : (c.cumulative_raised_m > 0
                          ? "$" + QString::number(qRound(c.cumulative_raised_m)) + "M"
                          : QString()));
        set_kv(emp_lbl_label_, emp_lbl_,
               c.employee_count_est > 0 ? "Employees" : "Filings",
               c.employee_count_est > 0
                   ? QString::number(c.employee_count_est) + "+"
                   : (c.rounds.isEmpty() ? QString() : QString::number(c.rounds.size())));
    } else if (has_marks) {
        // N-PORT-only path — surface what mutual-fund marks tell us.
        const auto& latest = c.fund_marks.first();
        set_kv(val_lbl_label_, val_lbl_, "Consensus Mark",
               c.analytics.consensus_mark_pps > 0
                   ? QString("$%1/sh").arg(c.analytics.consensus_mark_pps, 0, 'f', 2)
                   : QString());
        set_kv(round_lbl_label_, round_lbl_, "Latest Mark",
               latest.as_of.isValid() ? latest.as_of.toString("MMM yyyy") : QString());
        set_kv(rev_lbl_label_, rev_lbl_, "Funds Tracking",
               c.analytics.smart_money_index > 0
                   ? QString::number(c.analytics.smart_money_index) + " funds"
                   : QString());
        if (c.analytics.mark_drift_vs_last_round_pct != 0) {
            const double d = c.analytics.mark_drift_vs_last_round_pct;
            set_kv(emp_lbl_label_, emp_lbl_, "Mark Drift",
                   (d >= 0 ? "+" : "") + QString::number(d, 'f', 1) + "%");
            if (emp_lbl_)
                emp_lbl_->setStyleSheet(
                    QString("color:%1;font-size:13px;font-weight:700;"
                            "font-family:Consolas,monospace;background:transparent;")
                        .arg(d >= 0 ? colors::POSITIVE() : colors::NEGATIVE()));
        } else {
            set_kv(emp_lbl_label_, emp_lbl_, "Mark Dispersion",
                   c.analytics.mark_dispersion_pct > 0
                       ? "±" + QString::number(c.analytics.mark_dispersion_pct, 'f', 1) + "%"
                       : QString());
        }
    } else if (c.s1.first_filed.isValid()) {
        // S-1-only stub (filer not in Form D / N-PORT universe). Show
        // what we know about the IPO timeline.
        set_kv(val_lbl_label_, val_lbl_, "IPO Status",
               c.s1.status_label.isEmpty() ? QStringLiteral("Filed") : c.s1.status_label);
        set_kv(round_lbl_label_, round_lbl_, "First Filed",
               c.s1.first_filed.toString("MMM d, yyyy"));
        set_kv(rev_lbl_label_, rev_lbl_, "Amendments",
               c.s1.amendment_count > 0
                   ? "×" + QString::number(c.s1.amendment_count)
                   : QStringLiteral("0"));
        // Estimated pricing date — today + analytics.days_to_price_est.
        // The estimate is a rough sector-median lag from first S-1 filing
        // to pricing; presented as "Est. IPO ~MMM d" so the user knows
        // it's not an announced date.
        if (c.analytics.days_to_price_est > 0) {
            const QDate est = QDate::currentDate().addDays(c.analytics.days_to_price_est);
            set_kv(emp_lbl_label_, emp_lbl_, "Est. IPO",
                   "~" + est.toString("MMM d, yyyy"));
        } else {
            set_kv(emp_lbl_label_, emp_lbl_, "Days Since File",
                   c.s1.days_since_first_filed > 0
                       ? QString::number(c.s1.days_since_first_filed) + "d"
                       : QString());
        }
    } else {
        // Nothing — keep generic labels but make em-dashes uniform.
        set_kv(val_lbl_label_,   val_lbl_,   "Valuation",   QString());
        set_kv(round_lbl_label_, round_lbl_, "Last Round",  QString());
        set_kv(rev_lbl_label_,   rev_lbl_,   "Revenue Est.", QString());
        set_kv(emp_lbl_label_,   emp_lbl_,   "Employees",    QString());
    }

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
    if (desc_section_)
        desc_section_->setVisible(!c.description.trimmed().isEmpty());

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

    // Table is exactly tall enough for its rows — no blank rows below the
    // last data row. Cap = 200px (≈ 6-7 visible rows); excess scrolls inside
    // the table itself, so a 20-round company doesn't push the dossier past
    // the pane.
    const int rows     = rounds.size();
    const int row_h    = 26;
    const int header_h = 28;
    const int natural  = header_h + rows * row_h + 4;
    rounds_table_->setFixedHeight(qMin(200, std::max(header_h + row_h, natural)));
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
    // Same shrink-to-content as rebuild_rounds_table. Cap 180px (≈ 7 rows).
    const int rows     = sorted.size();
    const int row_h    = 22;
    const int header_h = 24;
    const int natural  = header_h + rows * row_h + 4;
    marks_table_->setFixedHeight(qMin(180, std::max(header_h + row_h, natural)));
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
    // Size the scroll viewport to fit actual names (no blank rows below the
    // last bullet). Cap at 200px (≈ 9 visible names) — beyond that, scroll.
    if (investors_scroll_) {
        const int row_h    = 20;   // matches font-size:12px line height
        const int pad      = 8;
        const int natural  = investors.size() * row_h + pad;
        investors_scroll_->setFixedHeight(qMin(200, std::max(row_h + pad, natural)));
    }
    // Hide the whole investors card when nothing to show — keeps the 2-col
    // mid band balanced for sparse companies instead of leaving an empty tile.
    if (investors_card_)
        investors_card_->setVisible(!investors.isEmpty());
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
    if (comps_section_)
        comps_section_->setVisible(!tickers.isEmpty());
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
    if (tags_section_)
        tags_section_->setVisible(!tags.isEmpty());
}

} // namespace fincept::screens

// src/screens/power_trader/CabinetPanel.cpp
#include "screens/power_trader/CabinetPanel.h"

#include "screens/power_trader/PowerTraderService.h"
#include "ui/theme/Theme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens {

// ── Styling helpers ───────────────────────────────────────────────────────────

static QString section_hdr() {
    return QString("background:%1;color:%2;font-size:12px;font-weight:700;"
                   "letter-spacing:0.5px;padding:6px 12px;border-bottom:1px solid %3;")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED());
}

static QString table_ss() {
    return QString("QTableWidget{background:%1;color:%2;border:none;font-size:12px;"
                   "font-family:Consolas,monospace;gridline-color:transparent;}"
                   "QTableWidget::item{padding:5px 10px;border-bottom:1px solid %3;}"
                   "QTableWidget::item:selected{background:rgba(217,119,6,0.18);color:%2;}"
                   "QTableWidget::item:hover{background:%4;}"
                   "QScrollBar:vertical{width:5px;background:%1;}"
                   "QScrollBar::handle:vertical{background:%3;min-height:20px;}")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_MED(), ui::colors::BG_HOVER());
}

static QString hdr_ss() {
    return QString("QHeaderView::section{background:%1;color:%2;border:none;"
                   "border-bottom:2px solid %3;border-right:1px solid %4;"
                   "padding:5px 10px;font-size:12px;font-weight:700;}")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::AMBER(), ui::colors::BORDER_MED());
}

static const char* conflict_color(double score) {
    if (score >= 70) return "#ef4444";
    if (score >= 45) return "#f97316";
    if (score >= 20) return "#eab308";
    return "#6b7280";
}

static QWidget* make_stat_tile(const QString& label, QLabel*& value_out, QWidget* parent = nullptr) {
    auto* w = new QWidget(parent);
    w->setStyleSheet(QString("background:%1;border:1px solid %2;border-radius:4px;")
                         .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 8, 12, 8);
    vl->setSpacing(2);

    auto* lbl = new QLabel(label);
    lbl->setStyleSheet(QString("color:%1;font-size:12px;font-weight:700;letter-spacing:0.5px;"
                               "background:transparent;")
                           .arg(ui::colors::TEXT_SECONDARY()));
    vl->addWidget(lbl);

    value_out = new QLabel(QStringLiteral("—"));
    value_out->setStyleSheet(QString("color:%1;font-size:18px;font-weight:700;"
                                     "font-family:Consolas,monospace;background:transparent;")
                                 .arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(value_out);
    return w;
}

// ── Construction ──────────────────────────────────────────────────────────────

CabinetPanel::CabinetPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void CabinetPanel::build_ui() {
    setStyleSheet(QString("background:%1;color:%2;")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    stack_ = new QStackedWidget;
    build_loading_page();
    build_content_page();
    root->addWidget(stack_, 1);
    stack_->setCurrentIndex(0);
}

void CabinetPanel::build_loading_page() {
    auto* page = new QWidget;
    auto* ll   = new QVBoxLayout(page);
    auto* lbl  = new QLabel("Loading executive branch financial disclosures…");
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet(QString("color:%1;font-size:13px;")
                           .arg(ui::colors::TEXT_SECONDARY()));
    ll->addStretch(); ll->addWidget(lbl); ll->addStretch();
    stack_->addWidget(page);  // index 0
}

void CabinetPanel::build_content_page() {
    auto* page = new QWidget;
    // ── Horizontal layout: narrow LEFT stats column | wide RIGHT main area ─────
    // Left column: title + source note + compact stats (height > width = correct)
    // Right: member list | detail splitter (existing, unchanged)
    auto* root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── LEFT column: stats (200px fixed, full height) ─────────────────────────
    auto* left_col = new QWidget(page);
    left_col->setFixedWidth(200);
    left_col->setStyleSheet(
        QString("QWidget{background:%1;border-right:1px solid %2;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    {
        auto* lcl = new QVBoxLayout(left_col);
        lcl->setContentsMargins(0, 0, 0, 0);
        lcl->setSpacing(0);

        // Title
        auto* title = new QLabel("EXECUTIVE BRANCH\nFINANCIAL DISCLOSURES");
        title->setWordWrap(true);
        title->setStyleSheet(
            QString("color:%1;font-size:13px;font-weight:700;padding:10px 12px;"
                    "border-bottom:1px solid %2;background:%3;")
                .arg(ui::colors::AMBER(), ui::colors::BORDER_DIM(), ui::colors::BG_RAISED()));
        lcl->addWidget(title);

        // Source note
        source_note_ = new QLabel;
        source_note_->setStyleSheet(
            QString("color:%1;font-size:11px;font-style:italic;padding:6px 12px;"
                    "border-bottom:1px solid %2;background:%3;")
                .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::BG_SURFACE()));
        source_note_->setWordWrap(true);
        lcl->addWidget(source_note_);

        // Compact stat rows — label:value, full height of left column
        const QString row_sep =
            QString("QWidget{background:transparent;border-bottom:1px solid %1;}")
                .arg(ui::colors::BORDER_DIM());
        const QString cap_ss =
            QString("color:%1;font-size:12px;font-weight:600;background:transparent;")
                .arg(ui::colors::TEXT_SECONDARY());
        const QString val_ss =
            QString("color:%1;font-size:14px;font-weight:700;"
                    "font-family:Consolas,monospace;background:transparent;")
                .arg(ui::colors::TEXT_PRIMARY());

        auto add_stat = [&](const QString& label, QLabel*& out) {
            auto* row = new QWidget(left_col);
            row->setStyleSheet(row_sep);
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(12, 10, 12, 10);
            hl->setSpacing(4);
            auto* cap = new QLabel(label, row);
            cap->setStyleSheet(cap_ss);
            cap->setWordWrap(true);
            hl->addWidget(cap, 1);
            out = new QLabel(QStringLiteral("—"), row);
            out->setStyleSheet(val_ss);
            out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            hl->addWidget(out);
            lcl->addWidget(row);
        };

        add_stat(QStringLiteral("Members"),           stat_members_);
        add_stat(QStringLiteral("Holdings Min"),      stat_total_lo_);
        add_stat(QStringLiteral("Holdings Max"),      stat_total_hi_);
        add_stat(QStringLiteral("Avg Conflict"),      stat_avg_conf_);
        add_stat(QStringLiteral("Highest Conflict"),  stat_top_conf_);
        lcl->addStretch();
    }
    root->addWidget(left_col);

    // ── RIGHT area: member list | detail — existing splitter ──────────────────
    auto* right_area = new QWidget(page);
    right_area->setStyleSheet(
        QString("QWidget{background:%1;}").arg(ui::colors::BG_BASE()));
    auto* ral = new QVBoxLayout(right_area);
    ral->setContentsMargins(0, 0, 0, 0);
    ral->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setStyleSheet(QString("QSplitter::handle{background:%1;}")
                                .arg(ui::colors::BORDER_DIM()));

    // ── Left: member list ─────────────────────────────────────────────────────
    auto* left = new QWidget;
    {
        auto* ll = new QVBoxLayout(left);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(0);

        auto* lhdr = new QLabel("RANKED BY CONFLICT SCORE");
        lhdr->setStyleSheet(section_hdr());
        ll->addWidget(lhdr);

        member_table_ = new QTableWidget;
        member_table_->setColumnCount(4);
        member_table_->setHorizontalHeaderLabels({"#", "NAME / TITLE", "CONFLICT", "HOLDINGS"});
        member_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        member_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        member_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        member_table_->setShowGrid(false);
        member_table_->verticalHeader()->setVisible(false);
        member_table_->setFocusPolicy(Qt::NoFocus);

        auto* mh = member_table_->horizontalHeader();
        mh->setSectionResizeMode(0, QHeaderView::Fixed); mh->resizeSection(0, 24);
        mh->setSectionResizeMode(1, QHeaderView::Interactive); mh->resizeSection(1, 220); mh->setStretchLastSection(true);
        mh->setSectionResizeMode(2, QHeaderView::Fixed); mh->resizeSection(2, 60);
        mh->setSectionResizeMode(3, QHeaderView::Fixed); mh->resizeSection(3, 70);
        mh->setStyleSheet(hdr_ss());
        member_table_->setStyleSheet(table_ss());

        connect(member_table_, &QTableWidget::currentCellChanged,
                this, [this](int row, int, int, int) { on_member_selected(row); });
        ll->addWidget(member_table_, 1);
    }

    // ── Right: detail tabs ────────────────────────────────────────────────────
    auto* right = new QWidget;
    right->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    {
        auto* rl = new QVBoxLayout(right);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);

        detail_tabs_ = new QTabWidget;
        detail_tabs_->setDocumentMode(true);
        detail_tabs_->setStyleSheet(QString(R"(
            QTabWidget::pane { border:0; background:%1; }
            QTabBar::tab {
                background:%2; color:%3; padding:6px 16px;
                border:0; border-bottom:2px solid transparent;
                font-size:12px; font-weight:700; letter-spacing:0.5px;
            }
            QTabBar::tab:selected { color:%4; border-bottom:2px solid %4; }
            QTabBar::tab:hover:!selected { color:%5; }
        )").arg(ui::colors::BG_BASE(), ui::colors::BG_SURFACE(),
                ui::colors::TEXT_SECONDARY(), ui::colors::AMBER(),
                ui::colors::TEXT_PRIMARY()));

        // ── OVERVIEW tab ──────────────────────────────────────────────────────
        auto* overview_page = new QScrollArea;
        overview_page->setWidgetResizable(true);
        overview_page->setFrameShape(QFrame::NoFrame);
        overview_page->setStyleSheet(
            QString("background:%1;border:none;"
                    "QScrollBar:vertical{width:4px;background:%1;}"
                    "QScrollBar::handle:vertical{background:%2;}")
                .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
        auto* ov_body = new QWidget;
        ov_body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
        auto* ovl = new QVBoxLayout(ov_body);
        ovl->setContentsMargins(0, 0, 0, 0);
        ovl->setSpacing(0);

        auto* ov_r_hdr = new QLabel("CONFLICT RANKING  ·  ALL CABINET MEMBERS");
        ov_r_hdr->setStyleSheet(section_hdr());
        ovl->addWidget(ov_r_hdr);

        o_ranking_table_ = new QTableWidget;
        o_ranking_table_->setColumnCount(5);
        o_ranking_table_->setHorizontalHeaderLabels(
            {"#", "NAME", "TITLE", "CONFLICT", "PORTFOLIO (EST)"});
        o_ranking_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        o_ranking_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        o_ranking_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        o_ranking_table_->setShowGrid(false);
        o_ranking_table_->verticalHeader()->setVisible(false);
        o_ranking_table_->setMinimumHeight(300);
        auto* orh = o_ranking_table_->horizontalHeader();
        orh->setSectionResizeMode(0, QHeaderView::Fixed); orh->resizeSection(0, 28);
        orh->setSectionResizeMode(1, QHeaderView::Interactive); orh->resizeSection(1, 200);
        orh->setSectionResizeMode(2, QHeaderView::Interactive); orh->resizeSection(2, 240); orh->setStretchLastSection(true);
        orh->setSectionResizeMode(3, QHeaderView::Fixed); orh->resizeSection(3, 70);
        orh->setSectionResizeMode(4, QHeaderView::Fixed); orh->resizeSection(4, 120);
        orh->setStyleSheet(hdr_ss());
        o_ranking_table_->setStyleSheet(table_ss());
        connect(o_ranking_table_, &QTableWidget::currentCellChanged,
                this, [this](int row, int, int, int) {
                    // sync with left panel
                    member_table_->setCurrentCell(row, 0);
                });
        ovl->addWidget(o_ranking_table_);

        auto* ov_s_hdr = new QLabel("CABINET-WIDE SECTOR EXPOSURE");
        ov_s_hdr->setStyleSheet(section_hdr());
        ovl->addWidget(ov_s_hdr);

        o_sector_table_ = new QTableWidget;
        o_sector_table_->setColumnCount(3);
        o_sector_table_->setHorizontalHeaderLabels({"SECTOR", "EST HOLDINGS", "MEMBERS"});
        o_sector_table_->setSelectionMode(QAbstractItemView::NoSelection);
        o_sector_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        o_sector_table_->setShowGrid(false);
        o_sector_table_->verticalHeader()->setVisible(false);
        auto* osh = o_sector_table_->horizontalHeader();
        osh->setSectionResizeMode(0, QHeaderView::Interactive); osh->resizeSection(0, 130); osh->setStretchLastSection(true);
        osh->setSectionResizeMode(1, QHeaderView::Fixed); osh->resizeSection(1, 110);
        osh->setSectionResizeMode(2, QHeaderView::Fixed); osh->resizeSection(2, 60);
        osh->setStyleSheet(hdr_ss());
        o_sector_table_->setStyleSheet(table_ss());
        ovl->addWidget(o_sector_table_);
        ovl->addStretch();
        overview_page->setWidget(ov_body);
        detail_tabs_->addTab(overview_page, "Cabinet Overview");

        // ── HOLDINGS tab ─────────────────────────────────────────────────────
        auto* holdings_page = new QWidget;
        holdings_page->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
        auto* hp = new QVBoxLayout(holdings_page);
        hp->setContentsMargins(0, 0, 0, 0);
        hp->setSpacing(0);

        h_header_ = new QLabel;
        h_header_->setStyleSheet(
            QString("background:%1;color:%2;font-size:12px;font-weight:700;"
                    "padding:10px 14px;border-bottom:1px solid %3;")
                .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
        hp->addWidget(h_header_);

        auto* h_tbl_hdr = new QLabel("DISCLOSED ASSETS  ·  Annual holdings snapshot");
        h_tbl_hdr->setStyleSheet(section_hdr());
        hp->addWidget(h_tbl_hdr);

        h_table_ = new QTableWidget;
        h_table_->setColumnCount(6);
        h_table_->setHorizontalHeaderLabels(
            {"ASSET", "TICKER", "TYPE", "SECTOR", "VALUE RANGE", "CONFLICT"});
        h_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        h_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        h_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        h_table_->setShowGrid(false);
        h_table_->verticalHeader()->setVisible(false);
        auto* hth = h_table_->horizontalHeader();
        hth->setSectionResizeMode(0, QHeaderView::Interactive); hth->resizeSection(0, 240); hth->setStretchLastSection(true);
        hth->setSectionResizeMode(1, QHeaderView::Fixed); hth->resizeSection(1, 60);
        hth->setSectionResizeMode(2, QHeaderView::Fixed); hth->resizeSection(2, 90);
        hth->setSectionResizeMode(3, QHeaderView::Fixed); hth->resizeSection(3, 100);
        hth->setSectionResizeMode(4, QHeaderView::Fixed); hth->resizeSection(4, 140);
        hth->setSectionResizeMode(5, QHeaderView::Fixed); hth->resizeSection(5, 55);
        hth->setStyleSheet(hdr_ss());
        h_table_->setStyleSheet(table_ss());
        hp->addWidget(h_table_, 1);
        detail_tabs_->addTab(holdings_page, "Holdings");

        // ── CONFLICTS tab ─────────────────────────────────────────────────────
        auto* conflicts_scroll = new QScrollArea;
        conflicts_scroll->setWidgetResizable(true);
        conflicts_scroll->setFrameShape(QFrame::NoFrame);
        conflicts_scroll->setStyleSheet(
            QString("background:%1;border:none;"
                    "QScrollBar:vertical{width:4px;background:%1;}"
                    "QScrollBar::handle:vertical{background:%2;}")
                .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
        auto* cf_body = new QWidget;
        cf_body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
        auto* cfl = new QVBoxLayout(cf_body);
        cfl->setContentsMargins(0, 0, 0, 0);
        cfl->setSpacing(0);

        c_header_ = new QLabel;
        c_header_->setStyleSheet(
            QString("background:%1;color:%2;font-size:12px;font-weight:700;"
                    "padding:10px 14px;border-bottom:1px solid %3;")
                .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
        cfl->addWidget(c_header_);

        c_domain_ = new QLabel;
        c_domain_->setStyleSheet(
            QString("color:%1;font-size:12px;padding:8px 14px;"
                    "background:%2;border-bottom:1px solid %3;")
                .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        c_domain_->setWordWrap(true);
        cfl->addWidget(c_domain_);

        auto* cf_flags_hdr = new QLabel("CONFLICT FLAGS  ·  Holdings overlapping regulatory domain");
        cf_flags_hdr->setStyleSheet(section_hdr());
        cfl->addWidget(cf_flags_hdr);

        c_flags_widget_  = new QWidget;
        c_flags_widget_->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
        c_flags_layout_  = new QVBoxLayout(c_flags_widget_);
        c_flags_layout_->setContentsMargins(14, 10, 14, 10);
        c_flags_layout_->setSpacing(8);
        cfl->addWidget(c_flags_widget_);
        cfl->addStretch();
        conflicts_scroll->setWidget(cf_body);
        detail_tabs_->addTab(conflicts_scroll, "Conflicts");

        // ── SECTOR tab ────────────────────────────────────────────────────────
        auto* sector_page = new QWidget;
        sector_page->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
        auto* sp = new QVBoxLayout(sector_page);
        sp->setContentsMargins(0, 0, 0, 0);
        sp->setSpacing(0);

        s_header_ = new QLabel;
        s_header_->setStyleSheet(
            QString("background:%1;color:%2;font-size:12px;font-weight:700;"
                    "padding:10px 14px;border-bottom:1px solid %3;")
                .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
        sp->addWidget(s_header_);

        auto* s_tbl_hdr = new QLabel("HOLDINGS BY SECTOR  ·  Estimated midpoint values");
        s_tbl_hdr->setStyleSheet(section_hdr());
        sp->addWidget(s_tbl_hdr);

        s_table_ = new QTableWidget;
        s_table_->setColumnCount(4);
        s_table_->setHorizontalHeaderLabels({"SECTOR", "EST AMOUNT", "% PORTFOLIO", "CONFLICT"});
        s_table_->setSelectionMode(QAbstractItemView::NoSelection);
        s_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        s_table_->setShowGrid(false);
        s_table_->verticalHeader()->setVisible(false);
        auto* sth = s_table_->horizontalHeader();
        sth->setSectionResizeMode(0, QHeaderView::Interactive); sth->resizeSection(0, 130); sth->setStretchLastSection(true);
        sth->setSectionResizeMode(1, QHeaderView::Fixed); sth->resizeSection(1, 110);
        sth->setSectionResizeMode(2, QHeaderView::Fixed); sth->resizeSection(2, 80);
        sth->setSectionResizeMode(3, QHeaderView::Fixed); sth->resizeSection(3, 60);
        sth->setStyleSheet(hdr_ss());
        s_table_->setStyleSheet(table_ss());
        sp->addWidget(s_table_, 1);
        detail_tabs_->addTab(sector_page, "Sector Breakdown");

        rl->addWidget(detail_tabs_, 1);
    }

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setSizes({320, 680});
    ral->addWidget(splitter, 1);
    root->addWidget(right_area, 1);

    stack_->addWidget(page);  // index 1
}

// ── Activation ────────────────────────────────────────────────────────────────

void CabinetPanel::activate() {
    auto& svc = power_trader::PowerTraderService::instance();
    connect(&svc, &power_trader::PowerTraderService::cabinet_data_loaded,
            this, &CabinetPanel::on_cabinet_loaded,
            Qt::UniqueConnection);

    if (svc.is_cabinet_loaded()) {
        on_cabinet_loaded(svc.cabinet_summary());
    } else {
        stack_->setCurrentIndex(0);
        svc.load_cabinet_data();
    }
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void CabinetPanel::on_cabinet_loaded(power_trader::CabinetSummary summary) {
    summary_ = summary;
    stack_->setCurrentIndex(1);

    source_note_->setText(
        QString("Source: %1  ·  Disclosure year: %2  ·  Annual snapshot — not individual trades  ·  %3")
            .arg(summary.members.isEmpty() ? "OGE Form 278"
                 : summary.members.first().data_source)
            .arg(summary.disclosure_year)
            .arg(summary.data_note.left(120)));

    populate_stat_tiles();
    populate_member_list();
    populate_overview_tab();

    if (!summary_.members.isEmpty())
        on_member_selected(0);
}

void CabinetPanel::on_member_selected(int row) {
    if (row < 0 || row >= summary_.members.size()) return;
    selected_row_ = row;
    member_table_->selectRow(row);

    // Sort by conflict score for display (same order as member_table_)
    auto ranked = power_trader::PowerTraderService::instance().cabinet_conflict_ranking();
    if (row >= ranked.size()) return;
    const auto& m = ranked[row];

    show_member(m);
}

// ── Population ────────────────────────────────────────────────────────────────

void CabinetPanel::populate_stat_tiles() {
    stat_members_->setText(QString::number(summary_.members.size()));

    auto fmt = [](double v) -> QString {
        if (v >= 1e9) return "$" + QString::number(v/1e9,'f',1) + "B";
        if (v >= 1e6) return "$" + QString::number(v/1e6,'f',1) + "M";
        return "$" + QString::number(v/1e3,'f',0) + "K";
    };
    stat_total_lo_->setText(fmt(summary_.total_est_min));
    stat_total_hi_->setText(fmt(summary_.total_est_max));

    double avg = 0;
    for (const auto& m : summary_.members) avg += m.conflict_score;
    if (!summary_.members.isEmpty()) avg /= summary_.members.size();
    stat_avg_conf_->setText(QString::number(avg,'f',0) + "/100");
    stat_avg_conf_->setStyleSheet(
        QString("color:%1;font-size:16px;font-weight:700;font-family:Consolas,monospace;"
                "background:transparent;").arg(conflict_color(avg)));

    auto ranked = power_trader::PowerTraderService::instance().cabinet_conflict_ranking();
    if (!ranked.isEmpty()) {
        stat_top_conf_->setText(
            ranked.first().full_name.split(' ').last() + " " +
            QString::number(ranked.first().conflict_score,'f',0));
        stat_top_conf_->setStyleSheet(
            QString("color:%1;font-size:13px;font-weight:700;"
                    "font-family:Consolas,monospace;background:transparent;")
                .arg(conflict_color(ranked.first().conflict_score)));
    }
}

void CabinetPanel::populate_member_list() {
    auto ranked = power_trader::PowerTraderService::instance().cabinet_conflict_ranking();
    member_table_->setRowCount(ranked.size());

    for (int r = 0; r < ranked.size(); ++r) {
        const auto& m = ranked[r];
        member_table_->setRowHeight(r, 46);

        auto mk = [&](int col, const QString& txt, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(txt);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            member_table_->setItem(r, col, item);
        };

        mk(0, QString::number(r + 1), ui::colors::TEXT_TERTIARY, Qt::AlignCenter);

        // Name + title combined
        auto* ni = new QTableWidgetItem(m.full_name + "\n" + m.title);
        ni->setForeground(QColor(ui::colors::CYAN()));
        ni->setFlags(ni->flags() & ~Qt::ItemIsEditable);
        member_table_->setItem(r, 1, ni);

        auto* ci = new QTableWidgetItem(QString::number(m.conflict_score,'f',0));
        ci->setTextAlignment(Qt::AlignCenter);
        ci->setForeground(QColor(conflict_color(m.conflict_score)));
        ci->setFlags(ci->flags() & ~Qt::ItemIsEditable);
        member_table_->setItem(r, 2, ci);

        const double mid = (m.est_total_min + m.est_total_max) / 2.0;
        auto fmt = [](double v) -> QString {
            if (v >= 1e9) return "$"+QString::number(v/1e9,'f',1)+"B";
            if (v >= 1e6) return "$"+QString::number(v/1e6,'f',1)+"M";
            return "$"+QString::number(v/1e3,'f',0)+"K";
        };
        mk(3, fmt(mid), ui::colors::TEXT_SECONDARY, Qt::AlignRight | Qt::AlignVCenter);
    }
}

void CabinetPanel::populate_overview_tab() {
    auto ranked = power_trader::PowerTraderService::instance().cabinet_conflict_ranking();
    o_ranking_table_->setRowCount(ranked.size());
    for (int r = 0; r < ranked.size(); ++r) {
        const auto& m = ranked[r];
        o_ranking_table_->setRowHeight(r, 30);

        auto mk = [&](int col, const QString& txt, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft|Qt::AlignVCenter) {
            auto* i = new QTableWidgetItem(txt);
            i->setTextAlignment(align);
            i->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            i->setFlags(i->flags() & ~Qt::ItemIsEditable);
            o_ranking_table_->setItem(r, col, i);
        };

        mk(0, QString::number(r+1), ui::colors::TEXT_TERTIARY, Qt::AlignCenter);
        mk(1, m.full_name, ui::colors::CYAN);
        mk(2, m.title, ui::colors::TEXT_SECONDARY);

        auto* ci = new QTableWidgetItem(QString::number(m.conflict_score,'f',0));
        ci->setTextAlignment(Qt::AlignCenter);
        ci->setForeground(QColor(conflict_color(m.conflict_score)));
        ci->setFlags(ci->flags() & ~Qt::ItemIsEditable);
        o_ranking_table_->setItem(r, 3, ci);

        const double mid = (m.est_total_min + m.est_total_max) / 2.0;
        mk(4, mid >= 1e6 ? "$"+QString::number(mid/1e6,'f',1)+"M"
                         : "$"+QString::number(mid/1e3,'f',0)+"K",
           ui::colors::TEXT_SECONDARY, Qt::AlignRight|Qt::AlignVCenter);
    }

    const auto sec_expo = power_trader::PowerTraderService::instance().cabinet_sector_exposure();
    double grand = 0;
    for (const auto& s : sec_expo) grand += s.total_est_amount;

    o_sector_table_->setRowCount(sec_expo.size());
    for (int r = 0; r < sec_expo.size(); ++r) {
        const auto& s = sec_expo[r];
        o_sector_table_->setRowHeight(r, 30);

        auto mk = [&](int col, const QString& txt, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft|Qt::AlignVCenter) {
            auto* i = new QTableWidgetItem(txt);
            i->setTextAlignment(align);
            i->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            i->setFlags(i->flags() & ~Qt::ItemIsEditable);
            o_sector_table_->setItem(r, col, i);
        };

        mk(0, s.sector);
        const double v = s.total_est_amount;
        mk(1, v >= 1e6 ? "$"+QString::number(v/1e6,'f',1)+"M"
                       : "$"+QString::number(v/1e3,'f',0)+"K",
           ui::colors::TEXT_SECONDARY, Qt::AlignRight|Qt::AlignVCenter);
        mk(2, grand > 0 ? QString::number(v/grand*100,'f',0)+"%" : "—",
           nullptr, Qt::AlignCenter);
    }
}

void CabinetPanel::show_member(const power_trader::CabinetMember& m) {
    const QString score_str = QString::number(m.conflict_score,'f',0);
    const char* sc = conflict_color(m.conflict_score);

    // Detail tabs: switch to overview if first call
    detail_tabs_->setCurrentIndex(1);  // Holdings

    populate_holdings_tab(m);
    populate_conflicts_tab(m);
    populate_sector_tab(m);
}

void CabinetPanel::populate_holdings_tab(const power_trader::CabinetMember& m) {
    h_header_->setText(m.full_name + "  ·  " + m.title);

    h_table_->setRowCount(m.holdings.size());
    for (int r = 0; r < m.holdings.size(); ++r) {
        const auto& h = m.holdings[r];
        h_table_->setRowHeight(r, 32);

        auto mk = [&](int col, const QString& txt, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft|Qt::AlignVCenter) {
            auto* i = new QTableWidgetItem(txt);
            i->setTextAlignment(align);
            i->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            i->setFlags(i->flags() & ~Qt::ItemIsEditable);
            // Highlight conflict rows
            if (h.is_conflict)
                i->setBackground(QColor("rgba(239,68,68,0.08)"));
            h_table_->setItem(r, col, i);
        };

        mk(0, h.asset_name);
        mk(1, h.ticker.isEmpty() ? "—" : h.ticker,
           h.ticker.isEmpty() ? ui::colors::TEXT_TERTIARY : ui::colors::CYAN);
        mk(2, h.asset_type, ui::colors::TEXT_SECONDARY);
        mk(3, h.sector, ui::colors::TEXT_SECONDARY);
        mk(4, h.value_range_label, ui::colors::TEXT_PRIMARY, Qt::AlignRight|Qt::AlignVCenter);

        auto* cf_i = new QTableWidgetItem(h.is_conflict ? "⚠" : "—");
        cf_i->setTextAlignment(Qt::AlignCenter);
        cf_i->setForeground(QColor(h.is_conflict ? "#ef4444" : ui::colors::TEXT_TERTIARY()));
        cf_i->setToolTip(h.conflict_note);
        cf_i->setFlags(cf_i->flags() & ~Qt::ItemIsEditable);
        h_table_->setItem(r, 5, cf_i);
    }
}

void CabinetPanel::populate_conflicts_tab(const power_trader::CabinetMember& m) {
    c_header_->setText(
        QString("%1  ·  Conflict Score: %2 / 100")
            .arg(m.full_name)
            .arg(m.conflict_score, 0, 'f', 0));
    c_header_->setStyleSheet(
        QString("background:%1;font-size:12px;font-weight:700;"
                "padding:10px 14px;border-bottom:1px solid %2;color:%3;")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(),
                 conflict_color(m.conflict_score)));

    c_domain_->setText(
        QString("Role: %1\n"
                "Regulatory domain: %2\n"
                "Conflict holdings: %3 of %4 positions (%5% overlap)")
            .arg(m.title)
            .arg(m.regulated_sectors.isEmpty() ? "—" : m.regulated_sectors.join(", "))
            .arg(m.conflict_count)
            .arg(m.holdings.size())
            .arg(m.holdings.isEmpty() ? 0
                 : int(double(m.conflict_count) / m.holdings.size() * 100)));

    // Clear existing flags
    while (QLayoutItem* item = c_flags_layout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (m.conflict_flags.isEmpty()) {
        auto* none_lbl = new QLabel("No conflict flags detected — holdings do not overlap regulatory domain.");
        none_lbl->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
        none_lbl->setWordWrap(true);
        c_flags_layout_->addWidget(none_lbl);
    } else {
        for (const auto& flag : m.conflict_flags) {
            auto* lbl = new QLabel("⚠  " + flag);
            lbl->setWordWrap(true);
            lbl->setStyleSheet(
                QString("color:#ef4444;font-size:12px;"
                        "background:rgba(239,68,68,0.08);"
                        "border-left:3px solid #ef4444;"
                        "padding:6px 10px;border-radius:2px;"));
            c_flags_layout_->addWidget(lbl);
        }
        // Show conflict holdings table
        auto* ch_hdr = new QLabel("CONFLICTED HOLDINGS DETAIL");
        ch_hdr->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;letter-spacing:0.5px;"
                    "margin-top:12px;").arg(ui::colors::TEXT_TERTIARY()));
        c_flags_layout_->addWidget(ch_hdr);

        for (const auto& h : m.holdings) {
            if (!h.is_conflict) continue;
            auto* hl  = new QWidget;
            hl->setStyleSheet(
                QString("background:rgba(239,68,68,0.05);border:1px solid rgba(239,68,68,0.2);"
                        "border-radius:3px;"));
            auto* hll = new QHBoxLayout(hl);
            hll->setContentsMargins(10, 6, 10, 6);
            hll->setSpacing(8);

            auto* an = new QLabel(h.asset_name);
            an->setStyleSheet(QString("color:%1;font-size:12px;font-weight:600;")
                                  .arg(ui::colors::TEXT_PRIMARY()));
            hll->addWidget(an);
            hll->addStretch();

            auto* tk = new QLabel(h.ticker.isEmpty() ? h.asset_type : h.ticker);
            tk->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::CYAN()));
            hll->addWidget(tk);

            auto* vr = new QLabel(h.value_range_label);
            vr->setStyleSheet(QString("color:%1;font-size:12px;font-family:Consolas,monospace;")
                                  .arg(ui::colors::AMBER()));
            hll->addWidget(vr);
            c_flags_layout_->addWidget(hl);
        }
    }
}

void CabinetPanel::populate_sector_tab(const power_trader::CabinetMember& m) {
    s_header_->setText(m.full_name + "  ·  " + m.title + "  ·  Sector Allocation");

    double total = 0;
    for (const auto& se : m.sector_breakdown) total += se.total_est_amount;

    s_table_->setRowCount(m.sector_breakdown.size());
    for (int r = 0; r < m.sector_breakdown.size(); ++r) {
        const auto& se = m.sector_breakdown[r];
        s_table_->setRowHeight(r, 30);

        // Check if this sector is in the regulated domain
        bool regulated = false;
        for (const auto& reg : m.regulated_sectors) {
            if (reg.toLower().left(6).contains(se.sector.toLower().left(6)) ||
                se.sector.toLower().left(6).contains(reg.toLower().left(6)))
                regulated = true;
        }

        auto mk = [&](int col, const QString& txt, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft|Qt::AlignVCenter) {
            auto* i = new QTableWidgetItem(txt);
            i->setTextAlignment(align);
            i->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            if (regulated) i->setBackground(QColor("rgba(239,68,68,0.06)"));
            i->setFlags(i->flags() & ~Qt::ItemIsEditable);
            s_table_->setItem(r, col, i);
        };

        mk(0, se.sector, regulated ? "#ef4444" : ui::colors::TEXT_PRIMARY);
        const double v = se.total_est_amount;
        mk(1, v >= 1e6 ? "$"+QString::number(v/1e6,'f',1)+"M"
                       : "$"+QString::number(v/1e3,'f',0)+"K",
           ui::colors::TEXT_SECONDARY, Qt::AlignRight|Qt::AlignVCenter);
        mk(2, total > 0 ? QString::number(v/total*100,'f',1)+"%" : "—",
           nullptr, Qt::AlignCenter);

        auto* cf_i = new QTableWidgetItem(regulated ? "⚠ Regulated" : "—");
        cf_i->setTextAlignment(Qt::AlignCenter);
        cf_i->setForeground(QColor(regulated ? "#ef4444" : ui::colors::TEXT_TERTIARY()));
        cf_i->setFlags(cf_i->flags() & ~Qt::ItemIsEditable);
        s_table_->setItem(r, 3, cf_i);
    }
}

} // namespace fincept::screens

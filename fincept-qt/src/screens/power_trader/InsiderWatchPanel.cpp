// src/screens/power_trader/InsiderWatchPanel.cpp
#include "screens/power_trader/InsiderWatchPanel.h"

#include "ui/components/LayoutHelpers.h"
#include "ui/components/SectionHeader.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace fincept::screens {

static const char* score_color(double score) {
    if (score >= 70) return "#ef4444";   // red — high risk
    if (score >= 45) return "#f97316";   // orange — elevated
    if (score >= 25) return "#eab308";   // yellow — moderate
    return "#6b7280";                    // gray — low
}

static QString score_label(double score) {
    if (score >= 70) return QStringLiteral("HIGH SIGNAL");
    if (score >= 45) return QStringLiteral("ELEVATED");
    if (score >= 25) return QStringLiteral("MODERATE");
    return QStringLiteral("LOW");
}

InsiderWatchPanel::InsiderWatchPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void InsiderWatchPanel::build_ui() {
    setStyleSheet(QString("background:%1;color:%2;")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header / methodology note ────────────────────────────────────────────
    auto* hdr = new QWidget;
    hdr->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                           .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hl = new QVBoxLayout(hdr);
    hl->setContentsMargins(14, 6, 14, 6);
    hl->setSpacing(2);

    auto* hdr_title = new QLabel(QStringLiteral("INSIDER WATCH LIST"));
    hdr_title->setStyleSheet(
        QString("color:%1;font-size:13px;font-weight:700;letter-spacing:1px;")
            .arg(ui::colors::AMBER()));
    hl->addWidget(hdr_title);

    auto* hdr_note = new QLabel(
        QStringLiteral("Score (0–100) weights: Committee overlap (35%) · Disclosure timing (25%) · "
                        "Trade size vs peers (15%) · Coordinated cluster (15%) · Estimated alpha (10%)  "
                        "·  For educational and informational purposes only."));
    hdr_note->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    hdr_note->setWordWrap(true);
    hl->addWidget(hdr_note);
    root->addWidget(hdr);

    // ── Splitter: watch list | detail ────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setStyleSheet(QString("QSplitter::handle{background:%1;}")
                                .arg(ui::colors::BORDER_DIM()));

    // ── Watch list ────────────────────────────────────────────────────────────
    auto* left = new QWidget;
    {
        auto* ll = new QVBoxLayout(left);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(0);

        ll->addWidget(fincept::ui::make_section_header(
            QStringLiteral("RANKED BY INSIDER SCORE"), left));

        watch_table_ = new QTableWidget;
        watch_table_->setColumnCount(7);
        watch_table_->setHorizontalHeaderLabels(
            {"#", "MEMBER", "PTY", "CHB", "SCORE", "CMTE %", "TRADES"});
        fincept::ui::ensure_header_fits(watch_table_);
        watch_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        watch_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        watch_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        watch_table_->setShowGrid(false);
        watch_table_->verticalHeader()->setVisible(false);
        watch_table_->setFocusPolicy(Qt::NoFocus);

        auto* wh = watch_table_->horizontalHeader();
        wh->setSectionResizeMode(0, QHeaderView::Fixed); wh->resizeSection(0, 28);
        wh->setSectionResizeMode(1, QHeaderView::Interactive); wh->resizeSection(1, 200); wh->setStretchLastSection(true);
        wh->setSectionResizeMode(2, QHeaderView::Fixed); wh->resizeSection(2, 28);
        wh->setSectionResizeMode(3, QHeaderView::Fixed); wh->resizeSection(3, 38);
        wh->setSectionResizeMode(4, QHeaderView::Fixed); wh->resizeSection(4, 52);
        wh->setSectionResizeMode(5, QHeaderView::Fixed); wh->resizeSection(5, 52);
        wh->setSectionResizeMode(6, QHeaderView::Fixed); wh->resizeSection(6, 44);
        wh->setStyleSheet(
            QString("QHeaderView::section{background:%1;color:%2;border:none;"
                    "border-bottom:2px solid %3;border-right:1px solid %4;"
                    "padding:5px 8px;font-size:12px;font-weight:700;letter-spacing:0.5px;}")
                .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                     ui::colors::AMBER(), ui::colors::BORDER_MED()));
        watch_table_->setStyleSheet(
            QString("QTableWidget{background:%1;color:%2;border:none;font-size:12px;"
                    "font-family:Consolas,monospace;gridline-color:transparent;}"
                    "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %3;}"
                    "QTableWidget::item:selected{background:rgba(217,119,6,0.18);color:%2;}"
                    "QTableWidget::item:hover{background:%4;}"
                    "QScrollBar:vertical{width:5px;background:%1;}"
                    "QScrollBar::handle:vertical{background:%3;min-height:20px;}")
                .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                     ui::colors::BORDER_MED(), ui::colors::BG_HOVER()));

        connect(watch_table_, &QTableWidget::currentCellChanged,
                this, [this](int row, int, int, int) { on_entry_selected(row); });
        ll->addWidget(watch_table_, 1);
    }

    // ── Detail pane ───────────────────────────────────────────────────────────
    auto* right = new QScrollArea;
    right->setWidgetResizable(true);
    right->setFrameShape(QFrame::NoFrame);
    right->setStyleSheet(QString("QScrollArea{background:%1;border:none;}"
                                 "QScrollBar:vertical{width:4px;background:%1;}"
                                 "QScrollBar::handle:vertical{background:%2;}")
                             .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
    auto* detail_body = new QWidget;
    detail_body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* dl = new QVBoxLayout(detail_body);
    dl->setContentsMargins(0, 0, 0, 16);
    dl->setSpacing(0);
    right->setWidget(detail_body);

    // Name + score
    auto* name_row = new QWidget;
    name_row->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                                .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* nrl = new QHBoxLayout(name_row);
    nrl->setContentsMargins(14, 10, 14, 10);

    detail_name_ = new QLabel(QStringLiteral("Select a member"));
    detail_name_->setStyleSheet(
        QString("color:%1;font-size:13px;font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
    nrl->addWidget(detail_name_);
    nrl->addStretch();

    detail_score_ = new QLabel;
    detail_score_->setStyleSheet(
        QString("font-size:22px;font-weight:700;font-family:Consolas,monospace;"));
    nrl->addWidget(detail_score_);
    dl->addWidget(name_row);

    detail_meta_ = new QLabel;
    detail_meta_->setStyleSheet(
        QString("color:%1;font-size:12px;padding:6px 14px;"
                "background:%2;border-bottom:1px solid %3;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    detail_meta_->setWordWrap(true);
    dl->addWidget(detail_meta_);

    // Score breakdown table
    dl->addWidget(fincept::ui::make_section_header(QStringLiteral("SCORE BREAKDOWN"), detail_body));

    score_breakdown_ = new QTableWidget;
    score_breakdown_->setColumnCount(3);
    score_breakdown_->setHorizontalHeaderLabels({"FACTOR", "SCORE", "WEIGHT"});
    fincept::ui::ensure_header_fits(score_breakdown_);
    score_breakdown_->setSelectionMode(QAbstractItemView::NoSelection);
    score_breakdown_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    score_breakdown_->setShowGrid(false);
    score_breakdown_->verticalHeader()->setVisible(false);
    score_breakdown_->setFocusPolicy(Qt::NoFocus);
    score_breakdown_->setMinimumHeight(170);
    auto* bh = score_breakdown_->horizontalHeader();
    bh->setSectionResizeMode(0, QHeaderView::Interactive); bh->resizeSection(0, 200);
    bh->setSectionResizeMode(1, QHeaderView::Fixed); bh->resizeSection(1, 60);
    bh->setSectionResizeMode(2, QHeaderView::Fixed); bh->resizeSection(2, 60);
    bh->setStyleSheet(
        QString("QHeaderView::section{background:%1;color:%2;border:none;"
                "border-bottom:1px solid %3;padding:3px 6px;font-size:12px;font-weight:700;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM()));
    score_breakdown_->setStyleSheet(
        QString("QTableWidget{background:%1;color:%2;border:none;font-size:12px;}"
                "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %3;}"
                "QHeaderView::section{background:%1;}")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
    dl->addWidget(score_breakdown_);

    // Evidence bullets
    dl->addWidget(fincept::ui::make_section_header(
        QStringLiteral("EVIDENCE  ·  KEY RED FLAGS"), detail_body));

    evidence_widget_ = new QWidget;
    evidence_widget_->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    evidence_layout_ = new QVBoxLayout(evidence_widget_);
    evidence_layout_->setContentsMargins(14, 8, 14, 8);
    evidence_layout_->setSpacing(6);
    dl->addWidget(evidence_widget_);
    dl->addStretch();

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setSizes({450, 550});
    root->addWidget(splitter, 1);
}

void InsiderWatchPanel::set_data(const QVector<power_trader::InsiderWatchEntry>& entries) {
    const bool had_selection = selected_row_ >= 0;
    entries_ = entries;

    // Preserve scroll + selection across the rebuild. A periodic refresh
    // repopulates the table, resetting scroll to 0 and clearing the row.
    // Key on member_id (stashed in each item's UserRole) — row index shifts.
    const int prev_scroll = watch_table_->verticalScrollBar()->value();
    QString prev_key;
    if (auto* cur = watch_table_->currentItem())
        prev_key = cur->data(Qt::UserRole).toString();

    populate_watch_list();

    int restore_row = -1;
    if (!prev_key.isEmpty()) {
        for (int r = 0; r < entries_.size(); ++r)
            if (entries_[r].member_id == prev_key) { restore_row = r; break; }
    }

    if (restore_row >= 0) {
        on_entry_selected(restore_row);
    } else if (!entries_.isEmpty() && !had_selection) {
        on_entry_selected(0); // first load — default to top
    } else {
        selected_row_ = -1;
        watch_table_->clearSelection();
    }

    QTimer::singleShot(0, watch_table_, [this, prev_scroll]() {
        watch_table_->verticalScrollBar()->setValue(prev_scroll);
    });
}

void InsiderWatchPanel::populate_watch_list() {
    watch_table_->setRowCount(entries_.size());
    for (int r = 0; r < entries_.size(); ++r) {
        const auto& e = entries_[r];
        watch_table_->setRowHeight(r, 30);

        const char* sc = score_color(e.insider_score);

        auto mk = [&](int col, const QString& txt, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(txt);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_SECONDARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setData(Qt::UserRole, e.member_id);
            watch_table_->setItem(r, col, item);
        };

        mk(0, QString::number(r + 1), ui::colors::TEXT_TERTIARY, Qt::AlignCenter);
        mk(1, e.member_name, ui::colors::TEXT_PRIMARY);

        const char* pc = e.party == QStringLiteral("D") ? "#3b82f6"
                       : e.party == QStringLiteral("R") ? "#ef4444"
                       : ui::colors::AMBER;
        mk(2, e.party, pc, Qt::AlignCenter);

        const QString chb = e.chamber == power_trader::MemberChamber::Senate ? "SEN" : "HSE";
        mk(3, chb, ui::colors::TEXT_TERTIARY, Qt::AlignCenter);

        // Score with color
        auto* score_item = new QTableWidgetItem(
            QString::number(e.insider_score, 'f', 0));
        score_item->setTextAlignment(Qt::AlignCenter);
        score_item->setForeground(QColor(sc));
        score_item->setData(Qt::UserRole, e.member_id);
        score_item->setFlags(score_item->flags() & ~Qt::ItemIsEditable);
        watch_table_->setItem(r, 4, score_item);

        mk(5, QString::number(e.cmte_overlap_pct, 'f', 0) + "%",
           e.cmte_overlap_pct > 40 ? sc : ui::colors::TEXT_TERTIARY,
           Qt::AlignCenter);
        mk(6, QString::number(e.total_trades), nullptr, Qt::AlignCenter);
    }
}

void InsiderWatchPanel::on_entry_selected(int row) {
    if (row < 0 || row >= entries_.size()) return;
    selected_row_ = row;
    watch_table_->selectRow(row);
    show_entry(entries_[row]);
    emit member_selected(entries_[row].member_id);
}

void InsiderWatchPanel::show_entry(const power_trader::InsiderWatchEntry& e) {
    const char* sc = score_color(e.insider_score);

    detail_name_->setText(e.member_name);
    detail_score_->setStyleSheet(
        QString("font-size:22px;font-weight:700;font-family:Consolas,monospace;color:%1;")
            .arg(sc));
    detail_score_->setText(
        QString("%1  %2").arg(int(e.insider_score)).arg(score_label(e.insider_score)));

    detail_meta_->setText(
        QString("%1  ·  %2  ·  %3%4  ·  "
                "%5 of %6 trades in committee sectors (%7%)  ·  "
                "Avg lag %8d  ·  Biggest trade ~$%9%10")
            .arg(e.party == QStringLiteral("D") ? "Democrat" :
                 e.party == QStringLiteral("R") ? "Republican" : "Independent")
            .arg(e.chamber == power_trader::MemberChamber::Senate ? "Senate" : "House")
            .arg(e.state)
            .arg(e.committees.isEmpty() ? "" : "  ·  " + e.committees.first())
            .arg(e.committee_trades)
            .arg(e.total_trades)
            .arg(e.cmte_overlap_pct, 0, 'f', 0)
            .arg(e.avg_disclosure_lag, 0, 'f', 0)
            .arg(e.biggest_trade_amt >= 1e6
                 ? QString::number(e.biggest_trade_amt/1e6,'f',1) : QString::number(e.biggest_trade_amt/1e3,'f',0))
            .arg(e.biggest_trade_amt >= 1e6 ? "M" : "K"));

    // Score breakdown table
    struct BreakdownRow { QString factor; double score; QString weight; };
    const QVector<BreakdownRow> rows = {
        {"Committee Overlap",    e.cmte_overlap_score,  "35%"},
        {"Disclosure Timing",    e.timing_score,        "25%"},
        {"Trade Size vs Peers",  e.size_score,          "15%"},
        {"Coordinated Cluster",  e.pattern_score,       "15%"},
        {"Estimated Alpha",      e.return_score,        "10%"},
    };
    score_breakdown_->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        score_breakdown_->setRowHeight(r, 32);
        const auto& row_data = rows[r];

        auto* f = new QTableWidgetItem(row_data.factor);
        f->setForeground(QColor(ui::colors::TEXT_PRIMARY()));
        f->setFlags(f->flags() & ~Qt::ItemIsEditable);
        score_breakdown_->setItem(r, 0, f);

        auto* s = new QTableWidgetItem(QString::number(row_data.score, 'f', 1));
        s->setTextAlignment(Qt::AlignCenter);
        s->setForeground(QColor(score_color(row_data.score * 2)));  // scale for color
        s->setFlags(s->flags() & ~Qt::ItemIsEditable);
        score_breakdown_->setItem(r, 1, s);

        auto* w = new QTableWidgetItem(row_data.weight);
        w->setTextAlignment(Qt::AlignCenter);
        w->setForeground(QColor(ui::colors::TEXT_SECONDARY()));
        w->setFlags(w->flags() & ~Qt::ItemIsEditable);
        score_breakdown_->setItem(r, 2, w);
    }

    // Evidence bullets
    while (QLayoutItem* item = evidence_layout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto& bullet : e.evidence_bullets) {
        auto* lbl = new QLabel("▸  " + bullet);
        lbl->setWordWrap(true);
        lbl->setStyleSheet(
            QString("color:%1;font-size:12px;padding:2px 0;")
                .arg(e.insider_score >= 45 ? sc : ui::colors::TEXT_SECONDARY()));
        evidence_layout_->addWidget(lbl);
    }
}

} // namespace fincept::screens

// src/screens/pre_ipo/IpoPipelinePanel.cpp
#include "screens/pre_ipo/IpoPipelinePanel.h"

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

// ── Constructor ───────────────────────────────────────────────────────────────

IpoPipelinePanel::IpoPipelinePanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(260);
    setMaximumWidth(400);
    build_ui();
}

// ── Public API ────────────────────────────────────────────────────────────────

void IpoPipelinePanel::set_pipeline(const QVector<S1Filing>& pipeline) {
    pipeline_ = pipeline;
    rebuild_pipeline();
}

void IpoPipelinePanel::set_form_d(const QVector<FormDFiling>& filings) {
    form_d_ = filings;
    rebuild_form_d();
}

// ── Build UI ─────────────────────────────────────────────────────────────────

void IpoPipelinePanel::build_ui() {
    setObjectName("ipoPipelinePanel");
    setStyleSheet(
        QString("#ipoPipelinePanel { background:%1; border-left:1px solid %2; }")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Section: IPO PIPELINE ─────────────────────────────────────────────────
    auto* pipeline_header = new QWidget;
    pipeline_header->setFixedHeight(36);
    pipeline_header->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;")
            .arg(colors::BG_RAISED(), colors::BORDER_DIM()));
    auto* ph_layout = new QHBoxLayout(pipeline_header);
    ph_layout->setContentsMargins(10, 0, 10, 0);
    auto* ph_lbl = new QLabel("IPO PIPELINE");
    ph_lbl->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:700; letter-spacing:1.2px; background:transparent;")
            .arg(colors::AMBER()));
    ph_layout->addWidget(ph_lbl);
    ph_layout->addStretch();
    root->addWidget(pipeline_header);

    // Pipeline table
    pipeline_table_ = new QTableWidget;
    pipeline_table_->setColumnCount(3);
    pipeline_table_->setHorizontalHeaderLabels({"Company", "Status", "Window"});
    pipeline_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pipeline_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    pipeline_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pipeline_table_->setShowGrid(false);
    pipeline_table_->setAlternatingRowColors(false);
    pipeline_table_->verticalHeader()->setVisible(false);
    pipeline_table_->setFocusPolicy(Qt::NoFocus);
    pipeline_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* hdr = pipeline_table_->horizontalHeader();
    hdr->setStretchLastSection(true);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    hdr->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hdr->setMinimumSectionSize(30);

    pipeline_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:12px; gridline-color:transparent; }"
                "QTableWidget::item { padding:5px 8px; border-bottom:1px solid %3; }"
                "QTableWidget::item:selected { background:rgba(217,119,6,0.12); color:%2; }"
                "QScrollBar:vertical { width:3px; background:%1; }"
                "QScrollBar::handle:vertical { background:%3; }")
            .arg(colors::BG_SURFACE(), colors::TEXT_PRIMARY(), colors::BORDER_DIM()));

    hdr->setStyleSheet(
        QString("QHeaderView::section { background:%1; color:%2; border:none;"
                "  border-bottom:2px solid %3; padding:3px 6px; font-size:11px; font-weight:700; }")
            .arg(colors::BG_RAISED(), colors::TEXT_SECONDARY(), colors::AMBER()));

    root->addWidget(pipeline_table_);

    // ── Divider ───────────────────────────────────────────────────────────────
    auto* divider = new QWidget;
    divider->setFixedHeight(1);
    divider->setStyleSheet(QString("background:%1;").arg(colors::BORDER_DIM()));
    root->addWidget(divider);

    // ── Section: DEAL FLOW FEED ───────────────────────────────────────────────
    auto* feed_header = new QWidget;
    feed_header->setFixedHeight(36);
    feed_header->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;")
            .arg(colors::BG_RAISED(), colors::BORDER_DIM()));
    auto* fh_layout = new QHBoxLayout(feed_header);
    fh_layout->setContentsMargins(10, 0, 10, 0);
    auto* fh_lbl = new QLabel("DEAL FLOW FEED");
    fh_lbl->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:700; letter-spacing:1.2px; background:transparent;")
            .arg(colors::CYAN()));
    fh_layout->addWidget(fh_lbl);
    fh_layout->addStretch();

    auto* form_d_badge = new QLabel("Form D");
    form_d_badge->setStyleSheet(
        QString("color:%1; font-size:10px; background:rgba(6,182,212,0.18);"
                "  border:1px solid rgba(6,182,212,0.3); border-radius:3px; padding:1px 5px;")
            .arg(colors::CYAN()));
    fh_layout->addWidget(form_d_badge);
    root->addWidget(feed_header);

    // Scrollable form D cards
    feed_scroll_ = new QScrollArea;
    feed_scroll_->setWidgetResizable(true);
    feed_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    feed_scroll_->setStyleSheet(
        QString("QScrollArea { border:none; background:%1; }"
                "QScrollBar:vertical { width:4px; background:transparent; }"
                "QScrollBar::handle:vertical { background:%2; border-radius:2px; min-height:20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
            .arg(colors::BG_SURFACE(), colors::BORDER_MED()));

    feed_container_ = new QWidget;
    feed_container_->setStyleSheet(
        QString("background:%1;").arg(colors::BG_SURFACE()));
    feed_layout_ = new QVBoxLayout(feed_container_);
    feed_layout_->setContentsMargins(6, 6, 6, 6);
    feed_layout_->setSpacing(4);
    feed_layout_->addStretch();

    feed_scroll_->setWidget(feed_container_);
    root->addWidget(feed_scroll_, 1);
}

// ── Rebuild pipeline table ─────────────────────────────────────────────────

void IpoPipelinePanel::rebuild_pipeline() {
    pipeline_table_->setRowCount(0);
    pipeline_table_->setRowCount(pipeline_.size());

    for (int i = 0; i < pipeline_.size(); ++i) {
        const auto& f = pipeline_[i];

        auto make_item = [](const QString& text, const QColor& fg = {}) {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (fg.isValid())
                it->setForeground(fg);
            return it;
        };

        pipeline_table_->setItem(i, 0, make_item(f.company_name));

        // Status badge — we infer status from fields: if S-1 filed → Filed
        // (since we only store S1Filings here they are always Filed)
        auto* status_item = make_item("Filed", QColor(colors::AMBER()));
        pipeline_table_->setItem(i, 1, status_item);

        // Expected window: show year of filing + "H1/H2"
        const QString window = [&]() {
            const int y = f.filed_date.year();
            const int m = f.filed_date.month();
            return QString("%1 %2").arg(m <= 6 ? "H1" : "H2").arg(y);
        }();
        pipeline_table_->setItem(i, 2, make_item(window));
    }

    // Auto-size table height
    const int rows = pipeline_.size();
    const int row_h = 26;
    const int header_h = 26;
    pipeline_table_->setFixedHeight(header_h + rows * row_h + 4);
}

// ── Rebuild Form D feed ──────────────────────────────────────────────────────

void IpoPipelinePanel::rebuild_form_d() {
    while (feed_layout_->count() > 1) {
        auto* item = feed_layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    int inserted = 0;
    for (const auto& f : form_d_) {
        auto* card = make_form_d_card(f);
        feed_layout_->insertWidget(inserted++, card);
    }
}

QWidget* IpoPipelinePanel::make_form_d_card(const FormDFiling& f) const {
    auto* card = new QWidget;
    card->setStyleSheet(
        QString("QWidget { background:%1; border:1px solid %2; border-radius:3px; }"
                "QWidget:hover { background:%3; }")
            .arg(colors::BG_RAISED(), colors::BORDER_DIM(), colors::BG_BASE()));
    card->setFixedHeight(68);

    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(8, 6, 8, 6);
    vl->setSpacing(3);

    // Row 1: company name + exemption badge
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(4);

    auto* name_lbl = new QLabel(f.company_name);
    name_lbl->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:600; background:transparent;")
            .arg(colors::TEXT_PRIMARY()));
    row1->addWidget(name_lbl, 1);

    auto* exempt_lbl = new QLabel(f.exemption);
    exempt_lbl->setStyleSheet(
        QString("color:%1; font-size:10px; background:rgba(6,182,212,0.15);"
                "  border-radius:2px; padding:1px 4px;")
            .arg(colors::CYAN()));
    row1->addWidget(exempt_lbl);
    vl->addLayout(row1);

    // Row 2: date + amount
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(4);

    auto* date_lbl = new QLabel(f.filed_date.toString("MMM d, yyyy"));
    date_lbl->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(colors::TEXT_TERTIARY()));
    row2->addWidget(date_lbl, 1);

    const QString amt = f.amount_raised > 0
        ? "$" + QString::number(qRound(f.amount_raised)) + "M"
        : "—";
    auto* amt_lbl = new QLabel(amt);
    amt_lbl->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:600; background:transparent;")
            .arg(colors::POSITIVE()));
    row2->addWidget(amt_lbl);
    vl->addLayout(row2);

    // Row 3: state + offering type
    auto* row3 = new QHBoxLayout;
    row3->setSpacing(4);
    auto* type_lbl = new QLabel(f.offering_type + " · " + f.state);
    type_lbl->setStyleSheet(
        QString("color:%1; font-size:11px; background:transparent;").arg(colors::TEXT_SECONDARY()));
    row3->addWidget(type_lbl);
    vl->addLayout(row3);

    return card;
}

} // namespace fincept::screens

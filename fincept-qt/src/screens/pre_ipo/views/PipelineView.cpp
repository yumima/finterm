// src/screens/pre_ipo/views/PipelineView.cpp
#include "screens/pre_ipo/views/PipelineView.h"

#include "ui/theme/Theme.h"

#include <QDate>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

PipelineView::PipelineView(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void PipelineView::build_ui() {
    setObjectName("preIpoPipeline");
    setStyleSheet(QString("#preIpoPipeline{background:%1;}").arg(colors::BG_BASE()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(8);

    auto* row = new QHBoxLayout;
    auto* title = new QLabel("IPO PIPELINE");
    title->setStyleSheet(
        QString("color:%1;font-size:14px;font-weight:800;letter-spacing:1.5px;background:transparent;")
            .arg(colors::AMBER()));
    row->addWidget(title);
    auto* sub = new QLabel("S-1, S-1/A, F-1 filers from SEC EDGAR — amendment cadence signals pricing window");
    sub->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
    row->addWidget(sub);
    row->addStretch();
    count_lbl_ = new QLabel("0 filers");
    count_lbl_->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(colors::CYAN()));
    row->addWidget(count_lbl_);
    root->addLayout(row);

    table_ = new QTableWidget;
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {"Company", "First Filed", "Days Since", "Amendments", "Latest A/E", "Window"});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->verticalHeader()->setVisible(false);
    table_->setSortingEnabled(true);
    table_->setFocusPolicy(Qt::NoFocus);

    auto* hdr = table_->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 6; ++c) hdr->setSectionResizeMode(c, QHeaderView::Fixed);
    hdr->resizeSection(1, 100);
    hdr->resizeSection(2, 90);
    hdr->resizeSection(3, 100);
    hdr->resizeSection(4, 100);
    hdr->resizeSection(5, 100);

    table_->setStyleSheet(
        QString("QTableWidget{background:%1;color:%2;border:1px solid %3;"
                "  gridline-color:transparent;font-size:12px;font-family:Consolas,monospace;}"
                "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %3;}"
                "QTableWidget::item:selected{background:rgba(217,119,6,0.18);color:%2;}")
            .arg(colors::BG_SURFACE(), colors::TEXT_PRIMARY(), colors::BORDER_DIM()));
    hdr->setStyleSheet(
        QString("QHeaderView::section{background:%1;color:%2;border:none;"
                "  border-bottom:2px solid %3;padding:5px 8px;font-size:12px;font-weight:700;text-align:left;}")
            .arg(colors::BG_RAISED(), colors::TEXT_SECONDARY(), colors::AMBER()));

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* it = table_->item(row, 0);
        if (!it) return;
        const QString id = it->data(Qt::UserRole).toString();
        if (!id.isEmpty()) emit company_selected(id);
    });

    root->addWidget(table_, 1);
}

void PipelineView::set_pipeline(const QVector<S1Filing>& pipeline,
                                const QVector<PrivateCompany>& companies) {
    pipeline_  = pipeline;
    companies_ = companies;
    rebuild_table();
}

void PipelineView::rebuild_table() {
    table_->setSortingEnabled(false);
    table_->setRowCount(0);

    QHash<QString, QString> cik_to_id;
    for (const auto& c : companies_)
        if (!c.cik.isEmpty()) cik_to_id[c.cik] = c.id;

    const QDate today = QDate::currentDate();

    table_->setRowCount(pipeline_.size());
    for (int i = 0; i < pipeline_.size(); ++i) {
        const auto& s = pipeline_[i];

        auto text = [](const QString& v) {
            auto* it = new QTableWidgetItem(v);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return it;
        };
        auto numeric = [](double v, const QString& display) {
            auto* it = new QTableWidgetItem(display);
            it->setData(Qt::DisplayRole, v);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            it->setTextAlignment(Qt::AlignCenter);
            return it;
        };

        auto* name = text(s.company_name);
        name->setData(Qt::UserRole, cik_to_id.value(s.cik));
        name->setForeground(QColor(colors::CYAN()));
        table_->setItem(i, 0, name);

        table_->setItem(i, 1, text(s.filed_date.isValid() ? s.filed_date.toString("yyyy-MM-dd") : "—"));

        const int days = s.filed_date.isValid() ? static_cast<int>(s.filed_date.daysTo(today)) : 0;
        auto* d_item = numeric(days, days > 0 ? QString::number(days) + "d" : "—");
        table_->setItem(i, 2, d_item);

        auto* am = numeric(s.amendment_count, s.amendment_count > 0 ? "×" + QString::number(s.amendment_count) : "—");
        if (s.amendment_count >= 3) am->setForeground(QColor(colors::AMBER()));
        table_->setItem(i, 3, am);

        // Latest amendment vs estimate. We don't have latest_amended on S1Filing;
        // status_label is approximated downstream.
        table_->setItem(i, 4, text(s.amendment_count > 0 ? "Active" : "Initial"));

        // Window: rough — within 90 days post-first-filing is "<3 mo", else longer
        QString window = "—";
        if (s.filed_date.isValid()) {
            if (days < 60)  window = "<3 mo";
            else if (days < 120) window = "3–6 mo";
            else if (days < 365) window = "6–12 mo";
            else window = ">12 mo";
        }
        table_->setItem(i, 5, text(window));
    }
    table_->setSortingEnabled(true);
    count_lbl_->setText(QString::number(pipeline_.size()) + " filers");
}

} // namespace fincept::screens

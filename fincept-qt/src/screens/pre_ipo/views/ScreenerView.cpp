// src/screens/pre_ipo/views/ScreenerView.cpp
#include "screens/pre_ipo/views/ScreenerView.h"

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

ScreenerView::ScreenerView(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void ScreenerView::build_ui() {
    setObjectName("preIpoScreener");
    setStyleSheet(QString("#preIpoScreener{background:%1;}").arg(colors::BG_BASE()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(8);

    // ── Title + filter chips ──────────────────────────────────────────────────
    auto* title_row = new QHBoxLayout;
    title_row->setSpacing(8);
    auto* title = new QLabel("SCREENER");
    title->setStyleSheet(
        QString("color:%1;font-size:14px;font-weight:800;letter-spacing:1.5px;background:transparent;")
            .arg(colors::AMBER()));
    title_row->addWidget(title);
    auto* sub = new QLabel("Scan the universe by readiness, drift, raise, sector");
    sub->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
    title_row->addWidget(sub);
    title_row->addStretch();
    root->addLayout(title_row);

    auto* filter_row = new QHBoxLayout;
    filter_row->setSpacing(6);

    search_ = new QLineEdit;
    search_->setPlaceholderText("Search name, sector, state…");
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(280);
    search_->setStyleSheet(
        QString("QLineEdit{background:%1;color:%2;border:1px solid %3;"
                "  border-radius:3px;padding:4px 8px;font-size:12px;}"
                "QLineEdit:focus{border-color:%4;}")
            .arg(colors::BG_RAISED(), colors::TEXT_PRIMARY(),
                 colors::BORDER_DIM(), colors::AMBER()));
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) {
        search_text_ = t;
        rebuild_table();
    });
    filter_row->addWidget(search_);

    auto chip = [&](const QString& label) {
        auto* b = new QPushButton(label);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(24);
        b->setStyleSheet(
            QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                    "  border-radius:12px;padding:0 10px;font-size:12px;font-weight:600;}"
                    "QPushButton:checked{background:%3;color:%4;border-color:%3;}"
                    "QPushButton:hover:!checked{color:%5;border-color:%5;}")
                .arg(colors::TEXT_SECONDARY(), colors::BORDER_DIM(),
                     colors::AMBER(), colors::BG_BASE(), colors::TEXT_PRIMARY()));
        connect(b, &QPushButton::toggled, this, [this](bool){ rebuild_table(); });
        return b;
    };

    filter_has_marks_ = chip("Has fund marks");
    filter_filed_     = chip("S-1 filed");
    filter_ai_        = chip("AI");
    filter_fintech_   = chip("Fintech");
    filter_row->addWidget(filter_has_marks_);
    filter_row->addWidget(filter_filed_);
    filter_row->addWidget(filter_ai_);
    filter_row->addWidget(filter_fintech_);
    filter_row->addStretch();
    root->addLayout(filter_row);

    // ── Table ─────────────────────────────────────────────────────────────────
    table_ = new QTableWidget;
    table_->setColumnCount(9);
    table_->setHorizontalHeaderLabels(
        {"Company", "Sector", "HQ", "Raised", "Funds", "Mark $", "Readiness", "S-1", "Drift"});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    table_->setSortingEnabled(true);
    table_->setFocusPolicy(Qt::NoFocus);

    auto* hdr = table_->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    hdr->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    for (int c = 3; c < 9; ++c) hdr->setSectionResizeMode(c, QHeaderView::Fixed);
    hdr->resizeSection(3, 80);
    hdr->resizeSection(4, 60);
    hdr->resizeSection(5, 80);
    hdr->resizeSection(6, 90);
    hdr->resizeSection(7, 70);
    hdr->resizeSection(8, 80);

    table_->setStyleSheet(
        QString("QTableWidget{background:%1;color:%2;border:1px solid %3;"
                "  gridline-color:transparent;font-size:12px;font-family:Consolas,monospace;}"
                "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %4;}"
                "QTableWidget::item:selected{background:rgba(217,119,6,0.18);color:%2;}"
                "QScrollBar:vertical{width:6px;background:%1;}"
                "QScrollBar::handle:vertical{background:%4;}")
            .arg(colors::BG_SURFACE(), colors::TEXT_PRIMARY(),
                 colors::BORDER_DIM(), colors::BORDER_DIM()));
    hdr->setStyleSheet(
        QString("QHeaderView::section{background:%1;color:%2;border:none;"
                "  border-bottom:2px solid %3;padding:5px 8px;font-size:12px;font-weight:700;"
                "  text-align:left;}")
            .arg(colors::BG_RAISED(), colors::TEXT_SECONDARY(), colors::AMBER()));

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* it = table_->item(row, 0);
        if (!it) return;
        const QString id = it->data(Qt::UserRole).toString();
        if (!id.isEmpty()) emit company_selected(id);
    });

    root->addWidget(table_, 1);
}

bool ScreenerView::passes_filter(const PrivateCompany& c) const {
    if (!search_text_.isEmpty()) {
        const QString lo = search_text_.toLower();
        if (!c.name.toLower().contains(lo) &&
            !c.sector.toLower().contains(lo) &&
            !c.hq_state.toLower().contains(lo) &&
            !c.hq_city.toLower().contains(lo))
            return false;
    }
    if (filter_has_marks_ && filter_has_marks_->isChecked() && c.fund_marks.isEmpty())
        return false;
    if (filter_filed_ && filter_filed_->isChecked() && !c.s1.first_filed.isValid())
        return false;
    if (filter_ai_ && filter_ai_->isChecked() && !c.tags.contains("ai"))
        return false;
    if (filter_fintech_ && filter_fintech_->isChecked() && !c.tags.contains("fintech"))
        return false;
    return true;
}

void ScreenerView::set_companies(const QVector<PrivateCompany>& companies) {
    companies_ = companies;
    rebuild_table();
}

void ScreenerView::rebuild_table() {
    table_->setSortingEnabled(false);
    table_->setRowCount(0);

    int row = 0;
    for (const auto& c : companies_) {
        if (!passes_filter(c)) continue;
        table_->insertRow(row);

        auto numeric = [](double v) {
            auto* it = new QTableWidgetItem(QString::number(v, 'f', 1));
            it->setData(Qt::DisplayRole, v);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return it;
        };
        auto text_item = [](const QString& s) {
            auto* it = new QTableWidgetItem(s);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return it;
        };

        auto* name_it = text_item(c.name);
        name_it->setData(Qt::UserRole, c.id);
        name_it->setForeground(QColor(colors::CYAN()));
        table_->setItem(row, 0, name_it);
        table_->setItem(row, 1, text_item(c.sector.isEmpty() ? "—" : c.sector));
        QString hq = c.hq_city;
        if (!hq.isEmpty() && !c.hq_state.isEmpty()) hq += ", " + c.hq_state;
        else if (hq.isEmpty()) hq = c.hq_state;
        table_->setItem(row, 2, text_item(hq.isEmpty() ? "—" : hq));

        auto* raised = numeric(c.cumulative_raised_m);
        raised->setText(c.cumulative_raised_m > 0
            ? "$" + QString::number(qRound(c.cumulative_raised_m)) + "M" : "—");
        table_->setItem(row, 3, raised);

        auto* funds = numeric(static_cast<double>(c.analytics.smart_money_index));
        funds->setText(c.analytics.smart_money_index > 0
            ? QString::number(c.analytics.smart_money_index) : "—");
        funds->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 4, funds);

        auto* mark = numeric(c.analytics.consensus_mark_pps);
        mark->setText(c.analytics.consensus_mark_pps > 0
            ? "$" + QString::number(c.analytics.consensus_mark_pps, 'f', 2) : "—");
        if (c.analytics.consensus_mark_pps > 0)
            mark->setForeground(QColor(colors::AMBER()));
        table_->setItem(row, 5, mark);

        auto* readi = numeric(c.analytics.ipo_readiness_score);
        readi->setText(QString::number(c.analytics.ipo_readiness_score) + "/100");
        if (c.analytics.ipo_readiness_score >= 70)
            readi->setForeground(QColor(colors::POSITIVE()));
        else if (c.analytics.ipo_readiness_score >= 40)
            readi->setForeground(QColor(colors::AMBER()));
        table_->setItem(row, 6, readi);

        QString s1_label = "—";
        if (c.s1.first_filed.isValid())
            s1_label = c.s1.amendment_count > 0
                ? QString("A×%1").arg(c.s1.amendment_count) : "Filed";
        auto* s1 = text_item(s1_label);
        s1->setTextAlignment(Qt::AlignCenter);
        if (c.s1.first_filed.isValid()) s1->setForeground(QColor(colors::AMBER()));
        table_->setItem(row, 7, s1);

        auto* drift = numeric(c.analytics.mark_drift_vs_last_round_pct);
        if (c.analytics.mark_drift_vs_last_round_pct != 0) {
            const double v = c.analytics.mark_drift_vs_last_round_pct;
            drift->setText((v >= 0 ? "+" : "") + QString::number(v, 'f', 1) + "%");
            drift->setForeground(QColor(v >= 0 ? colors::POSITIVE() : colors::NEGATIVE()));
        } else {
            drift->setText("—");
        }
        table_->setItem(row, 8, drift);
        ++row;
    }
    table_->setSortingEnabled(true);
}

} // namespace fincept::screens

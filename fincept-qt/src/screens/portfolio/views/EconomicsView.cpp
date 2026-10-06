// src/screens/portfolio/views/EconomicsView.cpp
#include "screens/portfolio/views/EconomicsView.h"

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

EconomicsView::EconomicsView(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void EconomicsView::build_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Top: Portfolio Holdings Economics ────────────────────────────────────
    auto* ind_section = new QWidget(this);
    auto* ind_layout = new QVBoxLayout(ind_section);
    ind_layout->setContentsMargins(12, 8, 12, 8);
    ind_layout->setSpacing(4);

    auto* ind_title = new QLabel("PORTFOLIO ECONOMICS OVERVIEW");
    ind_title->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;").arg(ui::colors::AMBER()));
    ind_layout->addWidget(ind_title);

    auto* ind_note = new QLabel("Per-holding contribution to portfolio value, P&L, and risk");
    ind_note->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    ind_layout->addWidget(ind_note);

    indicators_table_ = new QTableWidget;
    indicators_table_->setColumnCount(7);
    indicators_table_->setHorizontalHeaderLabels(
        {"SYMBOL", "SECTOR", "WEIGHT", "COST BASIS", "MARKET VALUE", "P&L", "P&L %"});
    indicators_table_->setSelectionMode(QAbstractItemView::NoSelection);
    indicators_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    indicators_table_->setShowGrid(false);
    indicators_table_->verticalHeader()->setVisible(false);
    indicators_table_->horizontalHeader()->setStretchLastSection(true);
    indicators_table_->setColumnWidth(0, 80);
    indicators_table_->setColumnWidth(1, 150);
    indicators_table_->setColumnWidth(2, 70);
    indicators_table_->setColumnWidth(3, 110);
    indicators_table_->setColumnWidth(4, 110);
    indicators_table_->setColumnWidth(5, 110);
    indicators_table_->setStyleSheet(QString("QTableWidget { background:%1; color:%2; border:none; font-size:12px; }"
                                             "QTableWidget::item { padding:3px 8px; border-bottom:1px solid %3; }"
                                             "QHeaderView::section { background:%4; color:%5; border:none;"
                                             "  border-bottom:2px solid %6; padding:3px 8px; font-size:12px;"
                                             "  font-weight:700; letter-spacing:0.5px; }")
                                         .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                                              ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY(), ui::colors::AMBER()));
    ind_layout->addWidget(indicators_table_, 1);
    layout->addWidget(ind_section, 6);

    // Separator
    auto* sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QString("background:%1;").arg(ui::colors::BORDER_DIM()));
    layout->addWidget(sep);

    // ── Bottom: Portfolio Factor Sensitivity ──────────────────────────────────
    // There is no factor model behind this section. It used to show
    // "sensitivities" and currency impacts computed from a hand-typed,
    // uncited sector→beta table (several rows were scalar rescalings of
    // others) and a hardcoded ticker→sector map. Those numbers were invented,
    // so the section now says the data does not exist rather than showing
    // them. A real version needs factor betas regressed on this portfolio's
    // returns against measured macro series.
    auto* sens_section = new QWidget(this);
    auto* sens_layout = new QVBoxLayout(sens_section);
    sens_layout->setContentsMargins(12, 8, 12, 8);
    sens_layout->setSpacing(4);

    auto* sens_title = new QLabel("PORTFOLIO FACTOR SENSITIVITY");
    sens_title->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;").arg(ui::colors::AMBER()));
    sens_layout->addWidget(sens_title);

    auto* sens_unavailable = new QLabel("Unavailable \u2014 no factor model data. Macro factor sensitivities "
                                        "require betas estimated from this portfolio's return history.");
    sens_unavailable->setWordWrap(true);
    sens_unavailable->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    sens_layout->addWidget(sens_unavailable);
    sens_layout->addStretch();
    layout->addWidget(sens_section, 2);
}

void EconomicsView::set_data(const portfolio::PortfolioSummary& summary, const QString& currency) {
    summary_ = summary;
    currency_ = currency;
    update_indicators();
}

void EconomicsView::update_indicators() {
    const auto& hv = summary_.holdings;
    indicators_table_->setRowCount(hv.size());

    // Sort by market value descending
    auto sorted = hv;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.market_value > b.market_value; });

    for (int r = 0; r < sorted.size(); ++r) {
        const auto& h = sorted[r];
        indicators_table_->setRowHeight(r, 26);

        auto set = [&](int col, const QString& text, const char* color = nullptr) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(col == 0 || col == 1 ? (Qt::AlignLeft | Qt::AlignVCenter)
                                                        : (Qt::AlignRight | Qt::AlignVCenter));
            if (color)
                item->setForeground(QColor(color));
            indicators_table_->setItem(r, col, item);
        };

        const char* pnl_color = h.unrealized_pnl >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
        const char* pnl_pct_color = h.unrealized_pnl_percent >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;

        set(0, h.symbol, ui::colors::CYAN);
        // The holding's resolved sector (stored or SectorResolver), not a
        // hardcoded ticker map that labelled everything else "Other".
        set(1, h.sector.isEmpty() ? QStringLiteral("Unclassified") : h.sector, ui::colors::TEXT_SECONDARY);
        set(2, QString("%1%").arg(QString::number(h.weight, 'f', 1)), ui::colors::TEXT_PRIMARY);
        set(3, h.fx_known ? QString("%1 %2").arg(currency_).arg(QString::number(h.cost_basis, 'f', 2))
                          : ui::formatting::placeholder(),
            ui::colors::TEXT_SECONDARY);
        if (!h.valued()) {
            // No price or no FX conversion → no market value or P&L to state.
            for (int col = 4; col <= 6; ++col)
                set(col, ui::formatting::placeholder(), ui::colors::TEXT_TERTIARY);
            continue;
        }
        set(4, QString("%1 %2").arg(currency_).arg(QString::number(h.market_value, 'f', 2)), ui::colors::WARNING);
        set(5,
            QString("%1%2 %3")
                .arg(h.unrealized_pnl >= 0 ? "+" : "")
                .arg(currency_)
                .arg(QString::number(std::abs(h.unrealized_pnl), 'f', 2)),
            pnl_color);
        set(6,
            QString("%1%2%")
                .arg(h.unrealized_pnl_percent >= 0 ? "+" : "")
                .arg(QString::number(h.unrealized_pnl_percent, 'f', 2)),
            pnl_pct_color);
    }
}

} // namespace fincept::screens

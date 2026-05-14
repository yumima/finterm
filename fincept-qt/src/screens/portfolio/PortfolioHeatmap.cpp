// src/screens/portfolio/PortfolioHeatmap.cpp
#include "screens/portfolio/PortfolioHeatmap.h"

#include "core/events/EventBus.h"
#include "python/PythonWorker.h"
#include "ui/theme/Theme.h"

#include <QAction>
#include <QDateTime>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

PortfolioHeatmap::PortfolioHeatmap(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(180);
    setMaximumWidth(220);
    build_ui();
}

void PortfolioHeatmap::build_ui() {
    setStyleSheet(
        QString("background:%1; border-right:1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 6);
    layout->setSpacing(5);

    // Header: clickable "PORTFOLIO" title on its own row. Clicking it
    // deselects the current symbol and returns the right-pane chart/blotter
    // to portfolio-level view — that's the affordance the former "← PORTFOLIO"
    // back pill provided; the pane title now IS the home-view button, and
    // list items below are drill-downs. (Title is also intentionally short
    // and bold so the pane reads as a navigation column, not just a list.)
    auto* title = new QPushButton(QStringLiteral("PORTFOLIO"));
    title->setCursor(Qt::PointingHandCursor);
    title->setFlat(true);
    title->setToolTip(QStringLiteral("Click to view the full portfolio (clears any selected symbol)"));
    title->setStyleSheet(
        QString("QPushButton { color:%1; font-size:12px; font-weight:700; letter-spacing:1.5px;"
                "  background:transparent; border:0; padding:0; text-align:left; }"
                "QPushButton:hover { color:%2; }")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::AMBER()));
    connect(title, &QPushButton::clicked, this, [this]() {
        if (!selected_symbol_.isEmpty()) {
            const QString prev = selected_symbol_;
            selected_symbol_.clear();
            restyle_selection(prev, QString());  // touch only the un-highlighted block
            update_detail();
        }
        emit portfolio_view_requested();
    });
    layout->addWidget(title);

    auto* mode_row = new QHBoxLayout;
    mode_row->setSpacing(4);
    mode_row->setContentsMargins(0, 0, 0, 0);

    auto make_mode_btn = [&](const QString& text) {
        auto* btn = new QPushButton(text);
        btn->setFixedSize(38, 20);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString("QPushButton { background:transparent; color:%1; border:1px solid %2;"
                                   "  font-size:12px; font-weight:700; border-radius:2px; }"
                                   "QPushButton:checked { background:%3; color:%4; border-color:%3; }"
                                   "QPushButton:hover:!checked { color:%5; border-color:%5; }")
                               .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER(),
                                    ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));
        mode_row->addWidget(btn);
        return btn;
    };

    pnl_btn_    = make_mode_btn("PNL");
    weight_btn_ = make_mode_btn("WT");
    day_btn_    = make_mode_btn("DAY");
    aft_btn_    = make_mode_btn("AFT");
    aft_btn_->setToolTip(QStringLiteral(
        "After-hours / pre-market change for each symbol. "
        "Live from the yfinance daemon — refreshed each time you switch "
        "to AFT mode."));
    pnl_btn_->setChecked(true);

    auto set_mode = [this](portfolio::HeatmapMode m) {
        mode_ = m;
        pnl_btn_->setChecked   (m == portfolio::HeatmapMode::Pnl);
        weight_btn_->setChecked(m == portfolio::HeatmapMode::Weight);
        day_btn_->setChecked   (m == portfolio::HeatmapMode::DayChange);
        aft_btn_->setChecked   (m == portfolio::HeatmapMode::Aft);
        if (m == portfolio::HeatmapMode::Aft) {
            fetch_aft_quotes();
        } else {
            // Switching out of AFT supersedes any in-flight fetch so its
            // callback won't fire a stale block refresh while the user is
            // looking at a different mode.
            ++aft_gen_;
            refresh_block_appearances();
        }
        emit mode_changed(m);
    };
    connect(pnl_btn_,    &QPushButton::clicked, this, [=]() { set_mode(portfolio::HeatmapMode::Pnl); });
    connect(weight_btn_, &QPushButton::clicked, this, [=]() { set_mode(portfolio::HeatmapMode::Weight); });
    connect(day_btn_,    &QPushButton::clicked, this, [=]() { set_mode(portfolio::HeatmapMode::DayChange); });
    connect(aft_btn_,    &QPushButton::clicked, this, [=]() { set_mode(portfolio::HeatmapMode::Aft); });

    layout->addLayout(mode_row);

    // Scrollable blocks area
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QString("QScrollArea { border:none; background:transparent; }"
                                  "QScrollBar:vertical { width:4px; background:transparent; }"
                                  "QScrollBar::handle:vertical { background:%1; }")
                              .arg(ui::colors::BORDER_BRIGHT()));

    blocks_container_ = new QWidget(this);
    blocks_container_->setStyleSheet("background:transparent;");
    scroll->setWidget(blocks_container_);
    layout->addWidget(scroll, 1);

    // Selected holding detail
    detail_panel_ = new QWidget(this);
    detail_panel_->setStyleSheet(
        QString("background:%1; border:1px solid %2; padding:4px;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    detail_panel_->setVisible(false);

    auto* dp_layout = new QVBoxLayout(detail_panel_);
    dp_layout->setContentsMargins(6, 4, 6, 4);
    dp_layout->setSpacing(2);

    detail_symbol_ = new QLabel;
    detail_symbol_->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700;").arg(ui::colors::CYAN()));
    dp_layout->addWidget(detail_symbol_);

    auto add_detail_row = [&](QLabel*& lbl, const QString& prefix) {
        auto* row = new QHBoxLayout;
        auto* lab = new QLabel(prefix);
        lab->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;").arg(ui::colors::TEXT_SECONDARY()));
        row->addWidget(lab);
        lbl = new QLabel("--");
        lbl->setAlignment(Qt::AlignRight);
        lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
        row->addWidget(lbl);
        dp_layout->addLayout(row);
    };

    add_detail_row(detail_price_, "PRICE");
    add_detail_row(detail_change_, "CHG%");
    add_detail_row(detail_qty_, "QTY");
    add_detail_row(detail_cost_, "COST");
    add_detail_row(detail_mv_, "MKT VAL");
    add_detail_row(detail_pnl_, "P&L");
    add_detail_row(detail_pnl_pct_, "P&L%");
    add_detail_row(detail_weight_, "WEIGHT");

    layout->addWidget(detail_panel_);

    // Portfolio-level detail (shown when no symbol is selected)
    portfolio_panel_ = new QWidget(this);
    portfolio_panel_->setStyleSheet(
        QString("background:%1; border:1px solid %2; padding:4px;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    portfolio_panel_->setVisible(false);

    auto* pp_layout = new QVBoxLayout(portfolio_panel_);
    pp_layout->setContentsMargins(6, 4, 6, 4);
    pp_layout->setSpacing(2);

    pfund_name_ = new QLabel("PORTFOLIO");
    pfund_name_->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700;").arg(ui::colors::AMBER()));
    pp_layout->addWidget(pfund_name_);

    auto add_pfund_row = [&](QLabel*& lbl, const QString& prefix) {
        auto* row = new QHBoxLayout;
        auto* lab = new QLabel(prefix);
        lab->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;").arg(ui::colors::TEXT_SECONDARY()));
        row->addWidget(lab);
        lbl = new QLabel("--");
        lbl->setAlignment(Qt::AlignRight);
        lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
        row->addWidget(lbl);
        pp_layout->addLayout(row);
    };

    add_pfund_row(pfund_tgt_low_,   "TGT LOW");
    add_pfund_row(pfund_tgt_mean_,  "TGT MEAN");
    add_pfund_row(pfund_tgt_high_,  "TGT HIGH");
    add_pfund_row(pfund_consensus_, "CONSENSUS");
    add_pfund_row(pfund_pe_,        "P/E");
    add_pfund_row(pfund_yield_,     "YIELD");
    add_pfund_row(pfund_beta_,      "BETA");
    add_pfund_row(pfund_breadth_,   "BREADTH");

    layout->addWidget(portfolio_panel_);

    // Risk gauge
    auto* risk_header = new QLabel("RISK SCORE");
    risk_header->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;").arg(ui::colors::TEXT_SECONDARY()));
    layout->addWidget(risk_header);

    risk_bar_ = new QWidget(this);
    risk_bar_->setFixedHeight(10);
    risk_bar_->setStyleSheet(QString("background:%1; border-radius:2px;").arg(ui::colors::BG_BASE()));
    layout->addWidget(risk_bar_);

    risk_value_ = new QLabel("--");
    risk_value_->setAlignment(Qt::AlignCenter);
    risk_value_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_SECONDARY()));
    layout->addWidget(risk_value_);

    // Top movers
    auto* movers_header = new QLabel("TOP MOVERS");
    movers_header->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px;").arg(ui::colors::TEXT_SECONDARY()));
    layout->addWidget(movers_header);

    top_gainer_ = new QLabel;
    top_gainer_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;").arg(ui::colors::POSITIVE()));
    layout->addWidget(top_gainer_);

    top_loser_ = new QLabel;
    top_loser_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;").arg(ui::colors::NEGATIVE()));
    layout->addWidget(top_loser_);

    // Quick stats
    auto add_stat = [&](QLabel*& lbl, const QString& prefix) {
        auto* row = new QHBoxLayout;
        auto* lab = new QLabel(prefix);
        lab->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;").arg(ui::colors::TEXT_SECONDARY()));
        row->addWidget(lab);
        lbl = new QLabel("--");
        lbl->setAlignment(Qt::AlignRight);
        lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_SECONDARY()));
        row->addWidget(lbl);
        layout->addLayout(row);
    };

    add_stat(stat_holdings_, "HOLDINGS");
    add_stat(stat_conc_, "CONC. TOP3");
    add_stat(stat_vol_, "VOL 30D");
}

void PortfolioHeatmap::set_holdings(const QVector<portfolio::HoldingWithQuote>& holdings) {
    // Detect whether the symbol set + order matches the existing layout. If
    // it does (the common case — same portfolio, just refreshed quotes), we
    // skip the layout dance entirely and just update text + color on the
    // existing widgets. Full rebuild only on actual structural change
    // (position added, removed, or re-sorted).
    bool same_layout = (holdings.size() == holdings_.size());
    if (same_layout) {
        for (int i = 0; i < holdings.size(); ++i) {
            if (holdings[i].symbol != holdings_[i].symbol) {
                same_layout = false;
                break;
            }
        }
    }

    holdings_ = holdings;
    if (same_layout)
        refresh_block_appearances();
    else
        rebuild_blocks();
    update_detail();
    update_top_movers();
    stat_holdings_->setText(QString::number(holdings.size()));
}

void PortfolioHeatmap::set_metrics(const portfolio::ComputedMetrics& metrics) {
    metrics_ = metrics;
    update_risk_gauge();

    stat_conc_->setText(metrics.concentration_top3.has_value()
                            ? QString("%1%").arg(QString::number(*metrics.concentration_top3, 'f', 1))
                            : "--");
    stat_vol_->setText(metrics.volatility.has_value() ? QString("%1%").arg(QString::number(*metrics.volatility, 'f', 1))
                                                      : "--");
}

void PortfolioHeatmap::set_selected_symbol(const QString& symbol) {
    if (selected_symbol_ == symbol)
        return;
    const QString prev = selected_symbol_;
    selected_symbol_ = symbol;
    restyle_selection(prev, symbol);
    update_detail();
}

void PortfolioHeatmap::set_currency(const QString& currency) {
    currency_ = currency;
}

QColor PortfolioHeatmap::block_color(const portfolio::HoldingWithQuote& h) const {
    double val = 0;
    switch (mode_) {
        case portfolio::HeatmapMode::Pnl:
            val = h.unrealized_pnl_percent;
            break;
        case portfolio::HeatmapMode::Weight:
            val = h.weight;
            break;
        case portfolio::HeatmapMode::DayChange:
            val = h.day_change_percent;
            break;
        case portfolio::HeatmapMode::Aft: {
            // Look up after-hours percent change from the local cache. If
            // the yfinance fetch hasn't returned yet, or this symbol has no
            // extended quote, render a neutral gray — NOT a 0-value green,
            // which would falsely flag the symbol as flat-but-quoted.
            auto it = aft_quotes_.find(h.symbol);
            if (it == aft_quotes_.end())
                return QColor(45, 45, 48);
            val = it.value();
            break;
        }
    }

    if (mode_ == portfolio::HeatmapMode::Weight) {
        // Amber: darker base, brighter at higher weights
        double t = std::min(val / 40.0, 1.0);
        int r = static_cast<int>(100 + t * 117); // 100 → 217
        int g = static_cast<int>(50 + t * 69);   // 50  → 119
        int b = static_cast<int>(6);
        return QColor(r, g, b);
    }

    // Green/Red: solid colors, brightness scales with magnitude
    double intensity = std::min(std::abs(val) / 20.0, 1.0); // saturates at ±20%
    if (val >= 0) {
        // Dark green → bright green
        int g = static_cast<int>(80 + intensity * 123); // 80 → 203
        int r = static_cast<int>(intensity * 22);
        return QColor(r, g, static_cast<int>(intensity * 30));
    } else {
        // Dark red → bright red
        int r = static_cast<int>(100 + intensity * 120); // 100 → 220
        return QColor(r, static_cast<int>(intensity * 20), static_cast<int>(intensity * 20));
    }
}

QPushButton* PortfolioHeatmap::create_block_widget(const QString& symbol) {
    auto* block = new QPushButton(blocks_container_);
    // Uniform tile height — weight is already shown as a column in the
    // PortfolioBlotter table and in the Position Detail panel, so we
    // don't need to encode it here as well. Variable heights also caused
    // the apparent grid-gap inconsistency (a QGridLayout row's height is
    // the tallest tile, leaving extra space below shorter neighbours).
    // 40px fits two lines (symbol + Δ%) at 10px font with comfortable
    // breathing room.
    block->setFixedHeight(40);
    block->setCursor(Qt::PointingHandCursor);

    connect(block, &QPushButton::clicked, this, [this, symbol]() {
        if (selected_symbol_ != symbol) {
            const QString prev = selected_symbol_;
            selected_symbol_ = symbol;
            restyle_selection(prev, symbol);
        }
        // Refresh the detail pane every click (preserves prior behaviour
        // where same-symbol clicks still updated detail from fresh data)
        // and re-emit so PortfolioScreen broadcasts to the linked group.
        update_detail();
        emit symbol_selected(symbol);
    });

    // Right-click context menu — mirrors PortfolioBlotter so the user
    // can take the same actions from this sidebar (especially useful in
    // a side-by-side layout where the blotter is hidden behind another
    // pane). "Open in Equity Research" uses split_alongside so ER opens
    // beside Portfolio rather than replacing it.
    block->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(block, &QWidget::customContextMenuRequested, this,
            [this, btn = QPointer<QPushButton>(block), symbol](const QPoint& pos) {
        if (!btn) return;
        // Parent menu to the heatmap widget, NOT the block button.  Parenting
        // to btn means Qt calls delete on the stack-allocated menu when btn is
        // destroyed via deleteLater() inside the nested event loop opened by
        // exec() — that double-frees a stack address → SIGABRT.
        QMenu menu(this);
        // Solid background — without this the menu inherits a transparent
        // style from the heatmap block (which is itself styled with rgba
        // colors), making the popup hard to read against the panels
        // behind it.
        menu.setStyleSheet(QString(
            "QMenu { background:%1; color:%2; border:1px solid %3; padding:4px; }"
            "QMenu::item { padding:6px 20px 6px 12px; font-size:12px; }"
            "QMenu::item:selected { background:%4; color:%5; }"
            "QMenu::item:disabled { color:%6; }"
            "QMenu::item[data='danger'] { color:%7; }"
            "QMenu::separator { height:1px; background:%3; margin:3px 0; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED(),
                 ui::colors::AMBER_DIM(), ui::colors::AMBER(), ui::colors::AMBER(),
                 ui::colors::NEGATIVE()));

        auto* sym_label = menu.addAction(symbol);
        sym_label->setEnabled(false);
        sym_label->setFont([] {
            QFont f;
            f.setBold(true);
            f.setPointSize(10);
            return f;
        }());
        menu.addSeparator();

        auto* research_act = menu.addAction("Open in Equity Research");
        menu.addSeparator();
        auto* edit_act = menu.addAction("Edit Transaction");
        auto* delete_act = menu.addAction("Close / Delete Position");
        delete_act->setData("danger");

        connect(research_act, &QAction::triggered, this, [symbol]() {
            EventBus::instance().publish("nav.split_alongside",
                                         QVariantMap{{"screen_id", "equity_research"}});
            EventBus::instance().publish("equity_research.load_symbol",
                                         QVariantMap{{"symbol", symbol}});
        });
        connect(edit_act, &QAction::triggered, this,
                [this, symbol]() { emit edit_transaction_requested(symbol); });
        connect(delete_act, &QAction::triggered, this,
                [this, symbol]() { emit delete_position_requested(symbol); });

        menu.exec(btn->mapToGlobal(pos));
    });

    return block;
}

void PortfolioHeatmap::update_block_appearance(QPushButton* block, const portfolio::HoldingWithQuote& h) {
    const bool selected = (h.symbol == selected_symbol_);
    const QColor bg = block_color(h);
    const QString border_style = selected ? QString("border:2px solid %1;").arg(ui::colors::AMBER())
                                          : QString("border:1px solid %1;").arg(ui::colors::BORDER_DIM());

    block->setStyleSheet(QString("QPushButton { background:rgb(%1,%2,%3); %4"
                                 "  text-align:left; padding:4px 6px; border-radius:2px;"
                                 "  color:%6; font-size:12px; font-weight:700; }"
                                 "QPushButton:hover { border-color:%5; }")
                             .arg(bg.red())
                             .arg(bg.green())
                             .arg(bg.blue())
                             .arg(border_style, ui::colors::AMBER(), ui::colors::TEXT_PRIMARY()));

    // Content. Each mode needs its own value source — falling through to
    // `weight` for Aft (the prior bug) made the tile display "+12.3%"
    // (weight, always positive prefix) on a color computed from the
    // after-hours change. We now read from aft_quotes_ for Aft and render
    // "—" when the daemon hasn't returned a quote for that symbol.
    QString chg_str;
    switch (mode_) {
        case portfolio::HeatmapMode::DayChange: {
            const double v = h.day_change_percent;
            chg_str = QString("%1%2%").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', 1));
            break;
        }
        case portfolio::HeatmapMode::Pnl: {
            const double v = h.unrealized_pnl_percent;
            chg_str = QString("%1%2%").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', 1));
            break;
        }
        case portfolio::HeatmapMode::Weight:
            chg_str = QString("%1%").arg(QString::number(h.weight, 'f', 1));
            break;
        case portfolio::HeatmapMode::Aft: {
            const auto it = aft_quotes_.find(h.symbol);
            if (it == aft_quotes_.end()) {
                chg_str = QStringLiteral("—");  // matches gray tile from block_color
            } else {
                const double v = it.value();
                chg_str = QString("%1%2%").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', 1));
            }
            break;
        }
    }

    block->setText(QString("%1\n%2").arg(h.symbol, chg_str));
}

void PortfolioHeatmap::refresh_block_appearances() {
    for (const auto& h : holdings_) {
        const auto it = block_widgets_.constFind(h.symbol);
        if (it != block_widgets_.constEnd() && it.value())
            update_block_appearance(it.value(), h);
    }
}

void PortfolioHeatmap::restyle_selection(const QString& prev, const QString& next) {
    if (prev == next)
        return;
    auto restyle_one = [this](const QString& sym) {
        if (sym.isEmpty()) return;
        const auto it = block_widgets_.constFind(sym);
        if (it == block_widgets_.constEnd() || !it.value()) return;
        for (const auto& h : holdings_) {
            if (h.symbol == sym) {
                update_block_appearance(it.value(), h);
                return;
            }
        }
    };
    restyle_one(prev);
    restyle_one(next);
}

void PortfolioHeatmap::rebuild_blocks() {
    // Take widgets out of the old layout without destroying them — they'll
    // be re-added in the new order. Only widgets whose symbols no longer
    // appear in `holdings_` are deleted at the end.
    if (auto* old_layout = blocks_container_->layout()) {
        QLayoutItem* item;
        while ((item = old_layout->takeAt(0)) != nullptr)
            delete item;  // owns only the QLayoutItem wrapper, not the widget
        delete old_layout;
    }

    auto* grid = new QGridLayout(blocks_container_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);

    QSet<QString> current_syms;
    int col = 0, row = 0;
    for (const auto& h : holdings_) {
        current_syms.insert(h.symbol);
        auto it = block_widgets_.find(h.symbol);
        QPushButton* block = (it != block_widgets_.end()) ? it.value() : nullptr;
        if (!block) {
            block = create_block_widget(h.symbol);
            block_widgets_.insert(h.symbol, block);
        }
        update_block_appearance(block, h);
        grid->addWidget(block, row, col);
        col++;
        if (col >= 2) {
            col = 0;
            row++;
        }
    }

    // Drop orphaned blocks (positions that were removed from the portfolio).
    for (auto it = block_widgets_.begin(); it != block_widgets_.end(); ) {
        if (!current_syms.contains(it.key())) {
            if (it.value())
                it.value()->deleteLater();
            it = block_widgets_.erase(it);
        } else {
            ++it;
        }
    }

    grid->setRowStretch(row + 1, 1); // push blocks up
}

void PortfolioHeatmap::update_detail() {
    const portfolio::HoldingWithQuote* found = nullptr;
    for (const auto& h : holdings_) {
        if (h.symbol == selected_symbol_) {
            found = &h;
            break;
        }
    }

    if (!found) {
        detail_panel_->setVisible(false);
        update_portfolio_detail();
        return;
    }

    portfolio_panel_->setVisible(false);
    detail_panel_->setVisible(true);
    const auto& h = *found;
    auto fmt = [](double v, int dp = 2) { return QString::number(v, 'f', dp); };
    auto color = [](double v) -> const char* { return v >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE; };

    detail_symbol_->setText(h.symbol);
    detail_price_->setText(fmt(h.current_price));
    detail_change_->setText(QString("%1%2%").arg(h.day_change_percent >= 0 ? "+" : "").arg(fmt(h.day_change_percent)));
    detail_change_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:600;").arg(color(h.day_change_percent)));
    detail_qty_->setText(fmt(h.quantity, h.quantity == std::floor(h.quantity) ? 0 : 2));
    detail_cost_->setText(fmt(h.avg_buy_price));
    detail_mv_->setText(fmt(h.market_value));
    detail_mv_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;").arg(ui::colors::WARNING()));
    detail_pnl_->setText(QString("%1%2").arg(h.unrealized_pnl >= 0 ? "+" : "").arg(fmt(h.unrealized_pnl)));
    detail_pnl_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;").arg(color(h.unrealized_pnl)));
    detail_pnl_pct_->setText(
        QString("%1%2%").arg(h.unrealized_pnl_percent >= 0 ? "+" : "").arg(fmt(h.unrealized_pnl_percent)));
    detail_pnl_pct_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:600;").arg(color(h.unrealized_pnl_percent)));
    detail_weight_->setText(QString("%1%").arg(fmt(h.weight, 1)));
}

void PortfolioHeatmap::update_portfolio_detail() {
    portfolio_panel_->setVisible(true);

    auto fmt_nav = [](double v) -> QString {
        if (v <= 0) return QStringLiteral("--");
        if (v >= 1e9) return QString("$%1B").arg(QString::number(v / 1e9, 'f', 2));
        if (v >= 1e6) return QString("$%1M").arg(QString::number(v / 1e6, 'f', 2));
        if (v >= 1e3) return QString("$%1K").arg(QString::number(v / 1e3, 'f', 1));
        return QString("$%1").arg(QString::number(v, 'f', 2));
    };

    const auto& f = fundamentals_;

    if (f.has_analyst_data) {
        pfund_tgt_low_->setText(fmt_nav(f.tgt_low));
        pfund_tgt_low_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_SECONDARY()));

        // Mean target with upside % relative to current MV.
        double cur_mv = 0;
        for (const auto& h : holdings_) cur_mv += h.market_value;
        if (f.tgt_mean > 0 && cur_mv > 0) {
            const double upside = (f.tgt_mean - cur_mv) / cur_mv * 100.0;
            const char* up_col = upside >= 0 ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
            pfund_tgt_mean_->setText(
                QString("%1 <span style='color:%2; font-size:11px;'>%3%4%</span>")
                    .arg(fmt_nav(f.tgt_mean), up_col,
                         upside >= 0 ? "+" : "",
                         QString::number(upside, 'f', 1)));
        } else {
            pfund_tgt_mean_->setText(fmt_nav(f.tgt_mean));
        }
        pfund_tgt_mean_->setTextFormat(Qt::RichText);
        pfund_tgt_mean_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::WARNING()));

        pfund_tgt_high_->setText(fmt_nav(f.tgt_high));
        pfund_tgt_high_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::POSITIVE()));
    } else {
        for (auto* lbl : {pfund_tgt_low_, pfund_tgt_mean_, pfund_tgt_high_})
            lbl->setText(QStringLiteral("--"));
    }

    // Consensus — color-coded by sentiment.
    if (!f.consensus.isEmpty()) {
        const char* c_col = (f.consensus == "Strong Buy" || f.consensus == "Buy") ? ui::colors::POSITIVE
                          : (f.consensus == "Sell" || f.consensus == "Strong Sell") ? ui::colors::NEGATIVE
                          : ui::colors::WARNING;
        pfund_consensus_->setText(f.consensus);
        pfund_consensus_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:700;").arg(c_col));
    } else {
        pfund_consensus_->setText(QStringLiteral("--"));
        pfund_consensus_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
    }

    // P/E
    pfund_pe_->setText(f.pe_ratio > 0 ? QString("%1×").arg(QString::number(f.pe_ratio, 'f', 1)) : "--");
    pfund_pe_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::CYAN()));

    // Dividend yield as %
    pfund_yield_->setText(f.div_yield > 0 ? QString("%1%").arg(QString::number(f.div_yield * 100.0, 'f', 2)) : "--");
    pfund_yield_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::CYAN()));

    // Beta from already-computed metrics
    if (metrics_.beta.has_value()) {
        const double b = *metrics_.beta;
        const char* b_col = b > 1.2 ? ui::colors::NEGATIVE : b < 0.8 ? ui::colors::POSITIVE : ui::colors::WARNING;
        pfund_beta_->setText(QString::number(b, 'f', 2));
        pfund_beta_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:700;").arg(b_col));
    } else {
        pfund_beta_->setText(QStringLiteral("--"));
        pfund_beta_->setStyleSheet(
            QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
    }

    // Breadth — count gainers / losers from existing holdings data.
    int gainers = 0, losers = 0;
    for (const auto& h : holdings_) {
        if      (h.day_change_percent > 0) ++gainers;
        else if (h.day_change_percent < 0) ++losers;
    }
    pfund_breadth_->setText(
        QString("▲ %1  ▼ %2").arg(gainers).arg(losers));
    pfund_breadth_->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
}

void PortfolioHeatmap::set_portfolio_fundamentals(const QString& /*portfolio_id*/,
                                                   const portfolio::PortfolioFundamentals& f) {
    fundamentals_ = f;
    // Only repopulate if the portfolio panel is currently visible (no stock selected).
    if (selected_symbol_.isEmpty())
        update_portfolio_detail();
}

void PortfolioHeatmap::update_risk_gauge() {
    double rs = metrics_.risk_score.value_or(0);
    const char* rs_color = rs < 30 ? ui::colors::POSITIVE : rs < 60 ? ui::colors::WARNING : ui::colors::NEGATIVE;

    // Use a colored inner bar proportional to score
    double pct = std::min(rs / 100.0, 1.0);
    risk_bar_->setStyleSheet(
        QString("background: qlineargradient(x1:0, x2:1, stop:0 %1, stop:%2 %1, stop:%3 transparent);")
            .arg(rs_color)
            .arg(pct)
            .arg(std::min(pct + 0.01, 1.0)));

    risk_value_->setText(QString::number(rs, 'f', 0));
    risk_value_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;").arg(rs_color));
}

void PortfolioHeatmap::update_top_movers() {
    if (holdings_.isEmpty()) {
        top_gainer_->setText("--");
        top_loser_->setText("--");
        return;
    }

    auto best = std::max_element(holdings_.begin(), holdings_.end(), [](const auto& a, const auto& b) {
        return a.day_change_percent < b.day_change_percent;
    });
    auto worst = std::min_element(holdings_.begin(), holdings_.end(), [](const auto& a, const auto& b) {
        return a.day_change_percent < b.day_change_percent;
    });

    top_gainer_->setText(QString("\u25B2 %1  %2%3%")
                             .arg(best->symbol)
                             .arg(best->day_change_percent >= 0 ? "+" : "")
                             .arg(QString::number(best->day_change_percent, 'f', 2)));

    top_loser_->setText(QString("\u25BC %1  %2%3%")
                            .arg(worst->symbol)
                            .arg(worst->day_change_percent >= 0 ? "+" : "")
                            .arg(QString::number(worst->day_change_percent, 'f', 2)));
}

void PortfolioHeatmap::refresh_theme() {
    setStyleSheet(
        QString("background:%1; border-right:1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));

    detail_panel_->setStyleSheet(
        QString("background:%1; border:1px solid %2; padding:4px;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));

    // Only block appearance depends on theme colors — refresh in place
    // rather than rebuilding the grid + recreating QPushButton widgets.
    refresh_block_appearances();
}

// Fetch after-hours / pre-market percent change for each held symbol via the
// persistent yfinance daemon. Same `extended_hours` action used by
// PortfolioFuturesView::refresh_extended_hours — the daemon is already
// running and warm, so this is a cheap network call rather than a fresh
// Python subprocess. Generation counter supersedes any in-flight stale
// requests when the user toggles AFT off and back on quickly.
void PortfolioHeatmap::fetch_aft_quotes() {
    if (holdings_.isEmpty()) {
        aft_quotes_.clear();
        refresh_block_appearances();
        return;
    }

    QJsonArray syms;
    for (const auto& h : holdings_) syms.append(h.symbol);

    const quint64 my_gen = ++aft_gen_;
    QPointer<PortfolioHeatmap> guard(this);

    // Wipe stale quotes immediately so blocks render gray ("loading") until
    // the fresh fetch returns; without this the user briefly sees the
    // previous AFT session's colors against a different symbol set.
    aft_quotes_.clear();
    refresh_block_appearances();

    QJsonObject payload;
    payload.insert(QStringLiteral("symbols"), syms);

    python::PythonWorker::instance().submit(
        QStringLiteral("extended_hours"), payload,
        [guard, my_gen](bool ok, QJsonObject result, QString /*err*/) {
            if (!guard || my_gen != guard->aft_gen_) return;  // superseded or destroyed
            if (!ok) return;
            const QJsonArray rows = result.contains(QStringLiteral("_value"))
                                        ? result.value(QStringLiteral("_value")).toArray()
                                        : result.value(QStringLiteral("data")).toArray();
            guard->aft_quotes_.clear();
            for (const auto& v : rows) {
                const auto o = v.toObject();
                const auto pct_val = o.value(QStringLiteral("ext_change_pct"));
                if (pct_val.isNull() || pct_val.isUndefined()) continue;
                guard->aft_quotes_.insert(
                    o.value(QStringLiteral("symbol")).toString(),
                    pct_val.toDouble());
            }
            guard->refresh_block_appearances();
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

} // namespace fincept::screens

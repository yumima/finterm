// src/screens/power_trader/SignalBuilderPanel.cpp
#include "screens/power_trader/SignalBuilderPanel.h"
#include "screens/power_trader/PowerTraderService.h"

#include "ui/theme/Theme.h"
#include "ui/components/LayoutHelpers.h"
#include "core/config/AppIdentity.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens {

using power_trader::SignalPreset;
using power_trader::TradeFactorScores;
using power_trader::PowerTraderService;

// ── Style helpers ──────────────────────────────────────────────────────────────

static QString section_hdr_ss() {
    return QString("background:%1;color:%2;font-size:12px;font-weight:700;"
                   "letter-spacing:0.5px;padding:4px 10px;border-bottom:1px solid %3;")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED());
}

static QString table_ss() {
    return QString("QTableWidget{background:%1;color:%2;border:none;font-size:12px;"
                   "font-family:Consolas,monospace;gridline-color:transparent;}"
                   "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %3;}"
                   "QTableWidget::item:selected{background:rgba(217,119,6,0.18);color:%2;}"
                   "QTableWidget::item:hover{background:%4;}"
                   "QScrollBar:vertical{width:8px;background:%1;border-radius:4px;}"
                   "QScrollBar::handle:vertical{background:%3;min-height:24px;border-radius:4px;}")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_MED(), ui::colors::BG_HOVER());
}

static QString hdr_ss() {
    return QString("QHeaderView::section{background:%1;color:%2;border:none;"
                   "border-bottom:2px solid %3;border-right:1px solid %4;"
                   "padding:5px 8px;font-size:12px;font-weight:700;}")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::AMBER(), ui::colors::BORDER_MED());
}

static const char* score_color(double s) {
    if (s >= 70) return "#ef4444";
    if (s >= 45) return "#f97316";
    if (s >= 25) return "#eab308";
    return "#6b7280";
}

// ── Constructor ────────────────────────────────────────────────────────────────

SignalBuilderPanel::SignalBuilderPanel(QWidget* parent) : QWidget(parent) {
    // Define the 8 factors
    factors_ = {
        {"committee", "Committee Overlap",
         "Trade falls within the sector regulated by the member's committee.", false},
        {"size",      "Trade Size",
         "Dollar amount disclosed — larger trades signal higher conviction.", false},
        {"lag",       "Disclosure Timing",
         "Days between trade and filing. Very short = high conviction. Over 45d = violation flag.", false},
        {"herd",      "Herd Signal",
         "2+ members trading same ticker within 14 days = coordinated signal.", false},
        {"timing",    "Legislative Timing",
         "Trade within 14 days of a committee markup, vote, or floor vote.", true},
        {"bill",      "Bill Correlation",
         "Member sponsored or voted on legislation affecting the company.", true},
        {"lobbying",  "Lobbying Signal",
         "Company's lobbyists contacted the member's committee (LDA data).", true},
        {"history",   "Historical Alpha",
         "Member's track record of profitable committee-adjacent trades.", false},
    };

    presets_ = PowerTraderService::builtin_presets();
    load_user_presets();
    build_ui();
}

// ── UI construction ────────────────────────────────────────────────────────────

void SignalBuilderPanel::build_ui() {
    setStyleSheet(QString("background:%1;color:%2;")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header ────────────────────────────────────────────────────────────────
    auto* hdr = new QWidget;
    hdr->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                           .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hl = new QVBoxLayout(hdr);
    hl->setContentsMargins(14, 8, 14, 8);
    hl->setSpacing(3);

    auto* title = new QLabel("SIGNAL BUILDER");
    title->setStyleSheet(
        QString("color:%1;font-size:14px;font-weight:700;letter-spacing:1px;")
            .arg(ui::colors::AMBER()));
    hl->addWidget(title);

    formula_lbl_ = new QLabel(
        "Composite Score = weighted average of 8 factors (0–100 each). "
        "Adjust weights to match your strategy. "
        "Gray factors require additional data integration.");
    formula_lbl_->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    formula_lbl_->setWordWrap(true);
    hl->addWidget(formula_lbl_);
    root->addWidget(hdr);

    // ── Main splitter: factors LEFT | preview RIGHT ───────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->setStyleSheet(
        QString("QSplitter::handle{background:%1;}").arg(ui::colors::BORDER_DIM()));

    // ── LEFT: factor sliders ──────────────────────────────────────────────────
    factor_pane_ = new QWidget(splitter);
    auto* left = factor_pane_;
    left->setMinimumWidth(280);
    left->setMaximumWidth(400);
    left->setStyleSheet(
        QString("QWidget{background:%1;border-right:1px solid %2;}")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
    build_factor_panel(left);

    // ── RIGHT: preview table + breakdown ─────────────────────────────────────
    auto* right = new QWidget(splitter);
    right->setStyleSheet(
        QString("QWidget{background:%1;}").arg(ui::colors::BG_BASE()));
    {
        auto* rl = new QVBoxLayout(right);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);

        // Preview table header
        auto* ph = new QLabel("LIVE PREVIEW  ·  Trades re-scored with your weights (top 50)");
        ph->setStyleSheet(section_hdr_ss());
        rl->addWidget(ph);

        preview_table_ = new QTableWidget;
        preview_table_->setColumnCount(8);
        preview_table_->setHorizontalHeaderLabels(
            {"SCORE", "MEMBER", "PTY", "TICKER", "B/S", "AMOUNT", "CMTE", "LAG"});
        fincept::ui::ensure_header_fits(preview_table_);
        preview_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        preview_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        preview_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        preview_table_->setShowGrid(false);
        preview_table_->verticalHeader()->setVisible(false);
        preview_table_->setFocusPolicy(Qt::NoFocus);

        auto* th = preview_table_->horizontalHeader();
        th->setSectionResizeMode(QHeaderView::Stretch);
        th->setSectionResizeMode(0, QHeaderView::Fixed); th->resizeSection(0, 58);
        th->setSectionResizeMode(2, QHeaderView::Fixed); th->resizeSection(2, 32);
        th->setSectionResizeMode(3, QHeaderView::Fixed); th->resizeSection(3, 60);
        th->setSectionResizeMode(4, QHeaderView::Fixed); th->resizeSection(4, 44);
        th->setSectionResizeMode(7, QHeaderView::Fixed); th->resizeSection(7, 40);
        th->setStyleSheet(hdr_ss());
        preview_table_->setStyleSheet(table_ss());

        connect(preview_table_, &QTableWidget::currentCellChanged,
                this, [this](int row, int, int, int) { on_trade_selected(row); });
        connect(preview_table_, &QTableWidget::cellClicked,
                this, [this](int row, int) {
                    auto* item = preview_table_->item(row, 1);
                    if (item) emit member_selected(item->data(Qt::UserRole).toString());
                });

        rl->addWidget(preview_table_, 3);

        // ── Factor breakdown panel ────────────────────────────────────────────
        breakdown_panel_ = new QWidget(right);
        breakdown_panel_->setStyleSheet(
            QString("QWidget{background:%1;border-top:1px solid %2;}")
                .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        {
            auto* bl = new QVBoxLayout(breakdown_panel_);
            bl->setContentsMargins(12, 8, 12, 8);
            bl->setSpacing(4);

            auto* bh = new QLabel("SCORE BREAKDOWN  ·  Select a trade row above");
            bh->setStyleSheet(
                QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                    .arg(ui::colors::TEXT_SECONDARY()));
            bl->addWidget(bh);

            auto* bd_row1 = new QHBoxLayout;
            bd_member_ = new QLabel("—");
            bd_member_->setStyleSheet(
                QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                    .arg(ui::colors::CYAN()));
            bd_row1->addWidget(bd_member_);
            bd_ticker_ = new QLabel;
            bd_ticker_->setStyleSheet(
                QString("color:%1;font-size:12px;background:transparent;")
                    .arg(ui::colors::AMBER()));
            bd_row1->addWidget(bd_ticker_);
            bd_row1->addStretch();
            bl->addLayout(bd_row1);

            // Factor contribution rows
            const QStringList factor_names = {
                "Committee", "Size", "Lag", "Herd",
                "Timing*", "Bill*", "Lobbying*", "History"
            };
            auto* grid = new QHBoxLayout;
            for (int i = 0; i < factors_.size(); ++i) {
                auto* col = new QVBoxLayout;
                auto* lbl = new QLabel(factor_names[i]);
                lbl->setStyleSheet(
                    QString("color:%1;font-size:12px;background:transparent;")
                        .arg(ui::colors::TEXT_SECONDARY()));
                lbl->setAlignment(Qt::AlignCenter);
                col->addWidget(lbl);
                auto* val = new QLabel("—");
                val->setStyleSheet(
                    QString("color:%1;font-size:13px;font-weight:700;"
                            "font-family:Consolas,monospace;background:transparent;")
                        .arg(ui::colors::TEXT_PRIMARY()));
                val->setAlignment(Qt::AlignCenter);
                col->addWidget(val);
                bd_factor_lbls_.append(val);
                grid->addLayout(col);
            }
            bl->addLayout(grid);

            auto* note = new QLabel("* Pending data integration (Congress.gov / LDA.gov)");
            note->setStyleSheet(
                QString("color:%1;font-size:12px;font-style:italic;background:transparent;")
                    .arg(ui::colors::TEXT_SECONDARY()));
            bl->addWidget(note);
        }
        rl->addWidget(breakdown_panel_, 1);
    }

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    root->addWidget(splitter, 1);
}

void SignalBuilderPanel::build_factor_panel(QWidget* parent) {
    auto* vl = new QVBoxLayout(parent);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* fhdr = new QLabel("FACTOR WEIGHTS");
    fhdr->setStyleSheet(section_hdr_ss());
    vl->addWidget(fhdr);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        QString("QScrollArea{background:%1;border:none;}"
                "QScrollBar:vertical{width:6px;background:%1;border-radius:3px;}"
                "QScrollBar::handle:vertical{background:%2;min-height:20px;border-radius:3px;}")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_MED()));

    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(10, 8, 10, 8);
    bl->setSpacing(10);

    const QString active_ss =
        QString("QSlider::groove:horizontal{height:5px;background:%1;border-radius:2px;}"
                "QSlider::sub-page:horizontal{background:%2;border-radius:2px;}"
                "QSlider::handle:horizontal{background:%2;width:14px;height:14px;"
                "border-radius:7px;margin:-5px 0;}")
            .arg(ui::colors::BORDER_DIM(), ui::colors::AMBER());
    const QString pending_ss =
        QString("QSlider::groove:horizontal{height:5px;background:%1;border-radius:2px;}"
                "QSlider::sub-page:horizontal{background:%1;border-radius:2px;}"
                "QSlider::handle:horizontal{background:%1;width:14px;height:14px;"
                "border-radius:7px;margin:-5px 0;}")
            .arg(ui::colors::BORDER_DIM());

    for (auto& f : factors_) {
        auto* row = new QWidget(body);
        row->setStyleSheet(
            QString("QWidget{background:%1;border:1px solid %2;border-radius:4px;}")
                .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        auto* rl = new QVBoxLayout(row);
        rl->setContentsMargins(10, 8, 10, 8);
        rl->setSpacing(4);

        // Label row
        auto* lbl_row = new QHBoxLayout;
        auto* lbl = new QLabel(f.label, row);
        lbl->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
                .arg(f.pending ? ui::colors::TEXT_SECONDARY() : ui::colors::TEXT_PRIMARY()));
        lbl_row->addWidget(lbl);
        lbl_row->addStretch();

        if (f.pending) {
            auto* badge = new QLabel("PENDING DATA", row);
            badge->setStyleSheet(
                QString("color:#6b7280;font-size:12px;font-weight:600;"
                        "background:rgba(107,114,128,0.15);border:1px solid rgba(107,114,128,0.3);"
                        "border-radius:3px;padding:1px 6px;"));
            lbl_row->addWidget(badge);
        } else {
            f.val_lbl = new QLabel("base: —", row);
            f.val_lbl->setStyleSheet(
                QString("color:%1;font-size:12px;background:transparent;")
                    .arg(ui::colors::TEXT_SECONDARY()));
            lbl_row->addWidget(f.val_lbl);
        }
        rl->addLayout(lbl_row);

        // Description
        auto* desc = new QLabel(f.description, row);
        desc->setWordWrap(true);
        desc->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;")
                .arg(ui::colors::TEXT_SECONDARY()));
        rl->addWidget(desc);

        // Slider row
        auto* sl_row = new QHBoxLayout;
        f.slider = new QSlider(Qt::Horizontal, row);
        f.slider->setRange(0, 20);          // 0–200% in 10% steps
        f.slider->setValue(f.pending ? 0 : 10);  // default 100%
        f.slider->setEnabled(!f.pending);
        f.slider->setStyleSheet(f.pending ? pending_ss : active_ss);
        sl_row->addWidget(f.slider, 1);

        f.pct_lbl = new QLabel(f.pending ? "0%" : "100%", row);
        f.pct_lbl->setFixedWidth(38);
        f.pct_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        f.pct_lbl->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;"
                    "font-family:Consolas,monospace;background:transparent;")
                .arg(f.pending ? ui::colors::TEXT_SECONDARY() : ui::colors::AMBER()));
        sl_row->addWidget(f.pct_lbl);
        rl->addLayout(sl_row);

        connect(f.slider, &QSlider::valueChanged, this, &SignalBuilderPanel::on_weight_changed);
        bl->addWidget(row);
    }
    bl->addStretch();
    scroll->setWidget(body);
    vl->addWidget(scroll, 1);

    build_preset_bar(parent);
}

void SignalBuilderPanel::build_preset_bar(QWidget* parent) {
    preset_bar_ = new QWidget(parent);
    preset_bar_->setStyleSheet(
        QString("QWidget{background:%1;border-top:1px solid %2;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* pl = new QVBoxLayout(preset_bar_);
    pl->setContentsMargins(10, 6, 10, 6);
    pl->setSpacing(6);

    auto* ph = new QLabel("PRESETS");
    ph->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
            .arg(ui::colors::TEXT_SECONDARY()));
    pl->addWidget(ph);

    auto* btn_row = new QHBoxLayout;
    btn_row->setSpacing(4);

    const QString btn_ss =
        QString("QPushButton{background:%1;color:%2;border:1px solid %3;"
                "border-radius:3px;padding:3px 8px;font-size:12px;font-weight:600;}"
                "QPushButton:hover{border-color:%4;color:%4;}"
                "QPushButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_SECONDARY(),
                 ui::colors::BORDER_DIM(), ui::colors::AMBER(),
                 ui::colors::AMBER_DIM(), ui::colors::AMBER());

    for (const auto& p : presets_) {
        auto* btn = new QPushButton(p.name, preset_bar_);
        btn->setCheckable(true);
        btn->setChecked(p.id == active_preset_id_);
        btn->setStyleSheet(btn_ss);
        btn->setCursor(Qt::PointingHandCursor);
        const QString pid = p.id;
        connect(btn, &QPushButton::clicked, this, [this, pid]() {
            on_preset_clicked(pid);
        });
        btn_row->addWidget(btn);
    }
    btn_row->addStretch();

    auto* save_btn = new QPushButton("+ Save", preset_bar_);
    save_btn->setStyleSheet(btn_ss);
    save_btn->setCursor(Qt::PointingHandCursor);
    connect(save_btn, &QPushButton::clicked, this, &SignalBuilderPanel::on_save_preset);
    btn_row->addWidget(save_btn);

    pl->addLayout(btn_row);

    auto* vl = qobject_cast<QVBoxLayout*>(parent->layout());
    if (vl) vl->addWidget(preset_bar_);
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void SignalBuilderPanel::on_weight_changed() {
    if (updating_) return;
    for (auto& f : factors_)
        if (f.pct_lbl && f.slider)
            f.pct_lbl->setText(QString("%1%").arg(f.slider->value() * 10));

    // Deselect preset buttons (user is now in custom mode)
    if (preset_bar_) {
        for (auto* btn : preset_bar_->findChildren<QPushButton*>())
            btn->setChecked(false);
    }
    active_preset_id_ = "custom";
    refresh_preview();
}

void SignalBuilderPanel::on_preset_clicked(const QString& preset_id) {
    for (const auto& p : presets_) {
        if (p.id == preset_id) {
            apply_preset(p);
            active_preset_id_ = preset_id;
            // Update button states
            if (preset_bar_) {
                for (auto* btn : preset_bar_->findChildren<QPushButton*>())
                    btn->setChecked(btn->text() == p.name);
            }
            break;
        }
    }
}

void SignalBuilderPanel::on_save_preset() {
    bool ok;
    QString name = QInputDialog::getText(this, "Save Signal Preset",
                                          "Preset name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    SignalPreset p = current_preset();
    p.id      = QString("user_%1").arg(QDateTime::currentSecsSinceEpoch());
    p.name    = name.trimmed();
    p.builtin = false;
    presets_.append(p);
    save_user_presets();

    // Rebuild preset bar with new preset
    if (preset_bar_) {
        preset_bar_->deleteLater();
        preset_bar_ = nullptr;
    }
    // factor_pane_ stored at construction; rebuild preset bar with new preset
    if (factor_pane_) build_preset_bar(factor_pane_);
}

void SignalBuilderPanel::on_trade_selected(int row) {
    if (row < 0 || row >= preview_table_->rowCount() || base_scores_.isEmpty()) return;
    update_breakdown(row);
}

// ── Data ───────────────────────────────────────────────────────────────────────

void SignalBuilderPanel::set_data(const power_trader::PowerTraderSummary& summary) {
    summary_     = summary;
    base_scores_ = PowerTraderService::instance().compute_trade_base_scores();
    refresh_preview();
}

void SignalBuilderPanel::refresh_preview() {
    if (base_scores_.isEmpty() || summary_.recent_trades.isEmpty()) {
        preview_table_->setRowCount(0);
        return;
    }

    const SignalPreset preset = current_preset();

    // Build (score, trade_index) pairs and sort descending
    QVector<QPair<double, int>> scored;
    scored.reserve(base_scores_.size());
    for (int i = 0; i < base_scores_.size() && i < summary_.recent_trades.size(); ++i)
        scored.append({preset.apply(base_scores_[i]), i});
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    const int rows = qMin(50, scored.size());
    preview_table_->setRowCount(rows);

    for (int r = 0; r < rows; ++r) {
        const double  score = scored[r].first;
        const int     idx   = scored[r].second;
        const auto&   t     = summary_.recent_trades[idx];

        preview_table_->setRowHeight(r, 30);

        auto mk = [&](int col, const QString& txt, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(txt);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setData(Qt::UserRole + 1, idx);  // store base score index
            preview_table_->setItem(r, col, item);
        };

        // SCORE
        auto* si = new QTableWidgetItem(QString::number(score, 'f', 0));
        si->setTextAlignment(Qt::AlignCenter);
        si->setForeground(QColor(score_color(score)));
        si->setFlags(si->flags() & ~Qt::ItemIsEditable);
        si->setData(Qt::UserRole + 1, idx);
        preview_table_->setItem(r, 0, si);

        // MEMBER
        auto* mi = new QTableWidgetItem(t.member_name);
        mi->setForeground(QColor(ui::colors::CYAN()));
        mi->setData(Qt::UserRole, t.member_id);
        mi->setData(Qt::UserRole + 1, idx);
        mi->setFlags(mi->flags() & ~Qt::ItemIsEditable);
        preview_table_->setItem(r, 1, mi);

        const char* pc = t.party == QStringLiteral("D") ? "#3b82f6"
                       : t.party == QStringLiteral("R") ? "#ef4444"
                       : ui::colors::AMBER;
        mk(2, t.party, pc, Qt::AlignCenter);
        mk(3, t.ticker, ui::colors::TEXT_PRIMARY);
        const bool buy = t.direction == power_trader::TradeDirection::Buy;
        mk(4, power_trader::direction_label(t.direction),
           buy ? ui::colors::POSITIVE : ui::colors::NEGATIVE, Qt::AlignCenter);
        mk(5, t.amount_range_label, ui::colors::TEXT_SECONDARY);
        mk(6, t.committee_relevance.isEmpty() ? "—" : t.committee_relevance,
           t.committee_relevance.isEmpty() ? ui::colors::TEXT_SECONDARY : ui::colors::AMBER);

        auto* lag_i = new QTableWidgetItem(QString::number(t.disclosure_lag_days));
        lag_i->setTextAlignment(Qt::AlignCenter);
        lag_i->setForeground(QColor(t.disclosure_lag_days > 30
                                    ? ui::colors::WARNING : ui::colors::TEXT_SECONDARY()));
        lag_i->setFlags(lag_i->flags() & ~Qt::ItemIsEditable);
        lag_i->setData(Qt::UserRole + 1, idx);
        preview_table_->setItem(r, 7, lag_i);
    }

    // Update base score labels in sliders
    if (!scored.isEmpty()) {
        const auto& top_base = base_scores_[scored[0].second];
        const QVector<double> vals = {
            top_base.committee, top_base.size,     top_base.lag,
            top_base.herd,      top_base.timing,   top_base.bill,
            top_base.lobbying,  top_base.history
        };
        for (int i = 0; i < factors_.size() && i < vals.size(); ++i)
            if (factors_[i].val_lbl && !factors_[i].pending)
                factors_[i].val_lbl->setText(
                    QString("base: %1").arg(vals[i], 0, 'f', 0));
    }
}

void SignalBuilderPanel::update_breakdown(int row) {
    auto* item = preview_table_->item(row, 1);
    if (!item) return;
    const int idx = item->data(Qt::UserRole + 1).toInt();
    if (idx < 0 || idx >= base_scores_.size()) return;

    const auto& t    = summary_.recent_trades[idx];
    const auto& base = base_scores_[idx];
    const SignalPreset preset = current_preset();

    bd_member_->setText(t.member_name);
    bd_ticker_->setText(QString("  %1  %2")
                            .arg(t.ticker)
                            .arg(power_trader::direction_label(t.direction)));

    const QVector<double> base_vals = {
        base.committee, base.size,    base.lag,      base.herd,
        base.timing,    base.bill,    base.lobbying,  base.history
    };
    const QVector<double> weights = {
        preset.w_committee, preset.w_size,     preset.w_lag,      preset.w_herd,
        preset.w_timing,    preset.w_bill,     preset.w_lobbying,  preset.w_history
    };

    double total_w = 0;
    for (double w : weights) total_w += w;

    for (int i = 0; i < bd_factor_lbls_.size() && i < 8; ++i) {
        // Weighted contribution: what this factor adds to the final score
        const double contrib = (total_w > 0 && !factors_[i].pending)
            ? qMin(100.0, (weights[i] * base_vals[i]) / total_w)
            : 0.0;
        const QString txt = factors_[i].pending
            ? QStringLiteral("—")
            : QString("%1→%2")
                  .arg(base_vals[i], 0, 'f', 0)
                  .arg(contrib, 0, 'f', 0);
        bd_factor_lbls_[i]->setText(txt);
        bd_factor_lbls_[i]->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;"
                    "font-family:Consolas,monospace;background:transparent;")
                .arg(factors_[i].pending
                     ? ui::colors::TEXT_SECONDARY()
                     : contrib >= 20 ? ui::colors::POSITIVE()
                       : contrib >= 10 ? ui::colors::AMBER()
                       : ui::colors::TEXT_SECONDARY()));
    }
}

// ── Preset persistence ─────────────────────────────────────────────────────────

SignalPreset SignalBuilderPanel::current_preset() const {
    SignalPreset p;
    p.id = "custom";
    p.name = "Custom";
    for (const auto& f : factors_) {
        if (!f.slider) continue;
        const double w = f.slider->value() / 10.0;
        if      (f.id == "committee") p.w_committee = w;
        else if (f.id == "size")      p.w_size      = w;
        else if (f.id == "lag")       p.w_lag       = w;
        else if (f.id == "herd")      p.w_herd      = w;
        else if (f.id == "timing")    p.w_timing    = w;
        else if (f.id == "bill")      p.w_bill      = w;
        else if (f.id == "lobbying")  p.w_lobbying  = w;
        else if (f.id == "history")   p.w_history   = w;
    }
    return p;
}

void SignalBuilderPanel::apply_preset(const SignalPreset& p) {
    updating_ = true;
    const QVector<QPair<QString, double>> wvals = {
        {"committee", p.w_committee}, {"size",     p.w_size},
        {"lag",       p.w_lag},       {"herd",     p.w_herd},
        {"timing",    p.w_timing},    {"bill",     p.w_bill},
        {"lobbying",  p.w_lobbying},  {"history",  p.w_history},
    };
    for (auto& f : factors_) {
        for (const auto& [id, w] : wvals) {
            if (f.id == id && f.slider) {
                f.slider->setValue(qRound(w * 10));
                if (f.pct_lbl) f.pct_lbl->setText(QString("%1%").arg(qRound(w * 100)));
            }
        }
    }
    updating_ = false;
    refresh_preview();
}

void SignalBuilderPanel::load_user_presets() {
    QSettings settings(AppIdentity::kOrg, "Terminal");
    const QByteArray data = settings.value("signal_presets").toByteArray();
    if (data.isEmpty()) return;
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) return;
    for (const auto& v : doc.array()) {
        const auto o = v.toObject();
        SignalPreset p;
        p.id          = o["id"].toString();
        p.name        = o["name"].toString();
        p.builtin     = false;
        p.w_committee = o["w_committee"].toDouble(1.0);
        p.w_size      = o["w_size"].toDouble(1.0);
        p.w_lag       = o["w_lag"].toDouble(1.0);
        p.w_herd      = o["w_herd"].toDouble(1.0);
        p.w_timing    = o["w_timing"].toDouble(0.0);
        p.w_bill      = o["w_bill"].toDouble(0.0);
        p.w_lobbying  = o["w_lobbying"].toDouble(0.0);
        p.w_history   = o["w_history"].toDouble(0.5);
        if (!p.id.isEmpty() && !p.name.isEmpty())
            presets_.append(p);
    }
}

void SignalBuilderPanel::save_user_presets() {
    QJsonArray arr;
    for (const auto& p : presets_) {
        if (p.builtin) continue;
        QJsonObject o;
        o["id"]          = p.id;
        o["name"]        = p.name;
        o["w_committee"] = p.w_committee;
        o["w_size"]      = p.w_size;
        o["w_lag"]       = p.w_lag;
        o["w_herd"]      = p.w_herd;
        o["w_timing"]    = p.w_timing;
        o["w_bill"]      = p.w_bill;
        o["w_lobbying"]  = p.w_lobbying;
        o["w_history"]   = p.w_history;
        arr.append(o);
    }
    QSettings settings(AppIdentity::kOrg, "Terminal");
    settings.setValue("signal_presets", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace fincept::screens

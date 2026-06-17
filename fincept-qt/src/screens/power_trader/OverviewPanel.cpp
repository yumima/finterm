// src/screens/power_trader/OverviewPanel.cpp
#include "screens/power_trader/OverviewPanel.h"

#include "ui/components/LayoutHelpers.h"
#include "ui/components/SectionHeader.h"
#include "ui/components/SignalTooltip.h"
#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace fincept::screens {

// ── Party color constants (semantic, not theme tokens) ────────────────────────
static constexpr const char* kPartyD = "#3b82f6";
static constexpr const char* kPartyR = "#ef4444";
static constexpr const char* kPartyI = "#eab308";

static const char* party_color(const QString& p) {
    if (p == QLatin1String("D")) return kPartyD;
    if (p == QLatin1String("R")) return kPartyR;
    return kPartyI;
}

// ── Sector color helper ───────────────────────────────────────────────────────
static const char* sector_color(const QString& sector) {
    if (sector.contains(QLatin1String("Tech"),       Qt::CaseInsensitive)) return ui::colors::CYAN();
    if (sector.contains(QLatin1String("Defense"),    Qt::CaseInsensitive)) return ui::colors::NEGATIVE();
    if (sector.contains(QLatin1String("Financial"),  Qt::CaseInsensitive)) return kPartyD;
    if (sector.contains(QLatin1String("Energy"),     Qt::CaseInsensitive)) return ui::colors::AMBER();
    if (sector.contains(QLatin1String("Health"),     Qt::CaseInsensitive)) return ui::colors::POSITIVE();
    return ui::colors::TEXT_SECONDARY();
}

// ── Format helpers ────────────────────────────────────────────────────────────

QString OverviewPanel::fmt_amount(double v) {
    if (v >= 1e9)  return QStringLiteral("$%1B").arg(v / 1e9, 0, 'f', 1);
    if (v >= 1e6)  return QStringLiteral("$%1M").arg(v / 1e6, 0, 'f', 1);
    if (v >= 1e3)  return QStringLiteral("$%1K").arg(v / 1e3, 0, 'f', 0);
    return QStringLiteral("$%1").arg(v, 0, 'f', 0);
}

QString OverviewPanel::fmt_pct(double v) {
    return QStringLiteral("%1%2%").arg(v >= 0 ? "+" : "").arg(v, 0, 'f', 1);
}

// ── Bar row widget — used by both alpha and sector charts ─────────────────────
// A fixed-height row widget that draws: [left label] [bar] [right label]
class BarRow : public QWidget {
  public:
    struct Config {
        QString left_label;
        QString right_label;
        double  fill_ratio   = 0.0;   // 0..1
        QColor  bar_color;
        QColor  left_color;
        QColor  right_color;
        bool    clickable    = false;
        QString member_id;
    };

    explicit BarRow(const Config& cfg, QWidget* parent = nullptr)
        : QWidget(parent), cfg_(cfg) {
        setFixedHeight(30);
        setCursor(cfg_.clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

  signals:
    // Qt MOC can't handle this in a nested class without Q_OBJECT;
    // we use a callback instead.

  public:
    std::function<void(const QString&)> on_click;

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int w   = width();
        const int h   = height();
        const int lw  = 150; // left label width
        const int rw  = 72;  // right label width
        const int pad = 6;
        const int bar_x     = lw + pad;
        const int bar_w_max = w - lw - rw - pad * 2;
        const int bar_y     = h / 2 - 4;
        const int bar_h     = 10;

        // Left label
        p.setPen(cfg_.left_color);
        QFont lf = p.font();
        lf.setPixelSize(12);
        p.setFont(lf);
        p.drawText(QRect(0, 0, lw, h), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(cfg_.left_label, Qt::ElideRight, lw));

        // Bar background
        QColor bg_col = cfg_.bar_color;
        bg_col.setAlpha(25);
        p.fillRect(QRect(bar_x, bar_y, bar_w_max, bar_h), bg_col);

        // Bar fill
        int fill_w = static_cast<int>(bar_w_max * qBound(0.0, cfg_.fill_ratio, 1.0));
        if (fill_w > 0)
            p.fillRect(QRect(bar_x, bar_y, fill_w, bar_h), cfg_.bar_color);

        // Right label
        p.setPen(cfg_.right_color);
        QFont rf = p.font();
        rf.setPixelSize(12);
        rf.setBold(true);
        p.setFont(rf);
        p.drawText(QRect(w - rw, 0, rw, h), Qt::AlignRight | Qt::AlignVCenter,
                   cfg_.right_label);
    }

    void mousePressEvent(QMouseEvent* ev) override {
        QWidget::mousePressEvent(ev);
        if (cfg_.clickable && on_click)
            on_click(cfg_.member_id);
    }

  private:
    Config cfg_;
};

// ── Constructor ───────────────────────────────────────────────────────────────

OverviewPanel::OverviewPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

// ── UI construction ───────────────────────────────────────────────────────────

void OverviewPanel::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setStyleSheet(
        QString("QScrollArea { background:%1; border:none; }"
                "QScrollBar:vertical { width:8px; background:%1; border-radius:4px; }"
                "QScrollBar::handle:vertical { background:%2; min-height:30px; border-radius:4px; }"
                "QScrollBar:horizontal { height:8px; background:%1; border-radius:4px; }"
                "QScrollBar::handle:horizontal { background:%2; min-width:30px; border-radius:4px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_BRIGHT()));

    scroll_body_ = new QWidget(this);
    scroll_body_->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));

    auto* body_layout = new QVBoxLayout(scroll_body_);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);

    // ── Bloomberg 3-column layout ─────────────────────────────────────────────
    // COL 1 (left):  2×2 stat grid + sector exposure bars
    // COL 2 (center): top traders alpha chart
    // COL 3 (right):  committee insider correlation table
    auto* three_col = new QHBoxLayout();
    three_col->setContentsMargins(0, 0, 0, 0);
    three_col->setSpacing(0);

    // ── COLUMN 1: stats + sectors ─────────────────────────────────────────────
    auto* col1 = new QWidget(scroll_body_);
    col1->setStyleSheet(
        QString("QWidget { background:%1; border-right:1px solid %2; }")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
    {
        auto* c1l = new QVBoxLayout(col1);
        c1l->setContentsMargins(0, 0, 0, 0);
        c1l->setSpacing(0);

        // Compact vertical stat list — no wide/short tiles; each row uses column height
        auto* stat_list = new QWidget(col1);
        stat_list->setStyleSheet(
            QString("QWidget { background:%1; border-bottom:1px solid %2; }")
                .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        auto* sl = new QVBoxLayout(stat_list);
        sl->setContentsMargins(12, 10, 12, 10);
        sl->setSpacing(0);

        const QString row_ss =
            QString("QWidget { background:transparent; border-bottom:1px solid %1; }")
                .arg(ui::colors::BORDER_DIM());
        const QString cap_ss =
            QString("color:%1; font-size:12px; font-weight:600; background:transparent;")
                .arg(ui::colors::TEXT_SECONDARY());
        const QString val_ss =
            QString("color:%1; font-size:16px; font-weight:700;"
                    " font-family:Consolas,monospace; background:transparent;")
                .arg(ui::colors::AMBER());

        struct StatSpec { QString label; QLabel** ptr; };
        const QVector<StatSpec> specs = {
            { QStringLiteral("MEMBERS TRACKED"),       &stat_members_   },
            { QStringLiteral("TOTAL DISCLOSED (EST)"), &stat_disclosed_ },
            { QStringLiteral("BEAT SPY"),               &stat_beat_spy_ },
            { QStringLiteral("MOST ACTIVE CMTE"),       &stat_cmte_      },
        };

        for (const auto& spec : specs) {
            auto* row = new QWidget(stat_list);
            row->setStyleSheet(row_ss);
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(0, 8, 0, 8);
            hl->setSpacing(8);
            auto* cap = new QLabel(spec.label, row);
            cap->setStyleSheet(cap_ss);
            hl->addWidget(cap);
            hl->addStretch();
            *spec.ptr = new QLabel(QStringLiteral("—"), row);
            (*spec.ptr)->setStyleSheet(val_ss);
            (*spec.ptr)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            hl->addWidget(*spec.ptr);
            sl->addWidget(row);
        }
        c1l->addWidget(stat_list);

        // Sector exposure bars below stat grid
        auto* sec_hdr = fincept::ui::make_section_header(QStringLiteral("SECTOR EXPOSURE"), col1);
        c1l->addWidget(sec_hdr);

        sector_chart_ = new QWidget(col1);
        sector_chart_->setStyleSheet("QWidget { background:transparent; border:none; }");
        auto* sc_layout = new QVBoxLayout(sector_chart_);
        sc_layout->setContentsMargins(10, 6, 10, 6);
        sc_layout->setSpacing(3);
        c1l->addWidget(sector_chart_);
        c1l->addStretch();
    }

    // ── COLUMN 2: top traders alpha chart ─────────────────────────────────────
    auto* col2 = new QWidget(scroll_body_);
    col2->setStyleSheet(
        QString("QWidget { background:%1; border-right:1px solid %2; }")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
    {
        auto* c2l = new QVBoxLayout(col2);
        c2l->setContentsMargins(0, 0, 0, 0);
        c2l->setSpacing(0);
        c2l->addWidget(fincept::ui::make_section_header(QStringLiteral("TOP TRADERS  ·  ALPHA vs SPY YTD"), col2));

        alpha_chart_ = new QWidget(col2);
        alpha_chart_->setStyleSheet("QWidget { background:transparent; border:none; }");
        auto* ac_layout = new QVBoxLayout(alpha_chart_);
        ac_layout->setContentsMargins(10, 6, 10, 6);
        ac_layout->setSpacing(3);
        c2l->addWidget(alpha_chart_);
        c2l->addStretch();
    }

    // ── COLUMN 3: committee signal table ──────────────────────────────────────
    auto* col3 = new QWidget(scroll_body_);
    col3->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    {
        auto* c3l = new QVBoxLayout(col3);
        c3l->setContentsMargins(0, 0, 0, 0);
        c3l->setSpacing(0);
        c3l->addWidget(fincept::ui::make_section_header(
            QStringLiteral("COMMITTEE INSIDER CORRELATION"), col3));

        static const QStringList kSigCols = {
            "MEMBER", "PARTY", "COMMITTEE", "SECTOR", "OVERLAP", "SIGNAL"
        };

        signal_table_ = new QTableWidget(col3);
        signal_table_->setColumnCount(kSigCols.size());
        signal_table_->setHorizontalHeaderLabels(kSigCols);
        fincept::ui::ensure_header_fits(signal_table_);
        signal_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        signal_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        signal_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        signal_table_->setShowGrid(false);
        signal_table_->setAlternatingRowColors(false);
        signal_table_->verticalHeader()->setVisible(false);
        signal_table_->setFocusPolicy(Qt::NoFocus);
        signal_table_->setSortingEnabled(true);

        auto* h = signal_table_->horizontalHeader();
        h->setMinimumSectionSize(20);
        // Equal distribution: all columns stretch — short fixed cols for party/signal only
        h->setSectionResizeMode(QHeaderView::Stretch);
        h->setSectionResizeMode(1, QHeaderView::Fixed);  h->resizeSection(1, 36);   // PARTY
        h->setSectionResizeMode(4, QHeaderView::Fixed);  h->resizeSection(4, 72);   // OVERLAP %
        h->setSectionResizeMode(5, QHeaderView::Fixed);  h->resizeSection(5, 58);   // SIGNAL

        signal_table_->setStyleSheet(
            QString("QTableWidget { background:%1; color:%2; border:none;"
                    "  font-size:12px; font-family:Consolas,monospace;"
                    "  gridline-color:transparent; }"
                    "QTableWidget::item { padding:4px 8px; border-bottom:1px solid %3; }"
                    "QTableWidget::item:selected { background:rgba(217,119,6,0.18); color:%2; }"
                    "QTableWidget::item:hover { background:%4; }"
                    "QScrollBar:vertical { width:8px; background:%1; border-radius:4px; }"
                    "QScrollBar::handle:vertical { background:%3; min-height:24px; border-radius:4px; }")
                .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                     ui::colors::BORDER_MED(), ui::colors::BG_HOVER()));

        h->setStyleSheet(
            QString("QHeaderView::section { background:%1; color:%2; border:none;"
                    "  border-bottom:2px solid %3; border-right:1px solid %4;"
                    "  padding:5px 8px; font-size:12px; font-weight:700;"
                    "  letter-spacing:0.5px; }")
                .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                     ui::colors::AMBER(), ui::colors::BORDER_MED()));

        c3l->addWidget(signal_table_, 1);
    }

    three_col->addWidget(col1, 1);
    three_col->addWidget(col2, 1);
    three_col->addWidget(col3, 1);
    body_layout->addLayout(three_col, 1);

    scroll_area_->setWidget(scroll_body_);
    root->addWidget(scroll_area_, 1);
}

// ── Data population ───────────────────────────────────────────────────────────

void OverviewPanel::set_data(const power_trader::PowerTraderSummary& summary,
                              const QVector<power_trader::SectorExposure>& sectors,
                              const QVector<power_trader::CommitteeInsiderSignal>& insider_signals) {
    members_cache_ = summary.members;
    sectors_cache_ = sectors;
    signals_cache_ = insider_signals;

    populate_stats();
    populate_alpha_chart();
    populate_sector_chart();
    populate_signal_table();
}

void OverviewPanel::refresh_theme() {
    if (!members_cache_.isEmpty() || !sectors_cache_.isEmpty() || !signals_cache_.isEmpty()) {
        populate_stats();
        populate_alpha_chart();
        populate_sector_chart();
        populate_signal_table();
    }
}

// ── Row 1: stat tiles ─────────────────────────────────────────────────────────

void OverviewPanel::populate_stats() {
    const auto& members = members_cache_;

    // MEMBERS TRACKED
    stat_members_->setText(QString::number(members.size()));

    // TOTAL DISCLOSED — sum of trade midpoints (all recent_trades)
    // We approximate via sectors total since sectors aggregate all trade midpoints
    double total = 0.0;
    for (const auto& s : sectors_cache_)
        total += s.total_est_amount;
    stat_disclosed_->setText(fmt_amount(total));

    // BEAT SPY — share of members with real, priced positive alpha. Only counts
    // members whose returns are actually priced (return_priced); unpriced members
    // have no real alpha and are excluded from both numerator and denominator.
    int beat = 0, priced = 0;
    for (const auto& m : members) {
        if (!m.return_priced) continue;
        ++priced;
        if (m.alpha_ytd > 0) ++beat;
    }
    if (priced > 0) {
        const double pct = beat * 100.0 / priced;
        stat_beat_spy_->setText(
            QString("%1% (%2/%3)").arg(pct, 0, 'f', 0).arg(beat).arg(priced));
    } else {
        stat_beat_spy_->setText(QStringLiteral("—")); // no priced members yet
    }

    // MOST ACTIVE CMTE — committee appearing most in signals, or fallback to members
    if (!signals_cache_.isEmpty()) {
        QHash<QString, int> cmte_count;
        for (const auto& sig : signals_cache_)
            cmte_count[sig.committee]++;
        QString best_cmte;
        int best_count = 0;
        for (auto it = cmte_count.cbegin(); it != cmte_count.cend(); ++it) {
            if (it.value() > best_count) {
                best_count = it.value();
                best_cmte  = it.key();
            }
        }
        stat_cmte_->setText(best_cmte.isEmpty() ? QStringLiteral("—") : best_cmte);
    } else if (!members.isEmpty()) {
        // Fallback: most common committee across all members
        QHash<QString, int> cmte_count;
        for (const auto& m : members)
            for (const auto& c : m.committees)
                cmte_count[c]++;
        QString best_cmte;
        int best_count = 0;
        for (auto it = cmte_count.cbegin(); it != cmte_count.cend(); ++it) {
            if (it.value() > best_count) {
                best_count = it.value();
                best_cmte  = it.key();
            }
        }
        stat_cmte_->setText(best_cmte.isEmpty() ? QStringLiteral("—") : best_cmte);
    } else {
        stat_cmte_->setText(QStringLiteral("—"));
    }
}

// ── Row 2 left: alpha bar chart ───────────────────────────────────────────────

void OverviewPanel::populate_alpha_chart() {
    // Clear existing bar rows
    auto* layout = qobject_cast<QVBoxLayout*>(alpha_chart_->layout());
    if (!layout) return;

    while (layout->count() > 0) {
        auto* item = layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    // Only members with real, priced alpha appear here — never unpriced 0s.
    QVector<power_trader::CongressMember> sorted;
    for (const auto& m : members_cache_)
        if (m.return_priced) sorted.append(m);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const power_trader::CongressMember& a,
                        const power_trader::CongressMember& b) {
                         return a.alpha_ytd > b.alpha_ytd;
                     });
    if (sorted.size() > 8)
        sorted.resize(8);

    if (sorted.isEmpty()) {
        // No priced members yet (prices still loading or unavailable).
        auto* empty = new QLabel(QStringLiteral("Awaiting price data…"), alpha_chart_);
        empty->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; }").arg(ui::colors::TEXT_SECONDARY()));
        layout->addWidget(empty);
        return;
    }

    // Find max absolute alpha for scaling
    double max_abs = 0.0;
    for (const auto& m : sorted)
        max_abs = qMax(max_abs, qAbs(m.alpha_ytd));
    if (max_abs < 0.001) max_abs = 1.0;

    for (const auto& m : sorted) {
        // Build display label: "[D] Last Name"
        QString display = QStringLiteral("[%1] %2").arg(m.party).arg(m.full_name);

        BarRow::Config cfg;
        cfg.left_label  = display;
        cfg.right_label = fmt_pct(m.alpha_ytd);
        cfg.fill_ratio  = qAbs(m.alpha_ytd) / max_abs;
        cfg.bar_color   = m.alpha_ytd >= 0 ? QColor(ui::colors::POSITIVE())
                                             : QColor(ui::colors::NEGATIVE());
        cfg.left_color  = QColor(ui::colors::CYAN());
        cfg.right_color = m.alpha_ytd >= 0 ? QColor(ui::colors::POSITIVE())
                                             : QColor(ui::colors::NEGATIVE());
        cfg.clickable   = true;
        cfg.member_id   = m.id;

        auto* row = new BarRow(cfg, alpha_chart_);
        row->on_click = [this](const QString& id) { emit member_selected(id); };
        layout->addWidget(row);
    }
}

// ── Row 2 right: sector exposure bar chart ────────────────────────────────────

void OverviewPanel::populate_sector_chart() {
    auto* layout = qobject_cast<QVBoxLayout*>(sector_chart_->layout());
    if (!layout) return;

    while (layout->count() > 0) {
        auto* item = layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    // Sort sectors by total_est_amount descending, take top 8
    QVector<power_trader::SectorExposure> sorted = sectors_cache_;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const power_trader::SectorExposure& a,
                        const power_trader::SectorExposure& b) {
                         return a.total_est_amount > b.total_est_amount;
                     });
    if (sorted.size() > 8)
        sorted.resize(8);

    if (sorted.isEmpty()) {
        auto* empty = new QLabel(QStringLiteral("No data"), sector_chart_);
        empty->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; }").arg(ui::colors::TEXT_SECONDARY()));
        layout->addWidget(empty);
        return;
    }

    const double max_amount = sorted.isEmpty() ? 1.0 : sorted[0].total_est_amount;

    for (const auto& sec : sorted) {
        BarRow::Config cfg;
        cfg.left_label  = sec.sector;
        cfg.right_label = QStringLiteral("%1%").arg(sec.pct_of_all, 0, 'f', 1);
        cfg.fill_ratio  = max_amount > 0 ? sec.total_est_amount / max_amount : 0.0;
        cfg.bar_color   = QColor(sector_color(sec.sector));
        cfg.left_color  = QColor(ui::colors::TEXT_PRIMARY());
        cfg.right_color = QColor(ui::colors::AMBER());
        cfg.clickable   = false;

        auto* row = new BarRow(cfg, sector_chart_);
        layout->addWidget(row);
    }
}

// ── Row 3: committee insider correlation table ────────────────────────────────

void OverviewPanel::populate_signal_table() {
    signal_table_->setSortingEnabled(false);

    // Sort signals by overlap_pct descending, show at most 12
    QVector<power_trader::CommitteeInsiderSignal> sorted = signals_cache_;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const power_trader::CommitteeInsiderSignal& a,
                        const power_trader::CommitteeInsiderSignal& b) {
                         return a.overlap_pct > b.overlap_pct;
                     });
    if (sorted.size() > 12)
        sorted.resize(12);

    signal_table_->setRowCount(sorted.size());

    for (int r = 0; r < sorted.size(); ++r) {
        const auto& sig = sorted[r];
        signal_table_->setRowHeight(r, 26);

        auto set_item = [&](int col, const QString& text, const char* color = nullptr,
                            Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            signal_table_->setItem(r, col, item);
        };

        // MEMBER (col 0) — store member_id in UserRole
        auto* name_item = new QTableWidgetItem(sig.member_name);
        name_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        name_item->setForeground(QColor(ui::colors::CYAN()));
        name_item->setData(Qt::UserRole, sig.member_id);
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        signal_table_->setItem(r, 0, name_item);

        // PARTY (col 1)
        set_item(1, sig.party, party_color(sig.party), Qt::AlignCenter);

        // COMMITTEE (col 2)
        set_item(2, sig.committee, ui::colors::TEXT_SECONDARY);

        // SECTOR (col 3)
        set_item(3, sig.sector, ui::colors::TEXT_SECONDARY);

        // OVERLAP (col 4) — "7/14 = 50%"
        const QString overlap_str =
            QStringLiteral("%1/%2 = %3%")
                .arg(sig.overlap_trades)
                .arg(sig.total_trades)
                .arg(sig.overlap_pct, 0, 'f', 0);
        set_item(4, overlap_str, ui::colors::TEXT_PRIMARY, Qt::AlignCenter);

        // SIGNAL (col 5) — amber if > 70
        {
            auto* sig_item = new QTableWidgetItem(
                QString::number(static_cast<int>(sig.signal_score)));
            sig_item->setTextAlignment(Qt::AlignCenter);
            sig_item->setForeground(
                QColor(sig.signal_score > 70 ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY()));
            sig_item->setFlags(sig_item->flags() & ~Qt::ItemIsEditable);
            // Store numeric value for sort
            sig_item->setData(Qt::UserRole, sig.signal_score);
            sig_item->setToolTip(fincept::ui::tooltip_for_peak_signal(sig.overlap_trades));
            signal_table_->setItem(r, 5, sig_item);
        }
    }

    // Wire row click → member_selected
    connect(signal_table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= signal_table_->rowCount()) return;
        auto* item = signal_table_->item(row, 0);
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty()) emit member_selected(id);
    }, Qt::UniqueConnection);

    signal_table_->setSortingEnabled(true);
    // Default: sort by OVERLAP descending (already pre-sorted, keep as-is)
    signal_table_->sortByColumn(4, Qt::DescendingOrder);
}

} // namespace fincept::screens

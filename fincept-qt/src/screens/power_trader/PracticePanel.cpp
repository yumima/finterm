// src/screens/power_trader/PracticePanel.cpp
#include "screens/power_trader/PracticePanel.h"

#include "screens/power_trader/PowerTraderService.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens {

// ── Style helpers ──────────────────────────────────────────────────────────────

static QString body_ss(bool secondary = false) {
    return QString("color:%1;font-size:12px;background:transparent;")
        .arg(secondary ? ui::colors::TEXT_SECONDARY() : ui::colors::TEXT_PRIMARY());
}
static QString heading_ss() {
    return QString("color:%1;font-size:14px;font-weight:700;background:transparent;"
                   "padding-top:8px;").arg(ui::colors::AMBER());
}
static QString callout_ss(const QString& border_color = {}) {
    const QString bc = border_color.isEmpty() ? ui::colors::AMBER() : border_color;
    return QString("background:rgba(217,119,6,0.08);border-left:3px solid %1;"
                   "border-radius:2px;padding:8px 12px;font-size:12px;color:%2;")
        .arg(bc, ui::colors::TEXT_PRIMARY());
}
static QString stat_card_ss() {
    return QString("QWidget{background:%1;border:1px solid %2;border-radius:4px;}")
        .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED());
}
static QWidget* make_divider(QWidget* parent) {
    auto* d = new QWidget(parent);
    d->setFixedHeight(1);
    d->setStyleSheet(QString("background:%1;").arg(ui::colors::BORDER_DIM()));
    return d;
}

// ── Stat card helper ───────────────────────────────────────────────────────────
static QWidget* make_stat(const QString& value, const QString& label,
                           const char* value_color = nullptr) {
    auto* w = new QWidget;
    w->setStyleSheet(stat_card_ss());
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 8, 12, 8);
    vl->setSpacing(2);
    auto* v = new QLabel(value);
    v->setStyleSheet(
        QString("color:%1;font-size:18px;font-weight:700;"
                "font-family:Consolas,monospace;background:transparent;")
            .arg(value_color ? value_color : ui::colors::AMBER()));
    auto* l = new QLabel(label);
    l->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;")
            .arg(ui::colors::TEXT_SECONDARY()));
    l->setWordWrap(true);
    vl->addWidget(v);
    vl->addWidget(l);
    return w;
}

// ── Scrollable content page ────────────────────────────────────────────────────
static QScrollArea* make_scroll(QWidget* content_widget) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        QString("QScrollArea{background:%1;border:none;}"
                "QScrollBar:vertical{width:8px;background:%1;border-radius:4px;}"
                "QScrollBar::handle:vertical{background:%2;min-height:24px;border-radius:4px;}")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_MED()));
    scroll->setWidget(content_widget);
    return scroll;
}

static void add_para(QVBoxLayout* vl, const QString& text,
                      bool secondary = false, bool wordwrap = true) {
    auto* lbl = new QLabel(text);
    lbl->setStyleSheet(body_ss(secondary));
    lbl->setWordWrap(wordwrap);
    vl->addWidget(lbl);
}

// ── Constructor ────────────────────────────────────────────────────────────────

PracticePanel::PracticePanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void PracticePanel::build_ui() {
    setStyleSheet(QString("background:%1;color:%2;")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header
    auto* hdr = new QWidget;
    hdr->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                           .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hl = new QVBoxLayout(hdr);
    hl->setContentsMargins(14, 8, 14, 8);
    hl->setSpacing(2);
    auto* title = new QLabel("PRACTICE  ·  Congressional Trading Intelligence Guide");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:700;letter-spacing:1px;")
                             .arg(ui::colors::AMBER()));
    hl->addWidget(title);
    auto* sub = new QLabel(
        "Research findings, signal interpretation, sector maps, and strategy templates — "
        "all using live data from this session.");
    sub->setStyleSheet(body_ss(true));
    sub->setWordWrap(true);
    hl->addWidget(sub);
    root->addWidget(hdr);

    sub_tabs_ = new QTabWidget;
    sub_tabs_->setDocumentMode(true);
    sub_tabs_->setStyleSheet(QString(R"(
        QTabWidget::pane{border:0;background:%1;}
        QTabBar::tab{background:%2;color:%3;padding:6px 14px;
            border:0;border-bottom:2px solid transparent;
            font-size:12px;font-weight:700;letter-spacing:0.5px;}
        QTabBar::tab:selected{color:%4;border-bottom:2px solid %4;}
        QTabBar::tab:hover:!selected{color:%5;}
    )").arg(ui::colors::BG_BASE(), ui::colors::BG_SURFACE(),
            ui::colors::TEXT_SECONDARY(), ui::colors::AMBER(),
            ui::colors::TEXT_PRIMARY()));

    sub_tabs_->addTab(build_research_tab(),      "Research");
    sub_tabs_->addTab(build_signal_guide_tab(),  "Signal Guide");
    sub_tabs_->addTab(build_sector_map_tab(),    "Sector Map");
    sub_tabs_->addTab(build_strategies_tab(),    "Strategies");

    root->addWidget(sub_tabs_, 1);
}

// ── RESEARCH TAB ──────────────────────────────────────────────────────────────

QWidget* PracticePanel::build_research_tab() {
    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* vl = new QVBoxLayout(body);
    vl->setContentsMargins(20, 16, 20, 20);
    vl->setSpacing(10);

    // Key stats row
    auto* stats_row = new QHBoxLayout;
    stats_row->setSpacing(8);
    stats_row->addWidget(make_stat("+47%", "Future leaders vs peers\n(post-STOCK Act, AInvest 2026)", ui::colors::POSITIVE()));
    stats_row->addWidget(make_stat("+12%", "Average S&P 500 annual\nreturn (benchmark)"));
    stats_row->addWidget(make_stat("45d",  "STOCK Act maximum\ndisclosure window"));
    stats_row->addWidget(make_stat("1,800+","Congressional trades\nper year (recent average)"));
    vl->addLayout(stats_row);

    vl->addWidget(make_divider(body));

    auto* h1 = new QLabel("What Academic Research Says");
    h1->setStyleSheet(heading_ss());
    vl->addWidget(h1);

    add_para(vl,
        "The landmark NBER working paper (Ziobrowski et al., 2004) found senators outperformed "
        "the market by ~12% annually before the STOCK Act (2012). After the STOCK Act required "
        "45-day disclosure, this alpha largely disappeared for rank-and-file members.");

    auto* callout1 = new QLabel(
        "Key finding: The alpha is concentrated in committee chairs and ranking members, "
        "not backbench members. A 2026 AInvest analysis found lawmakers who eventually "
        "rose to leadership outperformed peers by 47 percentage points annually.");
    callout1->setStyleSheet(callout_ss());
    callout1->setWordWrap(true);
    vl->addWidget(callout1);

    add_para(vl,
        "A 2024 ScienceDirect study of 100,000+ trades distinguishes politically informed trading "
        "(positions in sectors regulated by the member's own committee) from passive indexing. "
        "The committee-sector signal is the single strongest predictor of outperformance.");

    vl->addWidget(make_divider(body));

    auto* h2 = new QLabel("The STOCK Act Disclosure Lag");
    h2->setStyleSheet(heading_ss());
    vl->addWidget(h2);

    add_para(vl,
        "Members have up to 45 days to disclose a transaction. By the time a trade is publicly "
        "visible, the market has partially priced in the information. Research shows:");

    auto* lag_facts = new QLabel(
        "  •  Trades filed in 1–7 days: highest post-disclosure alpha (member filed immediately → "
        "high conviction)\n"
        "  •  Trades filed in 31–44 days: typically routine rebalancing\n"
        "  •  Trades filed 45+ days: STOCK Act violation — itself a signal of sensitivity\n"
        "  •  The first 2 weeks post-disclosure capture most residual alpha for rank-and-file");
    lag_facts->setStyleSheet(body_ss());
    lag_facts->setWordWrap(true);
    vl->addWidget(lag_facts);

    vl->addWidget(make_divider(body));

    auto* h3 = new QLabel("The Herd Signal");
    h3->setStyleSheet(heading_ss());
    vl->addWidget(h3);

    add_para(vl,
        "When 3+ members trade the same ticker within a 14-day window, the coordinated signal "
        "has historically been the most actionable. Bipartisan herds (both parties buying) carry "
        "2× the predictive weight of single-party clusters.");

    auto* callout2 = new QLabel(
        "Practical implication: the Signal Builder's Herd factor specifically detects "
        "these coordinated windows. Set Herd to 200% if you prioritize cluster signals.");
    callout2->setStyleSheet(callout_ss(QStringLiteral("#0891b2")));
    callout2->setWordWrap(true);
    vl->addWidget(callout2);

    vl->addWidget(make_divider(body));

    auto* h4 = new QLabel("Key Sources");
    h4->setStyleSheet(heading_ss());
    vl->addWidget(h4);

    const QStringList sources = {
        "Ziobrowski et al. (NBER w26975) — Senate stock picking pre/post STOCK Act",
        "Journal of Public Economics (2022) — Post-STOCK Act alpha disappears for rank-and-file",
        "ScienceDirect (2024) — 100,000 trades: committee-sector signal analysis",
        "AInvest (2026) — Leadership member alpha: +47% vs peers",
        "GAP-TGN (arXiv:2602.05514) — ML model: temporal graph of congressional trades",
        "Unusual Whales 2024 Report — ~$2.28B in congressional trade volume tracked",
    };
    for (const auto& s : sources) {
        auto* sl = new QLabel("  ·  " + s);
        sl->setStyleSheet(body_ss(true));
        sl->setWordWrap(true);
        vl->addWidget(sl);
    }

    vl->addStretch();
    return make_scroll(body);
}

// ── SIGNAL GUIDE TAB ──────────────────────────────────────────────────────────

QWidget* PracticePanel::build_signal_guide_tab() {
    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* vl = new QVBoxLayout(body);
    vl->setContentsMargins(20, 16, 20, 20);
    vl->setSpacing(8);

    auto* intro = new QLabel(
        "The Signal Builder computes a composite score (0–100) from 8 factors. "
        "Here is what each factor measures and how to interpret it:");
    intro->setStyleSheet(body_ss(true));
    intro->setWordWrap(true);
    vl->addWidget(intro);

    struct FactorDoc {
        const char* name;
        const char* range;
        const char* what;
        const char* high;
        const char* low;
        const char* weight_tip;
        const char* color;
    };

    const QVector<FactorDoc> docs = {
        {
            "1. Committee Overlap",    "0 or 85",
            "Does this trade fall in a sector directly regulated by the member's committee?",
            "85 — member sits on Armed Services and bought a defense stock",
            "15 — no committee connection to the traded sector",
            "Set to 200% if you focus only on committee-adjacent trades. "
            "This is the strongest single predictor per academic research.",
            ui::colors::POSITIVE()
        },
        {
            "2. Trade Size",           "10–100",
            "How large is the disclosed dollar amount? Larger = higher conviction.",
            "100 — $1M+ trade",
            "10 — $1,001–$15,000 (minimum disclosure bracket)",
            "High weight amplifies the large-position signal. "
            "Useful for tracking whale moves by wealthy members.",
            ui::colors::AMBER()
        },
        {
            "3. Disclosure Timing",    "25–95",
            "How quickly did the member file after trading? Both extremes are signals.",
            "95 — filed within 7 days (immediate conviction)\n"
            "80 — filed 45+ days (STOCK Act violation — suspicious sensitivity)",
            "25 — filed at the normal 31–44 day window",
            "Set to 200% if you want to surface either high-conviction fast filers "
            "or STOCK Act violators. The 'Fast Filers' preset optimizes for this.",
            "#f97316"
        },
        {
            "4. Herd Signal",          "0–100",
            "How many other members traded the same ticker within 14 days?",
            "90–100 — 3+ members (coordinated cluster, +30 per additional member)",
            "0 — no other members trading same ticker in window",
            "Set to 200% for the 'Herd Focus' strategy. "
            "Bipartisan herds are historically the most reliable signal.",
            "#3b82f6"
        },
        {
            "5. Historical Alpha",     "0–100",
            "The member's average signal score across all their recent trades.",
            "70–100 — member consistently makes high-signal trades",
            "0–20 — member has few trades or consistently low-signal activity",
            "Useful for identifying repeat offenders. "
            "Works best combined with Committee Overlap.",
            ui::colors::TEXT_SECONDARY()
        },
        {
            "6. Legislative Timing ⌛", "0 (pending)",
            "Is this trade within 14 days of a committee markup, vote, or floor vote?",
            "N/A — requires Congress.gov API integration",
            "N/A — coming in a future phase",
            "Will be the strongest predictive factor once Congress.gov data is wired. "
            "Leave at 0% for now.",
            "#6b7280"
        },
        {
            "7. Bill Correlation ⌛",   "0 (pending)",
            "Did the member sponsor or vote on legislation affecting this company's sector?",
            "N/A — requires Congress.gov bill stage data",
            "N/A — coming in a future phase",
            "Catches explicit legislative-financial conflicts of interest.",
            "#6b7280"
        },
        {
            "8. Lobbying Signal ⌛",    "0 (pending)",
            "Did the company's lobbyists contact this member's committee?",
            "N/A — requires LDA.gov lobbying disclosure API",
            "N/A — coming in a future phase",
            "Closes the loop: company lobbies committee → member trades company stock.",
            "#6b7280"
        },
    };

    for (const auto& d : docs) {
        auto* card = new QWidget(body);
        card->setStyleSheet(stat_card_ss());
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(14, 10, 14, 10);
        cl->setSpacing(5);

        auto* row1 = new QHBoxLayout;
        auto* name_lbl = new QLabel(d.name, card);
        name_lbl->setStyleSheet(
            QString("color:%1;font-size:13px;font-weight:700;background:transparent;")
                .arg(d.color));
        row1->addWidget(name_lbl);
        row1->addStretch();
        auto* range_lbl = new QLabel(
            QString("Range: %1").arg(d.range), card);
        range_lbl->setStyleSheet(body_ss(true));
        row1->addWidget(range_lbl);
        cl->addLayout(row1);

        auto* what_lbl = new QLabel(d.what, card);
        what_lbl->setStyleSheet(body_ss());
        what_lbl->setWordWrap(true);
        cl->addWidget(what_lbl);

        auto* examples = new QLabel(
            QString("High: %1\nLow: %2").arg(d.high, d.low), card);
        examples->setStyleSheet(body_ss(true));
        examples->setWordWrap(true);
        cl->addWidget(examples);

        auto* tip = new QLabel(QString("Strategy tip: %1").arg(d.weight_tip), card);
        tip->setStyleSheet(
            QString("color:%1;font-size:12px;font-style:italic;"
                    "background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
        tip->setWordWrap(true);
        cl->addWidget(tip);

        vl->addWidget(card);
    }

    // Try Signal Builder button
    auto* try_btn = new QPushButton("Open Signal Builder to adjust weights →");
    try_btn->setCursor(Qt::PointingHandCursor);
    try_btn->setStyleSheet(
        QString("QPushButton{background:%1;color:%2;border:1px solid %3;"
                "border-radius:3px;padding:8px 16px;font-size:12px;font-weight:600;}"
                "QPushButton:hover{border-color:%2;}")
            .arg(ui::colors::BG_RAISED(), ui::colors::AMBER(), ui::colors::BORDER_MED()));
    connect(try_btn, &QPushButton::clicked,
            this, &PracticePanel::navigate_to_signal_builder);
    vl->addWidget(try_btn);

    vl->addStretch();
    return make_scroll(body);
}

// ── SECTOR MAP TAB ────────────────────────────────────────────────────────────

QWidget* PracticePanel::build_sector_map_tab() {
    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* vl = new QVBoxLayout(body);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* intro_row = new QWidget;
    intro_row->setStyleSheet(
        QString("background:%1;border-bottom:1px solid %2;")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* il = new QVBoxLayout(intro_row);
    il->setContentsMargins(14, 8, 14, 8);
    auto* intro = new QLabel(
        "Live committee→sector correlation map from current session data. "
        "Shows which committees have members actively trading in their regulated sectors — "
        "the highest-signal overlap.");
    intro->setStyleSheet(body_ss(true));
    intro->setWordWrap(true);
    il->addWidget(intro);
    vl->addWidget(intro_row);

    // Sector map table
    sector_map_table_ = new QTableWidget;
    sector_map_table_->setColumnCount(6);
    sector_map_table_->setHorizontalHeaderLabels(
        {"COMMITTEE", "SECTOR", "MEMBERS TRADING", "TRADES", "AVG SIGNAL", "OVERLAP %"});
    sector_map_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sector_map_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    sector_map_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sector_map_table_->setShowGrid(false);
    sector_map_table_->verticalHeader()->setVisible(false);
    sector_map_table_->setFocusPolicy(Qt::NoFocus);

    auto* th = sector_map_table_->horizontalHeader();
    th->setSectionResizeMode(QHeaderView::Stretch);
    th->setSectionResizeMode(2, QHeaderView::Fixed); th->resizeSection(2, 130);
    th->setSectionResizeMode(3, QHeaderView::Fixed); th->resizeSection(3, 58);
    th->setSectionResizeMode(4, QHeaderView::Fixed); th->resizeSection(4, 80);
    th->setSectionResizeMode(5, QHeaderView::Fixed); th->resizeSection(5, 74);
    th->setStyleSheet(
        QString("QHeaderView::section{background:%1;color:%2;border:none;"
                "border-bottom:2px solid %3;border-right:1px solid %4;"
                "padding:5px 8px;font-size:12px;font-weight:700;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::AMBER(), ui::colors::BORDER_MED()));
    sector_map_table_->setStyleSheet(
        QString("QTableWidget{background:%1;color:%2;border:none;font-size:12px;"
                "font-family:Consolas,monospace;gridline-color:transparent;}"
                "QTableWidget::item{padding:4px 8px;border-bottom:1px solid %3;}"
                "QTableWidget::item:selected{background:rgba(217,119,6,0.18);color:%2;}"
                "QTableWidget::item:hover{background:%4;}"
                "QScrollBar:vertical{width:8px;background:%1;border-radius:4px;}"
                "QScrollBar::handle:vertical{background:%3;min-height:24px;border-radius:4px;}")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BORDER_MED(), ui::colors::BG_HOVER()));

    vl->addWidget(sector_map_table_, 1);

    auto* note = new QLabel(
        "  Map updates automatically when congressional trade data refreshes. "
        "High Overlap % = committee members actively trading in their regulated sector.");
    note->setStyleSheet(
        QString("color:%1;font-size:12px;padding:6px 14px;"
                "background:%2;border-top:1px solid %3;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE(),
                 ui::colors::BORDER_DIM()));
    note->setWordWrap(true);
    vl->addWidget(note);

    return body;  // no scroll — table fills the space
}

// ── STRATEGIES TAB ────────────────────────────────────────────────────────────

QWidget* PracticePanel::build_strategies_tab() {
    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* vl = new QVBoxLayout(body);
    vl->setContentsMargins(20, 16, 20, 20);
    vl->setSpacing(12);

    auto* intro = new QLabel(
        "Pre-configured signal presets with the research rationale behind each. "
        "Click 'Apply' to open Signal Builder with that preset loaded.");
    intro->setStyleSheet(body_ss(true));
    intro->setWordWrap(true);
    vl->addWidget(intro);

    struct StrategyDoc {
        const char* name;
        const char* preset_id;
        const char* tagline;
        const char* rationale;
        const char* factors;
        const char* risk;
        const char* color;
    };

    const QVector<StrategyDoc> strategies = {
        {
            "Committee Insider",    "committee_heavy",
            "Follow what regulators know",
            "The strongest academic finding: committee chairs and ranking members outperform "
            "by 47% annually when trading in their regulated sectors. This preset "
            "double-weights Committee Overlap to surface only the most relevant trades.",
            "Committee: 200%  ·  Size: 80%  ·  Herd: 100%",
            "More concentrated — fewer trades qualify, but higher average signal quality.",
            ui::colors::POSITIVE()
        },
        {
            "Herd Detector",        "herd_focus",
            "Follow coordinated clusters of members",
            "When 3+ members trade the same ticker within 14 days, the coordinated signal "
            "has historically been the most actionable. Bipartisan herds carry 2× weight. "
            "This preset surfaces all cluster activity regardless of committee connection.",
            "Herd: 200%  ·  Committee: 150%",
            "Higher volume of signals — not all clusters are committee-related.",
            "#3b82f6"
        },
        {
            "Large Position Tracker", "size_first",
            "Watch where conviction is highest",
            "Members file amounts as ranges ($1K–$15K, $15K–$50K, etc.). Larger positions "
            "signal higher conviction regardless of committee affiliation. Useful for tracking "
            "wealthy members whose minimum trades start at $500K.",
            "Size: 200%  ·  Committee: 80%  ·  Herd: 80%",
            "Does not filter for committee relevance — catches conviction but not insider info.",
            ui::colors::AMBER()
        },
        {
            "Fast Filer Alert",     "timing_sensitive",
            "Members who file immediately after trading",
            "Filing within 7 days is unusual — the 45-day window gives members time to "
            "delay. Immediate filers are either rule-followers or very confident. "
            "This preset doubles the Disclosure Timing factor to surface fast filers.",
            "Timing: 200%  ·  Committee: 100%",
            "Can surface routine trades by compliant members — combine with Committee filter.",
            "#f97316"
        },
        {
            "Default Composite",    "default",
            "Balanced across all live factors",
            "Equal weights on all four active factors. Good starting point before you "
            "know which factors matter most for your strategy.",
            "All factors: 100%  ·  History: 50%",
            "No specialization — use when exploring all trades equally.",
            ui::colors::TEXT_SECONDARY()
        },
    };

    for (const auto& s : strategies) {
        auto* card = new QWidget(body);
        card->setStyleSheet(stat_card_ss());
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(14, 12, 14, 12);
        cl->setSpacing(6);

        auto* row1 = new QHBoxLayout;
        auto* name_lbl = new QLabel(s.name, card);
        name_lbl->setStyleSheet(
            QString("color:%1;font-size:14px;font-weight:700;background:transparent;")
                .arg(s.color));
        row1->addWidget(name_lbl);
        row1->addStretch();

        auto* apply_btn = new QPushButton("Apply Preset →", card);
        apply_btn->setCursor(Qt::PointingHandCursor);
        apply_btn->setStyleSheet(
            QString("QPushButton{background:%1;color:%2;border:1px solid %3;"
                    "border-radius:3px;padding:3px 10px;font-size:12px;font-weight:600;}"
                    "QPushButton:hover{border-color:%2;}")
                .arg(ui::colors::BG_BASE(), ui::colors::AMBER(),
                     ui::colors::BORDER_MED()));
        const QString pid = s.preset_id;
        connect(apply_btn, &QPushButton::clicked, this, [this, pid]() {
            emit preset_requested(pid);
            emit navigate_to_signal_builder();
        });
        row1->addWidget(apply_btn);
        cl->addLayout(row1);

        auto* tagline = new QLabel(s.tagline, card);
        tagline->setStyleSheet(
            QString("color:%1;font-size:13px;font-weight:600;background:transparent;")
                .arg(ui::colors::TEXT_PRIMARY()));
        cl->addWidget(tagline);

        auto* rationale = new QLabel(s.rationale, card);
        rationale->setStyleSheet(body_ss());
        rationale->setWordWrap(true);
        cl->addWidget(rationale);

        auto* details_row = new QHBoxLayout;
        auto* factors_lbl = new QLabel(
            QString("Weights: %1").arg(s.factors), card);
        factors_lbl->setStyleSheet(body_ss(true));
        details_row->addWidget(factors_lbl);
        details_row->addStretch();
        cl->addLayout(details_row);

        auto* risk_lbl = new QLabel(QString("Note: %1").arg(s.risk), card);
        risk_lbl->setStyleSheet(
            QString("color:%1;font-size:12px;font-style:italic;background:transparent;")
                .arg(ui::colors::TEXT_SECONDARY()));
        risk_lbl->setWordWrap(true);
        cl->addWidget(risk_lbl);

        vl->addWidget(card);
    }

    vl->addWidget(make_divider(body));

    auto* disclaimer = new QLabel(
        "⚠  Disclaimer: This tool is for educational and research purposes only. "
        "Congressional disclosures reflect past transactions — not investment advice. "
        "All data is public STOCK Act information; academic research on alpha is "
        "retrospective and does not guarantee future returns.");
    disclaimer->setStyleSheet(
        QString("color:%1;font-size:12px;font-style:italic;background:transparent;"
                "padding:8px 0;").arg(ui::colors::TEXT_SECONDARY()));
    disclaimer->setWordWrap(true);
    vl->addWidget(disclaimer);

    vl->addStretch();
    return make_scroll(body);
}

// ── Data refresh ───────────────────────────────────────────────────────────────

void PracticePanel::set_data(const power_trader::PowerTraderSummary& summary) {
    summary_ = summary;
    refresh_sector_map();
}

void PracticePanel::refresh_sector_map() {
    if (!sector_map_table_ || summary_.recent_trades.isEmpty()) return;

    // Build committee → (member count, trade count, avg signal, total trades for overlap)
    struct CmteData {
        QString committee;
        QString sector;
        QSet<QString> member_ids;
        int trade_count  = 0;
        double sig_sum   = 0;
        int total_member_trades = 0;
    };
    QHash<QString, CmteData> cmte_map;

    // Total trades per member (for overlap %)
    QHash<QString, int> member_total;
    for (const auto& t : summary_.recent_trades)
        member_total[t.member_id]++;

    for (const auto& t : summary_.recent_trades) {
        if (t.committee_relevance.isEmpty()) continue;
        auto& c = cmte_map[t.committee_relevance];
        c.committee = t.committee_relevance;
        c.member_ids.insert(t.member_id);
        c.trade_count++;
        c.sig_sum += t.signal_score;
    }

    // Compute overlap % (committee-relevant trades / total member trades)
    for (auto& c : cmte_map) {
        for (const auto& mid : c.member_ids)
            c.total_member_trades += member_total.value(mid, 0);
        // Sector from PowerTraderService::ticker_committee_map inverse
        c.sector = QStringLiteral("—");
    }

    // sector_breakdown unused — using committee_insider_signals for sector names
    // Map committee name to top sector via committee_insider_signals
    const auto cmte_signals = power_trader::PowerTraderService::instance().committee_insider_signals();
    QHash<QString, QString> cmte_to_sector;
    for (const auto& sig : cmte_signals)
        if (!cmte_to_sector.contains(sig.committee))
            cmte_to_sector[sig.committee] = sig.sector;

    QVector<CmteData> rows = cmte_map.values().toVector();
    std::sort(rows.begin(), rows.end(),
              [](const CmteData& a, const CmteData& b) {
                  return a.trade_count > b.trade_count;
              });

    sector_map_table_->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const auto& c = rows[r];
        sector_map_table_->setRowHeight(r, 30);
        const double avg_sig  = c.trade_count > 0 ? c.sig_sum / c.trade_count : 0;
        const double overlap  = c.total_member_trades > 0
            ? (c.trade_count * 100.0 / c.total_member_trades) : 0;

        auto mk = [&](int col, const QString& txt, const char* color = nullptr,
                      Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(txt);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            sector_map_table_->setItem(r, col, item);
        };

        mk(0, c.committee, ui::colors::AMBER);
        mk(1, cmte_to_sector.value(c.committee, "—"), ui::colors::TEXT_SECONDARY);
        mk(2, QString::number(c.member_ids.size()) + " members", nullptr, Qt::AlignCenter);
        mk(3, QString::number(c.trade_count), nullptr, Qt::AlignCenter);

        auto* sig_i = new QTableWidgetItem(QString::number(avg_sig, 'f', 0));
        sig_i->setTextAlignment(Qt::AlignCenter);
        sig_i->setForeground(QColor(avg_sig >= 60 ? ui::colors::AMBER()
                                   : ui::colors::TEXT_SECONDARY()));
        sig_i->setFlags(sig_i->flags() & ~Qt::ItemIsEditable);
        sector_map_table_->setItem(r, 4, sig_i);

        auto* ov_i = new QTableWidgetItem(QString("%1%").arg(overlap, 0, 'f', 0));
        ov_i->setTextAlignment(Qt::AlignCenter);
        ov_i->setForeground(QColor(overlap > 30 ? ui::colors::POSITIVE()
                                  : overlap > 15 ? ui::colors::AMBER()
                                  : ui::colors::TEXT_SECONDARY()));
        ov_i->setFlags(ov_i->flags() & ~Qt::ItemIsEditable);
        sector_map_table_->setItem(r, 5, ov_i);
    }
}

} // namespace fincept::screens

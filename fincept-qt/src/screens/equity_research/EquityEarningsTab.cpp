// src/screens/equity_research/EquityEarningsTab.cpp
#include "screens/equity_research/EquityEarningsTab.h"

#include "services/equity/EquityResearchService.h"
#include "storage/repositories/EarningsSignalRepository.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

using services::equity::EarningsAnalysis;
using services::equity::EarningsVerdict;
using services::equity::SignalDirection;

// ── File-local presentation helpers ──────────────────────────────────────────
// Panel / card chrome deliberately mirrors EquityAnalysisTab so the two tabs
// read as one surface when the user flips between them.
namespace {

QFrame* make_panel(const QString& title, const QString& accent_color, QVBoxLayout** body_out) {
    auto* f = new QFrame;
    f->setStyleSheet(QString("QFrame { background:%1; border:1px solid %2; border-radius:4px; }")
                         .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* vl = new QVBoxLayout(f);
    vl->setContentsMargins(16, 14, 16, 16);
    vl->setSpacing(12);

    auto* hdr = new QWidget(nullptr);
    hdr->setStyleSheet(QString("background:transparent; border:0; border-bottom:2px solid %1;").arg(accent_color));
    auto* hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(0, 0, 0, 8);
    hl->setSpacing(8);

    auto* bar = new QFrame;
    bar->setFixedSize(4, 16);
    bar->setStyleSheet(QString("background:%1; border:0; border-radius:0;").arg(accent_color));
    hl->addWidget(bar);

    auto* lbl = new QLabel(title);
    lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px; "
                               "background:transparent; border:0;")
                           .arg(accent_color));
    hl->addWidget(lbl);
    hl->addStretch();
    vl->addWidget(hdr);

    if (body_out) *body_out = vl;
    return f;
}

QLabel* make_caption(const QString& text) {
    auto* l = new QLabel(text);
    l->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px; "
                             "background:transparent; border:0;")
                         .arg(ui::colors::TEXT_SECONDARY()));
    return l;
}

QLabel* make_value(QLabel*& out, const QString& color, int px = 16) {
    out = new QLabel(ui::formatting::placeholder());
    out->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:monospace; "
                               "background:transparent; border:0;")
                           .arg(color).arg(px));
    return out;
}

/// Small stacked "LABEL / value" cell used inside the summary cards.
QWidget* make_stat(const QString& label, QLabel*& val_out, const QString& color, int px = 16) {
    auto* w = new QWidget(nullptr);
    w->setStyleSheet("background:transparent; border:0;");
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(3);
    vl->addWidget(make_caption(label));
    vl->addWidget(make_value(val_out, color, px));
    return w;
}

void style_table(QTableWidget* t, const QStringList& headers) {
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setStyleSheet(QString(R"(
        QTableWidget {
            background:%1; alternate-background-color:%2;
            gridline-color:%3; color:%4; border:0; font-size:12px;
        }
        QHeaderView::section {
            background:%5; color:%6; font-size:12px; font-weight:700;
            padding:5px 4px; border:0; border-bottom:1px solid %3;
            letter-spacing:1px;
        }
        QTableWidget::item { padding:2px 6px; }
    )")
                         .arg(ui::colors::BG_SURFACE(), ui::colors::BG_BASE(), ui::colors::BORDER_DIM(),
                              ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY()));
    t->setAlternatingRowColors(true);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    // No horizontal scrolling: these tables are fixed-height and sized to
    // their content, so a scrollbar appearing would both hide columns and
    // steal the pixels the last row is standing on. Columns compress instead.
    t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(24);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->horizontalHeader()->setStretchLastSection(true);
    // Column 0 is the row label ("CURRENT YEAR", a report date) — an equal
    // share of a narrow table elides it. Everything else is numeric and
    // stretches fine.
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
}

/// Table row height + a little chrome; tables live inside a scroll column, so
/// they must size to their content rather than scroll internally.
void fit_table_height(QTableWidget* t) {
    const int rows = t->rowCount();
    // height() is 0 until the header has been laid out — which is the case on
    // the first populate, before the tab has ever been shown. sizeHint is
    // available immediately, so take whichever is larger.
    const int header = std::max(t->horizontalHeader()->height(),
                                t->horizontalHeader()->sizeHint().height());
    t->setFixedHeight(header + rows * t->verticalHeader()->defaultSectionSize() + 4);
}

/// Repaint a monospace stat value with a state-dependent colour. Always call
/// it (even for the placeholder) so a value left red by the previous symbol
/// doesn't bleed into the next one.
/// `px` must match the size the label was built with — the stylesheet is
/// replaced wholesale, so repainting at the default would quietly resize any
/// stat that was deliberately made smaller.
void set_stat(QLabel* l, const QString& text, const QString& color, int px = 16) {
    l->setText(text);
    l->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:monospace; "
                             "background:transparent; border:0;")
                         .arg(color).arg(px));
}

QTableWidgetItem* cell(const QString& text, const QString& color = QString(),
                       Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter) {
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(align);
    if (!color.isEmpty()) it->setForeground(QColor(color));
    return it;
}

QString color_for(double v) {
    if (v > 0) return ui::colors::POSITIVE();
    if (v < 0) return ui::colors::NEGATIVE();
    return ui::colors::TEXT_PRIMARY();
}

QString opt_pct(const std::optional<double>& v, int dp = 1, bool sign = true) {
    if (!v.has_value()) return ui::formatting::placeholder();
    return sign ? QString("%1%2%").arg(*v >= 0 ? "+" : "").arg(QString::number(*v, 'f', dp))
                : QString("%1%").arg(QString::number(*v, 'f', dp));
}

QString opt_num(const std::optional<double>& v, int dp = 2) {
    if (!v.has_value()) return ui::formatting::placeholder();
    return QString::number(*v, 'f', dp);
}

QString opt_compact(const std::optional<double>& v) {
    if (!v.has_value()) return ui::formatting::placeholder();
    return ui::formatting::format_compact(*v, 1);
}

QString opt_count(const std::optional<double>& v) {
    if (!v.has_value()) return ui::formatting::placeholder();
    return QString::number(static_cast<int>(*v));
}

} // namespace

// ── EquityEarningsTab ────────────────────────────────────────────────────────

EquityEarningsTab::EquityEarningsTab(QWidget* parent) : QWidget(parent) {
    build_ui();
    set_metric(selected_metric_);   // paints the initial toggle state
}

void EquityEarningsTab::set_symbol(const QString& symbol) {
    if (symbol.isEmpty())
        return;
    // Re-subscribing for the SAME symbol is deliberate. This is called every
    // time the tab is activated, and returning early meant a tab left open
    // across a print kept showing the picture it was first opened with —
    // consensus, the reported actual and the reaction all land within a day of
    // each other, and none of them arrived. QueryStore serves its cached copy
    // while it is fresh and revalidates when it isn't, so the re-subscribe
    // costs nothing when nothing has changed.
    const bool new_symbol = (symbol != current_symbol_);
    current_symbol_ = symbol;

    if (new_symbol) {
        // Only a genuinely new symbol blanks the panel. Flashing the overlay
        // over data that is about to be repainted with the same numbers reads
        // as a stutter every time the user comes back to the tab.
        loading_overlay_->show_loading("LOADING EARNINGS…");
        message_label_->hide();
        content_widget_->show();
    }

    auto& store = services::query::QueryStore::instance();
    store.unsubscribe_all(this);
    services::equity::EquityResearchService::instance().subscribe_earnings_analysis(
        this, symbol,
        [this](const services::query::QueryStore::State& s) { apply_state(s); });
}

void EquityEarningsTab::apply_state(const services::query::QueryStore::State& s) {
    const bool have_data = s.data.isValid() && !s.data.isNull();

    if (!s.error.isEmpty() && !have_data) {
        loading_overlay_->hide_loading();
        show_message(QString("Couldn't load earnings data — %1").arg(s.error));
        // Forget the symbol so re-activating the tab retries. QueryStore
        // leaves no cached value behind a failed fetch, so the re-subscribe
        // takes its cold path and kicks a fresh one; without this reset
        // set_symbol's same-symbol guard would leave the tab dead until the
        // user picked a different ticker.
        current_symbol_.clear();
        return;
    }
    if (!have_data)
        return;  // still loading; the overlay stays up

    // A failed *revalidate* arrives as error + the previously cached data.
    // Keep rendering that data — replacing a good panel with an error page
    // because a background refresh timed out loses more than it tells.
    loading_overlay_->hide_loading();
    const auto analysis = s.data.value<EarningsAnalysis>();
    if (!analysis.valid || !analysis.has_content()) {
        show_message(QStringLiteral(
            "No earnings data is published for this security.\n\n"
            "ETFs, index trackers and mutual funds don't report earnings — "
            "check the holdings' own tickers instead."));
        return;
    }
    message_label_->hide();
    content_widget_->show();
    populate(analysis);
}

void EquityEarningsTab::show_message(const QString& text) {
    message_label_->setText(text);
    message_label_->show();
    content_widget_->hide();
}

void EquityEarningsTab::build_ui() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    loading_overlay_ = new ui::LoadingOverlay(this);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background:transparent; border:0;");

    auto* outer = new QWidget(this);
    auto* outer_vl = new QVBoxLayout(outer);
    outer_vl->setContentsMargins(12, 12, 12, 12);
    outer_vl->setSpacing(10);

    message_label_ = new QLabel;
    message_label_->setWordWrap(true);
    message_label_->setAlignment(Qt::AlignCenter);
    message_label_->setStyleSheet(QString("color:%1; font-size:13px; padding:40px; background:transparent;")
                                      .arg(ui::colors::TEXT_SECONDARY()));
    message_label_->hide();
    outer_vl->addWidget(message_label_);

    content_widget_ = new QWidget(outer);
    content_widget_->setStyleSheet("background:transparent;");
    auto* vl = new QVBoxLayout(content_widget_);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    vl->addWidget(build_summary_row());
    vl->addWidget(build_scorecard());
    vl->addWidget(build_history_panel());
    vl->addWidget(build_current_panel());
    vl->addWidget(build_future_panel());

    auto* footer = new QLabel(
        "Consensus, estimates and revisions from Yahoo Finance · reactions computed from daily closes. "
        "The scorecard is a rules-based summary of the panels above, not investment advice — "
        "an earnings print can break either way regardless of the setup.");
    footer->setWordWrap(true);
    footer->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                              .arg(ui::colors::TEXT_TERTIARY()));
    vl->addWidget(footer);
    vl->addStretch();

    outer_vl->addWidget(content_widget_);
    scroll->setWidget(outer);

    auto* ol = new QVBoxLayout(this);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addWidget(scroll);
}

QWidget* EquityEarningsTab::build_summary_row() {
    auto* row = new QWidget(nullptr);
    row->setStyleSheet("background:transparent;");
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    // ── Next report ──────────────────────────────────────────────────────────
    QVBoxLayout* next_body = nullptr;
    auto* next_panel = make_panel("NEXT REPORT", ui::colors::AMBER(), &next_body);
    next_date_ = new QLabel(ui::formatting::placeholder());
    next_date_->setStyleSheet(QString("color:%1; font-size:20px; font-weight:700; "
                                      "background:transparent; border:0;")
                                  .arg(ui::colors::TEXT_PRIMARY()));
    next_body->addWidget(next_date_);

    next_countdown_ = new QLabel;
    next_countdown_->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700; "
                                           "background:transparent; border:0;")
                                       .arg(ui::colors::AMBER()));
    next_body->addWidget(next_countdown_);

    next_confirmed_ = new QLabel;
    next_confirmed_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                       .arg(ui::colors::TEXT_TERTIARY()));
    next_body->addWidget(next_confirmed_);

    auto* next_grid = new QGridLayout;
    next_grid->setSpacing(12);
    next_grid->addWidget(make_stat("CONSENSUS EPS", next_eps_, ui::colors::TEXT_PRIMARY()), 0, 0);
    next_grid->addWidget(make_stat("EPS RANGE", next_eps_range_, ui::colors::TEXT_SECONDARY(), 13), 0, 1);
    next_grid->addWidget(make_stat("CONSENSUS REVENUE", next_rev_, ui::colors::TEXT_PRIMARY()), 1, 0);
    next_grid->addWidget(make_stat("EXPECTED YoY", next_yoy_, ui::colors::TEXT_PRIMARY()), 1, 1);
    next_body->addLayout(next_grid);
    next_body->addStretch();
    hl->addWidget(next_panel, 1);

    // ── Verdict ──────────────────────────────────────────────────────────────
    QVBoxLayout* verdict_body = nullptr;
    auto* verdict_panel = make_panel("PRE-EARNINGS SIGNAL", "#a855f7", &verdict_body);

    auto* badge_row = new QHBoxLayout;
    badge_row->setSpacing(10);
    verdict_badge_ = new QLabel("—");
    verdict_badge_->setAlignment(Qt::AlignCenter);
    verdict_badge_->setMinimumWidth(96);
    verdict_badge_->setStyleSheet(QString("color:%1; background:%2; border:0; border-radius:3px; "
                                          "padding:6px 14px; font-size:20px; font-weight:700; letter-spacing:2px;")
                                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_SECONDARY()));
    badge_row->addWidget(verdict_badge_);

    auto* score_box = new QVBoxLayout;
    score_box->setSpacing(2);
    verdict_score_ = new QLabel(ui::formatting::placeholder());
    verdict_score_->setStyleSheet(QString("color:%1; font-size:16px; font-weight:700; font-family:monospace; "
                                          "background:transparent; border:0;")
                                      .arg(ui::colors::TEXT_PRIMARY()));
    score_box->addWidget(verdict_score_);
    verdict_confidence_ = new QLabel;
    verdict_confidence_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                           .arg(ui::colors::TEXT_SECONDARY()));
    score_box->addWidget(verdict_confidence_);
    badge_row->addLayout(score_box);
    badge_row->addStretch();

    verdict_axes_ = new QLabel;
    verdict_axes_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    verdict_axes_->setToolTip(QStringLiteral(
        "SETUP is how the business and the street's view of it read: track record, "
        "revisions, guidance drift, breadth, expected growth.\n\n"
        "BAR is how much of that is already in the price: the expectations gap, "
        "positioning, and where the price sits against analyst targets.\n\n"
        "They are the same composite split in two. A high setup against a deeply "
        "negative bar is the classic beat-and-fall configuration — good company, "
        "nothing left to surprise anyone with."));
    badge_row->addWidget(verdict_axes_);
    verdict_body->addLayout(badge_row);

    verdict_headline_ = new QLabel;
    verdict_headline_->setWordWrap(true);
    verdict_headline_->setStyleSheet(QString("color:%1; font-size:13px; background:transparent; border:0;")
                                         .arg(ui::colors::TEXT_PRIMARY()));
    verdict_body->addWidget(verdict_headline_);

    verdict_horizon_ = new QLabel;
    verdict_horizon_->setWordWrap(true);
    verdict_horizon_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                        .arg(ui::colors::TEXT_TERTIARY()));
    verdict_body->addWidget(verdict_horizon_);

    // Caveats live with the verdict, not down in the breakdown: they are the
    // reasons not to trust the badge sitting immediately above them, and that
    // is exactly where they need to be read.
    caveats_label_ = new QLabel;
    caveats_label_->setWordWrap(true);
    caveats_label_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0; "
                                          "border-top:1px solid %2; padding-top:8px;")
                                      .arg(ui::colors::WARNING(), ui::colors::BORDER_DIM()));
    verdict_body->addWidget(caveats_label_);
    verdict_body->addStretch();
    hl->addWidget(verdict_panel, 2);

    // ── Setup / expected move ────────────────────────────────────────────────
    QVBoxLayout* setup_body = nullptr;
    auto* setup_panel = make_panel("THE SETUP", "#22d3ee", &setup_body);
    auto* setup_grid = new QGridLayout;
    setup_grid->setSpacing(12);
    setup_grid->addWidget(make_stat("PREDICTED MOVE", setup_predicted_, "#22d3ee"), 0, 0);
    setup_grid->addWidget(make_stat("TYPICAL MOVE", setup_move_, ui::colors::TEXT_PRIMARY(), 13), 0, 1);
    setup_grid->addWidget(make_stat("EST SPREAD", setup_spread_, ui::colors::TEXT_PRIMARY(), 13), 3, 1);
    setup_grid->addWidget(make_stat("AVG REACTION", setup_reaction_, ui::colors::TEXT_PRIMARY(), 13), 1, 0);
    setup_grid->addWidget(make_stat("RUN-UP 5D", setup_runup_, ui::colors::TEXT_PRIMARY(), 13), 1, 1);
    setup_grid->addWidget(make_stat("RUN-UP 20D", setup_runup20_, ui::colors::TEXT_PRIMARY(), 13), 2, 0);
    setup_grid->addWidget(make_stat("BEAT RATE", setup_beat_, ui::colors::TEXT_PRIMARY(), 13), 3, 0);
    setup_predicted_->setToolTip(QStringLiteral(
        "The engine's own estimate for the next-session move, signed. Computed, not asked of "
        "a model: the composite's lean scaled by how far this name typically travels on a "
        "print, scaled again by how much of the scorecard actually had data behind it.\n\n"
        "It has no established accuracy — that is precisely why every reading is written down "
        "before the print and held against the outcome in SIGNAL vs OUTCOME below. Treat the "
        "TYPICAL MOVE beside it as the range that matters far more than the point."));
    setup_move_->setToolTip(QStringLiteral(
        "Mean absolute close-to-close move over past prints — how big a session this name "
        "usually has on earnings, regardless of direction."));
    setup_spread_->setToolTip(QStringLiteral(
        "How far apart analysts are on the coming quarter's EPS: (high − low) as a share of "
        "the mean. A risk gauge, not a direction — a wide spread means a bigger surprise "
        "whichever way it lands, which is why it never moves the score."));
    setup_runup_->setToolTip(QStringLiteral("Price change over the last 5 sessions."));
    setup_runup20_->setToolTip(QStringLiteral(
        "Price change over the last 20 sessions — the slower version of the same question: "
        "how much of the expected news is already in the price."));
    setup_body->addLayout(setup_grid);
    setup_body->addStretch();
    hl->addWidget(setup_panel, 1);

    return row;
}

QWidget* EquityEarningsTab::build_scorecard() {
    auto* row = new QWidget(nullptr);
    row->setStyleSheet("background:transparent;");
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    QVBoxLayout* body = nullptr;
    auto* panel = make_panel("SIGNAL BREAKDOWN", "#a855f7", &body);

    score_rows_layout_ = new QVBoxLayout;
    score_rows_layout_->setContentsMargins(0, 0, 0, 0);
    score_rows_layout_->setSpacing(6);
    body->addLayout(score_rows_layout_);
    // 4 / 5, matching the quarters-table / reaction-chart row below, so the
    // chart in this row sits directly above the one in that row and a quarter
    // can be read straight down through both.
    hl->addWidget(panel, 4);

    QVBoxLayout* chart_body = nullptr;
    auto* chart_panel = make_panel("SIGNAL vs OUTCOME · PREDICTED AGAINST REALISED", "#a855f7",
                                   &chart_body);
    outcome_chart_ = new SignalOutcomeChart;
    chart_body->addWidget(outcome_chart_, 1);

    outcome_note_ = new QLabel;
    outcome_note_->setWordWrap(true);
    outcome_note_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                     .arg(ui::colors::TEXT_TERTIARY()));
    chart_body->addWidget(outcome_note_);
    hl->addWidget(chart_panel, 5);

    return row;
}

void EquityEarningsTab::fill_outcome_chart(const EarningsAnalysis& a, const EarningsVerdict& v) {
    const auto recorded = EarningsSignalRepository::instance().for_symbol(a.symbol);
    outcome_chart_->set_data(a, recorded, v);

    // The dotted stretch needs explaining every time, not once in a tooltip:
    // a reader comparing two lines will otherwise take the muted one for a
    // weak signal rather than a partially-blind one.
    const QString base = QStringLiteral(
        "Quarters before today are rebuilt from the backward legs alone — surprise record, "
        "reaction history, run-up — because consensus and revisions are published without "
        "history and cannot be recovered. Those run through the same formula with the missing "
        "legs counted absent, so they predict smaller moves than a live reading will. Solid "
        "from the first estimate actually recorded before a print.");
    const QString stats = outcome_chart_->summary();
    outcome_note_->setText(stats.isEmpty() ? base : stats + QStringLiteral(". ") + base);
}

QWidget* EquityEarningsTab::build_history_panel() {
    auto* row = new QWidget(nullptr);
    row->setStyleSheet("background:transparent;");
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    // ── Left: the numbers ────────────────────────────────────────────────────
    QVBoxLayout* body = nullptr;
    auto* panel = make_panel("PAST · REPORTED QUARTERS", ui::colors::POSITIVE(), &body);

    history_summary_ = new QLabel;
    history_summary_->setWordWrap(true);
    history_summary_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                        .arg(ui::colors::TEXT_SECONDARY()));
    body->addWidget(history_summary_);

    history_table_ = new QTableWidget;
    // Short headers: eight columns share this pane with the chart, and an
    // elided "1D REACTIO…" reads worse than a terse but complete label.
    style_table(history_table_,
                {"REPORTED", "EPS EST", "EPS ACT", "SURPRISE", "QoQ", "YoY", "1D MOVE", "5D RUN-UP"});
    body->addWidget(history_table_);
    hl->addWidget(panel, 4);

    // ── Right: the same quarters as a curve ──────────────────────────────────
    QVBoxLayout* chart_body = nullptr;
    auto* chart_panel = make_panel("PAST · EARNINGS CHANGE vs NEXT-DAY MOVE", ui::colors::POSITIVE(), &chart_body);

    auto* switch_row = new QHBoxLayout;
    switch_row->setSpacing(4);
    switch_row->setContentsMargins(0, 0, 0, 0);
    switch_row->addWidget(make_caption("BARS:"));
    for (const auto m : {services::equity::ReactionMetric::QoQ,
                         services::equity::ReactionMetric::YoY,
                         services::equity::ReactionMetric::Surprise}) {
        auto* btn = new QPushButton;
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { background:transparent; color:%1; border:1px solid %2;"
                    "  font-size:12px; font-weight:700; border-radius:2px; padding:2px 8px; }"
                    "QPushButton:checked { background:%3; color:%4; border-color:%3; }"
                    "QPushButton:hover:!checked { color:%5; border-color:%5; }")
                .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER(),
                     ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));
        connect(btn, &QPushButton::clicked, this, [this, m]() { set_metric(m); });
        metric_buttons_.insert(m, btn);
        switch_row->addWidget(btn);
    }
    switch_row->addStretch();
    chart_body->addLayout(switch_row);

    reaction_chart_ = new EarningsReactionChart;
    chart_body->addWidget(reaction_chart_, 1);

    correlation_note_ = new QLabel;
    correlation_note_->setWordWrap(true);
    correlation_note_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                         .arg(ui::colors::TEXT_TERTIARY()));
    chart_body->addWidget(correlation_note_);

    hl->addWidget(chart_panel, 5);
    return row;
}

void EquityEarningsTab::set_metric(services::equity::ReactionMetric m) {
    selected_metric_ = m;
    for (auto it = metric_buttons_.constBegin(); it != metric_buttons_.constEnd(); ++it)
        it.value()->setChecked(it.key() == m);
    if (reaction_chart_)
        reaction_chart_->set_metric(m);
}

void EquityEarningsTab::fill_correlations(const EarningsAnalysis& a) {
    const auto correlations = services::equity::correlate_reactions(a);

    const services::equity::ReactionCorrelation* strongest = nullptr;
    for (const auto& c : correlations) {
        auto* btn = metric_buttons_.value(c.metric, nullptr);
        if (btn) {
            // The button carries its own r, so picking a series is an informed
            // choice rather than a guess.
            btn->setText(c.r.has_value()
                             ? QString("%1  r%2%3").arg(c.label,
                                                        *c.r >= 0 ? "+" : "",
                                                        QString::number(*c.r, 'f', 2))
                             : c.label);
            btn->setToolTip(c.r.has_value()
                                ? QString("Correlation between %1 and the next-session move, "
                                          "measured over %2 reported quarters.")
                                      .arg(c.label.toLower())
                                      .arg(c.n)
                                : QString("Not enough quarters carry both %1 and a price reaction.")
                                      .arg(c.label.toLower()));
        }
        if (c.r.has_value() && (!strongest || std::abs(*c.r) > std::abs(*strongest->r)))
            strongest = &c;
    }

    if (!strongest) {
        correlation_note_->setText(QStringLiteral(
            "Not enough reported quarters with price history to measure a relationship."));
        return;
    }
    // Say plainly how little a dozen quarters proves. The panel exists so the
    // reader can see whether this name trades off earnings size at all — it is
    // not a fitted predictor, and presenting r without n would imply it was.
    const QString strength = std::abs(*strongest->r) >= 0.6   ? QStringLiteral("tracks")
                             : std::abs(*strongest->r) >= 0.3 ? QStringLiteral("loosely tracks")
                                                              : QStringLiteral("barely tracks");
    correlation_note_->setText(
        QString("Of the three, the next-session move %1 %2 (r %3%4 over %5 quarters). "
                "At this sample size treat it as directional colour, not a predictor — and note "
                "QoQ carries the company's seasonality, which YoY strips out.")
            .arg(strength, strongest->label.toLower(),
                 *strongest->r >= 0 ? "+" : "", QString::number(*strongest->r, 'f', 2))
            .arg(strongest->n));
}

QWidget* EquityEarningsTab::build_current_panel() {
    auto* row = new QWidget(nullptr);
    row->setStyleSheet("background:transparent;");
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    QVBoxLayout* trend_body = nullptr;
    auto* trend_panel = make_panel("CURRENT · WHERE CONSENSUS HAS MOVED", "#eab308", &trend_body);
    trend_body->addWidget(make_caption("CONSENSUS EPS, NOW VS EARLIER"));
    trend_table_ = new QTableWidget;
    style_table(trend_table_, {"PERIOD", "CURRENT", "7D AGO", "30D AGO", "60D AGO", "90D AGO", "Δ 90D"});
    trend_body->addWidget(trend_table_);
    hl->addWidget(trend_panel, 1);

    QVBoxLayout* rev_body = nullptr;
    auto* rev_panel = make_panel("CURRENT · WHO IS REVISING", "#eab308", &rev_body);
    rev_body->addWidget(make_caption("ANALYST ESTIMATE CHANGES"));
    revisions_table_ = new QTableWidget;
    style_table(revisions_table_, {"PERIOD", "UP 7D", "DOWN 7D", "UP 30D", "DOWN 30D", "NET 30D"});
    rev_body->addWidget(revisions_table_);
    hl->addWidget(rev_panel, 1);

    return row;
}

QWidget* EquityEarningsTab::build_future_panel() {
    auto* row = new QWidget(nullptr);
    row->setStyleSheet("background:transparent;");
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    QVBoxLayout* est_body = nullptr;
    auto* est_panel = make_panel("FUTURE · ANALYST ESTIMATES", "#60a5fa", &est_body);
    estimates_table_ = new QTableWidget;
    style_table(estimates_table_,
                {"PERIOD", "EPS", "LOW–HIGH", "ANALYSTS", "EPS YoY", "REVENUE", "REV YoY"});
    est_body->addWidget(estimates_table_);
    hl->addWidget(est_panel, 2);

    QVBoxLayout* growth_body = nullptr;
    auto* growth_panel = make_panel("FUTURE · VS INDEX", "#60a5fa", &growth_body);
    growth_table_ = new QTableWidget;
    style_table(growth_table_, {"PERIOD", "STOCK", "INDEX", "EDGE"});
    growth_body->addWidget(growth_table_);
    hl->addWidget(growth_panel, 1);

    return row;
}

// ── Population ───────────────────────────────────────────────────────────────

void EquityEarningsTab::populate(const EarningsAnalysis& a) {
    currency_ = a.currency.isEmpty() ? QStringLiteral("USD") : a.currency;
    const EarningsVerdict verdict = services::equity::evaluate_earnings(a);

    // ── Next report ──────────────────────────────────────────────────────────
    if (a.next.timestamp.has_value()) {
        // Render in US market time, not the viewer's: "30 Jul, after close" is
        // a fact about the exchange session. Read locally it would drift a
        // date for anyone west of ET and mislabel every after-close print.
        const auto when = QDateTime::fromSecsSinceEpoch(*a.next.timestamp)
                              .toTimeZone(QTimeZone("America/New_York"));
        next_date_->setText(when.toString("ddd d MMM yyyy"));
        const int days = services::equity::days_to_next_earnings(a);
        const QString slot = when.time().hour() >= 16   ? QStringLiteral("after close")
                             : when.time().hour() <= 9  ? QStringLiteral("before open")
                                                        : QStringLiteral("intraday");
        next_countdown_->setText(days < 0    ? QStringLiteral("date has passed — awaiting update")
                                 : days == 0 ? QString("TODAY · %1 ET").arg(slot)
                                             : QString("IN %1 DAY%2 · %3 ET")
                                                   .arg(days).arg(days == 1 ? "" : "S", slot));
        next_confirmed_->setText(a.next.is_estimated ? "Estimated date — not yet confirmed by the company"
                                                     : "Company-confirmed date");
    } else {
        next_date_->setText(ui::formatting::placeholder());
        next_countdown_->setText(QStringLiteral("No scheduled report"));
        next_confirmed_->clear();
    }

    next_eps_->setText(a.next.eps_avg.has_value()
                           ? ui::formatting::format_money(*a.next.eps_avg, currency_)
                           : ui::formatting::placeholder());
    next_eps_range_->setText(
        a.next.eps_low.has_value() && a.next.eps_high.has_value()
            ? QString("%1 – %2").arg(ui::formatting::format_money(*a.next.eps_low, currency_),
                                     ui::formatting::format_money(*a.next.eps_high, currency_))
            : ui::formatting::placeholder());
    next_rev_->setText(opt_compact(a.next.rev_avg));

    if (a.next.eps_growth.has_value()) {
        const double g = *a.next.eps_growth * 100.0;
        set_stat(next_yoy_, QString("EPS %1").arg(opt_pct(g)), color_for(g));
    } else {
        set_stat(next_yoy_, ui::formatting::placeholder(), ui::colors::TEXT_PRIMARY());
    }

    // ── Verdict card ─────────────────────────────────────────────────────────
    const QString verdict_color = verdict.direction == SignalDirection::Bullish  ? ui::colors::POSITIVE()
                                  : verdict.direction == SignalDirection::Bearish ? ui::colors::NEGATIVE()
                                                                                  : ui::colors::WARNING();
    verdict_badge_->setText(verdict.label);
    verdict_badge_->setStyleSheet(QString("color:%1; background:%2; border:0; border-radius:3px; "
                                          "padding:6px 14px; font-size:20px; font-weight:700; letter-spacing:2px;")
                                      .arg(ui::colors::BG_BASE(), verdict_color));
    verdict_score_->setText(QString("SCORE %1%2 / 100")
                                .arg(verdict.score >= 0 ? "+" : "")
                                .arg(QString::number(verdict.score, 'f', 0)));
    verdict_score_->setStyleSheet(QString("color:%1; font-size:16px; font-weight:700; font-family:monospace; "
                                          "background:transparent; border:0;")
                                      .arg(verdict_color));
    verdict_confidence_->setText(QString("confidence %1% · %2 of %3 signals have data")
                                     .arg(QString::number(verdict.confidence * 100.0, 'f', 0))
                                     .arg(std::count_if(verdict.components.begin(), verdict.components.end(),
                                                        [](const auto& c) { return c.available; }))
                                     .arg(verdict.components.size()));
    verdict_headline_->setText(verdict.headline);
    verdict_horizon_->setText(verdict.horizon_note);

    // Axes: two monospace numbers, each coloured on its own sign, so a strong
    // business against a high bar reads as the tension it is rather than as
    // one averaged-out figure.
    auto axis_span = [](const char* label, const std::optional<double>& v) {
        if (!v.has_value())
            return QString("<span style='color:%1'>%2 —</span>")
                .arg(ui::colors::TEXT_TERTIARY(), QString::fromLatin1(label));
        return QString("<span style='color:%1'>%2</span> "
                       "<span style='color:%3; font-weight:700'>%4%5</span>")
            .arg(ui::colors::TEXT_TERTIARY(), QString::fromLatin1(label), color_for(*v),
                 *v >= 0 ? "+" : "", QString::number(*v, 'f', 0));
    };
    verdict_axes_->setText(QString("<div style='font-family:monospace; font-size:12px'>%1<br>%2</div>")
                               .arg(axis_span("SETUP", verdict.setup_score),
                                    axis_span("BAR  ", verdict.bar_score)));

    // ── Setup card ───────────────────────────────────────────────────────────
    set_stat(setup_predicted_,
             verdict.predicted_move_pct
                 ? QString("%1%2%").arg(*verdict.predicted_move_pct >= 0 ? "+" : "")
                       .arg(QString::number(*verdict.predicted_move_pct, 'f', 2))
                 : ui::formatting::placeholder(),
             verdict.predicted_move_pct ? color_for(*verdict.predicted_move_pct) : "#22d3ee");
    set_stat(setup_move_,
             verdict.typical_move_pct > 0
                 ? QString("±%1%").arg(QString::number(verdict.typical_move_pct, 'f', 1))
                 : ui::formatting::placeholder(),
             ui::colors::TEXT_PRIMARY(), 13);
    // Beat rate is descriptive only now — the track-record leg scores the size
    // of the surprise against its own spread, because nearly every large cap
    // beats. Colouring it as though a high rate were bullish would contradict
    // the leg sitting directly below it, so it stays neutral.
    set_stat(setup_beat_,
             verdict.scored_quarters > 0 ? QString("%1%  (%2q)")
                                               .arg(QString::number(verdict.beat_rate * 100.0, 'f', 0))
                                               .arg(verdict.scored_quarters)
                                         : ui::formatting::placeholder(),
             ui::colors::TEXT_PRIMARY(), 13);
    set_stat(setup_reaction_,
             verdict.reaction_quarters > 0 ? opt_pct(verdict.avg_reaction_pct)
                                           : ui::formatting::placeholder(),
             verdict.reaction_quarters > 0 ? color_for(verdict.avg_reaction_pct)
                                           : ui::colors::TEXT_PRIMARY(),
             13);
    // The consensus spread is a magnitude, never a direction: amber when it is
    // wide enough to matter, plain otherwise, but never red or green.
    set_stat(setup_spread_,
             verdict.dispersion_pct.has_value()
                 ? QString("%1% wide").arg(QString::number(*verdict.dispersion_pct, 'f', 0))
                 : ui::formatting::placeholder(),
             verdict.dispersion_is_wide ? ui::colors::WARNING() : ui::colors::TEXT_PRIMARY(),
             13);
    set_stat(setup_runup_, opt_pct(a.runup_5d_pct),
             a.runup_5d_pct.has_value() ? color_for(*a.runup_5d_pct) : ui::colors::TEXT_PRIMARY(), 13);
    set_stat(setup_runup20_, opt_pct(a.runup_20d_pct),
             a.runup_20d_pct.has_value() ? color_for(*a.runup_20d_pct) : ui::colors::TEXT_PRIMARY(), 13);

    fill_scorecard(verdict);
    record_and_resolve(a, verdict);
    fill_outcome_chart(a, verdict);
    fill_history(a, verdict);
    fill_trend(a);
    fill_revisions(a);
    fill_estimates(a);
    fill_growth(a);
}

// ── Signal ledger ────────────────────────────────────────────────────────────
// Writes only. Yahoo's estimates and revisions are a point-in-time snapshot
// with no history, so what the scorecard would have said before a past print
// cannot be reconstructed — the only way to ever check the prediction is to
// write each reading down as it happens and settle it afterwards.
//
// It has no panel. A table of one pending row restates the scorecard sitting
// directly above it and earns none of the space it costs; the comparison only
// becomes worth showing once there are enough settled prints to say something
// the card doesn't already. Until then this accumulates quietly.

void EquityEarningsTab::record_and_resolve(const EarningsAnalysis& a, const EarningsVerdict& v) {
    auto& ledger = EarningsSignalRepository::instance();

    // ── Settle anything whose print has since landed ─────────────────────────
    // Matched on the calendar date in market time, not the raw timestamp:
    // Yahoo routinely shifts a scheduled 16:00 placeholder to the actual
    // announcement time once the company reports, so the seconds never agree
    // even though it is plainly the same event.
    const QTimeZone et("America/New_York");
    auto et_date = [&et](qint64 ts) {
        return QDateTime::fromSecsSinceEpoch(ts).toTimeZone(et).date();
    };
    for (const auto& pending : ledger.unresolved(a.symbol)) {
        const QDate want = et_date(pending.report_ts);
        for (const auto& p : a.history) {
            if (p.is_estimate || !p.reaction_pct.has_value())
                continue;   // no settled reaction yet — leave it open
            if (et_date(p.timestamp) != want)
                continue;
            ledger.resolve(a.symbol, pending.report_ts, p.eps_actual, p.surprise_pct,
                           *p.reaction_pct);
            break;
        }
    }

    // ── Write today's reading ────────────────────────────────────────────────
    // Only about a print that hasn't happened. A reading taken after the fact
    // is not a prediction, and letting one in would quietly flatter every
    // statistic computed from this table.
    if (!a.next.timestamp.has_value())
        return;
    const int days = services::equity::days_to_next_earnings(a);
    if (days < 0)
        return;

    EarningsSignalRecord rec;
    rec.symbol = a.symbol;
    rec.report_ts = *a.next.timestamp;
    rec.observed_on = QDateTime::currentDateTime().toTimeZone(et).date().toString(Qt::ISODate);
    rec.captured_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    rec.days_to_report = days;
    rec.verdict = v.label;
    rec.score = v.score;
    rec.confidence = v.confidence;
    rec.setup_score = v.setup_score;
    rec.bar_score = v.bar_score;
    rec.expected_move_pct = v.typical_move_pct > 0 ? std::optional<double>(v.typical_move_pct)
                                                   : std::nullopt;
    rec.consensus_eps = a.next.eps_avg;
    rec.dispersion_pct = v.dispersion_pct;
    rec.runup_5d_pct = a.runup_5d_pct;
    rec.price_at_capture = a.valuation.price;
    rec.predicted_move_pct = v.predicted_move_pct;
    ledger.observe(rec);
}

void EquityEarningsTab::fill_scorecard(const EarningsVerdict& v) {
    // Rows are rebuilt per symbol — the component set is fixed, but their
    // availability and colouring are not, and a handful of widgets per symbol
    // switch is cheaper than keeping a parallel widget cache in sync.
    while (QLayoutItem* item = score_rows_layout_->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (const auto& c : v.components) {
        auto* row = new QWidget(nullptr);
        row->setStyleSheet("background:transparent; border:0;");
        row->setToolTip(c.explanation);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(10);

        auto* name = new QLabel(c.name);
        name->setFixedWidth(190);
        name->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1px; "
                                    "background:transparent; border:0;")
                                .arg(c.available ? ui::colors::TEXT_PRIMARY() : ui::colors::TEXT_TERTIARY()));
        hl->addWidget(name);

        // Score bar: a centre-anchored ±1 gauge. Left half = negative.
        auto* gauge = new QWidget(nullptr);
        gauge->setFixedSize(120, 10);
        if (c.available) {
            const double frac = std::clamp(std::abs(c.score), 0.0, 1.0) / 2.0;  // half-width share
            const double start = c.score >= 0 ? 0.5 : 0.5 - frac;
            const QString fill = c.score >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE();
            gauge->setStyleSheet(
                QString("background: qlineargradient(x1:0, x2:1, stop:0 %1, stop:%2 %1, stop:%3 %4, "
                        "stop:%5 %4, stop:%6 %1, stop:1 %1); border:1px solid %7; border-radius:2px;")
                    .arg(ui::colors::BG_BASE())
                    .arg(std::max(0.0, start - 0.001))
                    .arg(start)
                    .arg(fill)
                    .arg(std::min(1.0, start + frac))
                    .arg(std::min(1.0, start + frac + 0.001))
                    .arg(ui::colors::BORDER_DIM()));
        } else {
            gauge->setStyleSheet(QString("background:%1; border:1px solid %2; border-radius:2px;")
                                     .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
        }
        hl->addWidget(gauge);

        auto* score = new QLabel(c.available ? QString("%1%2")
                                                   .arg(c.score >= 0 ? "+" : "")
                                                   .arg(QString::number(c.score, 'f', 2))
                                             : ui::formatting::placeholder());
        score->setFixedWidth(46);
        score->setAlignment(Qt::AlignRight);
        score->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; font-family:monospace; "
                                     "background:transparent; border:0;")
                                 .arg(c.available ? color_for(c.score) : ui::colors::TEXT_TERTIARY()));
        hl->addWidget(score);

        auto* weight = new QLabel(QString("×%1").arg(QString::number(c.weight, 'f', 2)));
        weight->setFixedWidth(44);
        weight->setStyleSheet(QString("color:%1; font-size:12px; font-family:monospace; "
                                      "background:transparent; border:0;")
                                  .arg(ui::colors::TEXT_TERTIARY()));
        hl->addWidget(weight);

        auto* detail = new QLabel(c.detail);
        detail->setWordWrap(true);
        detail->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                  .arg(c.available ? ui::colors::TEXT_SECONDARY() : ui::colors::TEXT_TERTIARY()));
        hl->addWidget(detail, 1);

        score_rows_layout_->addWidget(row);
    }

    if (v.caveats.isEmpty()) {
        caveats_label_->hide();
    } else {
        QStringList bullets;
        for (const auto& c : v.caveats) bullets << "• " + c;
        caveats_label_->setText(bullets.join("\n"));
        caveats_label_->show();
    }
}

void EquityEarningsTab::fill_history(const EarningsAnalysis& a, const EarningsVerdict& v) {
    history_table_->setRowCount(a.history.size());
    int row = 0;
    for (const auto& p : a.history) {
        const auto when = QDateTime::fromSecsSinceEpoch(p.timestamp);
        // The projected row is amber end-to-end in its identity columns: this
        // quarter hasn't happened, and nothing about it should read like a
        // reported number sitting one row below a reported number.
        const QString date_text = p.is_estimate
                                      ? (p.has_forward_estimate
                                             ? when.toString("d MMM yyyy") + QStringLiteral("  EST")
                                             : QStringLiteral("today"))
                                      : when.toString("d MMM yyyy");
        history_table_->setItem(row, 0, cell(date_text,
                                             p.is_estimate ? ui::colors::AMBER() : ui::colors::TEXT_PRIMARY(),
                                             Qt::AlignLeft | Qt::AlignVCenter));
        history_table_->setItem(row, 1, cell(opt_num(p.eps_estimate),
                                             p.is_estimate ? ui::colors::AMBER() : QString()));
        // Projected row: the EST tag on the date already says why there is no
        // actual, so the cell takes the plain missing sentinel rather than a
        // long phrase the column can't fit.
        history_table_->setItem(row, 2, cell(p.eps_actual.has_value() ? opt_num(p.eps_actual)
                                             : p.is_estimate          ? ui::formatting::placeholder()
                                                                      : QStringLiteral("pending"),
                                             p.is_estimate ? ui::colors::AMBER() : ui::colors::TEXT_PRIMARY()));
        history_table_->setItem(row, 3, cell(opt_pct(p.surprise_pct, 2),
                                             p.surprise_pct.has_value() ? color_for(*p.surprise_pct) : QString()));
        history_table_->setItem(row, 4, cell(opt_pct(p.eps_qoq_pct, 1),
                                             p.eps_qoq_pct.has_value() ? color_for(*p.eps_qoq_pct) : QString()));
        history_table_->setItem(row, 5, cell(opt_pct(p.eps_yoy_pct, 1),
                                             p.eps_yoy_pct.has_value() ? color_for(*p.eps_yoy_pct) : QString()));
        // On the projected row there is no print reaction yet — the live
        // "where the price is now against the last print" stands in its place,
        // marked so it can't be misread as a completed session move.
        if (p.is_estimate && p.move_since_last_pct.has_value()) {
            // A one-character marker, not the word "now": the column is only
            // wide enough for "+10.40%", so a "now " prefix pushed the value
            // out and Qt elided the cell to "now …" — the marker survived and
            // the number, which is the entire point of the cell, did not.
            // The arrow plus the row's amber and its EST-tagged date carry the
            // "this is live, not a completed session" meaning between them,
            // and the tooltip says it in full.
            auto* it = cell(QString("→%1").arg(opt_pct(p.move_since_last_pct, 2)),
                            color_for(*p.move_since_last_pct));
            it->setToolTip(QStringLiteral(
                "Where the price sits right now against the close after the last reported "
                "print — a live number that is still moving, not a finished one-day "
                "reaction. It is never counted in the averages or the correlations."));
            history_table_->setItem(row, 6, it);
        } else {
            history_table_->setItem(row, 6, cell(opt_pct(p.reaction_pct, 2),
                                                 p.reaction_pct.has_value() ? color_for(*p.reaction_pct)
                                                                            : QString()));
        }
        history_table_->setItem(row, 7, cell(opt_pct(p.runup_pct, 2),
                                             p.runup_pct.has_value() ? color_for(*p.runup_pct) : QString()));
        ++row;
    }
    fit_table_height(history_table_);
    reaction_chart_->set_history(a.history);
    fill_correlations(a);

    // Built from whichever halves have data: a symbol can have surprises with
    // no usable price history (yfinance dropped the bars), and claiming "rose
    // on 0% of prints" off an empty reaction set would be a lie, not a zero.
    QStringList summary;
    if (v.scored_quarters > 0) {
        summary << QString("Beat %1 of the last %2 reported quarters · average surprise %3")
                       .arg(static_cast<int>(std::lround(v.beat_rate * v.scored_quarters)))
                       .arg(v.scored_quarters)
                       .arg(opt_pct(v.avg_surprise_pct, 2));
    }
    if (v.reaction_quarters > 0) {
        summary << QString("the stock rose on %1% of %2 prints, average %3, typical move ±%4%")
                       .arg(QString::number(v.up_reaction_rate * 100.0, 'f', 0))
                       .arg(v.reaction_quarters)
                       .arg(opt_pct(v.avg_reaction_pct, 2))
                       .arg(QString::number(v.typical_move_pct, 'f', 1));
    }
    history_summary_->setText(
        summary.isEmpty() ? QStringLiteral("No reported quarters with a published consensus.")
                          : summary.join(" · ") + ".");
}

void EquityEarningsTab::fill_trend(const EarningsAnalysis& a) {
    trend_table_->setRowCount(a.trend.size());
    int row = 0;
    for (const auto& t : a.trend) {
        trend_table_->setItem(row, 0, cell(t.label, ui::colors::TEXT_SECONDARY(),
                                           Qt::AlignLeft | Qt::AlignVCenter));
        trend_table_->setItem(row, 1, cell(opt_num(t.current), ui::colors::TEXT_PRIMARY()));
        trend_table_->setItem(row, 2, cell(opt_num(t.d7)));
        trend_table_->setItem(row, 3, cell(opt_num(t.d30)));
        trend_table_->setItem(row, 4, cell(opt_num(t.d60)));
        trend_table_->setItem(row, 5, cell(opt_num(t.d90)));
        // The Δ column is the leg the scorecard actually reads — a consensus
        // creeping up over 90 days is the signal, not the absolute level.
        QString delta = ui::formatting::placeholder();
        QString delta_color;
        if (t.current.has_value() && t.d90.has_value() && std::abs(*t.d90) > 1e-6) {
            const double pct = (*t.current - *t.d90) / std::abs(*t.d90) * 100.0;
            delta = opt_pct(pct, 2);
            delta_color = color_for(pct);
        }
        trend_table_->setItem(row, 6, cell(delta, delta_color));
        ++row;
    }
    fit_table_height(trend_table_);
}

void EquityEarningsTab::fill_revisions(const EarningsAnalysis& a) {
    revisions_table_->setRowCount(a.revisions.size());
    int row = 0;
    for (const auto& r : a.revisions) {
        revisions_table_->setItem(row, 0, cell(r.label, ui::colors::TEXT_SECONDARY(),
                                               Qt::AlignLeft | Qt::AlignVCenter));
        revisions_table_->setItem(row, 1, cell(opt_count(r.up_7d), ui::colors::POSITIVE()));
        revisions_table_->setItem(row, 2, cell(opt_count(r.down_7d), ui::colors::NEGATIVE()));
        revisions_table_->setItem(row, 3, cell(opt_count(r.up_30d), ui::colors::POSITIVE()));
        revisions_table_->setItem(row, 4, cell(opt_count(r.down_30d), ui::colors::NEGATIVE()));
        const double net = r.up_30d.value_or(0.0) - r.down_30d.value_or(0.0);
        revisions_table_->setItem(row, 5, cell(QString("%1%2").arg(net >= 0 ? "+" : "").arg(net, 0, 'f', 0),
                                               color_for(net)));
        ++row;
    }
    fit_table_height(revisions_table_);
}

void EquityEarningsTab::fill_estimates(const EarningsAnalysis& a) {
    estimates_table_->setRowCount(a.estimates.size());
    int row = 0;
    for (const auto& e : a.estimates) {
        estimates_table_->setItem(row, 0, cell(e.label, ui::colors::TEXT_SECONDARY(),
                                               Qt::AlignLeft | Qt::AlignVCenter));
        estimates_table_->setItem(row, 1, cell(opt_num(e.eps_avg), ui::colors::TEXT_PRIMARY()));
        estimates_table_->setItem(row, 2,
            cell(e.eps_low.has_value() && e.eps_high.has_value()
                     ? QString("%1 – %2").arg(opt_num(e.eps_low), opt_num(e.eps_high))
                     : ui::formatting::placeholder()));
        estimates_table_->setItem(row, 3, cell(opt_count(e.analysts)));
        const QString eps_yoy = e.eps_growth.has_value() ? opt_pct(*e.eps_growth * 100.0)
                                                         : ui::formatting::placeholder();
        estimates_table_->setItem(row, 4, cell(eps_yoy, e.eps_growth.has_value()
                                                            ? color_for(*e.eps_growth) : QString()));
        estimates_table_->setItem(row, 5, cell(opt_compact(e.rev_avg), ui::colors::TEXT_PRIMARY()));
        const QString rev_yoy = e.rev_growth.has_value() ? opt_pct(*e.rev_growth * 100.0)
                                                         : ui::formatting::placeholder();
        estimates_table_->setItem(row, 6, cell(rev_yoy, e.rev_growth.has_value()
                                                            ? color_for(*e.rev_growth) : QString()));
        ++row;
    }
    fit_table_height(estimates_table_);
}

void EquityEarningsTab::fill_growth(const EarningsAnalysis& a) {
    growth_table_->setRowCount(a.growth.size());
    int row = 0;
    for (const auto& g : a.growth) {
        growth_table_->setItem(row, 0, cell(g.label, ui::colors::TEXT_SECONDARY(),
                                            Qt::AlignLeft | Qt::AlignVCenter));
        growth_table_->setItem(row, 1, cell(g.stock.has_value() ? opt_pct(*g.stock * 100.0)
                                                                : ui::formatting::placeholder(),
                                            g.stock.has_value() ? color_for(*g.stock) : QString()));
        growth_table_->setItem(row, 2, cell(g.index.has_value() ? opt_pct(*g.index * 100.0)
                                                                : ui::formatting::placeholder()));
        QString edge = ui::formatting::placeholder();
        QString edge_color;
        if (g.stock.has_value() && g.index.has_value()) {
            const double d = (*g.stock - *g.index) * 100.0;
            edge = opt_pct(d);
            edge_color = color_for(d);
        }
        growth_table_->setItem(row, 3, cell(edge, edge_color));
        ++row;
    }
    fit_table_height(growth_table_);
}

} // namespace fincept::screens

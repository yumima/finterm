// src/screens/equity_research/EquityEarningsTab.cpp
#include "screens/equity_research/EquityEarningsTab.h"

#include "core/util/BarTime.h"

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
#include <QHash>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

using services::equity::EarningsAnalysis;
using services::equity::EarningsOutlook;
using services::equity::EstimateDrift;

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
    // Pinned to its own height. A plain QWidget stretches vertically, so in any
    // panel whose body doesn't fill its row the header quietly absorbs the
    // slack — which shows up as a growing gap between the title and the accent
    // rule under it, since that rule is the header's own bottom border. It was
    // a full row's height on SIGNAL BREAKDOWN once that panel started sharing
    // a row with the quarters table.
    hdr->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
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

/// How far a recorded reading's report date may sit from the print it is
/// about, in ET days.
///
/// Yahoo's scheduled date is a placeholder that firms up — often by a day or
/// two, occasionally more — so a reading keyed under the placeholder has to be
/// matched with slack or it never finds its print. Five days spans a reporting
/// week; quarters are ~90 days apart, so the window cannot reach the wrong
/// print. ONE constant because there are two matchers (settling a reading, and
/// plotting it), and when they were written independently the settler used ±5
/// days while the chart demanded an exact date — so precisely the readings the
/// window existed to rescue were the ones that never drew.
constexpr int kSignalMatchWindowDays = 5;

/// Index of the point in `points` whose date is nearest `report_ts`, within
/// the window; -1 when nothing is close enough. `accept` screens candidates.
template <typename Points, typename Accept>
int nearest_within_window(const Points& points, qint64 report_ts, Accept accept) {
    const auto day_of = [](qint64 ts) { return core::bartime::market_date_et(ts); };
    const QDate want = day_of(report_ts);
    int best = -1, best_gap = kSignalMatchWindowDays + 1;
    for (int i = 0; i < points.size(); ++i) {
        if (!accept(points[i]))
            continue;
        const int gap = std::abs(static_cast<int>(want.daysTo(day_of(points[i].timestamp))));
        if (gap < best_gap) {
            best_gap = gap;
            best = i;
        }
    }
    return best;
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

/// "11 Sep" from an ISO expiry, falling back to the raw string.
QString expiry_text(const QString& iso) {
    const QDate d = QDate::fromString(iso, Qt::ISODate);
    return d.isValid() ? d.toString("d MMM") : iso;
}

/// Standing explanation of the MARKET-IMPLIED stat. Factored out because
/// populate() appends this print's actual decomposition to it, and the two
/// halves must not drift.
QString implied_tooltip() {
    return QStringLiteral(
        "What the option market is pricing for THIS print, and the only genuinely "
        "forward-looking figure on the tab: everything else is derived from what the "
        "company already did.\n\n"
        "It is the at-the-money straddle expiring just after the report, with ordinary "
        "volatility taken out in quadrature — event² = straddle² − (daily vol × √sessions)². "
        "That subtraction is what makes the number comparable with EXPECTED MOVE beside "
        "it, which is a single-session forecast: a straddle prices every session to "
        "expiry, so the raw quote weeks ahead of a print is mostly calendar time and "
        "reads several times too large.\n\n"
        "Implied above the historical estimate means the market is braced for more than "
        "this name usually does; below, less. Unlike the rest of the tab this cannot be "
        "backtested here (no historical option data), so it is reported as the market's "
        "pricing and never scored.");
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
        // No stale provenance on the chip while the new symbol loads.
        data_fetched_at_ = QDateTime();
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
        data_fetched_at_ = QDateTime();
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
        // Nothing is displayed, so there is no "data as of" — stamping here
        // would put a fetch time on an empty panel.
        data_fetched_at_ = QDateTime();
        return;
    }
    message_label_->hide();
    content_widget_->show();
    // The payload's own `as_of` — stamped by the daemon when it actually went
    // upstream — outranks the store's resolve time. The daemon caches an
    // earnings payload for up to 15 minutes (2 minutes within three days of a
    // print), so a C++ refetch on the 180s TTL can be answered from that cache
    // while the store stamps it "now" and the freshness chip reads "just now"
    // over a quarter-hour-old consensus.
    data_fetched_at_ = analysis.as_of > 0
                           // EVENT-STAMP: the daemon's upstream fetch instant.
                           ? QDateTime::fromSecsSinceEpoch(analysis.as_of)
                           : s.fetched_at;
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

    vl->addWidget(build_next_report());
    vl->addWidget(build_answers_row());
    vl->addWidget(build_chart_panel());
    vl->addWidget(build_history_panel());
    vl->addWidget(build_current_panel());
    vl->addWidget(build_future_panel());

    auto* footer = new QLabel(
        "Consensus, estimates and revisions from Yahoo Finance · moves are close-to-close over the "
        "print, from daily bars. Probabilities and ranges are calibrated walk-forward on 9,665 "
        "prints across 236 US names (2014–2026); scenarios are EPS-only — Yahoo carries no "
        "revenue-consensus history or guidance, which move stocks as much as the EPS line. "
        "Not investment advice.");
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

QWidget* EquityEarningsTab::build_next_report() {
    QVBoxLayout* body = nullptr;
    auto* panel = make_panel("NEXT REPORT", ui::colors::AMBER(), &body);

    auto* row = new QHBoxLayout;
    row->setSpacing(24);

    auto* when = new QVBoxLayout;
    when->setSpacing(2);
    next_date_ = new QLabel(ui::formatting::placeholder());
    next_date_->setStyleSheet(QString("color:%1; font-size:20px; font-weight:700; "
                                      "background:transparent; border:0;")
                                  .arg(ui::colors::TEXT_PRIMARY()));
    when->addWidget(next_date_);
    next_countdown_ = new QLabel;
    next_countdown_->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700; "
                                           "background:transparent; border:0;")
                                       .arg(ui::colors::AMBER()));
    when->addWidget(next_countdown_);
    next_confirmed_ = new QLabel;
    next_confirmed_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                       .arg(ui::colors::TEXT_TERTIARY()));
    when->addWidget(next_confirmed_);
    row->addLayout(when);

    row->addWidget(make_stat("CONSENSUS EPS", next_eps_, ui::colors::TEXT_PRIMARY()));
    row->addWidget(make_stat("EPS RANGE", next_eps_range_, ui::colors::TEXT_SECONDARY(), 13));
    row->addWidget(make_stat("CONSENSUS REVENUE", next_rev_, ui::colors::TEXT_PRIMARY()));
    row->addWidget(make_stat("EXPECTED YoY", next_yoy_, ui::colors::TEXT_PRIMARY(), 13));
    row->addWidget(make_stat("ANALYSTS", next_analysts_, ui::colors::TEXT_PRIMARY(), 13));
    row->addStretch();
    body->addLayout(row);

    headline_ = new QLabel;
    headline_->setWordWrap(true);
    headline_->setStyleSheet(QString("color:%1; font-size:14px; background:transparent; border:0; "
                                     "border-top:1px solid %2; padding-top:8px;")
                                 .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
    body->addWidget(headline_);
    return panel;
}

QWidget* EquityEarningsTab::build_answers_row() {
    auto* row = new QWidget(nullptr);
    row->setStyleSheet("background:transparent;");
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    auto big_value = [](QLabel*& out, const QString& color) {
        out = new QLabel(ui::formatting::placeholder());
        out->setStyleSheet(QString("color:%1; font-size:30px; font-weight:700; font-family:monospace; "
                                   "background:transparent; border:0;")
                               .arg(color));
        return out;
    };
    auto body_text = [](QLabel*& out, const QString& color) {
        out = new QLabel;
        out->setWordWrap(true);
        out->setTextFormat(Qt::RichText);
        out->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                               .arg(color));
        return out;
    };

    // ── 1 · Will they beat? ──────────────────────────────────────────────────
    QVBoxLayout* beat_body = nullptr;
    auto* beat_panel = make_panel("1 · WILL THEY BEAT?", ui::colors::POSITIVE(), &beat_body);
    beat_body->addWidget(big_value(beat_value_, ui::colors::TEXT_PRIMARY()));
    beat_value_->setToolTip(QStringLiteral(
        "Chance that reported EPS comes in above consensus.\n\n"
        "This company's own record over its last eight scored quarters, blended with the "
        "pooled rate (82% of prints beat) as if the pool were eight more quarters. Out of "
        "sample that beats both the pooled rate alone and the raw record — a name with "
        "eight beats in eight is not a 100% bet, and one with a single bad quarter is not "
        "a coin flip.\n\n"
        "Quarters whose 'surprise' is a GAAP figure set against an adjusted consensus are "
        "left out of the record."));
    beat_body->addWidget(body_text(beat_sub_, ui::colors::TEXT_SECONDARY()));

    beat_strip_ = new QWidget;
    beat_strip_->setFixedHeight(14);
    beat_body->addWidget(beat_strip_);
    beat_body->addWidget(body_text(beat_strip_legend_, ui::colors::TEXT_SECONDARY()));

    beat_body->addWidget(make_caption("ESTIMATE MOMENTUM"));
    beat_body->addWidget(body_text(beat_drift_, ui::colors::TEXT_SECONDARY()));
    beat_drift_->setToolTip(QStringLiteral(
        "Which way analysts have been moving this quarter's EPS number. Rising estimates make "
        "the bar harder to clear and usually mean the company has been talking it up; falling "
        "ones are the classic walk-down to a beatable number.\n\n"
        "Shown as context and deliberately NOT in the probability: Yahoo publishes the "
        "revision trend as a snapshot with no history, so there is no record to measure what "
        "it adds. The ledger records it with every reading, so in time it can be."));
    beat_body->addStretch();
    hl->addWidget(beat_panel, 1);

    // ── 2 · How big a move? ──────────────────────────────────────────────────
    QVBoxLayout* size_body = nullptr;
    auto* size_panel = make_panel("2 · HOW BIG A MOVE?", "#22d3ee", &size_body);
    size_body->addWidget(big_value(size_value_, ui::colors::TEXT_PRIMARY()));
    size_value_->setToolTip(QStringLiteral(
        "Forecast size of the session after the print, either direction — the part of an "
        "earnings reaction that IS predictable.\n\n"
        "Half this name's average move over its last twelve prints plus its realised daily "
        "volatility over the last 20 sessions. The history says how big this company's prints "
        "run; the volatility says whether the stock is calm or wild going into this one. Held "
        "out by company across 181 names it cut the error ~7% against the history alone."));
    size_body->addWidget(body_text(size_dollars_, ui::colors::TEXT_SECONDARY()));
    size_body->addWidget(body_text(size_ranges_, ui::colors::TEXT_PRIMARY()));
    size_body->addWidget(make_caption("OPTIONS"));
    size_body->addWidget(body_text(size_implied_, ui::colors::TEXT_SECONDARY()));
    size_body->addWidget(make_caption("RECENT PRINTS"));
    size_body->addWidget(body_text(size_history_, ui::colors::TEXT_SECONDARY()));
    size_body->addStretch();
    hl->addWidget(size_panel, 1);

    // ── 3 · Which way? ───────────────────────────────────────────────────────
    QVBoxLayout* dir_body = nullptr;
    auto* dir_panel = make_panel("3 · WHICH WAY?", "#a855f7", &dir_body);
    auto* no_call = new QLabel(QStringLiteral("NO RELIABLE CALL"));
    no_call->setStyleSheet(QString("color:%1; font-size:20px; font-weight:700; letter-spacing:1px; "
                                   "background:transparent; border:0;")
                               .arg(ui::colors::TEXT_SECONDARY()));
    no_call->setToolTip(QStringLiteral(
        "Nothing available before a print predicts which way the stock goes — not the beat "
        "record, not how it traded on past prints, not the run-up, not this tab's own former "
        "BUY / HOLD / SELL scorecard, which called the direction right 50.7% of the time over "
        "9,665 prints: slightly worse than 'always up' (50.9%). No serious earnings product makes a "
        "directional call either.\n\n"
        "What is known is what each outcome has meant, and that is what the table shows."));
    dir_body->addWidget(no_call);
    auto* dir_sub = new QLabel(QStringLiteral(
        "What each outcome has meant for the price — the reaction depends on HOW MUCH they beat, "
        "not whether."));
    dir_sub->setWordWrap(true);
    dir_sub->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                               .arg(ui::colors::TEXT_SECONDARY()));
    dir_body->addWidget(dir_sub);

    scenario_table_ = new QTableWidget;
    style_table(scenario_table_, {"IF EPS IS…", "CHANCE", "TYPICAL MOVE", "ROSE ON"});
    scenario_table_->setToolTip(QStringLiteral(
        "CHANCE — how likely each outcome is for this company (its record, shrunk toward the "
        "pooled mix).\n\n"
        "TYPICAL MOVE — the pooled average next-session move for that outcome, expressed as a "
        "share of this name's own expected move and scaled back to it. Per-company versions "
        "don't persist from one year to the next; the pooled ones held steady across both "
        "halves of the sample.\n\n"
        "ROSE ON — share of such prints, pooled, after which the stock closed higher.\n\n"
        "EPS-only: revenue and guidance surprises move stocks as much and aren't in Yahoo's "
        "history, which is why even a big beat rises only ~60% of the time."));
    dir_body->addWidget(scenario_table_);
    dir_body->addWidget(body_text(scenario_note_, ui::colors::TEXT_TERTIARY()));
    dir_body->addWidget(make_caption("ALREADY PRICED IN"));
    dir_body->addWidget(body_text(priced_in_, ui::colors::TEXT_SECONDARY()));

    caveats_label_ = new QLabel;
    caveats_label_->setWordWrap(true);
    caveats_label_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0; "
                                          "border-top:1px solid %2; padding-top:8px;")
                                      .arg(ui::colors::WARNING(), ui::colors::BORDER_DIM()));
    dir_body->addWidget(caveats_label_);
    dir_body->addStretch();
    hl->addWidget(dir_panel, 1);

    return row;
}

/// The reaction chart, full width — the only panel here that reads better
/// the wider it gets.
QWidget* EquityEarningsTab::build_chart_panel() {
    QVBoxLayout* chart_body = nullptr;
    auto* chart_panel = make_panel("PAST PRINTS · WHAT LANDED AND HOW IT TRADED", ui::colors::POSITIVE(),
                                   &chart_body);

    auto* switch_row = new QHBoxLayout;
    switch_row->setSpacing(4);
    switch_row->setContentsMargins(0, 0, 0, 0);
    switch_row->addWidget(make_caption("BARS:"));
    for (const auto m : {services::equity::ReactionMetric::Surprise,
                         services::equity::ReactionMetric::YoY,
                         services::equity::ReactionMetric::QoQ}) {
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

    chart_note_ = new QLabel;
    chart_note_->setWordWrap(true);
    chart_note_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                   .arg(ui::colors::TEXT_TERTIARY()));
    chart_body->addWidget(chart_note_);
    return chart_panel;
}

QWidget* EquityEarningsTab::build_history_panel() {
    auto* row = new QWidget(nullptr);
    row->setStyleSheet("background:transparent;");
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    QVBoxLayout* body = nullptr;
    auto* panel = make_panel("PAST · REPORTED QUARTERS", ui::colors::POSITIVE(), &body);
    history_summary_ = new QLabel;
    history_summary_->setWordWrap(true);
    history_summary_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                                        .arg(ui::colors::TEXT_SECONDARY()));
    body->addWidget(history_summary_);
    history_table_ = new QTableWidget;
    style_table(history_table_,
                {"REPORTED", "EPS EST", "EPS ACT", "SURPRISE", "OUTCOME", "YoY", "1D MOVE", "EXPECTED"});
    body->addWidget(history_table_);
    hl->addWidget(panel, 3);

    // ── Forecast record: how the two forecasts have done on THIS name ────────
    QVBoxLayout* rec_body = nullptr;
    auto* rec_panel = make_panel("FORECAST RECORD · THIS NAME", "#a855f7", &rec_body);
    auto add_block = [&](const QString& caption, QLabel*& out) {
        rec_body->addWidget(make_caption(caption));
        out = new QLabel;
        out->setWordWrap(true);
        out->setTextFormat(Qt::RichText);
        out->setStyleSheet(QString("color:%1; font-size:12px; background:transparent; border:0;")
                               .arg(ui::colors::TEXT_SECONDARY()));
        rec_body->addWidget(out);
    };
    add_block(QStringLiteral("SIZE FORECAST"), record_size_);
    add_block(QStringLiteral("BEAT PROBABILITY"), record_beat_);
    add_block(QStringLiteral("RECORDED BEFORE THE PRINT"), record_ledger_);
    rec_body->addStretch();
    hl->addWidget(rec_panel, 2);
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
    // The r rides in each button's tooltip: it explains past moves (these
    // metrics only exist after the print), so it belongs beside the switch,
    // not in a headline.
    for (const auto& c : services::equity::correlate_reactions(a)) {
        auto* btn = metric_buttons_.value(c.metric, nullptr);
        if (!btn) continue;
        btn->setText(c.label);
        btn->setToolTip(c.r.has_value()
                            ? QString("Correlation between %1 and the next-session move on this name: "
                                      "r %2%3 over %4 quarters. Descriptive — the figure only exists "
                                      "once the company has reported.")
                                  .arg(c.label.toLower(), *c.r >= 0 ? "+" : "",
                                       QString::number(*c.r, 'f', 2))
                                  .arg(c.n)
                            : QString("Not enough quarters carry both %1 and a price reaction.")
                                  .arg(c.label.toLower()));
    }
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
    // Yahoo's stock-vs-index growth table is not shown: its INDEX column is one
    // market-wide constant (identical to four decimals across twenty large
    // caps), so its "edge" was the company's own growth shifted by a constant.
    QVBoxLayout* est_body = nullptr;
    auto* est_panel = make_panel("FUTURE · ANALYST ESTIMATES", "#60a5fa", &est_body);
    estimates_table_ = new QTableWidget;
    style_table(estimates_table_,
                {"PERIOD", "EPS", "LOW–HIGH", "ANALYSTS", "EPS YoY", "REVENUE", "REV YoY"});
    est_body->addWidget(estimates_table_);
    return est_panel;
}

// ── Population ───────────────────────────────────────────────────────────────

void EquityEarningsTab::populate(const EarningsAnalysis& a) {
    currency_ = a.currency.isEmpty() ? QStringLiteral("USD") : a.currency;
    const EarningsOutlook outlook = services::equity::evaluate_outlook(a);
    const auto record = services::equity::forecast_record(a);

    fill_next_report(a, outlook);
    fill_beat(outlook);
    fill_size(a, outlook);
    fill_direction(outlook);
    record_and_resolve(a, outlook);

    reaction_chart_->set_history(a.history);
    reaction_chart_->set_forecasts(record.prints, outlook.expected_move_pct);
    fill_correlations(a);
    if (record.size_graded > 0) {
        chart_note_->setText(
            QString("Shaded band: the expected move that stood before each print, rebuilt from the "
                    "prints before it only. %1 of %2 moves landed inside it — about half is what a "
                    "well-sized forecast should manage; far more means it runs wide, far fewer "
                    "that this name has been surprising the market more than usual.")
                .arg(record.size_inside)
                .arg(record.size_graded));
    } else {
        chart_note_->setText(QStringLiteral(
            "Shaded band: the expected move before each print. It needs three earlier prints, so "
            "it is absent on the oldest quarters."));
    }

    fill_history(a, outlook, record);
    fill_record(a, record);
    fill_trend(a);
    fill_revisions(a);
    fill_estimates(a);
}

void EquityEarningsTab::fill_next_report(const EarningsAnalysis& a, const EarningsOutlook& o) {
    if (a.next.timestamp.has_value()) {
        // Render in US market time, not the viewer's: "30 Jul, after close" is
        // a fact about the exchange session.
        // EVENT-STAMP: earnings announcement — ET.
        const auto when = QDateTime::fromSecsSinceEpoch(*a.next.timestamp)
                              .toTimeZone(QTimeZone("America/New_York"));
        next_date_->setText(when.toString("ddd d MMM yyyy"));
        // Same convention as the daemon's reaction windows: 16:00+ ET is after
        // close, and a date-only midnight stamp is TREATED as after close.
        const QTime t = when.time();
        const bool date_only = t.hour() == 0 && t.minute() == 0;
        const QString slot = (t.hour() >= 16 || date_only) ? QStringLiteral("after close")
                             : t < QTime(9, 30)            ? QStringLiteral("before open")
                                                           : QStringLiteral("intraday");
        const int days = o.days_to_report;
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
        set_stat(next_yoy_, QString("EPS %1").arg(opt_pct(g)), color_for(g), 13);
    } else {
        set_stat(next_yoy_, ui::formatting::placeholder(), ui::colors::TEXT_PRIMARY(), 13);
    }
    next_analysts_->setText(opt_count(a.next.analysts));
    headline_->setText(o.headline);
}

void EquityEarningsTab::fill_beat(const EarningsOutlook& o) {
    if (o.p_beat) {
        const double p = *o.p_beat;
        set_stat(beat_value_, QString("%1%").arg(QString::number(p * 100.0, 'f', 0)),
                 p >= 0.5 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE(), 30);
        QStringList bits;
        bits << QString("chance of beating EPS consensus · %1% chance of a miss")
                    .arg(QString::number((1.0 - p) * 100.0, 'f', 0));
        bits << QString("Beat %1 of the last %2 quarters").arg(o.beats).arg(o.scored_quarters);
        if (o.typical_surprise_pct)
            bits.last() += QString(", by a median %1").arg(opt_pct(*o.typical_surprise_pct));
        bits << QString("An average company: %1%")
                    .arg(QString::number(o.pooled_beat_rate * 100.0, 'f', 0));
        beat_sub_->setText(bits.join(QStringLiteral("<br>")));
    } else {
        set_stat(beat_value_, ui::formatting::placeholder(), ui::colors::TEXT_PRIMARY(), 30);
        beat_sub_->setText(QString("Too few reported quarters to say for this company. An average "
                                   "company beats %1% of the time.")
                               .arg(QString::number(o.pooled_beat_rate * 100.0, 'f', 0)));
    }

    // Stacked strip: the four outcomes, worst to best, sized by probability.
    const QString colors[] = {ui::colors::NEGATIVE(), ui::colors::WARNING(), QStringLiteral("#4ade80"),
                              ui::colors::POSITIVE()};
    QStringList stops, legend;
    double acc = 0;
    for (int k = 0; k < o.scenarios.size() && k < 4; ++k) {
        const auto& sc = o.scenarios[k];
        const double from = acc, to = std::min(1.0, acc + sc.probability);
        stops << QString("stop:%1 %2, stop:%3 %2").arg(from, 0, 'f', 4).arg(colors[k]).arg(
            std::max(from, to - 0.0005), 0, 'f', 4);
        acc = to;
        legend << QString("<span style='color:%1'>■</span> %2 %3%")
                      .arg(colors[k], sc.label.toLower())
                      .arg(QString::number(sc.probability * 100.0, 'f', 0));
    }
    beat_strip_->setStyleSheet(
        stops.isEmpty() ? QString("background:%1; border-radius:2px;").arg(ui::colors::BG_BASE())
                        : QString("background:qlineargradient(x1:0, x2:1, %1); border-radius:2px;")
                              .arg(stops.join(", ")));
    beat_strip_->setToolTip(QStringLiteral(
        "How the print is likely to land against consensus EPS: miss, beat by 0–3%, by 3–10%, "
        "or by more than 10%. The line that matters for the price is 3% — see WHICH WAY?"));
    beat_strip_legend_->setText(legend.join(QStringLiteral(" &nbsp; ")));

    const QString drift_word = o.drift == EstimateDrift::Rising    ? QStringLiteral("Rising")
                               : o.drift == EstimateDrift::Falling ? QStringLiteral("Falling")
                               : o.drift == EstimateDrift::Flat    ? QStringLiteral("Flat")
                                                                   : QString();
    const QString drift_color = o.drift == EstimateDrift::Rising    ? ui::colors::POSITIVE()
                                : o.drift == EstimateDrift::Falling ? ui::colors::NEGATIVE()
                                                                    : ui::colors::TEXT_PRIMARY();
    QString drift = drift_word.isEmpty()
                        ? QStringLiteral("No revision history published.")
                        : QString("<b style='color:%1'>%2</b> — %3").arg(drift_color, drift_word, o.drift_detail);
    if (o.dispersion_pct)
        drift += QString("<br>Analysts span %1% of the mean%2")
                     .arg(QString::number(*o.dispersion_pct, 'f', 0),
                          o.dispersion_is_wide ? QStringLiteral(" — wide") : QString());
    beat_drift_->setText(drift);
}

void EquityEarningsTab::fill_size(const EarningsAnalysis& a, const EarningsOutlook& o) {
    if (!o.expected_move_pct) {
        set_stat(size_value_, ui::formatting::placeholder(), ui::colors::TEXT_PRIMARY(), 30);
        size_dollars_->setText(QStringLiteral("Needs at least three past prints with price history."));
        size_ranges_->clear();
    } else {
        const double e = *o.expected_move_pct;
        set_stat(size_value_, QString("±%1%").arg(QString::number(e, 'f', 1)), "#22d3ee", 30);
        QString sub = QStringLiteral("expected move the session after the print, either way");
        if (a.valuation.price.has_value() && *a.valuation.price > 0)
            sub += QString("<br>≈ ±%1 a share on %2")
                       .arg(ui::formatting::format_money(*a.valuation.price * e / 100.0, currency_),
                            ui::formatting::format_money(*a.valuation.price, currency_));
        size_dollars_->setText(sub);
        size_ranges_->setText(
            QString("Half of prints land within <b>±%1%</b><br>"
                    "4 in 5 within <b>±%2%</b><br>"
                    "1 in 10 go beyond <b>±%3%</b>")
                .arg(QString::number(*o.half_within_pct, 'f', 1),
                     QString::number(*o.most_within_pct, 'f', 1),
                     QString::number(*o.tail_beyond_pct, 'f', 1)));
    }

    // Options: the event component only. A straddle prices every session to
    // expiry, so the raw quote weeks ahead of a print overstates the market's
    // earnings estimate several times over.
    if (o.implied_move_pct) {
        QString text = QString("Options price <b>±%1%</b> for the print")
                           .arg(QString::number(*o.implied_move_pct, 'f', 1));
        if (o.implied_ratio) {
            const double r = *o.implied_ratio;
            text += r >= 1.15   ? QString(" — %1× our estimate: braced for more than usual.")
                                      .arg(QString::number(r, 'f', 1))
                    : r <= 0.87 ? QString(" — %1× our estimate: pricing a quieter print than usual.")
                                      .arg(QString::number(r, 'f', 1))
                                : QStringLiteral(" — in line with our estimate.");
        }
        const auto& imp = *a.next.implied;
        size_implied_->setToolTip(
            implied_tooltip() +
            QString("\n\nThis print: the straddle expiring %1 costs %2% of spot; stripping the "
                    "ordinary sessions between now and then leaves %3%.")
                .arg(expiry_text(imp.expiry), QString::number(imp.total_move_pct.value_or(0.0), 'f', 1),
                     QString::number(*o.implied_move_pct, 'f', 1)));
        size_implied_->setText(text);
    } else {
        QString why = !a.next.timestamp.has_value()
                          ? QStringLiteral("No report date to price a straddle against.")
                          : QStringLiteral("Unavailable — no listed expiry within ten days after "
                                           "the report, or no options at all.");
        if (a.next.implied.has_value() && a.next.implied->total_move_pct.has_value())
            why = QString("The %1 straddle (%2% of spot) can't yet be separated into an earnings "
                          "component — too much ordinary volatility rides on it.")
                      .arg(expiry_text(a.next.implied->expiry),
                           QString::number(*a.next.implied->total_move_pct, 'f', 1));
        size_implied_->setText(why);
        size_implied_->setToolTip(implied_tooltip());
    }

    // The last four settled moves, newest first — the column every options
    // desk reads beside the implied move.
    QStringList moves;
    for (const auto& p : a.history) {
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        moves << QString("<span style='color:%1'>%2</span>").arg(color_for(*p.reaction_pct),
                                                                 opt_pct(p.reaction_pct, 1));
        if (moves.size() == 4) break;
    }
    QString hist = moves.isEmpty() ? QStringLiteral("No settled prints with price history.")
                                   : QStringLiteral("Last %1: ").arg(moves.size()) + moves.join(QStringLiteral(" · "));
    if (o.trailing_move_pct)
        hist += QString("<br>Average size over the last %1: ±%2%")
                    .arg(std::min(o.reaction_quarters, 12))
                    .arg(QString::number(*o.trailing_move_pct, 'f', 1));
    size_history_->setText(hist);
}

void EquityEarningsTab::fill_direction(const EarningsOutlook& o) {
    scenario_table_->setRowCount(o.scenarios.size());
    const bool have_size = o.expected_move_pct.has_value();
    for (int k = 0; k < o.scenarios.size(); ++k) {
        const auto& sc = o.scenarios[k];
        auto* name = cell(sc.label, ui::colors::TEXT_PRIMARY(), Qt::AlignLeft | Qt::AlignVCenter);
        name->setToolTip(QString("EPS %1 consensus").arg(sc.range));
        scenario_table_->setItem(k, 0, name);
        scenario_table_->setItem(k, 1, cell(QString("%1%").arg(QString::number(sc.probability * 100.0, 'f', 0))));
        scenario_table_->setItem(
            k, 2,
            have_size ? cell(opt_pct(sc.typical_move_pct, 1), color_for(sc.typical_move_pct))
                      : cell(QString("%1×").arg(QString::number(sc.move_multiple, 'f', 2)),
                             color_for(sc.move_multiple)));
        scenario_table_->setItem(k, 3, cell(QString("%1%").arg(QString::number(sc.up_rate * 100.0, 'f', 0)),
                                            sc.up_rate >= 0.5 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));
    }
    fit_table_height(scenario_table_);
    scenario_note_->setText(QStringLiteral(
        "A beat of under 3% has traded down — the market expects the usual beat, so "
        "clearing consensus by a hair reads as a miss. Even a big beat rises only ~60% of the "
        "time: revenue and the guide matter as much, and aren't in this table."));

    priced_in_->setText(o.priced_in.isEmpty() ? QStringLiteral("No recent price history.")
                                              : o.priced_in.join(QStringLiteral("<br>")));
    if (o.caveats.isEmpty()) {
        caveats_label_->hide();
    } else {
        QStringList bullets;
        for (const auto& c : o.caveats) bullets << "• " + c;
        caveats_label_->setText(bullets.join("\n"));
        caveats_label_->show();
    }
}

void EquityEarningsTab::fill_record(const EarningsAnalysis& a, const services::equity::ForecastRecord& r) {
    if (r.size_graded > 0) {
        const bool beats_baseline = r.size_mae < r.trailing_mae;
        record_size_->setText(
            QString("Landed inside the expected move on <b>%1 of %2</b> prints (about half is the "
                    "target).<br>Missed the actual size by %3 pp on average, against %4 pp for "
                    "this name's plain average move — %5.")
                .arg(r.size_inside)
                .arg(r.size_graded)
                .arg(QString::number(r.size_mae, 'f', 1), QString::number(r.trailing_mae, 'f', 1),
                     beats_baseline ? QStringLiteral("the volatility blend has helped here")
                                    : QStringLiteral("on this name the plain average has done as well")));
    } else {
        record_size_->setText(QStringLiteral("Not enough prints yet to have graded a forecast."));
    }

    if (r.beat_graded > 0) {
        record_beat_->setText(
            QString("Stated an average <b>%1%</b> beforehand; it beat <b>%2 of %3</b> (%4%). "
                    "Pooled across 9,665 prints the stated chances track the realised rate to "
                    "within a few points in every band.")
                .arg(QString::number(r.mean_p_beat * 100.0, 'f', 0))
                .arg(r.beat_hits)
                .arg(r.beat_graded)
                .arg(QString::number(100.0 * r.beat_hits / r.beat_graded, 'f', 0)));
    } else {
        record_beat_->setText(QStringLiteral("Not enough prints yet to have graded a probability."));
    }

    // Readings genuinely written down before a print. Everything above is a
    // reconstruction from history; only these were the future when recorded.
    //
    // Grouped by the SETTLED PRINT each reading matches, within the same
    // ±window the settler uses — not by the reading's own report_ts. Yahoo's
    // placeholder date moves before the company confirms it, so two readings
    // about one print can carry different report_ts values, and keying on
    // that counted the print twice. The last reading before the print wins.
    QHash<int, const EarningsSignalRecord*> last_before;
    const auto rows = EarningsSignalRepository::instance().for_symbol(a.symbol);
    for (const auto& row : rows) {
        if (!row.resolved || row.model_version.value_or(1) < 2 || !row.actual_move_pct) continue;
        const int i = nearest_within_window(a.history, row.report_ts,
                                            [](const services::equity::EarningsPoint& p) {
                                                return !p.is_estimate && p.reaction_pct.has_value();
                                            });
        if (i < 0) continue;
        const auto seen = last_before.constFind(i);
        if (seen == last_before.constEnd() || row.observed_on > seen.value()->observed_on)
            last_before.insert(i, &row);
    }
    int n = 0, size_n = 0, inside = 0, implied_n = 0, implied_inside = 0, beat_n = 0, beat_hit = 0;
    double p_sum = 0;
    for (auto it = last_before.constBegin(); it != last_before.constEnd(); ++it) {
        const auto* row = it.value();
        const auto& print = a.history[it.key()];
        ++n;
        const double actual = std::abs(*row->actual_move_pct);
        // Each tally is graded only over readings that stated that number: a
        // reading taken while the name had too little history carries no
        // expected move, and counting it as a miss would understate the record.
        if (row->expected_move_pct) {
            ++size_n;
            if (actual <= *row->expected_move_pct) ++inside;
        }
        if (row->implied_move_pct) {
            ++implied_n;
            if (actual <= *row->implied_move_pct) ++implied_inside;
        }
        // Beat graded against the print's own row, so a GAAP-vs-adjusted
        // artefact is excluded here exactly as it is everywhere else.
        if (row->p_beat && services::equity::scenario_index(print) >= 0) {
            ++beat_n;
            p_sum += *row->p_beat;
            if (*print.surprise_pct > 0) ++beat_hit;
        }
    }
    // Waiting: unresolved readings that CAN still settle. One taken on or after
    // its print day never will — resolve() refuses it as hindsight — so it
    // must not sit in this count forever.
    int pending = 0;
    for (const auto& row : rows) {
        if (row.resolved || row.model_version.value_or(1) < 2) continue;
        const QDate observed = QDate::fromString(row.observed_on, Qt::ISODate);
        // EVENT-STAMP: earnings announcement — ET.
        if (observed.isValid() && observed < core::bartime::market_date_et(row.report_ts)) ++pending;
    }
    if (n == 0) {
        record_ledger_->setText(
            QString("Nothing settled yet. Each day the tab is open before a print, the outlook is "
                    "written down and checked once the print lands%1.")
                .arg(pending > 0 ? QString(" — %1 reading%2 waiting").arg(pending).arg(pending == 1 ? "" : "s")
                                 : QString()));
        return;
    }
    QStringList bits;
    bits << QString("%1 print%2 settled").arg(n).arg(n == 1 ? "" : "s");
    if (size_n > 0)
        bits << QString("inside the expected move %1 of %2").arg(inside).arg(size_n);
    if (implied_n > 0)
        bits << QString("inside the options-implied move %1 of %2").arg(implied_inside).arg(implied_n);
    if (beat_n > 0)
        bits << QString("stated %1% to beat, beat %2 of %3")
                    .arg(QString::number(p_sum / beat_n * 100.0, 'f', 0))
                    .arg(beat_hit)
                    .arg(beat_n);
    record_ledger_->setText(bits.join(QStringLiteral("<br>")));
}

// ── Signal ledger ────────────────────────────────────────────────────────────
// Yahoo's estimates, revisions and option chains are point-in-time snapshots
// with no history, so what stood before a past print cannot be reconstructed —
// the only way to check those inputs is to write each reading down as it
// happens and settle it afterwards.

void EquityEarningsTab::record_and_resolve(const EarningsAnalysis& a, const EarningsOutlook& o) {
    auto& ledger = EarningsSignalRepository::instance();

    // ── Settle anything whose print has since landed ─────────────────────────
    // Matched on the calendar date in market time, within a window: Yahoo
    // shifts a scheduled placeholder to the real announcement once it lands.
    // A quarter with only `reaction_live_pct` is no match — its session is
    // still trading, and the row waits for the close.
    const auto et_date = [](qint64 ts) { return core::bartime::market_date_et(ts); };
    for (const auto& pending : ledger.unresolved(a.symbol)) {
        const int i = nearest_within_window(
            a.history, pending.report_ts, [](const services::equity::EarningsPoint& p) {
                return !p.is_estimate && p.reaction_pct.has_value();
            });
        if (i < 0)
            continue;
        const auto& best = a.history[i];
        // The repository refuses readings taken on or after the print day.
        ledger.resolve(a.symbol, pending.report_ts, best.eps_actual, best.surprise_pct,
                       *best.reaction_pct, et_date(best.timestamp).toString(Qt::ISODate));
    }

    // ── Write today's reading — only about a print that hasn't happened ──────
    if (!a.next.timestamp.has_value() || o.days_to_report < 0)
        return;

    EarningsSignalRecord rec;
    rec.symbol = a.symbol;
    rec.report_ts = *a.next.timestamp;
    rec.observed_on = core::bartime::market_today_et().toString(Qt::ISODate);
    rec.captured_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    rec.days_to_report = o.days_to_report;
    rec.model_version = 2;
    rec.verdict = QStringLiteral("OUTLOOK");   // no directional call: never graded as a hit
    rec.score = 0.0;
    rec.confidence = 0.0;
    rec.expected_move_pct = o.expected_move_pct;
    rec.p_beat = o.p_beat;
    rec.implied_move_pct = o.implied_move_pct;
    rec.consensus_eps = a.next.eps_avg;
    rec.dispersion_pct = o.dispersion_pct;
    rec.runup_5d_pct = a.runup_5d_pct;
    rec.price_at_capture = a.valuation.price;
    ledger.observe(rec);
}

void EquityEarningsTab::fill_history(const EarningsAnalysis& a, const EarningsOutlook& o,
                                     const services::equity::ForecastRecord& record) {
    history_table_->setRowCount(a.history.size());
    int row = 0;
    for (const auto& p : a.history) {
        // ET, like the NEXT REPORT card and the engine's date matching: a
        // 16:00 ET announcement is already tomorrow in Tokyo, so rendering
        // the viewer's local date showed every AMC quarter a day late for
        // anyone east of roughly UTC+3 — disagreeing with the card above and
        // with the filing date.
        // EVENT-STAMP: earnings announcement — ET.
        const auto when = QDateTime::fromSecsSinceEpoch(p.timestamp)
                              .toTimeZone(QTimeZone("America/New_York"));
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
        // A flagged surprise is a GAAP figure subtracted from an adjusted
        // consensus — an accounting artefact, not a beat (GOOG +213%, META
        // -84%). Painting it green or red endorses it; grey plus a tooltip
        // reports it without doing so. The scorer already skips these rows.
        {
            auto* sur_cell = cell(opt_pct(p.surprise_pct, 2),
                                  !p.surprise_pct.has_value() ? QString()
                                  : p.surprise_suspect        ? QString(ui::colors::TEXT_SECONDARY())
                                                              : color_for(*p.surprise_pct));
            if (p.surprise_suspect)
                sur_cell->setToolTip(
                    "Likely a GAAP-vs-adjusted mismatch, not a real beat or miss.\n\n"
                    "Yahoo reports as-reported (GAAP) EPS against the street's adjusted "
                    "consensus. When a quarter carries a large one-off — an investment "
                    "gain, a tax charge — the two are not the same measure, and the "
                    "difference describes the one-off rather than the business. Measured "
                    "across 3,857 quarters, surprises beyond ±100% carry no relationship "
                    "to the price reaction (p = 0.32) while ordinary ones do.\n\n"
                    "Shown for the record; excluded from the beat rate and the scorecard.");
            history_table_->setItem(row, 3, sur_cell);
        }
        // OUTCOME: which of the four scenarios the print landed in — the same
        // buckets WHICH WAY? reads its moves from.
        {
            const int k = services::equity::scenario_index(p);
            static const char* names[] = {"miss", "slight beat", "solid beat", "big beat"};
            static const QString colors[] = {ui::colors::NEGATIVE(), ui::colors::WARNING(),
                                             QStringLiteral("#4ade80"), ui::colors::POSITIVE()};
            history_table_->setItem(row, 4, k >= 0 ? cell(QString::fromLatin1(names[k]), colors[k])
                                                   : cell(ui::formatting::placeholder()));
        }
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
        } else if (p.reaction_live_pct.has_value()) {
            // The reaction session is still trading. Same "→" marker the projected
            // row uses, for the same reason: this number is still moving, and
            // nothing on the tab counts it. Without the split it arrived in
            // reaction_pct and read as a finished close-to-close move — which is
            // what let the signal ledger settle a print, permanently, against
            // whatever the tape happened to say mid-morning.
            auto* it = cell(QString("→%1").arg(opt_pct(p.reaction_live_pct, 2)),
                            color_for(*p.reaction_live_pct));
            it->setToolTip(QStringLiteral(
                "The session after this print is still open — this is where the stock is "
                "trading right now, not a completed close-to-close reaction.\n\n"
                "It is excluded from the expected move, the forecast record and the "
                "correlations until that session closes, and the ledger leaves this print "
                "unsettled until then."));
            history_table_->setItem(row, 6, it);
        } else {
            history_table_->setItem(row, 6, cell(opt_pct(p.reaction_pct, 2),
                                                 p.reaction_pct.has_value() ? color_for(*p.reaction_pct)
                                                                            : QString()));
        }
        // EXPECTED: the size forecast that stood before this print, so the
        // 1D MOVE beside it can be read as inside or outside it.
        {
            std::optional<double> expected;
            for (const auto& f : record.prints)
                if (f.timestamp == p.timestamp) { expected = f.expected_move_pct; break; }
            if (p.is_estimate && p.has_forward_estimate) {
                // The projected row carries today's forecast.
                expected = o.expected_move_pct;
            }
            QString color = ui::colors::TEXT_SECONDARY();
            if (expected && p.reaction_pct)
                color = std::abs(*p.reaction_pct) <= *expected ? ui::colors::TEXT_SECONDARY()
                                                               : ui::colors::WARNING();
            auto* it = cell(expected ? QString("±%1%").arg(QString::number(*expected, 'f', 1))
                                     : ui::formatting::placeholder(),
                            p.is_estimate ? ui::colors::AMBER() : color);
            if (expected && p.reaction_pct && std::abs(*p.reaction_pct) > *expected)
                it->setToolTip(QStringLiteral("The move landed outside the expected range."));
            history_table_->setItem(row, 7, it);
        }
        ++row;
    }
    fit_table_height(history_table_);

    // Built from whichever halves have data: claiming "rose on 0% of prints"
    // off an empty reaction set would be a lie, not a zero.
    int scored = 0, beats = 0, settled = 0, ups = 0;
    double abs_sum = 0;
    for (const auto& p : a.history) {
        if (services::equity::scenario_index(p) >= 0) {
            ++scored;
            if (*p.surprise_pct > 0) ++beats;
        }
        if (!p.is_estimate && p.reaction_pct.has_value()) {
            ++settled;
            if (*p.reaction_pct > 0) ++ups;
            abs_sum += std::abs(*p.reaction_pct);
        }
    }
    QStringList summary;
    if (scored > 0)
        summary << QString("Beat %1 of %2 reported quarters").arg(beats).arg(scored);
    if (settled > 0)
        summary << QString("the stock rose after %1 of %2 prints, moving ±%3% on average")
                       .arg(ups).arg(settled).arg(QString::number(abs_sum / settled, 'f', 1));
    history_summary_->setText(summary.isEmpty() ? QStringLiteral("No reported quarters with a published consensus.")
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

} // namespace fincept::screens

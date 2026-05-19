// src/screens/equity_research/EquityOverviewTab.cpp
#include "screens/equity_research/EquityOverviewTab.h"

#include "screens/knowledge/HelpHint.h"
#include "services/equity/EquityResearchService.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace fincept::screens {

// ── Accent colors without a theme token ──────────────────────────────────────
static constexpr const char* CYAN = "#22d3ee";
static constexpr const char* YELLOW = "#eab308";
static constexpr const char* PURPLE = "#a855f7";
static constexpr const char* BLUE = "#3b82f6";
static constexpr const char* MAGENTA = "#e879f9";

static constexpr int FONT_KEY = 12;   // key labels
static constexpr int FONT_VAL = 13;   // value labels
static constexpr int FONT_TITLE = 12; // panel titles
static constexpr int FONT_DESC = 12;  // description text

// ── Panel helpers ─────────────────────────────────────────────────────────────

namespace {

QFrame* make_panel(const QString& title, const char* title_color) {
    auto* f = new QFrame;
    f->setStyleSheet(QString("QFrame{background:%1;border:1px solid %2;border-radius:2px;}")
                         .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* vl = new QVBoxLayout(f);
    vl->setContentsMargins(10, 7, 10, 7);
    vl->setSpacing(4);

    if (!title.isEmpty()) {
        auto* lbl = new QLabel(title);
        lbl->setStyleSheet(QString("color:%1;font-size:%2px;font-weight:700;"
                                   "letter-spacing:1px;background:transparent;border:0;")
                               .arg(title_color)
                               .arg(FONT_TITLE));
        vl->addWidget(lbl);
        auto* sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFixedHeight(1);
        sep->setStyleSheet(QString("border:0;background:%1;").arg(ui::colors::BORDER_DIM()));
        vl->addWidget(sep);
    }
    return f;
}

// entry_id: if non-empty, appends a HelpHint "?" button after the key label
QLabel* add_row(QFrame* panel, const QString& key, const char* val_color,
                const QString& entry_id = {}) {
    auto* hl = new QHBoxLayout;
    hl->setSpacing(4);
    hl->setContentsMargins(0, 0, 0, 0);

    auto* k = new QLabel(key);
    k->setStyleSheet(QString("color:%1;font-size:%2px;background:transparent;border:0;")
                         .arg(ui::colors::TEXT_SECONDARY())
                         .arg(FONT_KEY));

    auto* v = new QLabel("\xe2\x80\x94");
    v->setStyleSheet(QString("color:%1;font-size:%2px;font-weight:600;background:transparent;border:0;")
                         .arg(val_color)
                         .arg(FONT_VAL));
    v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hl->addWidget(k);
    if (!entry_id.isEmpty()) {
        auto* hint = new fincept::knowledge::HelpHint(entry_id, panel);
        hint->setFixedSize(14, 14);
        hl->addWidget(hint);
    }
    hl->addWidget(v, 1);
    static_cast<QVBoxLayout*>(panel->layout())->addLayout(hl);
    return v;
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════════
//  ResearchCandleCanvas — QPainter candlestick chart
// ══════════════════════════════════════════════════════════════════════════════

ResearchCandleCanvas::ResearchCandleCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(250);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);  // receive mouseMoveEvent without a button held
}

void ResearchCandleCanvas::set_candles(const QVector<services::equity::Candle>& candles, const QString& cs) {
    candles_ = candles;
    currency_sym_ = cs;
    dirty_ = true;
    hover_idx_ = -1;  // candles changed; previous hover index is meaningless
    // A non-empty fetch result means the placeholder shouldn't show again
    // until a future clear() / explicit state change. An empty result keeps
    // whatever state the caller has set (NoData / Error).
    if (!candles.isEmpty())
        placeholder_state_ = PlaceholderState::Loading;
    update();
}

void ResearchCandleCanvas::clear() {
    candles_.clear();
    dirty_ = true;
    hover_idx_ = -1;
    placeholder_state_ = PlaceholderState::Loading;
    update();
}

void ResearchCandleCanvas::set_placeholder_state(PlaceholderState state) {
    if (placeholder_state_ == state) return;
    placeholder_state_ = state;
    dirty_ = true;
    update();
}

void ResearchCandleCanvas::set_log_scale(bool on) {
    if (log_scale_ == on) return;
    log_scale_ = on;
    dirty_ = true;
    update();
}

void ResearchCandleCanvas::set_show_volume(bool on) {
    if (show_volume_ == on) return;
    show_volume_ = on;
    dirty_ = true;
    update();
}

void ResearchCandleCanvas::set_show_sma(int period, bool on) {
    bool* target = nullptr;
    switch (period) {
        case 20:  target = &show_sma20_;  break;
        case 50:  target = &show_sma50_;  break;
        case 200: target = &show_sma200_; break;
        default: return;
    }
    if (*target == on) return;
    *target = on;
    dirty_ = true;
    update();
}

void ResearchCandleCanvas::set_comparisons(const QVector<Comparison>& list) {
    comparisons_ = list;
    dirty_ = true;
    update();
}

void ResearchCandleCanvas::set_earnings_events(const QVector<services::equity::EarningsEvent>& events) {
    earnings_events_ = events;
    dirty_ = true;
    update();
}

void ResearchCandleCanvas::set_week52_high(double v) {
    if (qFuzzyCompare(week52_high_, v)) return;
    week52_high_ = v;
    // 52w high only affects the hover overlay readout, not the cached
    // pixmap — no need to dirty. The overlay redraws on every mouse move.
}

void ResearchCandleCanvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    dirty_ = true;
}

void ResearchCandleCanvas::paintEvent(QPaintEvent*) {
    if (dirty_) {
        rebuild_cache();
        dirty_ = false;
    }
    QPainter p(this);
    p.drawPixmap(0, 0, cache_);
    // Hover overlay is painted live (not into the cache) so a mouse
    // move only re-runs draw_hover_overlay's small overhead, not the
    // full candle re-render.
    if (hover_idx_ >= 0)
        draw_hover_overlay(p);
}

void ResearchCandleCanvas::rebuild_cache() {
    // Reset cached geometry up front; any early return below leaves
    // the hover overlay disabled rather than acting on stale numbers.
    last_plot_w_ = last_plot_h_ = last_start_ = last_count_ = 0;
    last_slot_w_ = 0.0;

    const int W = width();
    const int H = height();
    if (W <= 0 || H <= 0)
        return;

    cache_ = QPixmap(W, H);
    cache_.fill(QColor(ui::colors::BG_BASE()));

    QPainter p(&cache_);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int total = candles_.size();
    if (total == 0) {
        // The LoadingOverlay sitting above the canvas already animates the
        // loading state, so the Loading placeholder draws nothing — anything
        // we drew would just sit dimly behind the overlay. NoData/Error are
        // shown directly because the overlay is hidden in those states.
        if (placeholder_state_ != PlaceholderState::Loading) {
            p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
            p.setFont(QFont("Consolas", 11));
            const QString msg = (placeholder_state_ == PlaceholderState::Error)
                ? "Failed to load chart \xe2\x80\x94 try again"
                : "No data available for this period";
            p.drawText(cache_.rect(), Qt::AlignCenter, msg);
        }
        return;
    }

    const int start = qMax(0, total - MAX_VISIBLE);
    const int count = total - start;

    // ── Plot-area geometry ───────────────────────────────────────────────────
    // Optional volume strip steals VOLUME_FRAC of the available plot height.
    // Layout (top to bottom): price plot | (gap) | volume strip | time axis.
    const int plot_w = W - PRICE_AXIS_W;
    const int total_plot_h = H - TIME_AXIS_H;
    if (plot_w <= 0 || total_plot_h <= 0)
        return;
    const int volume_gap = show_volume_ ? 4 : 0;
    const int volume_h = show_volume_
                             ? qMax(36, static_cast<int>(total_plot_h * VOLUME_FRAC))
                             : 0;
    const int plot_h = total_plot_h - volume_h - volume_gap;
    if (plot_h <= 0)
        return;
    const int volume_top = plot_h + volume_gap;

    // ── Price range (over visible window) ────────────────────────────────────
    double lo = 1e18, hi = 0.0;
    for (int i = start; i < total; ++i) {
        lo = std::min(lo, candles_[i].low);
        hi = std::max(hi, candles_[i].high);
    }
    if (lo >= hi)
        return;

    // Extend the range to include each comparison series' actual closes.
    // Comparison curves are rendered at the comp's own prices (so the user
    // can read e.g. GOOG ≈ $190 directly off the y-axis when GOOG is in
    // the chart), which means the y-axis has to span the union of all
    // tickers' visible ranges. For mixed price levels (AMZN ~$220, MSFT
    // ~$500), this compresses each ticker's vertical band — accepted as
    // the cost of showing real prices instead of relative performance.
    if (!comparisons_.isEmpty() && count > 0) {
        const qint64 anchor_ts = candles_[start].timestamp;
        for (const auto& comp : comparisons_) {
            if (comp.candles.isEmpty()) continue;
            // Walk the same time window we'll render. Anchor index lets us
            // skip the comp's pre-window candles for the y-range too.
            int comp_anchor_idx = -1;
            for (int k = 0; k < comp.candles.size(); ++k) {
                if (comp.candles[k].timestamp >= anchor_ts) {
                    comp_anchor_idx = k; break;
                }
            }
            if (comp_anchor_idx < 0) continue;
            int j = comp_anchor_idx;
            for (int i = 0; i < count; ++i) {
                const qint64 ts_i = candles_[start + i].timestamp;
                while (j + 1 < comp.candles.size() &&
                       comp.candles[j + 1].timestamp <= ts_i) ++j;
                const double v = comp.candles[j].close;
                if (v > 0.0) {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            }
        }
    }

    const double margin = (hi - lo) * 0.06;
    lo -= margin;
    hi += margin;

    // Log-scale projection — kept as a single lambda so SMA/candle/grid all
    // share the same mapping. lo_l/hi_l are precomputed; log(<=0) guarded.
    const double lo_l = log_scale_ ? std::log(qMax(1e-9, lo)) : lo;
    const double hi_l = log_scale_ ? std::log(qMax(1e-9, hi)) : hi;
    auto py = [&](double price) -> int {
        if (log_scale_) {
            const double v = std::log(qMax(1e-9, price));
            return static_cast<int>(plot_h - (v - lo_l) / (hi_l - lo_l) * plot_h);
        }
        return static_cast<int>(plot_h - (price - lo) / (hi - lo) * plot_h);
    };

    // ── Price-plot grid lines ────────────────────────────────────────────────
    p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1, Qt::DotLine));
    for (int g = 1; g < 6; ++g) {
        int gy = plot_h * g / 6;
        p.drawLine(0, gy, plot_w, gy);
    }

    // Candle layout
    const double slot_w = static_cast<double>(plot_w) / count;
    const int body_w = qMax(1, static_cast<int>(slot_w * 0.65));
    const int half = body_w / 2;

    // Snapshot the visible-window geometry for mouseMoveEvent /
    // draw_hover_overlay — they map cursor x → candle without
    // re-deriving the layout.
    last_plot_w_ = plot_w;
    last_plot_h_ = plot_h;
    last_start_  = start;
    last_count_  = count;
    last_slot_w_ = slot_w;

    const QColor bull_color(ui::colors::POSITIVE.get());
    const QColor bear_color(ui::colors::NEGATIVE.get());
    const QColor wick_bull("#2a9d5c");
    const QColor wick_bear("#b83a3a");

    // ── Candles ──────────────────────────────────────────────────────────────
    for (int i = 0; i < count; ++i) {
        const auto& c = candles_[start + i];
        const int cx = static_cast<int>((i + 0.5) * slot_w);
        const bool bull = c.close >= c.open;
        const QColor& col = bull ? bull_color : bear_color;
        const QColor& wcol = bull ? wick_bull : wick_bear;

        const int open_y = py(c.open);
        const int close_y = py(c.close);
        const int high_y = py(c.high);
        const int low_y = py(c.low);

        const int body_top = std::min(open_y, close_y);
        const int body_bot = std::max(open_y, close_y);
        const int body_h = qMax(1, body_bot - body_top);

        p.setPen(QPen(wcol, 1));
        p.drawLine(cx, high_y, cx, body_top);
        p.drawLine(cx, body_bot, cx, low_y);
        p.fillRect(cx - half, body_top, body_w, body_h, col);
    }

    // ── SMA overlays ─────────────────────────────────────────────────────────
    // Computed across the full candles_ array in one O(n) pass per period.
    // Skipped if the array is shorter than the SMA window (no value to draw).
    auto compute_sma = [&](int period) -> QVector<double> {
        QVector<double> out(candles_.size(), std::numeric_limits<double>::quiet_NaN());
        if (static_cast<int>(candles_.size()) < period) return out;
        double sum = 0.0;
        for (int i = 0; i < period; ++i) sum += candles_[i].close;
        out[period - 1] = sum / period;
        for (int i = period; i < candles_.size(); ++i) {
            sum += candles_[i].close - candles_[i - period].close;
            out[i] = sum / period;
        }
        return out;
    };
    auto draw_sma = [&](int period, const QColor& color) {
        if (static_cast<int>(candles_.size()) < period) return;
        const auto sma = compute_sma(period);
        p.setPen(QPen(color, 1, Qt::SolidLine));
        QPainterPath path;
        bool started = false;
        for (int i = 0; i < count; ++i) {
            const double v = sma[start + i];
            if (std::isnan(v)) continue;
            const QPointF pt(static_cast<double>(i + 0.5) * slot_w, py(v));
            if (!started) { path.moveTo(pt); started = true; }
            else            path.lineTo(pt);
        }
        if (started)
            p.drawPath(path);
    };
    p.setRenderHint(QPainter::Antialiasing, true);
    if (show_sma20_)  draw_sma(20,  QColor("#3b82f6"));   // blue
    if (show_sma50_)  draw_sma(50,  QColor("#a855f7"));   // purple
    if (show_sma200_) draw_sma(200, QColor("#eab308"));   // amber

    // ── Comparison overlays ──────────────────────────────────────────────────
    // Comparison curves render at the comp's actual closing prices on the
    // same y-axis as the primary's candles. The y-range pass above already
    // extended lo/hi to include each comp's visible range, so the labels
    // span the union and every series is readable. Tradeoff vs normalized
    // overlay: relative performance is harder to eyeball, but the user can
    // read each ticker's actual price directly off the axis.
    if (!comparisons_.isEmpty() && count > 0) {
        for (const auto& comp : comparisons_) {
            if (comp.candles.isEmpty()) continue;
            // Find comp's anchor: first candle at or after candles_[start]'s
            // timestamp. Linear scan — comparisons are date-sorted ascending.
            const qint64 anchor_ts = candles_[start].timestamp;
            int comp_anchor_idx = -1;
            for (int k = 0; k < comp.candles.size(); ++k) {
                if (comp.candles[k].timestamp >= anchor_ts) {
                    comp_anchor_idx = k; break;
                }
            }
            if (comp_anchor_idx < 0) continue;

            p.setPen(QPen(comp.color, 1.5, Qt::SolidLine));
            QPainterPath path;
            bool started = false;
            int j = comp_anchor_idx;
            for (int i = 0; i < count; ++i) {
                const qint64 ts_i = candles_[start + i].timestamp;
                // Advance j until comp.candles[j].timestamp >= ts_i (sparse
                // matching — we paint the comp's latest known close at or
                // before each primary candle). j only moves forward.
                while (j + 1 < comp.candles.size() &&
                       comp.candles[j + 1].timestamp <= ts_i) ++j;
                const double y_value = comp.candles[j].close;
                const QPointF pt(static_cast<double>(i + 0.5) * slot_w, py(y_value));
                if (!started) { path.moveTo(pt); started = true; }
                else            path.lineTo(pt);
            }
            if (started) p.drawPath(path);
        }

        // Legend chips in a single right-aligned row at the top of the plot.
        // Each chip: colored swatch + symbol + total-window return %. Chips
        // are placed right-to-left so the first comparison sits closest to
        // the price axis; this keeps the user's eye-path from the rightmost
        // candle to the rightmost chip natural.
        QFont legend_font("Consolas", 9, QFont::Bold);
        p.setFont(legend_font);
        QFontMetrics lfm(legend_font);
        const int lh    = lfm.height();
        const int ly    = 4;
        int       lx_r  = width() - PRICE_AXIS_W - 6; // running right edge
        for (const auto& comp : comparisons_) {
            if (comp.candles.isEmpty()) continue;
            const qint64 anchor_ts = candles_[start].timestamp;
            int idx_first = -1, idx_last = comp.candles.size() - 1;
            for (int k = 0; k < comp.candles.size(); ++k) {
                if (comp.candles[k].timestamp >= anchor_ts) {
                    idx_first = k; break;
                }
            }
            if (idx_first < 0) continue;
            const double first_c = comp.candles[idx_first].close;
            const double last_c  = comp.candles[idx_last].close;
            if (first_c <= 0.0) continue;
            const double pct = (last_c - first_c) / first_c * 100.0;
            const QString text = comp.symbol + "  " +
                                  (pct >= 0 ? "+" : "") +
                                  QString::number(pct, 'f', 1) + "%";
            const int tw    = lfm.horizontalAdvance(text);
            const int box_w = tw + 14;
            const QRect r(lx_r - box_w, ly, box_w, lh + 2);
            p.fillRect(r, QColor(0, 0, 0, 160));
            // Color swatch on the left of each chip
            p.fillRect(r.left() + 3, r.center().y() - 4, 6, 8, comp.color);
            p.setPen(QColor(220, 220, 220));
            p.drawText(r.adjusted(12, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
            lx_r -= box_w + 6;
        }
    }
    p.setRenderHint(QPainter::Antialiasing, false);

    // ── Earnings markers ─────────────────────────────────────────────────────
    // For each event, find the first visible candle whose timestamp ≥ the
    // event time; draw a dashed vertical line + small "E" badge above the
    // chart. Events outside the visible window are skipped.
    if (!earnings_events_.isEmpty()) {
        const qint64 first_ts = candles_[start].timestamp;
        const qint64 last_ts  = candles_.last().timestamp;
        QPen earn_pen(QColor(255, 200, 64, 140), 1, Qt::DashLine);
        QFont earn_font("Consolas", 8, QFont::Bold);
        QFontMetrics efm(earn_font);
        for (const auto& e : earnings_events_) {
            if (e.timestamp < first_ts || e.timestamp > last_ts) continue;
            // Binary-search the visible slice for the first candle ≥ event ts.
            int lo_i = 0, hi_i = count - 1, found = -1;
            while (lo_i <= hi_i) {
                const int mid = (lo_i + hi_i) / 2;
                if (candles_[start + mid].timestamp >= e.timestamp) {
                    found = mid; hi_i = mid - 1;
                } else {
                    lo_i = mid + 1;
                }
            }
            if (found < 0) continue;
            const int ex = static_cast<int>((found + 0.5) * slot_w);
            p.setPen(earn_pen);
            p.drawLine(ex, 0, ex, plot_h + volume_h + volume_gap);
            p.setPen(QColor(255, 200, 64, 230));
            p.setFont(earn_font);
            // Pick color tinge by surprise sign (only if we have actual data).
            QString label = "E";
            if (e.has_actual && e.has_surprise) {
                label = e.surprise_pct >= 0 ? "E+" : "E-";
                p.setPen(QColor(e.surprise_pct >= 0
                                    ? ui::colors::POSITIVE.get()
                                    : ui::colors::NEGATIVE.get()));
            }
            const int tw = efm.horizontalAdvance(label);
            p.drawText(ex - tw / 2, efm.ascent() + 2, label);
        }
    }

    // ── Volume strip ─────────────────────────────────────────────────────────
    if (show_volume_) {
        // Range over visible window only — different period selections have
        // very different volume scales (5Y dwarfs 1M).
        double vmax = 1.0;
        for (int i = start; i < total; ++i)
            vmax = std::max(vmax, static_cast<double>(candles_[i].volume));
        // Strip border
        p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1));
        p.drawLine(0, volume_top, plot_w, volume_top);
        for (int i = 0; i < count; ++i) {
            const auto& c = candles_[start + i];
            if (c.volume <= 0) continue;
            const int cx = static_cast<int>((i + 0.5) * slot_w);
            const bool bull = c.close >= c.open;
            QColor col = bull ? bull_color : bear_color;
            col.setAlpha(180);
            const int bar_h = qMax(1, static_cast<int>(c.volume / vmax * (volume_h - 2)));
            const int by = volume_top + 1 + (volume_h - 2 - bar_h);
            p.fillRect(cx - half, by, body_w, bar_h, col);
        }
    }

    // ── Price axis (right) ───────────────────────────────────────────────────
    p.setPen(QPen(QColor(ui::colors::BORDER_DIM.get()), 1));
    p.drawLine(plot_w, 0, plot_w, plot_h);

    QFont lbl_font("Consolas", 9);
    p.setFont(lbl_font);
    QFontMetrics fm(lbl_font);
    p.setPen(QColor(ui::colors::TEXT_SECONDARY.get()));

    const bool is_large = hi > 1000;
    for (int g = 0; g <= 6; ++g) {
        // In log mode, distribute the tick prices in log space so the labels
        // match the visual position. In linear mode this is just lo+(hi-lo)*t.
        double price;
        if (log_scale_) {
            const double t = static_cast<double>(g) / 6.0;
            price = std::exp(lo_l + (hi_l - lo_l) * t);
        } else {
            price = lo + (hi - lo) * g / 6.0;
        }
        int gy = py(price);
        QString txt = currency_sym_ + (is_large ? QString::number(price, 'f', 0)
                                                : QString::number(price, 'f', 2));
        p.drawText(plot_w + 6, gy + fm.ascent() / 2, txt);
    }

    // ── Time axis (bottom — sits below volume strip if present) ──────────────
    const int time_axis_y = plot_h + volume_h + volume_gap;
    p.setPen(QPen(QColor(ui::colors::BORDER_DIM.get()), 1));
    p.drawLine(0, time_axis_y, plot_w, time_axis_y);

    p.setPen(QColor(ui::colors::TEXT_SECONDARY.get()));
    const qint64 span_sec = candles_.last().timestamp - candles_.first().timestamp;
    const int label_step = qMax(1, count / 8);

    for (int i = 0; i < count; i += label_step) {
        const auto& c = candles_[start + i];
        QDateTime dt = QDateTime::fromSecsSinceEpoch(c.timestamp);
        QString label;
        if (span_sec > 365LL * 86400)
            label = dt.toString("MMM yy");
        else if (span_sec > 60LL * 86400)
            label = dt.toString("dd MMM");
        else
            label = dt.toString("dd MMM");

        int lx = static_cast<int>((i + 0.5) * slot_w);
        int tw = fm.horizontalAdvance(label);
        if (lx - tw / 2 > 0 && lx + tw / 2 < plot_w)
            p.drawText(lx - tw / 2, time_axis_y + TIME_AXIS_H - 4, label);
    }

    // Last candle close price line — drawn after volume so it doesn't get
    // hidden if the user toggles volume.
    if (!candles_.isEmpty()) {
        double last_close = candles_.last().close;
        int ly = py(last_close);
        p.setPen(QPen(QColor(ui::colors::AMBER.get()), 1, Qt::DashLine));
        p.drawLine(0, ly, plot_w, ly);
    }
}

void ResearchCandleCanvas::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
    if (last_count_ <= 0 || last_slot_w_ <= 0.0) {
        if (hover_idx_ != -1) { hover_idx_ = -1; update(); }
        return;
    }
    const int x = event->position().toPoint().x();
    const int y = event->position().toPoint().y();
    // Outside the plot area? Drop hover.
    if (x < 0 || x >= last_plot_w_ || y < 0 || y >= last_plot_h_) {
        if (hover_idx_ != -1) { hover_idx_ = -1; update(); }
        return;
    }
    int visible_i = static_cast<int>(x / last_slot_w_);
    visible_i = std::clamp(visible_i, 0, last_count_ - 1);
    const int abs_idx = last_start_ + visible_i;
    if (abs_idx != hover_idx_) {
        hover_idx_ = abs_idx;
        update();  // full repaint is cheap (cached pixmap blit + overlay)
    }
}

void ResearchCandleCanvas::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (hover_idx_ != -1) {
        hover_idx_ = -1;
        update();
    }
}

void ResearchCandleCanvas::draw_hover_overlay(QPainter& p) {
    if (hover_idx_ < 0 || hover_idx_ >= candles_.size()) return;
    if (last_count_ <= 0 || last_slot_w_ <= 0.0) return;

    const int visible_i = hover_idx_ - last_start_;
    if (visible_i < 0 || visible_i >= last_count_) return;

    const int cx = static_cast<int>((visible_i + 0.5) * last_slot_w_);

    // 1. Dashed vertical crosshair line through the candle.
    p.setPen(QPen(QColor(ui::colors::TEXT_SECONDARY()), 1, Qt::DashLine));
    p.drawLine(cx, 0, cx, last_plot_h_);

    // 2. Build the readout text segments.
    const auto& c = candles_[hover_idx_];
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(c.timestamp);
    const QString date_str = dt.toString("ddd dd MMM yyyy");

    // Δ vs previous close (or open if it's the first candle).
    const double prev_close = (hover_idx_ > 0) ? candles_[hover_idx_ - 1].close : c.open;
    const double dchg = c.close - prev_close;
    const double dpct = (prev_close != 0.0) ? (dchg / prev_close * 100.0) : 0.0;

    auto fmt_p = [](double v) { return QString::number(v, 'f', 2); };
    auto fmt_v = [](double v) -> QString {
        if (v >= 1e9) return QString::number(v / 1e9, 'f', 2) + "B";
        if (v >= 1e6) return QString::number(v / 1e6, 'f', 1) + "M";
        if (v >= 1e3) return QString::number(v / 1e3, 'f', 1) + "K";
        return QString::number(v, 'f', 0);
    };
    auto fmt_signed = [&](double v) {
        return (v >= 0 ? QString("+") : QString()) + fmt_p(v);
    };
    auto fmt_signed2 = [](double v) {
        return (v >= 0 ? QString("+") : QString()) + QString::number(v, 'f', 2);
    };

    const QString left  = QString("%1   O %2  H %3  L %4  C %5")
                              .arg(date_str, fmt_p(c.open), fmt_p(c.high), fmt_p(c.low), fmt_p(c.close));
    const QString delta = QString("Δ %1 (%2%)").arg(fmt_signed(dchg), fmt_signed2(dpct));
    const QString right = QString("Vol %1").arg(fmt_v(c.volume));

    // Comparators — only built if the source data is set. These add context
    // a pro looks for at a glance: how far below 52w-high is this print, how
    // far from the 50d trend, etc.
    // Compute the comparator deltas once each. Both color and label below
    // reuse these values — previously each was computed twice per hover and
    // the SMA50 label had a broken format string ("SMA50 +%X.Y%" instead
    // of "SMA50 +X.Y%") because %1% in QString::arg() leaves a literal %
    // after the substitution.
    double off_high = std::numeric_limits<double>::quiet_NaN();
    QString vs_52w;
    if (week52_high_ > 0.0) {
        off_high = (c.close - week52_high_) / week52_high_ * 100.0;
        vs_52w = QString("52w-hi %1%").arg(off_high, 0, 'f', 1);  // negative = below
    }
    double off_sma = std::numeric_limits<double>::quiet_NaN();
    QString vs_sma50;
    if (candles_.size() >= 50) {
        // Reproduce the SMA50 value at hover_idx_ — same compute path as the
        // chart overlay. O(50) per hover, negligible.
        double sum = 0.0;
        const int s = qMax(0, hover_idx_ - 49);
        for (int k = s; k <= hover_idx_; ++k) sum += candles_[k].close;
        const int n = hover_idx_ - s + 1;
        if (n >= 50) {
            const double sma = sum / n;
            off_sma = (c.close - sma) / sma * 100.0;
            vs_sma50 = QString("SMA50 ") + (off_sma >= 0 ? "+" : "")
                           + QString::number(off_sma, 'f', 1) + "%";
        }
    }

    // Per-comparison readouts at the hovered date — actual close, colored to
    // match the curve. Lets the user trace each line back to a real price
    // as they move the crosshair along the time axis. Linear scan per comp
    // (≤3 comps × ≤5Y daily ≈ 4K ops per hover); negligible vs the paint cost.
    struct CompReadout { QColor color; QString text; int width = 0; };
    QVector<CompReadout> comp_readouts;
    const qint64 hover_ts = c.timestamp;
    for (const auto& comp : comparisons_) {
        if (comp.candles.isEmpty()) continue;
        int idx = -1;
        for (int k = 0; k < comp.candles.size(); ++k) {
            if (comp.candles[k].timestamp > hover_ts) break;
            idx = k;
        }
        if (idx < 0) continue;
        comp_readouts.push_back({comp.color,
                                  comp.symbol + " " + fmt_p(comp.candles[idx].close)});
    }

    // 3. Render the readout strip at the top-left of the plot area.
    QFont lbl_font("Consolas", 9);
    p.setFont(lbl_font);
    QFontMetrics fm(lbl_font);

    constexpr int PAD_X = 8;
    constexpr int PAD_Y = 4;
    constexpr int GAP   = 12;

    const int left_w   = fm.horizontalAdvance(left);
    const int delta_w  = fm.horizontalAdvance(delta);
    const int right_w  = fm.horizontalAdvance(right);
    const int v52w_w   = vs_52w.isEmpty()   ? 0 : fm.horizontalAdvance(vs_52w);
    const int vsma_w   = vs_sma50.isEmpty() ? 0 : fm.horizontalAdvance(vs_sma50);
    int primary_w = left_w + GAP + delta_w + GAP + right_w;
    if (v52w_w) primary_w += GAP + v52w_w;
    if (vsma_w) primary_w += GAP + vsma_w;
    int comp_w = 0;
    for (auto& cr : comp_readouts) {
        cr.width = fm.horizontalAdvance(cr.text);
        comp_w += (comp_w > 0 ? GAP : 0) + cr.width;
    }
    const int line_h  = fm.height();

    // If primary + comps on one line would spill past the plot's right edge,
    // wrap comps to a second line below the primary stats. Threshold uses
    // last_plot_w_ (snapshotted by rebuild_cache for this hover path) and
    // accounts for the bg-rect's own outer padding so the wrap fires just
    // before clipping starts, not after. Single-line otherwise.
    const int avail_w = last_plot_w_ - 2 * PAD_X;
    const bool wrap   = !comp_readouts.isEmpty() && avail_w > 0 &&
                        (primary_w + GAP + comp_w) > avail_w;
    const int total_w   = wrap ? std::max(primary_w, comp_w)
                               : primary_w + (comp_w > 0 ? GAP + comp_w : 0);
    const int total_h   = wrap ? (line_h * 2 + 2) : line_h;

    const QRect bg(PAD_X - 4, PAD_Y - 2, total_w + 8, total_h + 4);
    p.fillRect(bg, QColor(0, 0, 0, 180));
    p.setPen(QColor(ui::colors::BORDER_DIM()));
    p.drawRect(bg);

    int x_cursor       = PAD_X;
    int baseline       = PAD_Y + fm.ascent();

    p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
    p.drawText(x_cursor, baseline, left);
    x_cursor += left_w + GAP;

    p.setPen(QColor(dchg >= 0 ? ui::colors::POSITIVE.get() : ui::colors::NEGATIVE.get()));
    p.drawText(x_cursor, baseline, delta);
    x_cursor += delta_w + GAP;

    p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    p.drawText(x_cursor, baseline, right);
    x_cursor += right_w;

    if (!vs_52w.isEmpty()) {
        x_cursor += GAP;
        // Always "below" — green when close, red when far. Threshold 5%.
        p.setPen(off_high > -5.0
                     ? QColor(ui::colors::POSITIVE.get())
                     : QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(x_cursor, baseline, vs_52w);
        x_cursor += v52w_w;
    }
    if (!vs_sma50.isEmpty()) {
        x_cursor += GAP;
        p.setPen(off_sma >= 0 ? QColor(ui::colors::POSITIVE.get())
                              : QColor(ui::colors::NEGATIVE.get()));
        p.drawText(x_cursor, baseline, vs_sma50);
        x_cursor += vsma_w;
    }
    if (wrap) {
        // Drop to the second line; comps start flush with the primary row's
        // left edge so the bg rect's two rows line up.
        x_cursor = PAD_X;
        baseline += line_h;
    }
    bool first_comp = true;
    for (const auto& cr : comp_readouts) {
        // On a wrapped second line, the first comp has no leading gap (it
        // sits at PAD_X). On a single line, every comp gets a GAP separator
        // from whatever primary segment preceded it.
        if (!(wrap && first_comp))
            x_cursor += GAP;
        first_comp = false;
        p.setPen(cr.color);
        p.drawText(x_cursor, baseline, cr.text);
        x_cursor += cr.width;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  EquityOverviewTab
// ══════════════════════════════════════════════════════════════════════════════

EquityOverviewTab::EquityOverviewTab(QWidget* parent) : QWidget(parent) {
    build_ui();
    // No connects here — subscriptions are bound per-symbol in set_symbol()
    // via QueryStore. The legacy broadcast signals still fire on the service
    // for non-migrated tabs (Analysis, Technicals, …); we just don't listen
    // to them anymore. QueryStore::subscribe is responsible for routing the
    // State tuple only to subscribers of the matching key.
}

void EquityOverviewTab::set_symbol(const QString& symbol, bool force) {
    if (symbol.isEmpty())
        return;
    if (!force && symbol == current_symbol_)
        return;
    current_symbol_ = symbol;
    // Comparisons are picked per-primary (Apple → Msft/Goog; Boeing →
    // Lockheed/RTX). Switching primary invalidates that choice, so clear
    // the running comparison set, the canvas overlay, and the chip strip.
    comp_state_.clear();
    if (candle_canvas_) candle_canvas_->set_comparisons({});
    rebuild_comp_strip();
    // Restore the previously-saved chart view for this ticker (period +
    // overlay toggles + custom range). load_chart_view sets current_period_
    // before we subscribe below so the historical fetch uses the restored
    // period in one shot — avoids a wasted fetch for the default period.
    load_chart_view(symbol);
    if (candle_canvas_) {
        candle_canvas_->clear();
        candle_canvas_->set_placeholder_state(ResearchCandleCanvas::PlaceholderState::Loading);
    }
    if (loading_overlay_)
        loading_overlay_->show_loading("LOADING OVERVIEW\xe2\x80\xa6");

    // Drop every prior subscription before rebinding. Without this, a late
    // resolve for the previous symbol (quote/info/historical) would still
    // call into apply_*_state with stale data — exactly the filter-on-
    // receive bug class QueryStore is meant to eliminate.
    auto& store = services::query::QueryStore::instance();
    store.unsubscribe_all(this);

    auto& svc = services::equity::EquityResearchService::instance();
    svc.subscribe_quote(this, symbol,
        [this](const services::query::QueryStore::State& s) { apply_quote_state(s); });
    svc.subscribe_info(this, symbol,
        [this](const services::query::QueryStore::State& s) { apply_info_state(s); });
    svc.subscribe_historical(this, symbol, current_period_,
        [this](const services::query::QueryStore::State& s) { apply_historical_state(s); });
    current_historical_key_ = "equity:candles:" + symbol + ":" + current_period_;

    // Speculatively warm the most-likely-next periods. yfinance's
    // ticker.history is the heaviest call after .info, so pre-fetching while
    // the user examines the current period turns subsequent period clicks
    // into cache hits. Strategy: warm the longest non-active period (5Y) and
    // the most common alternative (1Y) — covers the typical drill-out
    // behavior. No-op for whichever one matches current_period_.
    for (const QString& period : {QStringLiteral("1y"), QStringLiteral("5y")}) {
        if (period != current_period_)
            svc.prefetch_historical(symbol, period);
    }

    // Earnings markers re-bind to the new symbol if the toggle is on.
    refresh_earnings_subscription();
}

// ── Build UI ──────────────────────────────────────────────────────────────────

void EquityOverviewTab::build_ui() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background:transparent;border:0;");
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* content = new QWidget(this);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* vl = new QVBoxLayout(content);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(6);

    auto* top = new QHBoxLayout;
    top->setSpacing(6);

    auto* col1 = build_col1();
    col1->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    col1->setFixedWidth(220);

    // build_chart_panel() creates candle_canvas_ as a member side effect.
    auto* chart = build_chart_panel();
    chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* col4 = build_col4();
    col4->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    col4->setFixedWidth(220);

    top->addWidget(col1);
    top->addWidget(chart, 1);
    top->addWidget(col4);

    vl->addLayout(top, 1);
    vl->addWidget(build_bottom_row(), 0);

    scroll->setWidget(content);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    // Scope the loading overlay to the candle canvas only — not the whole
    // tab. Side panels (Today's Trading, Valuation, etc.) populate as their
    // own signals arrive and stay readable; period buttons remain clickable
    // so the user can switch periods to interrupt a slow 5Y fetch.
    loading_overlay_ = new ui::LoadingOverlay(candle_canvas_);
}

// ── Column 1: Trading + Valuation + Share Stats ───────────────────────────────

QWidget* EquityOverviewTab::build_col1() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(6);
    vl->addWidget(build_trading_panel());
    vl->addWidget(build_valuation_panel());
    vl->addWidget(build_share_stats_panel());
    return w;
}

QWidget* EquityOverviewTab::build_trading_panel() {
    auto* p = make_panel("TODAY'S TRADING", ui::colors::AMBER);
    open_val_ = add_row(p, "OPEN", CYAN);
    high_val_ = add_row(p, "HIGH", ui::colors::POSITIVE);
    low_val_ = add_row(p, "LOW", ui::colors::NEGATIVE);
    prev_close_val_ = add_row(p, "PREV CLOSE", ui::colors::TEXT_PRIMARY);
    vol_val_ = add_row(p, "VOLUME", YELLOW, "volume");
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

QWidget* EquityOverviewTab::build_valuation_panel() {
    auto* p = make_panel("VALUATION", CYAN);
    mktcap_val_ = add_row(p, "MARKET CAP", CYAN,  "market-cap");
    pe_val_     = add_row(p, "P/E RATIO",  YELLOW,"pe-ratio");
    fwd_pe_val_ = add_row(p, "FWD P/E",    YELLOW,"pe-ratio");
    peg_val_    = add_row(p, "PEG RATIO",  YELLOW,"peg-ratio");
    pb_val_     = add_row(p, "P/B RATIO",  CYAN,  "pb-ratio");
    div_val_    = add_row(p, "DIV YIELD",  ui::colors::POSITIVE, "dividend-yield");
    beta_val_   = add_row(p, "BETA",       ui::colors::TEXT_PRIMARY, "beta");
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

QWidget* EquityOverviewTab::build_share_stats_panel() {
    auto* p = make_panel("SHARE STATS", PURPLE);
    shares_out_val_  = add_row(p, "SHARES OUT",    CYAN, "market-cap");
    float_val_       = add_row(p, "FLOAT",         CYAN, "float");
    insiders_val_    = add_row(p, "INSIDERS",      YELLOW);
    institutions_val_= add_row(p, "INSTITUTIONS",  YELLOW);
    short_pct_val_   = add_row(p, "SHORT %",       ui::colors::NEGATIVE, "short-interest");
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

// ── Chart Panel ───────────────────────────────────────────────────────────────

QWidget* EquityOverviewTab::build_chart_panel() {
    auto* p = new QFrame;
    chart_panel_ = p;
    p->setStyleSheet(QString("QFrame{background:%1;border:1px solid %2;border-radius:2px;}")
                         .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));

    auto* vl = new QVBoxLayout(p);
    vl->setContentsMargins(8, 6, 8, 6);
    vl->setSpacing(4);

    // Period buttons row
    auto* btn_row = new QHBoxLayout;
    btn_row->setSpacing(4);
    btn_row->setContentsMargins(0, 0, 0, 0);

    auto* period_lbl = new QLabel("PERIOD");
    period_lbl->setStyleSheet(QString("color:%1;font-size:12px;font-weight:600;background:transparent;border:0;")
                                  .arg(ui::colors::TEXT_SECONDARY()));
    btn_row->addWidget(period_lbl);

    auto btn_style_inactive =
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                "border-radius:2px;padding:3px 10px;font-size:12px;font-weight:700;font-family:'Consolas',monospace;}"
                "QPushButton:hover{border-color:%3;background:%4;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER(), ui::colors::BG_RAISED());

    auto btn_style_active =
        QString("QPushButton{background:%1;color:%2;border:1px solid %1;"
                "border-radius:2px;padding:3px 10px;font-size:12px;font-weight:700;font-family:'Consolas',monospace;}")
            .arg(ui::colors::AMBER(), ui::colors::BG_BASE());

    auto make_btn = [&](const QString& label, QPushButton*& out, const QString& period) {
        out = new QPushButton(label);
        out->setCursor(Qt::PointingHandCursor);
        out->setStyleSheet(period == current_period_ ? btn_style_active : btn_style_inactive);
        connect(out, &QPushButton::clicked, this, [this, out, period]() { switch_period(out, period); });
        btn_row->addWidget(out);
    };

    make_btn("1M", btn_1m_, "1mo");
    make_btn("3M", btn_3m_, "3mo");
    make_btn("6M", btn_6m_, "6mo");
    make_btn("1Y", btn_1y_, "1y");
    make_btn("5Y", btn_5y_, "5y");
    active_period_btn_ = btn_1y_;

    // Custom date-range button. Opens a tiny popover with two QDateEdits;
    // confirming routes through switch_period with a "range:start:end"
    // encoded period string. The daemon's get_historical_period decodes
    // it back to yfinance .history(start=, end=) — same code path as the
    // fixed periods so QueryStore caching, late-arrival filtering, and
    // overlay rendering all just work.
    btn_custom_ = new QPushButton(QStringLiteral("\xe2\x9c\x8e"));   // ✎ pencil glyph
    btn_custom_->setCursor(Qt::PointingHandCursor);
    btn_custom_->setToolTip("Custom date range");
    btn_custom_->setStyleSheet(btn_style_inactive);
    connect(btn_custom_, &QPushButton::clicked, this,
            [this]() { open_custom_range_picker(); });
    btn_row->addWidget(btn_custom_);

    // Canvas first — the chart toggles need candle_canvas_ to wire callbacks.
    candle_canvas_ = new ResearchCandleCanvas;

    // ── Chart overlay toggles ───────────────────────────────────────────────
    // LOG / VOL / SMA20/50/200 / EARN. Checkable QPushButtons; clicking
    // toggles the corresponding canvas option. Persist across symbol changes
    // (they live on the canvas, which we don't rebuild).
    btn_row->addSpacing(12);

    auto make_toggle = [&](const QString& label, const QString& tooltip,
                            const QString& accent,
                            std::function<void(bool)> on_toggle,
                            bool initial = false) -> QPushButton* {
        auto* b = new QPushButton(label);
        b->setCheckable(true);
        b->setChecked(initial);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tooltip);
        const QString ss = QString(
            "QPushButton{background:transparent;color:%1;border:1px solid %2;"
            "border-radius:2px;padding:3px 8px;font-size:11px;font-weight:700;"
            "font-family:'Consolas',monospace;}"
            "QPushButton:hover{border-color:%3;background:%4;}"
            "QPushButton:checked{background:%3;color:%5;border-color:%3;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), accent,
                 ui::colors::BG_RAISED(), ui::colors::BG_BASE());
        b->setStyleSheet(ss);
        QObject::connect(b, &QPushButton::toggled, this, on_toggle);
        btn_row->addWidget(b);
        return b;
    };

    btn_log_ = make_toggle("LOG", "Log-scale price axis (better for long periods)",
                ui::colors::AMBER(),
                [this](bool on) {
                    if (candle_canvas_) candle_canvas_->set_log_scale(on);
                    save_chart_view();
                });
    btn_vol_ = make_toggle("VOL", "Volume subchart under candles",
                ui::colors::AMBER(),
                [this](bool on) {
                    if (candle_canvas_) candle_canvas_->set_show_volume(on);
                    save_chart_view();
                });
    btn_row->addSpacing(8);
    btn_sma20_ = make_toggle("SMA20", "20-day simple moving average overlay", "#3b82f6",
                [this](bool on) {
                    if (candle_canvas_) candle_canvas_->set_show_sma(20, on);
                    save_chart_view();
                });
    btn_sma50_ = make_toggle("SMA50", "50-day simple moving average overlay", "#a855f7",
                [this](bool on) {
                    if (candle_canvas_) candle_canvas_->set_show_sma(50, on);
                    save_chart_view();
                });
    btn_sma200_= make_toggle("SMA200", "200-day simple moving average overlay", "#eab308",
                [this](bool on) {
                    if (candle_canvas_) candle_canvas_->set_show_sma(200, on);
                    save_chart_view();
                });
    btn_row->addSpacing(8);
    btn_earn_ = make_toggle("EARN", "Earnings markers — vertical lines on report dates",
                ui::colors::AMBER(),
                [this](bool on) {
                    show_earnings_ = on;
                    refresh_earnings_subscription();
                    save_chart_view();
                });

    btn_row->addSpacing(8);
    btn_comp_ = new QPushButton(QStringLiteral("COMP"));
    btn_comp_->setCursor(Qt::PointingHandCursor);
    btn_comp_->setToolTip("Comparison overlays — click to focus the ticker input");
    btn_comp_->setStyleSheet(QString(
        "QPushButton{background:transparent;color:%1;border:1px solid %2;"
        "border-radius:2px;padding:3px 8px;font-size:11px;font-weight:700;"
        "font-family:'Consolas',monospace;}"
        "QPushButton:hover{border-color:%3;background:%4;}")
        .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
             ui::colors::CYAN(), ui::colors::BG_RAISED()));
    // Click the COMP label → focus the inline input. Keeps the keyboard
    // shortcut (C) cheap: also just focus the input.
    connect(btn_comp_, &QPushButton::clicked, this,
            [this]() { if (comp_input_) comp_input_->setFocus(); });
    btn_row->addWidget(btn_comp_);

    // Inline chip strip — chips + ticker input live on the same row as the
    // overlay toggle buttons so add/delete is one click, no popup.
    auto* chips_holder = new QWidget;
    comp_chips_ = new QHBoxLayout(chips_holder);
    comp_chips_->setContentsMargins(0, 0, 0, 0);
    comp_chips_->setSpacing(4);
    btn_row->addWidget(chips_holder);

    comp_input_ = new QLineEdit;
    comp_input_->setMaxLength(12);
    comp_input_->setFixedWidth(80);
    comp_input_->setStyleSheet(QString(
        "QLineEdit{background:%1;color:%2;border:1px solid %3;border-radius:2px;"
        "padding:2px 6px;font-size:11px;font-family:'Consolas',monospace;}"
        "QLineEdit:focus{border-color:%4;}"
        "QLineEdit:disabled{color:%5;background:transparent;}")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_DIM(), ui::colors::CYAN(),
             ui::colors::TEXT_SECONDARY()));
    connect(comp_input_, &QLineEdit::returnPressed, this, [this]() {
        const QString sym = comp_input_->text().trimmed().toUpper();
        if (sym.isEmpty()) return;
        comp_input_->clear();
        add_comparison(sym);
    });
    btn_row->addWidget(comp_input_);
    refresh_comp_input_state();

    btn_row->addStretch();
    vl->addLayout(btn_row);
    vl->addWidget(candle_canvas_, 1);

    return p;
}

void EquityOverviewTab::switch_period(QPushButton* btn, const QString& period) {
    // Same period as current and we already have candles? Nothing to do.
    // (The cached_candles_ check replaces the old historical_loaded_ flag;
    // QueryStore owns the load state now, but we still want to avoid
    // re-binding the subscription when the user re-clicks the active button.)
    if (period == current_period_ && !cached_candles_.isEmpty())
        return;
    current_period_ = period;

    // Update button styles
    auto inactive =
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                "border-radius:2px;padding:3px 10px;font-size:12px;font-weight:700;font-family:'Consolas',monospace;}"
                "QPushButton:hover{border-color:%3;background:%4;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER(), ui::colors::BG_RAISED());

    auto active =
        QString("QPushButton{background:%1;color:%2;border:1px solid %1;"
                "border-radius:2px;padding:3px 10px;font-size:12px;font-weight:700;font-family:'Consolas',monospace;}")
            .arg(ui::colors::AMBER(), ui::colors::BG_BASE());

    for (auto* b : {btn_1m_, btn_3m_, btn_6m_, btn_1y_, btn_5y_, btn_custom_})
        if (b) b->setStyleSheet(b == btn ? active : inactive);
    active_period_btn_ = btn;

    // Clear the canvas + show the overlay so the user gets immediate visual
    // feedback that the chart is updating. QueryStore takes care of the
    // gate logic — switching the subscription key replaces the previous
    // historical subscription so late deliveries for the old period (if any
    // are still in flight) go to the dropped subscription, not us.
    if (candle_canvas_) {
        candle_canvas_->clear();
        candle_canvas_->set_placeholder_state(ResearchCandleCanvas::PlaceholderState::Loading);
    }
    if (loading_overlay_)
        loading_overlay_->show_loading("LOADING CHART\xe2\x80\xa6");

    // Rebind the historical subscription to the new period. Quote+info
    // subscriptions keep their existing keys (they don't depend on period).
    if (!current_symbol_.isEmpty()) {
        auto& store = services::query::QueryStore::instance();
        // Drop the prior period's historical subscription before binding
        // the new one. A late resolve for the previous period would
        // otherwise overwrite the chart with old data once the new period's
        // fetch finishes.
        if (!current_historical_key_.isEmpty())
            store.unsubscribe(this, current_historical_key_);
        services::equity::EquityResearchService::instance().subscribe_historical(
            this, current_symbol_, period,
            [this](const services::query::QueryStore::State& s) { apply_historical_state(s); });
        current_historical_key_ = "equity:candles:" + current_symbol_ + ":" + period;
    }
    // Re-fetch comparison series at the new period so the overlays stay in
    // sync with the primary chart's window.
    refresh_comparisons();
    save_chart_view();
}

void EquityOverviewTab::save_chart_view() const {
    if (restoring_view_) return;
    if (current_symbol_.isEmpty()) return;
    // Read overlay state from the button check marks rather than mirroring
    // it on the tab. Those bools live on the canvas (ResearchCandleCanvas)
    // and accessors aren't exposed — buttons are the single source of truth
    // for what the user has toggled.
    QJsonObject view;
    view.insert("period",  current_period_);
    view.insert("log",     btn_log_    ? btn_log_->isChecked()    : false);
    view.insert("vol",     btn_vol_    ? btn_vol_->isChecked()    : false);
    view.insert("sma20",   btn_sma20_  ? btn_sma20_->isChecked()  : false);
    view.insert("sma50",   btn_sma50_  ? btn_sma50_->isChecked()  : false);
    view.insert("sma200",  btn_sma200_ ? btn_sma200_->isChecked() : false);
    view.insert("earn",    show_earnings_);
    const QString blob = QString::fromUtf8(
        QJsonDocument(view).toJson(QJsonDocument::Compact));
    fincept::SettingsRepository::instance().set(
        "er.chart_view." + current_symbol_, blob);
}

void EquityOverviewTab::load_chart_view(const QString& symbol) {
    auto r = fincept::SettingsRepository::instance().get(
        "er.chart_view." + symbol);
    if (!r.is_ok() || r.value().isEmpty()) {
        // No saved view for this symbol — keep the previous tab's settings.
        // (We don't reset toggles to off; users typically want their chart
        // preferences to persist across un-saved tickers too.)
        return;
    }
    const auto doc = QJsonDocument::fromJson(r.value().toUtf8());
    if (!doc.isObject()) return;
    const auto v = doc.object();

    restoring_view_ = true;
    const QString period = v.value("period").toString("1y");
    current_period_ = period;

    auto apply = [](QPushButton* b, bool on) {
        if (b && b->isChecked() != on) b->setChecked(on);
    };
    apply(btn_log_,    v.value("log").toBool(false));
    apply(btn_vol_,    v.value("vol").toBool(false));
    apply(btn_sma20_,  v.value("sma20").toBool(false));
    apply(btn_sma50_,  v.value("sma50").toBool(false));
    apply(btn_sma200_, v.value("sma200").toBool(false));
    apply(btn_earn_,   v.value("earn").toBool(false));

    // Reflect the saved period in the active-button highlight. The set of
    // fixed period buttons may not include the saved period (e.g. saved
    // value was a "range:…" custom). In that case nothing gets the active
    // style — the custom-range pencil glyph signals the state implicitly.
    QPushButton* match = nullptr;
    if      (period == "1mo") match = btn_1m_;
    else if (period == "3mo") match = btn_3m_;
    else if (period == "6mo") match = btn_6m_;
    else if (period == "1y")  match = btn_1y_;
    else if (period == "5y")  match = btn_5y_;
    else if (period.startsWith("range:")) match = btn_custom_;

    auto inactive_ss =
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                "border-radius:2px;padding:3px 10px;font-size:12px;font-weight:700;font-family:'Consolas',monospace;}"
                "QPushButton:hover{border-color:%3;background:%4;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                 ui::colors::AMBER(), ui::colors::BG_RAISED());
    auto active_ss =
        QString("QPushButton{background:%1;color:%2;border:1px solid %1;"
                "border-radius:2px;padding:3px 10px;font-size:12px;font-weight:700;font-family:'Consolas',monospace;}")
            .arg(ui::colors::AMBER(), ui::colors::BG_BASE());
    for (auto* b : {btn_1m_, btn_3m_, btn_6m_, btn_1y_, btn_5y_, btn_custom_})
        if (b) b->setStyleSheet(b == match ? active_ss : inactive_ss);
    active_period_btn_ = match;

    restoring_view_ = false;
}

// Tight cap on series count — beyond 3 the chart starts to wash out and
// the legend column eats the right edge of the plot.
static constexpr int kMaxComparisons = 3;

// Fixed 6-step palette for comparison lines. We pick by index modulo so
// even after removes the remaining series keep deterministic colours
// matching the canvas legend.
static const QVector<QColor>& comp_palette() {
    static const QVector<QColor> kPalette = {
        QColor("#06b6d4"), QColor("#10b981"), QColor("#f59e0b"),
        QColor("#ef4444"), QColor("#a855f7"), QColor("#3b82f6"),
    };
    return kPalette;
}

void EquityOverviewTab::add_comparison(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty()) return;
    if (sym == current_symbol_) return;               // pointless self-comp
    for (const auto& c : comp_state_)
        if (c.symbol == sym) return;                  // already added
    if (comp_state_.size() >= kMaxComparisons) return;

    const QColor color = comp_palette().at(comp_state_.size() % comp_palette().size());
    comp_state_.append({sym, color, {}});
    refresh_comparisons();
    rebuild_comp_strip();
}

void EquityOverviewTab::rebuild_comp_strip() {
    if (!comp_chips_) return;
    // Tear down existing chip widgets. Layout is small (≤3 chips) so the
    // takeAt loop is trivially fast; deleteLater keeps us safe if any
    // signal handler is mid-fire on the buttons we're removing.
    while (auto* it = comp_chips_->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    for (int i = 0; i < comp_state_.size(); ++i) {
        const QString sym = comp_state_[i].symbol;
        const QString color_name = comp_state_[i].color.name();

        auto* chip = new QFrame;
        chip->setStyleSheet(QString(
            "QFrame{background:%1;border:1px solid %2;border-radius:2px;}")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
        auto* hl = new QHBoxLayout(chip);
        hl->setContentsMargins(4, 1, 2, 1);
        hl->setSpacing(4);

        auto* swatch = new QLabel;
        swatch->setFixedSize(8, 8);
        swatch->setStyleSheet(QString("background:%1;border-radius:1px;").arg(color_name));
        hl->addWidget(swatch);

        auto* sym_lbl = new QLabel(sym);
        sym_lbl->setStyleSheet(QString(
            "color:%1;font-weight:700;font-size:11px;font-family:'Consolas',monospace;"
            "background:transparent;border:0;")
            .arg(ui::colors::TEXT_PRIMARY()));
        hl->addWidget(sym_lbl);

        auto* rm = new QPushButton(QStringLiteral("✕"));
        rm->setCursor(Qt::PointingHandCursor);
        rm->setToolTip("Remove from comparison");
        rm->setFixedSize(16, 16);
        rm->setStyleSheet(QString(
            "QPushButton{background:transparent;color:%1;border:0;"
            "padding:0;font-size:11px;font-weight:700;}"
            "QPushButton:hover{color:%2;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::NEGATIVE()));
        // Remove-by-symbol (not by index) — index could shift if a
        // simultaneous add/remove races; symbol is the stable key.
        //
        // Re-entrance: rebuild_comp_strip() below schedules every existing
        // chip widget (including `rm`, the button currently in its slot)
        // for deleteLater. That's safe — deleteLater defers destruction to
        // the next event-loop iteration, and the connect's receiver context
        // is `this` (the tab), not the button, so the connection's lifetime
        // tracker doesn't fire mid-emit.
        connect(rm, &QPushButton::clicked, this, [this, sym]() {
            for (int j = 0; j < comp_state_.size(); ++j) {
                if (comp_state_[j].symbol == sym) {
                    comp_state_.removeAt(j);
                    break;
                }
            }
            refresh_comparisons();
            rebuild_comp_strip();
        });
        hl->addWidget(rm);

        comp_chips_->addWidget(chip);
    }
    refresh_comp_input_state();
}

void EquityOverviewTab::refresh_comp_input_state() {
    if (!comp_input_) return;
    const bool at_max = comp_state_.size() >= kMaxComparisons;
    comp_input_->setEnabled(!at_max);
    comp_input_->setPlaceholderText(at_max ? QStringLiteral("(max %1)").arg(kMaxComparisons)
                                           : QStringLiteral("ticker…"));
}

void EquityOverviewTab::refresh_comparisons() {
    if (!candle_canvas_) return;
    if (comp_state_.isEmpty()) {
        candle_canvas_->set_comparisons({});
        return;
    }

    // Push the current candle snapshots up to the canvas right away so the
    // user sees the existing overlays immediately on dialog close. Any
    // empty `candles` slots will be filled in by the callbacks below.
    auto push_to_canvas = [this]() {
        QVector<ResearchCandleCanvas::Comparison> out;
        out.reserve(comp_state_.size());
        for (const auto& cs : comp_state_) {
            out.append({cs.symbol, cs.color, cs.candles});
        }
        candle_canvas_->set_comparisons(out);
    };
    push_to_canvas();

    auto& svc = services::equity::EquityResearchService::instance();
    const QString period_at_fetch = current_period_;
    const QString primary_at_fetch = current_symbol_;
    for (int i = 0; i < comp_state_.size(); ++i) {
        const QString sym = comp_state_[i].symbol;
        svc.subscribe_historical(
            this, sym, period_at_fetch,
            [this, sym, period_at_fetch, primary_at_fetch, push_to_canvas]
            (const services::query::QueryStore::State& s) {
                // Drop stale callbacks (primary or period changed mid-flight).
                if (primary_at_fetch != current_symbol_) return;
                if (period_at_fetch != current_period_) return;
                if (!candle_canvas_) return;
                if (!s.data.isValid() || s.data.isNull()) return;
                const auto candles = s.data.value<QVector<services::equity::Candle>>();
                if (candles.isEmpty()) return;
                // Update the running CompState entry — by symbol, since
                // index could have shifted if the user removed a chip
                // mid-flight. Re-push the whole list so the canvas keeps
                // all currently-loaded series visible.
                for (auto& cs : comp_state_) {
                    if (cs.symbol == sym) { cs.candles = candles; break; }
                }
                push_to_canvas();
            });
    }
}

// ── Keyboard shortcuts ───────────────────────────────────────────────────────
//
// Each method routes the keystroke through the same path a mouse click
// would take — clicking the button, toggling the canvas, opening the
// dialog. No new state, no side channels. The QPushButton::click() call
// fires the existing toggled/clicked signals so save_chart_view runs
// automatically.

void EquityOverviewTab::shortcut_set_period(int slot) {
    QPushButton* b = nullptr;
    switch (slot) {
        case 1: b = btn_1m_;  break;
        case 2: b = btn_3m_;  break;
        case 3: b = btn_6m_;  break;
        case 4: b = btn_1y_;  break;
        case 5: b = btn_5y_;  break;
        default: return;
    }
    if (b) b->click();
}

void EquityOverviewTab::shortcut_toggle_log() {
    if (btn_log_) btn_log_->toggle();
}
void EquityOverviewTab::shortcut_toggle_volume() {
    if (btn_vol_) btn_vol_->toggle();
}
void EquityOverviewTab::shortcut_toggle_sma50() {
    if (btn_sma50_) btn_sma50_->toggle();
}
void EquityOverviewTab::shortcut_toggle_earnings() {
    if (btn_earn_) btn_earn_->toggle();
}
void EquityOverviewTab::shortcut_open_comparison() {
    if (comp_input_) comp_input_->setFocus();
}
void EquityOverviewTab::shortcut_open_range_picker() {
    if (btn_custom_) btn_custom_->click();
}

void EquityOverviewTab::open_custom_range_picker() {
    // Two QDateEdits in a small dialog — Bloomberg-style date input is over-
    // kill here, the user picks a window once and lives with it. Seeds with
    // the last 90 days so the dialog opens to a useful default.
    QDialog dlg(this);
    dlg.setWindowTitle("Custom date range");
    dlg.setModal(true);

    auto* fl = new QFormLayout(&dlg);
    fl->setContentsMargins(16, 16, 16, 16);
    fl->setSpacing(10);

    const QDate today = QDate::currentDate();
    auto* start_edit = new QDateEdit(today.addDays(-90), &dlg);
    auto* end_edit   = new QDateEdit(today, &dlg);
    for (auto* de : {start_edit, end_edit}) {
        de->setCalendarPopup(true);
        de->setDisplayFormat("yyyy-MM-dd");
        // Hard cap at today — yfinance can't price the future.
        de->setMaximumDate(today);
        // Yahoo only retains ~30 years of daily data; deeper than 1990
        // wastes a daemon round-trip that always comes back empty.
        de->setMinimumDate(QDate(1990, 1, 1));
    }
    fl->addRow("Start", start_edit);
    fl->addRow("End",   end_edit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                          Qt::Horizontal, &dlg);
    fl->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    QDate start = start_edit->date();
    QDate end   = end_edit->date();
    // Swap if user picked backwards — keeps the daemon path safe and the
    // resulting candle vector non-empty.
    if (start > end) std::swap(start, end);
    if (start == end) return;   // degenerate — yfinance returns 0 candles

    // Encode as "range:YYYY-MM-DD:YYYY-MM-DD". get_historical_period in
    // yfinance_data.py decodes this back to ticker.history(start=, end=).
    // QueryStore caches by key, so re-picking the same window in the same
    // session hits cache.
    const QString range = "range:" + start.toString(Qt::ISODate) + ":" +
                          end.toString(Qt::ISODate);
    switch_period(btn_custom_, range);
}

// ── Column 4: Analyst + 52W + Profitability + Growth ─────────────────────────

QWidget* EquityOverviewTab::build_col4() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(6);
    vl->addWidget(build_analyst_panel());
    vl->addWidget(build_52w_panel());
    vl->addWidget(build_profitability_panel());
    vl->addWidget(build_growth_panel());
    return w;
}

QWidget* EquityOverviewTab::build_analyst_panel() {
    auto* p = make_panel("ANALYST TARGETS", MAGENTA);
    target_high_val_ = add_row(p, "HIGH", ui::colors::POSITIVE);
    target_mean_val_ = add_row(p, "MEAN", YELLOW);
    target_low_val_ = add_row(p, "LOW", ui::colors::NEGATIVE);
    analyst_count_val_ = add_row(p, "ANALYSTS", CYAN);

    rec_key_label_ = new QLabel("\xe2\x80\x94");
    rec_key_label_->setAlignment(Qt::AlignCenter);
    rec_key_label_->setStyleSheet(QString("background:%1;color:%2;border-radius:2px;padding:3px 8px;"
                                          "font-size:12px;font-weight:700;")
                                      .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY()));
    static_cast<QVBoxLayout*>(p->layout())->addWidget(rec_key_label_);
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

QWidget* EquityOverviewTab::build_52w_panel() {
    auto* p = make_panel("52 WEEK RANGE", YELLOW);
    w52h_val_ = add_row(p, "HIGH", ui::colors::POSITIVE);
    w52l_val_ = add_row(p, "LOW", ui::colors::NEGATIVE);
    avg_vol_val_ = add_row(p, "AVG VOL", CYAN);
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

QWidget* EquityOverviewTab::build_profitability_panel() {
    auto* p = make_panel("PROFITABILITY", ui::colors::POSITIVE);
    gross_margin_val_ = add_row(p, "GROSS MARGIN", ui::colors::POSITIVE);
    op_margin_val_ = add_row(p, "OPER. MARGIN", ui::colors::POSITIVE);
    profit_margin_val_ = add_row(p, "PROFIT MARGIN", ui::colors::POSITIVE);
    roa_val_ = add_row(p, "ROA", CYAN);
    roe_val_ = add_row(p, "ROE", CYAN);
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

QWidget* EquityOverviewTab::build_growth_panel() {
    auto* p = make_panel("GROWTH RATES", BLUE);
    rev_growth_val_ = add_row(p, "REVENUE", ui::colors::POSITIVE);
    earnings_growth_val_ = add_row(p, "EARNINGS", ui::colors::POSITIVE);
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

// ── Bottom Row ────────────────────────────────────────────────────────────────

QWidget* EquityOverviewTab::build_bottom_row() {
    auto* w = new QWidget(this);
    auto* hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);
    hl->addWidget(build_company_desc_panel(), 2);

    auto* right = new QWidget(this);
    auto* rvl = new QVBoxLayout(right);
    rvl->setContentsMargins(0, 0, 0, 0);
    rvl->setSpacing(6);
    rvl->addWidget(build_company_info_panel());
    rvl->addWidget(build_financial_health_panel());

    hl->addWidget(right, 1);
    return w;
}

QWidget* EquityOverviewTab::build_company_desc_panel() {
    auto* p = make_panel("COMPANY OVERVIEW", CYAN);
    company_desc_ = new QLabel("\xe2\x80\x94");
    company_desc_->setWordWrap(true);
    company_desc_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    company_desc_->setStyleSheet(QString("color:%1;font-size:%2px;line-height:1.5;"
                                         "background:transparent;border:0;")
                                     .arg(ui::colors::TEXT_PRIMARY())
                                     .arg(FONT_DESC));
    company_desc_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    static_cast<QVBoxLayout*>(p->layout())->addWidget(company_desc_);
    return p;
}

QWidget* EquityOverviewTab::build_company_info_panel() {
    auto* p = make_panel("COMPANY INFO", ui::colors::TEXT_PRIMARY);
    company_emp_ = add_row(p, "EMPLOYEES", CYAN);
    company_web_ = add_row(p, "WEBSITE", BLUE);
    company_currency_ = add_row(p, "CURRENCY", CYAN);
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

QWidget* EquityOverviewTab::build_financial_health_panel() {
    auto* p = make_panel("FINANCIAL HEALTH", ui::colors::AMBER);
    cash_val_ = add_row(p, "CASH", ui::colors::POSITIVE);
    debt_val_ = add_row(p, "DEBT", ui::colors::NEGATIVE);
    free_cf_val_ = add_row(p, "FREE CF", CYAN);
    static_cast<QVBoxLayout*>(p->layout())->addStretch();
    return p;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void EquityOverviewTab::apply_quote_state(const services::query::QueryStore::State& s) {
    // Loading/error transitions: no panel update needed — the quote panel
    // already shows dashes from clear_quote_bar() / initial state. If a
    // future iteration wants a skeleton-pulse on loading, it slots in here.
    if (!s.data.isValid() || s.data.isNull())
        return;
    const auto q = s.data.value<services::equity::QuoteData>();
    if (!q.valid)
        return;
    cached_quote_ = q;
    open_val_->setText(fmt_price(q.open));
    high_val_->setText(fmt_price(q.high));
    low_val_->setText(fmt_price(q.low));
    prev_close_val_->setText(fmt_price(q.prev_close));
    vol_val_->setText(fmt_large(q.volume));
}

void EquityOverviewTab::apply_info_state(const services::query::QueryStore::State& s) {
    if (!s.data.isValid() || s.data.isNull())
        return;
    const auto info = s.data.value<services::equity::StockInfo>();
    if (!info.valid)
        return;
    cached_info_ = info;
    current_currency_ = info.currency;
    // Feed the 52-week high to the canvas so the hover crosshair can show
    // "vs 52w-high" relative %. Zero suppresses the readout cleanly.
    if (candle_canvas_)
        candle_canvas_->set_week52_high(info.week52_high);

    // Re-render quote (now with correct currency symbol) and chart (likewise).
    if (cached_quote_.valid) {
        open_val_->setText(fmt_price(cached_quote_.open));
        high_val_->setText(fmt_price(cached_quote_.high));
        low_val_->setText(fmt_price(cached_quote_.low));
        prev_close_val_->setText(fmt_price(cached_quote_.prev_close));
    }
    if (!cached_candles_.isEmpty()) {
        candle_canvas_->set_candles(cached_candles_,
                                    currency_symbol(current_currency_.isEmpty() ? "USD" : current_currency_));
    }

    // Valuation
    mktcap_val_->setText(info.market_cap > 0 ? fmt_large(info.market_cap) : "N/A");
    pe_val_->setText(info.pe_ratio > 0 ? QString::number(info.pe_ratio, 'f', 2) : "N/A");
    fwd_pe_val_->setText(info.forward_pe > 0 ? QString::number(info.forward_pe, 'f', 2) : "N/A");
    peg_val_->setText(info.peg_ratio > 0 ? QString::number(info.peg_ratio, 'f', 2) : "N/A");
    pb_val_->setText(info.price_to_book > 0 ? QString::number(info.price_to_book, 'f', 2) : "N/A");
    div_val_->setText(info.dividend_yield > 0 ? fmt_pct(info.dividend_yield) : "N/A");
    beta_val_->setText(info.beta != 0.0 ? QString::number(info.beta, 'f', 2) : "N/A");

    // Share Stats
    shares_out_val_->setText(fmt_large(info.shares_outstanding));
    float_val_->setText(fmt_large(info.float_shares));
    insiders_val_->setText(fmt_pct(info.held_insiders_pct));
    institutions_val_->setText(fmt_pct(info.held_institutions_pct));
    short_pct_val_->setText(fmt_pct(info.short_pct_of_float));

    // 52 Week Range
    w52h_val_->setText(fmt_price(info.week52_high));
    w52l_val_->setText(fmt_price(info.week52_low));
    avg_vol_val_->setText(fmt_large(info.avg_volume));

    // Analyst Targets
    target_high_val_->setText(fmt_price(info.target_high));
    target_mean_val_->setText(fmt_price(info.target_mean));
    target_low_val_->setText(fmt_price(info.target_low));
    analyst_count_val_->setText(info.analyst_count > 0 ? QString::number(info.analyst_count) : "N/A");

    QString rec = info.recommendation_key.toUpper().replace("_", " ");
    const char* rec_color = ui::colors::TEXT_SECONDARY;
    if (rec == "STRONG BUY" || rec == "BUY")
        rec_color = ui::colors::POSITIVE;
    else if (rec == "STRONG SELL" || rec == "SELL")
        rec_color = ui::colors::NEGATIVE;
    else if (rec == "HOLD")
        rec_color = ui::colors::AMBER;
    rec_key_label_->setText(rec.isEmpty() ? "\xe2\x80\x94" : rec);
    rec_key_label_->setStyleSheet(QString("background:%1;color:%2;border-radius:2px;padding:3px 8px;"
                                          "font-size:12px;font-weight:700;")
                                      .arg(ui::colors::BG_RAISED(), rec_color));

    // Profitability
    gross_margin_val_->setText(fmt_pct(info.gross_margins));
    op_margin_val_->setText(fmt_pct(info.operating_margins));
    profit_margin_val_->setText(fmt_pct(info.profit_margins));
    roa_val_->setText(fmt_pct(info.roa));
    roe_val_->setText(fmt_pct(info.roe));

    // Growth
    rev_growth_val_->setText(fmt_pct(info.revenue_growth));
    earnings_growth_val_->setText(fmt_pct(info.earnings_growth));

    // Company Info
    company_desc_->setText(info.description);
    company_emp_->setText(info.employees > 0 ? fmt_large(static_cast<double>(info.employees)) : "N/A");
    company_web_->setText(info.website.left(40));
    company_currency_->setText(info.currency.isEmpty() ? "N/A" : info.currency);

    // Financial Health
    cash_val_->setText(fmt_large(info.total_cash));
    debt_val_->setText(fmt_large(info.total_debt));
    free_cf_val_->setText(fmt_large(info.free_cashflow));
}

void EquityOverviewTab::apply_historical_state(const services::query::QueryStore::State& s) {
    // The historical subscription owns the overlay. Other categories
    // (quote/info) populate their panels asynchronously and don't gate the
    // chart-loading affordance.
    if (!s.error.isEmpty()) {
        // Fetch failed — show the Error placeholder and hide the overlay so
        // the message is readable. Cached candles, if any, are kept on
        // screen until the next successful fetch overwrites them.
        if (candle_canvas_)
            candle_canvas_->set_placeholder_state(ResearchCandleCanvas::PlaceholderState::Error);
        if (loading_overlay_)
            loading_overlay_->hide_loading();
        return;
    }
    if (!s.data.isValid() || s.data.isNull()) {
        // Still loading — keep the overlay up.
        if (s.loading && loading_overlay_ && !loading_overlay_->isVisible())
            loading_overlay_->show_loading("LOADING CHART\xe2\x80\xa6");
        return;
    }
    const auto candles = s.data.value<QVector<services::equity::Candle>>();
    cached_candles_ = candles;
    // Empty success = valid symbol, no candles for this period (delisted /
    // pre-IPO / illiquid). Show the NoData placeholder; the Error
    // placeholder is reserved for actual fetch failures (s.error path
    // above).
    if (candle_canvas_ && candles.isEmpty())
        candle_canvas_->set_placeholder_state(ResearchCandleCanvas::PlaceholderState::NoData);
    rebuild_chart(candles);
    if (loading_overlay_ && !s.loading)
        loading_overlay_->hide_loading();
}

void EquityOverviewTab::apply_earnings_state(const services::query::QueryStore::State& s) {
    if (!candle_canvas_) return;
    if (!s.error.isEmpty() || !s.data.isValid() || s.data.isNull()) {
        candle_canvas_->set_earnings_events({});
        return;
    }
    const auto events = s.data.value<QVector<services::equity::EarningsEvent>>();
    candle_canvas_->set_earnings_events(events);
}

void EquityOverviewTab::refresh_earnings_subscription() {
    auto& store = services::query::QueryStore::instance();
    // Drop any prior earnings subscription unconditionally — either the
    // toggle is off (clear markers) or the symbol has changed (rebind below).
    if (!current_earnings_key_.isEmpty())
        store.unsubscribe(this, current_earnings_key_);
    current_earnings_key_.clear();
    if (candle_canvas_)
        candle_canvas_->set_earnings_events({});  // clear any prior

    if (!show_earnings_ || current_symbol_.isEmpty())
        return;
    services::equity::EquityResearchService::instance().subscribe_earnings(
        this, current_symbol_,
        [this](const services::query::QueryStore::State& st) { apply_earnings_state(st); });
    current_earnings_key_ = "equity:earnings:" + current_symbol_;
}

void EquityOverviewTab::rebuild_chart(const QVector<services::equity::Candle>& candles) {
    const QString cs = currency_symbol(current_currency_.isEmpty() ? "USD" : current_currency_);
    candle_canvas_->set_candles(candles, cs);
}

// ── Formatters ────────────────────────────────────────────────────────────────

QString EquityOverviewTab::fmt_large(double v) {
    bool neg = v < 0;
    double av = qAbs(v);
    QString s;
    if (av >= 1e12)
        s = QString("%1T").arg(av / 1e12, 0, 'f', 2);
    else if (av >= 1e9)
        s = QString("%1B").arg(av / 1e9, 0, 'f', 2);
    else if (av >= 1e6)
        s = QString("%1M").arg(av / 1e6, 0, 'f', 2);
    else if (av >= 1e3)
        s = QString("%1K").arg(av / 1e3, 0, 'f', 1);
    else
        s = QString::number(static_cast<qint64>(av));
    return neg ? "-" + s : s;
}

QString EquityOverviewTab::fmt_pct(double v) {
    return QString("%1%").arg(v * 100.0, 0, 'f', 2);
}

QString EquityOverviewTab::currency_symbol(const QString& currency_code) {
    static const QHash<QString, QString> map = {
        {"USD", "$"},
        {"EUR", "\xe2\x82\xac"},
        {"GBP", "\xc2\xa3"},
        {"JPY", "\xc2\xa5"},
        {"CNY", "\xc2\xa5"},
        {"INR", "\xe2\x82\xb9"},
        {"KRW", "\xe2\x82\xa9"},
        {"RUB", "\xe2\x82\xbd"},
        {"BRL", "R$"},
        {"TRY", "\xe2\x82\xba"},
        {"CHF", "CHF "},
        {"CAD", "C$"},
        {"AUD", "A$"},
        {"NZD", "NZ$"},
        {"HKD", "HK$"},
        {"SGD", "S$"},
        {"TWD", "NT$"},
        {"MXN", "MX$"},
        {"ZAR", "R"},
        {"SEK", "kr"},
        {"NOK", "kr"},
        {"DKK", "kr"},
        {"PLN", "z\xc5\x82"},
        {"THB", "\xe0\xb8\xbf"},
        {"IDR", "Rp"},
        {"MYR", "RM"},
        {"PHP", "\xe2\x82\xb1"},
        {"CZK", "K\xc4\x8d"},
        {"HUF", "Ft"},
        {"ILS", "\xe2\x82\xaa"},
        {"CLP", "CL$"},
        {"COP", "COL$"},
        {"PEN", "S/."},
        {"ARS", "AR$"},
        {"EGP", "E\xc2\xa3"},
        {"NGN", "\xe2\x82\xa6"},
        {"PKR", "Rs"},
        {"BDT", "\xe0\xa7\xb3"},
        {"VND", "\xe2\x82\xab"},
        {"SAR", "SR"},
        {"QAR", "QR"},
        {"AED", "AED "},
        {"KWD", "KD"},
    };
    const auto it = map.find(currency_code.toUpper());
    return (it != map.end()) ? it.value() : (currency_code + " ");
}

QString EquityOverviewTab::fmt_price(double v) const {
    if (v == 0.0)
        return "\xe2\x80\x94";
    const QString sym = current_currency_.isEmpty() ? "$" : currency_symbol(current_currency_);
    return QString("%1%2").arg(sym).arg(v, 0, 'f', 2);
}

} // namespace fincept::screens

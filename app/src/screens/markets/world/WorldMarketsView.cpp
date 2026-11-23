#include "screens/markets/world/WorldMarketsView.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "screens/dashboard/widgets/SectorHeatmapStyle.h"
#include "screens/markets/world/MarketSessions.h"
#include "screens/markets/world/OvernightSummary.h"
#include "services/economics/EconomicsService.h"
#include "services/markets/LastKnownQuote.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <cmath>

namespace finterm::screens::world {

namespace {

constexpr int kTitleH = 22;
constexpr int kColH = 16;
constexpr int kRowH = 21;
constexpr int kPad = 8;
constexpr int kGap = 6;
constexpr int kLastW = 88;
constexpr int kChgW = 70;
constexpr int kPctW = 66;
constexpr int kSparkW = 60;
constexpr int kSessW = 52;
constexpr int kFutW = 58;
constexpr int kSparkMinWidth = 430; ///< narrower sections drop the sparkline first

// Futures prints are ~10 min delayed on the batch quote; the stream fills
// between fetches (same cadence as the dashboard's FUT% column).
constexpr int kFutRefreshMs = 60'000;
// The par curve is published once a day after the close.
constexpr int kTreasuryRefreshMs = 30 * 60'000;

const QStringList& column_order(int col) {
    static const QVector<QStringList> cols = {
        {"AMERICAS", "EUROPE", "US FUTURES"},
        {"ASIA-PACIFIC", "US RATES", "VOLATILITY"},
        {"FX", "COMMODITIES", "CRYPTO"},
    };
    return cols[col];
}

QFont data_font(int px_offset, bool bold = false) {
    QFont f{QString(ui::fonts::DATA_FAMILY)}; // braces: with () Clang parses a function declaration
    f.setPixelSize(ui::fonts::font_px(px_offset));
    f.setBold(bold);
    return f;
}

QColor phase_color(Phase p) {
    switch (p) {
    case Phase::Open:
        return QColor(ui::colors::POSITIVE());
    case Phase::Pre:
    case Phase::Post:
        return QColor(ui::colors::WARNING());
    case Phase::Break:
        return QColor(ui::colors::INFO());
    case Phase::Closed:
        break;
    }
    return QColor(ui::colors::TEXT_TERTIARY());
}

QString holidays_note() {
    return QStringLiteral("Holidays and half-days are not checked");
}

/// What one row prints, decided once for the painter and the tooltip.
struct RowView {
    QString last, chg, pct;
    double heat = kNaN;  ///< tint value for the %CHG cell
    double sign = kNaN;  ///< sign source for the CHG text colour
    RowStatus status = RowStatus::Missing;
    QString tag;         ///< small dim tag after the name ("STALE", "EOD 13 Nov")
    bool clickable = true;
};

RowView row_view(const Instrument& in, const WorldMarketsView& v) {
    RowView r;
    const QString dash(QChar(0x2014));
    r.last = r.chg = r.pct = dash;
    if (in.kind == Kind::Treasury) {
        r.clickable = false;
        r.chg.clear();
        const TreasuryCurve& c = v.treasury();
        if (!c.valid())
            return r;
        r.status = RowStatus::Live;
        r.tag = QStringLiteral("EOD ") + c.date.toString(QStringLiteral("d MMM"));
        if (in.symbol == kUst2y) {
            r.last = QString::number(c.y2, 'f', in.decimals) + QLatin1Char('%');
            const double bp = c.y2_change_bp();
            r.pct = signed_bp(bp);
            r.heat = heat_value(in.kind, kNaN, bp);
            r.sign = bp;
        } else {
            const double s = c.s2s10_bp();
            r.last = signed_bp(s);
            const double bp = c.s2s10_change_bp();
            r.pct = signed_bp(bp);
            r.heat = heat_value(in.kind, kNaN, bp);
            r.sign = bp;
            if (!std::isfinite(s))
                r.status = RowStatus::Missing;
        }
        return r;
    }
    r.status = v.status_of(in.symbol);
    if (r.status == RowStatus::Missing)
        return r;
    const services::QuoteData& q = *v.quote(in.symbol);
    if (r.status == RowStatus::Stale)
        r.tag = QStringLiteral("STALE");
    if (in.kind == Kind::Yield) {
        const double y = yield_percent(q.price);
        r.last = std::isfinite(y) ? QString::number(y, 'f', in.decimals) + QLatin1Char('%') : dash;
        r.chg.clear();
        const double bp = change_bp(q.price, q.change);
        r.pct = signed_bp(bp);
        r.heat = heat_value(in.kind, kNaN, bp);
        r.sign = bp;
        return r;
    }
    r.last = price_text(q.price, in.decimals);
    r.chg = signed_num(q.change, in.decimals);
    r.pct = signed_pct(q.change_pct);
    r.heat = heat_value(in.kind, q.change_pct, kNaN);
    r.sign = q.change;
    return r;
}

/// FUT% is shown for a cash index with a future while its cash market is
/// outside its trading day (closed, pre or post) — the only after-hours number
/// a cash index has. Not during a lunch BREAK: the cash prints are live then.
bool fut_applies(const Instrument& in, const QDateTime& now) {
    if (in.kind != Kind::Index || !services::index_future_for(in.symbol))
        return false;
    const ExchangeHours* ex = find_exchange(in.exchange);
    if (!ex)
        return false;
    const Phase ph = session_at(*ex, now).phase;
    return ph != Phase::Open && ph != Phase::Break;
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════
// SessionsStrip
// ════════════════════════════════════════════════════════════════════════════

SessionsStrip::SessionsStrip(QWidget* parent) : QWidget(parent), ids_(strip_exchange_ids()) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

QSize SessionsStrip::sizeHint() const {
    return {900, 46};
}

QSize SessionsStrip::minimumSizeHint() const {
    return {400, 46};
}

QRect SessionsStrip::cell_rect(int i) const {
    const int n = ids_.size();
    const int x0 = width() * i / n, x1 = width() * (i + 1) / n;
    return QRect(x0, 0, x1 - x0, height());
}

void SessionsStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(ui::colors::BG_SURFACE()));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QFont city_f = data_font(-3, true), time_f = data_font(-2), small_f = data_font(-3);
    QString prev_region;
    for (int i = 0; i < ids_.size(); ++i) {
        const ExchangeHours* ex = find_exchange(ids_[i]);
        if (!ex)
            continue;
        const QRect r = cell_rect(i);
        const QString region = region_of(ids_[i]);
        // A brighter divider where the region changes.
        if (i > 0) {
            p.setPen(QColor(region != prev_region ? ui::colors::BORDER_BRIGHT() : ui::colors::BORDER_DIM()));
            p.drawLine(r.left(), 6, r.left(), r.bottom() - 6);
        }
        prev_region = region;
        const SessionInfo s = session_at(*ex, now);
        const QRect inner = r.adjusted(8, 4, -6, -4);
        const QRect line1(inner.left(), inner.top(), inner.width(), 18);
        const QRect line2(inner.left(), inner.top() + 20, inner.width(), 16);

        p.setFont(city_f);
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(line1, Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1(ex->city));
        p.setFont(time_f);
        p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
        p.drawText(line1, Qt::AlignRight | Qt::AlignVCenter, s.local_now.toString(QStringLiteral("HH:mm")));

        // State as a word in a tinted pill — colour repeats the word, never replaces it.
        const QString state = phase_label(s.phase);
        p.setFont(small_f);
        const QFontMetrics fm(small_f);
        const QColor pc = phase_color(s.phase);
        const QRect pill(line2.left(), line2.top() + 1, fm.horizontalAdvance(state) + 10, line2.height() - 2);
        QColor bg = pc;
        bg.setAlpha(45);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(pill, 3, 3);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(pc);
        p.drawText(pill, Qt::AlignCenter, state);
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
        const QRect rest(pill.right() + 6, line2.top(), line2.right() - pill.right() - 6, line2.height());
        p.drawText(rest, Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(next_change_text(s, now), Qt::ElideRight, rest.width()));
    }
    p.setPen(QColor(ui::colors::BORDER_DIM()));
    p.drawLine(0, height() - 1, width(), height() - 1);
}

bool SessionsStrip::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        for (int i = 0; i < ids_.size(); ++i) {
            if (!cell_rect(i).contains(he->pos()))
                continue;
            if (const ExchangeHours* ex = find_exchange(ids_[i])) {
                QToolTip::showText(he->globalPos(),
                                   QStringLiteral("%1 — %2\n%3\n\n%4")
                                       .arg(QString::fromLatin1(ex->city), QString::fromLatin1(ex->venue),
                                            hours_text(*ex), holidays_note()),
                                   this, cell_rect(i));
                return true;
            }
        }
        QToolTip::hideText();
        e->ignore();
        return true;
    }
    return QWidget::event(e);
}

// ════════════════════════════════════════════════════════════════════════════
// WorldSectionWidget
// ════════════════════════════════════════════════════════════════════════════

WorldSectionWidget::WorldSectionWidget(const Section& section, const WorldMarketsView* view, QWidget* parent)
    : QWidget(parent), section_(section), view_(view) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

bool WorldSectionWidget::has_term_row() const {
    return section_.title == QLatin1String("VOLATILITY");
}

bool WorldSectionWidget::contains_symbol(const QString& sym) const {
    for (const auto& r : section_.rows)
        if (r.symbol == sym)
            return true;
    return false;
}

QSize WorldSectionWidget::sizeHint() const {
    const int rows = section_.rows.size() + (has_term_row() ? 1 : 0);
    return {460, title_h() + kColH + rows * kRowH + 4};
}

QSize WorldSectionWidget::minimumSizeHint() const {
    return {show_title_ ? 300 : 200, sizeHint().height()};
}

int WorldSectionWidget::title_h() const {
    return show_title_ ? kTitleH : 0;
}

WorldSectionWidget::Cols WorldSectionWidget::columns(int y) const {
    Cols c;
    int x = width() - kPad;
    auto take = [&](int w) {
        x -= w;
        const QRect r(x, y, w, kRowH);
        x -= kGap;
        return r;
    };
    if (section_.show_fut)
        c.fut = take(kFutW);
    if (section_.show_session)
        c.sess = take(kSessW);
    if (width() >= kSparkMinWidth)
        c.spark = take(kSparkW);
    c.pct = take(kPctW);
    c.chg = take(kChgW);
    c.last = take(kLastW);
    c.name = QRect(kPad, y, std::max(0, x - kPad + kGap), kRowH);
    return c;
}

int WorldSectionWidget::row_at(int y) const {
    const int top = title_h() + kColH;
    if (y < top)
        return -1;
    const int row = (y - top) / kRowH;
    return row < section_.rows.size() ? row : -1;
}

void WorldSectionWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QColor surface(ui::colors::BG_SURFACE());
    p.fillRect(rect(), surface);

    // Title bar (not on a tile: the tile's own title names the section).
    if (show_title_) {
        const QRect title(0, 0, width(), kTitleH);
        p.fillRect(title, QColor(ui::colors::BG_RAISED()));
        p.setPen(QColor(ui::colors::AMBER()));
        p.drawLine(0, kTitleH - 1, width(), kTitleH - 1);
        p.setFont(data_font(-2, true));
        p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
        p.drawText(title.adjusted(kPad, 0, -kPad, 0), Qt::AlignLeft | Qt::AlignVCenter, section_.title);
    }

    // Column headers.
    const bool rates = section_.title == QLatin1String("US RATES");
    const Cols hc = columns(title_h());
    const QRect hrow(0, title_h(), width(), kColH);
    p.setFont(data_font(-4, true));
    p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
    auto head = [&](const QRect& r, const QString& t) {
        if (!r.isNull())
            p.drawText(QRect(r.left(), hrow.top(), r.width(), kColH), Qt::AlignRight | Qt::AlignVCenter, t);
    };
    head(hc.last, rates ? QStringLiteral("YIELD") : QStringLiteral("LAST"));
    head(hc.chg, rates ? QString() : QStringLiteral("CHG"));
    head(hc.pct, rates ? QStringLiteral("CHG bp") : QStringLiteral("%CHG"));
    if (!hc.spark.isNull())
        p.drawText(QRect(hc.spark.left(), hrow.top(), hc.spark.width(), kColH), Qt::AlignCenter,
                   QStringLiteral("5D"));
    if (!hc.sess.isNull())
        p.drawText(QRect(hc.sess.left(), hrow.top(), hc.sess.width(), kColH), Qt::AlignCenter,
                   QStringLiteral("SESSION"));
    head(hc.fut, QStringLiteral("FUT%"));

    const QDateTime now = view_->now_utc().isValid() ? view_->now_utc() : QDateTime::currentDateTimeUtc();
    const QFont name_f = data_font(-2), num_f = data_font(-2), tag_f = data_font(-4), sess_f = data_font(-4, true);
    const QColor primary(ui::colors::TEXT_PRIMARY()), secondary(ui::colors::TEXT_SECONDARY()),
        tertiary(ui::colors::TEXT_TERTIARY());

    int y = title_h() + kColH;
    for (int i = 0; i < section_.rows.size(); ++i, y += kRowH) {
        const Instrument& in = section_.rows[i];
        const RowView rv = row_view(in, *view_);
        const Cols c = columns(y);
        if (i % 2 == 1)
            p.fillRect(QRect(0, y, width(), kRowH), QColor(ui::colors::ROW_ALT()));
        const bool live = rv.status == RowStatus::Live;
        const bool missing = rv.status == RowStatus::Missing;

        // Name + tag.
        p.setFont(name_f);
        p.setPen(missing ? tertiary : primary);
        const QFontMetrics nfm(name_f);
        const QString name = nfm.elidedText(in.label, Qt::ElideRight, c.name.width());
        p.drawText(c.name, Qt::AlignLeft | Qt::AlignVCenter, name);
        if (!rv.tag.isEmpty()) {
            const int nx = c.name.left() + nfm.horizontalAdvance(name) + 6;
            const QRect tr(nx, y, c.name.right() - nx, kRowH);
            if (tr.width() > 20) {
                p.setFont(tag_f);
                p.setPen(rv.status == RowStatus::Stale ? QColor(ui::colors::WARNING()) : tertiary);
                p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
                           QFontMetrics(tag_f).elidedText(rv.tag, Qt::ElideRight, tr.width()));
            }
        }

        // Numbers.
        p.setFont(num_f);
        p.setPen(live ? QColor(ui::colors::AMBER()) : tertiary);
        p.drawText(c.last, Qt::AlignRight | Qt::AlignVCenter, rv.last);
        p.setPen(live && std::isfinite(rv.sign) ? QColor(ui::change_color(rv.sign)) : tertiary);
        p.drawText(c.chg, Qt::AlignRight | Qt::AlignVCenter, rv.chg);

        // Heat cell: tint by the move, text chosen for contrast on the tint;
        // the sign is always printed, so colour never carries the meaning alone.
        const QRect cell = c.pct.adjusted(0, 2, 0, -2);
        if (live && std::isfinite(rv.heat)) {
            const QString bg = widgets::heatmap::cell_bg(rv.heat);
            p.fillRect(cell, QColor(bg));
            p.setPen(QColor(widgets::heatmap::label_colors(bg, rv.heat).chg));
        } else {
            p.setPen(tertiary);
        }
        p.drawText(cell.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter, rv.pct);

        // Sparkline.
        if (!c.spark.isNull() && !missing) {
            if (const QVector<double>* pts = view_->sparkline(in.symbol)) {
                QVector<double> v;
                v.reserve(pts->size());
                for (double d : *pts)
                    if (std::isfinite(d) && d > 0)
                        v.append(d);
                if (v.size() >= 2) {
                    const auto [lo_it, hi_it] = std::minmax_element(v.cbegin(), v.cend());
                    const double lo = *lo_it, hi = *hi_it, span = hi - lo;
                    const QRectF sr = QRectF(c.spark).adjusted(2, 4, -2, -4);
                    QPainterPath path;
                    for (int k = 0; k < v.size(); ++k) {
                        const double fx = sr.left() + sr.width() * k / (v.size() - 1);
                        const double fy = span > 0 ? sr.bottom() - sr.height() * (v[k] - lo) / span : sr.center().y();
                        if (k == 0)
                            path.moveTo(fx, fy);
                        else
                            path.lineTo(fx, fy);
                    }
                    p.setRenderHint(QPainter::Antialiasing, true);
                    QColor lc(live ? ui::change_color(v.last() - v.first()) : QString(ui::colors::TEXT_TERTIARY()));
                    p.setPen(QPen(lc, 1.2));
                    p.setBrush(Qt::NoBrush);
                    p.drawPath(path);
                    p.setRenderHint(QPainter::Antialiasing, false);
                }
            }
        }

        // Session state.
        if (!c.sess.isNull() && !in.exchange.isEmpty()) {
            if (const ExchangeHours* ex = find_exchange(in.exchange)) {
                const Phase ph = session_at(*ex, now).phase;
                p.setFont(sess_f);
                p.setPen(phase_color(ph));
                p.drawText(c.sess, Qt::AlignCenter, phase_label(ph));
            }
        }

        // FUT%.
        if (!c.fut.isNull() && fut_applies(in, now)) {
            const services::IndexFutureQuote* f = view_->future_for(in.symbol);
            p.setFont(num_f);
            if (f && !services::is_pending(*f)) {
                p.setPen(QColor(ui::change_color(f->pct)));
                p.drawText(c.fut, Qt::AlignRight | Qt::AlignVCenter, signed_pct(f->pct));
            } else {
                p.setPen(tertiary);
                p.drawText(c.fut, Qt::AlignRight | Qt::AlignVCenter, QString(QChar(0x2014)));
            }
        }
    }

    if (has_term_row()) {
        const auto* vix = view_->quote(QStringLiteral("^VIX"));
        const auto* v3m = view_->quote(QStringLiteral("^VIX3M"));
        const TermStructure t = vix_term(vix ? vix->price : kNaN, v3m ? v3m->price : kNaN);
        const Cols c = columns(y);
        p.setPen(QColor(ui::colors::BORDER_DIM()));
        p.drawLine(kPad, y, width() - kPad, y);
        p.setFont(name_f);
        p.setPen(secondary);
        p.drawText(c.name, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("VIX ÷ VIX3M"));
        p.setPen(std::isfinite(t.ratio) ? primary : tertiary);
        p.drawText(c.last, Qt::AlignRight | Qt::AlignVCenter,
                   std::isfinite(t.ratio) ? QString::number(t.ratio, 'f', 2) : QString(QChar(0x2014)));
        p.setPen(secondary);
        p.drawText(QRect(c.chg.left(), y, c.pct.right() - c.chg.left(), kRowH), Qt::AlignRight | Qt::AlignVCenter,
                   t.label);
    }
}

QString WorldSectionWidget::row_tooltip(int row, const QPoint& pos) const {
    const Instrument& in = section_.rows[row];
    const QDateTime now = view_->now_utc().isValid() ? view_->now_utc() : QDateTime::currentDateTimeUtc();
    const Cols c = columns(title_h() + kColH + row * kRowH);
    QStringList lines;
    if (in.kind == Kind::Treasury) {
        const TreasuryCurve& t = view_->treasury();
        lines << (in.symbol == kUst2y ? QStringLiteral("US 2-year Treasury par yield")
                                      : QStringLiteral("2s10s: 10-year minus 2-year par yield"));
        if (t.valid()) {
            lines << QStringLiteral("US Treasury daily par yield curve, %1 (end of day)")
                         .arg(t.date.toString(QStringLiteral("ddd d MMM yyyy")));
            if (t.prev_date.isValid())
                lines << QStringLiteral("Change vs %1").arg(t.prev_date.toString(QStringLiteral("ddd d MMM")));
        } else {
            lines << QStringLiteral("Not loaded: %1").arg(t.error.isEmpty() ? QStringLiteral("fetching…") : t.error);
        }
        lines << QStringLiteral("No chart: not a Yahoo symbol.");
        return lines.join(QLatin1Char('\n'));
    }
    lines << QStringLiteral("%1  (%2)").arg(in.label, in.symbol);
    switch (view_->status_of(in.symbol)) {
    case RowStatus::Missing:
        lines << (view_->error_of(in.symbol).isEmpty()
                      ? QStringLiteral("No quote yet.")
                      : QStringLiteral("No quote: %1").arg(view_->error_of(in.symbol).left(160)));
        break;
    case RowStatus::Stale:
        lines << (view_->error_of(in.symbol).isEmpty()
                      ? QStringLiteral("Stale — waiting for a fresh quote")
                      : QStringLiteral("Stale — refresh failed (%1)")
                            .arg(view_->error_of(in.symbol).left(160)));
        break;
    case RowStatus::Live:
        break;
    }
    if (in.kind == Kind::Yield)
        lines << QStringLiteral("Cboe yield index; change in basis points.");
    if (!c.fut.isNull() && c.fut.contains(pos) && services::index_future_for(in.symbol)) {
        const services::IndexFutureQuote* f = view_->future_for(in.symbol);
        if (!fut_applies(in, now))
            lines << QStringLiteral("FUT% is shown while the cash market is outside its trading day.");
        else if (f && !services::is_pending(*f))
            lines << services::index_future_tooltip(*f, in.label);
        else
            lines << services::index_future_unavailable_tooltip(in.symbol, in.label);
    }
    if (const ExchangeHours* ex = find_exchange(in.exchange)) {
        const SessionInfo s = session_at(*ex, now);
        lines << QString() << QStringLiteral("%1: %2 %3").arg(QString::fromLatin1(ex->venue), phase_label(s.phase),
                                                               next_change_text(s, now));
        lines << hours_text(*ex);
        if (ex->shape == WeekShape::Cash || ex->shape == WeekShape::Globex)
            lines << holidays_note();
    }
    if (!section_.note.isEmpty())
        lines << QString() << section_.note;
    lines << QString() << QStringLiteral("Click for the chart.");
    return lines.join(QLatin1Char('\n'));
}

bool WorldSectionWidget::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const int row = row_at(he->pos().y());
        if (row >= 0) {
            QToolTip::showText(he->globalPos(), row_tooltip(row, he->pos()), this,
                               QRect(0, title_h() + kColH + row * kRowH, width(), kRowH));
        } else if (he->pos().y() < title_h() + kColH && !section_.note.isEmpty()) {
            QToolTip::showText(he->globalPos(), section_.note, this, QRect(0, 0, width(), title_h() + kColH));
        } else {
            QToolTip::hideText();
            e->ignore();
        }
        return true;
    }
    return QWidget::event(e);
}

void WorldSectionWidget::mouseMoveEvent(QMouseEvent* e) {
    const int row = row_at(e->position().toPoint().y());
    const bool click = row >= 0 && section_.rows[row].kind != Kind::Treasury;
    setCursor(click ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(e);
}

void WorldSectionWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const int row = row_at(e->position().toPoint().y());
        if (row >= 0 && section_.rows[row].kind != Kind::Treasury) {
            const Instrument& in = section_.rows[row];
            emit activated(in.symbol, in.label);
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

// ════════════════════════════════════════════════════════════════════════════
// WorldMarketsView
// ════════════════════════════════════════════════════════════════════════════

WorldMarketsView::WorldMarketsView(QWidget* parent) : QWidget(parent) {
    init();
}

WorldMarketsView::WorldMarketsView(const QStringList& parts, QWidget* parent) : QWidget(parent), parts_(parts) {
    init();
}

QStringList WorldMarketsView::my_symbols() const {
    if (shows_header())
        return world_quote_symbols();
    QStringList out;
    for (const auto& s : world_sections())
        if (shows_section(s.title))
            for (const auto& r : s.rows)
                if (!is_treasury_key(r.symbol) && !out.contains(r.symbol))
                    out << r.symbol;
    return out;
}

bool WorldMarketsView::wants_futures() const {
    if (shows_header())
        return true; // the overnight line reads the S&P future
    for (const auto& s : world_sections())
        if (shows_section(s.title) && s.show_fut)
            return true;
    return false;
}

bool WorldMarketsView::wants_treasury() const {
    for (const auto& s : world_sections())
        if (shows_section(s.title))
            for (const auto& r : s.rows)
                if (r.kind == Kind::Treasury)
                    return true;
    return false;
}

void WorldMarketsView::init() {
    now_utc_ = QDateTime::currentDateTimeUtc();
    build_ui();

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, &WorldMarketsView::on_clock);

    fut_timer_ = new QTimer(this);
    fut_timer_->setInterval(kFutRefreshMs);
    connect(fut_timer_, &QTimer::timeout, this, &WorldMarketsView::fetch_futures);

    ust_timer_ = new QTimer(this);
    ust_timer_->setInterval(kTreasuryRefreshMs);
    connect(ust_timer_, &QTimer::timeout, this, &WorldMarketsView::fetch_treasury);

    // Live prints of the pinned contracts behind FUT% between fetches.
    connect(&services::MarketDataService::instance(), &services::MarketDataService::stream_ticks, this,
            [this](const QVector<services::StreamTick>& ticks) {
                if (fut_.isEmpty() || !shown_)
                    return;
                bool moved = false;
                for (const auto& t : ticks)
                    for (auto it = fut_.begin(); it != fut_.end(); ++it)
                        if (services::apply_future_tick(it.value(), t.symbol, t.price, t.change, t.time_ms)) {
                            moved = true;
                            mark_dirty(it.key());
                        }
                if (moved)
                    render_.request();
            });

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });
    refresh_theme();
}

void WorldMarketsView::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    if (shows_header()) {
    top_bar_ = new QWidget(this);
    top_bar_->setFixedHeight(28);
    auto* tl = new QHBoxLayout(top_bar_);
    tl->setContentsMargins(kPad + 2, 0, kPad + 2, 0);
    tl->setSpacing(10);
    auto* tag = new QLabel(QStringLiteral("OVERNIGHT"), top_bar_);
    tag->setObjectName(QStringLiteral("worldTag"));
    tl->addWidget(tag);
    overnight_label_ = new QLabel(QStringLiteral("Waiting for quotes…"), top_bar_);
    overnight_label_->setObjectName(QStringLiteral("worldOvernight"));
    overnight_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    overnight_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    overnight_label_->setToolTip(
        QStringLiteral("Generated from the numbers below by fixed rules"));
    tl->addWidget(overnight_label_, 1);
    asof_label_ = new QLabel(top_bar_);
    asof_label_->setObjectName(QStringLiteral("worldAsof"));
    tl->addWidget(asof_label_);
    root->addWidget(top_bar_);

    strip_ = new SessionsStrip(this);
    root->addWidget(strip_);
    }
    if (!parts_.isEmpty() && parts_ == QStringList{kSessionsPart}) {
        root->addStretch(1);
        return; // the sessions tile: no sections
    }

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("worldBody"));
    auto* grid = new QHBoxLayout(body);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setSpacing(6);
    // The whole board in three columns; a part in one.
    for (int col = 0; col < (parts_.isEmpty() ? 3 : 1); ++col) {
        auto* vl = new QVBoxLayout;
        vl->setSpacing(6);
        QStringList titles = parts_.isEmpty() ? column_order(col) : parts_;
        for (const QString& title : titles) {
            for (const Section& s : world_sections()) {
                if (s.title != title)
                    continue;
                auto* w = new WorldSectionWidget(s, this, body);
                w->set_show_title(parts_.isEmpty()); // a tile is titled by its frame
                connect(w, &WorldSectionWidget::activated, this, &WorldMarketsView::symbol_activated);
                sections_.append(w);
                vl->addWidget(w);
            }
        }
        vl->addStretch(1);
        grid->addLayout(vl, 1);
    }
    scroll->setWidget(body);
    root->addWidget(scroll, 1);
}

void WorldMarketsView::refresh_theme() {
    setStyleSheet(QStringLiteral("background:%1;").arg(ui::colors::BG_BASE()));
    if (top_bar_)
        top_bar_->setStyleSheet(
            QStringLiteral("QWidget{background:%1;border-bottom:1px solid %2;}"
                           "QLabel{border:none;background:transparent;font-family:'%3';font-size:%4px;}"
                           "QLabel#worldTag{color:%5;font-weight:700;}"
                           "QLabel#worldOvernight{color:%6;}"
                           "QLabel#worldAsof{color:%7;}")
                .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM(), QString(ui::fonts::DATA_FAMILY))
                .arg(ui::fonts::font_px(-2))
                .arg(ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(), ui::colors::TEXT_TERTIARY()));
    if (strip_)
        strip_->update();
    for (auto* s : sections_)
        s->update();
}

const services::QuoteData* WorldMarketsView::quote(const QString& sym) const {
    const auto it = quotes_.constFind(sym);
    return it == quotes_.constEnd() ? nullptr : &it.value();
}

const QVector<double>* WorldMarketsView::sparkline(const QString& sym) const {
    const auto it = sparks_.constFind(sym);
    return it == sparks_.constEnd() ? nullptr : &it.value();
}

const services::IndexFutureQuote* WorldMarketsView::future_for(const QString& index) const {
    const auto it = fut_.constFind(index);
    return it == fut_.constEnd() ? nullptr : &it.value();
}

RowStatus WorldMarketsView::status_of(const QString& sym) const {
    const auto* q = quote(sym);
    // The hub's state, not errors_: a seeded quote's error predates this
    // view's error subscription and is never replayed to it.
    return row_status(q != nullptr, q ? q->price : kNaN, services::MarketDataService::instance().has_live_quote(sym),
                      services::quote_topic_state(sym) == datahub::SubscriptionState::Error);
}

// ── lifecycle ──────────────────────────────────────────────────────────────

bool WorldMarketsView::has_error(const QString& sym) const {
    // The hub clears a topic's error on a successful refresh even when the
    // quote is identical and is not re-published, so ask it rather than
    // trusting errors_ (which only a delivery clears).
    return errors_.contains(sym) && datahub::DataHub::instance().subscription_state(
                                        QStringLiteral("market:quote:") + sym) == datahub::SubscriptionState::Error;
}

void WorldMarketsView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    shown_ = true;
    now_utc_ = QDateTime::currentDateTimeUtc();
    if (!subscribed_)
        subscribe_all();
    clock_timer_->start();
    if (wants_futures()) {
        fut_timer_->start();
        fetch_futures();
    }
    // The curve changes once a day: re-read on show only when it is old.
    if (wants_treasury()) {
        ust_timer_->start();
        if (!ust_.valid() || !ust_fetched_at_.isValid() ||
            ust_fetched_at_.secsTo(QDateTime::currentDateTimeUtc()) > 1800)
            fetch_treasury();
    }
    sync_fut_stream();
    all_dirty_ = true;
    render_.request();
}

void WorldMarketsView::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    shown_ = false;
    // Paused while hidden: no subscriptions (the hub stops polling topics no
    // one watches), no timers, no futures stream.
    unsubscribe_all();
    clock_timer_->stop();
    fut_timer_->stop();
    ust_timer_->stop();
    sync_fut_stream();
}

void WorldMarketsView::subscribe_all() {
    auto& hub = datahub::DataHub::instance();
    for (const QString& sym : my_symbols()) {
        const QString qt = QStringLiteral("market:quote:") + sym;
        hub.subscribe(this, qt, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            quotes_.insert(sym, v.value<services::QuoteData>());
            errors_.remove(sym);
            last_quote_at_ = QDateTime::currentDateTime();
            mark_dirty(sym);
        });
        // Per-topic errors: one failing symbol marks its own row, never the section.
        hub.subscribe_errors(this, qt, [this, sym](const QString& err) {
            errors_.insert(sym, err);
            mark_dirty(sym);
        });
        hub.subscribe(this, QStringLiteral("market:sparkline:") + sym, [this, sym](const QVariant& v) {
            if (!v.canConvert<QVector<double>>())
                return;
            sparks_.insert(sym, v.value<QVector<double>>());
            mark_dirty(sym);
        });
    }
    subscribed_ = true;
    // subscribe() replays only values younger than the TTL: shown minutes
    // after startup while refreshes are refused, every row would read "—"
    // though the hub holds its last quote. status_of() marks these STALE.
    if (services::seed_last_known_quotes(my_symbols(), quotes_) > 0) {
        all_dirty_ = true;
        render_.request();
    }
}

void WorldMarketsView::unsubscribe_all() {
    if (!subscribed_)
        return;
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    for (const QString& sym : my_symbols())
        hub.unsubscribe_errors(this, QStringLiteral("market:quote:") + sym);
    subscribed_ = false;
    // Quotes stay cached so a quick hide/show doesn't flash dashes.
}

void WorldMarketsView::refresh(bool force) {
    if (!shown_)
        return;
    QStringList topics;
    for (const QString& sym : my_symbols())
        topics << QStringLiteral("market:quote:") + sym;
    datahub::DataHub::instance().request(topics, force);
    if (wants_futures())
        fetch_futures();
    if (force && wants_treasury())
        fetch_treasury();
}

// ── FUT% ───────────────────────────────────────────────────────────────────

void WorldMarketsView::fetch_futures() {
    if (fut_in_flight_)
        return;
    QStringList indices;
    for (const Section& s : world_sections())
        for (const Instrument& in : s.rows)
            if (in.kind == Kind::Index && services::index_future_for(in.symbol))
                indices << in.symbol;
    fut_in_flight_ = true;
    services::fetch_index_futures(
        indices, this, [this](bool ok, QHash<QString, services::IndexFutureQuote> fresh, QString err) {
            fut_in_flight_ = false;
            if (!ok) {
                // Keep the last good numbers; their contracts still take live prints.
                LOG_WARN("WorldMarkets", QStringLiteral("index futures fetch failed: %1").arg(err));
                sync_fut_stream();
                return;
            }
            services::merge_fetched_futures(fut_, fresh);
            fut_ = fresh;
            sync_fut_stream();
            all_dirty_ = true;
            render_.request();
        });
}

void WorldMarketsView::sync_fut_stream() {
    QStringList contracts;
    if (shown_)
        for (const auto& f : std::as_const(fut_))
            if (!f.contract.isEmpty())
                contracts << f.contract;
    services::MarketDataService::instance().set_stream_extras(this, contracts);
}

// ── Treasury curve ─────────────────────────────────────────────────────────

void WorldMarketsView::fetch_treasury() {
    if (ust_in_flight_)
        return;
    ust_in_flight_ = true;
    QPointer<WorldMarketsView> self = this;
    services::EconomicsService::instance().fetch_treasury_par_curve([self](const services::EconomicsResult& r) {
        if (!self)
            return;
        self->ust_in_flight_ = false;
        TreasuryCurve c;
        if (r.success) {
            c = parse_treasury_curve(r.data);
            if (!c.valid() && c.error.isEmpty())
                c.error = QStringLiteral("no curve in the reply");
        } else {
            c.error = r.error.isEmpty() ? QStringLiteral("script failed") : r.error;
        }
        if (c.valid()) {
            self->ust_ = c;
            self->ust_fetched_at_ = r.as_of.isValid() ? r.as_of : QDateTime::currentDateTimeUtc();
        } else if (!self->ust_.valid()) {
            self->ust_ = c; // nothing better to show: keep the reason for the tooltip
        } else {
            LOG_WARN("WorldMarkets", QStringLiteral("Treasury curve refresh failed: %1").arg(c.error));
        }
        self->mark_dirty(kUst2y);
        self->mark_dirty(kUst2s10s);
    });
}

// ── rendering ──────────────────────────────────────────────────────────────

void WorldMarketsView::mark_dirty(const QString& sym) {
    dirty_.insert(sym);
    render_.request();
}

void WorldMarketsView::on_clock() {
    now_utc_ = QDateTime::currentDateTimeUtc();
    const int minute = now_utc_.time().minute();
    if (minute == last_minute_) {
        // A hydrated quote turns fresh when its poll lands — but the hub skips
        // re-publishing a byte-identical quote, so no delivery repaints the
        // row. Re-check STALE rows every few seconds rather than once a minute.
        if (now_utc_.time().second() % 5 == 0) {
            for (const QString& sym : world_quote_symbols()) {
                if (errors_.contains(sym) && !has_error(sym)) {
                    errors_.remove(sym); // recovered without a re-publish
                    mark_dirty(sym);
                } else if (quotes_.contains(sym) && status_of(sym) != RowStatus::Live) {
                    mark_dirty(sym);
                }
            }
        }
        return;
    }
    // Clocks, countdowns and session columns move once a minute.
    last_minute_ = minute;
    if (strip_)
        strip_->update();
    all_dirty_ = true;
    render_.request();
}

void WorldMarketsView::render() {
    if (!shown_)
        return;
    for (auto* s : sections_) {
        bool hit = all_dirty_;
        if (!hit) {
            for (const QString& sym : std::as_const(dirty_))
                if (s->contains_symbol(sym)) {
                    hit = true;
                    break;
                }
        }
        if (hit)
            s->update();
    }
    dirty_.clear();
    all_dirty_ = false;
    update_overnight();
    if (asof_label_ && last_quote_at_.isValid())
        asof_label_->setText(QStringLiteral("quotes %1").arg(last_quote_at_.toString(QStringLiteral("HH:mm:ss"))));
}

void WorldMarketsView::update_overnight() {
    if (!overnight_label_)
        return; // a section tile: the sessions tile carries the line
    auto live_pct = [this](const QString& sym) {
        if (status_of(sym) != RowStatus::Live)
            return kNaN;
        return quote(sym)->change_pct;
    };
    OvernightInputs in;
    in.asia = {{QStringLiteral("Nikkei"), live_pct(QStringLiteral("^N225"))},
               {QStringLiteral("Hang Seng"), live_pct(QStringLiteral("^HSI"))},
               {QStringLiteral("Shanghai"), live_pct(QStringLiteral("000001.SS"))},
               {QStringLiteral("KOSPI"), live_pct(QStringLiteral("^KS11"))},
               {QStringLiteral("ASX 200"), live_pct(QStringLiteral("^AXJO"))},
               {QStringLiteral("Taiex"), live_pct(QStringLiteral("^TWII"))}};
    in.europe = {{QStringLiteral("Euro Stoxx 50"), live_pct(QStringLiteral("^STOXX50E"))},
                 {QStringLiteral("DAX"), live_pct(QStringLiteral("^GDAXI"))},
                 {QStringLiteral("FTSE 100"), live_pct(QStringLiteral("^FTSE"))},
                 {QStringLiteral("CAC 40"), live_pct(QStringLiteral("^FCHI"))},
                 {QStringLiteral("IBEX 35"), live_pct(QStringLiteral("^IBEX"))},
                 {QStringLiteral("FTSE MIB"), live_pct(QStringLiteral("FTSEMIB.MI"))},
                 {QStringLiteral("SMI"), live_pct(QStringLiteral("^SSMI"))},
                 {QStringLiteral("AEX"), live_pct(QStringLiteral("^AEX"))}};
    // US futures: the pinned S&P 500 contract when it has a sound number,
    // else the continuous ES quote.
    const services::IndexFutureQuote* es = future_for(QStringLiteral("^GSPC"));
    in.us_futures_pct = es && !services::is_pending(*es) ? es->pct : live_pct(QStringLiteral("ES=F"));
    if (status_of(QStringLiteral("^TNX")) == RowStatus::Live) {
        const auto* q = quote(QStringLiteral("^TNX"));
        in.us10y_pct = yield_percent(q->price);
        in.us10y_bp = change_bp(q->price, q->change);
    }
    in.dollar_pct = live_pct(QStringLiteral("DX-Y.NYB"));
    const QString line = overnight_line(in);
    const QString text = line.isEmpty() ? QStringLiteral("Waiting for quotes…") : line;
    if (overnight_label_->text() != text)
        overnight_label_->setText(text);
}

} // namespace finterm::screens::world

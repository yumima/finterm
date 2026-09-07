#include "screens/ownership/ShortInterestPanel.h"

#include "screens/ownership/OwnershipFlags.h"
#include "screens/ownership/OwnershipUi.h"
#include "ui/components/TooltipText.h"

#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QPainter>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

using namespace fincept::ownership;
using namespace fincept::screens::ownership_ui;

/// Bars, oldest left, one per settlement date. Height is shares short as a
/// share of float when the float is known, else shares short; the axis
/// says which. The newest bar carries its value; the others are a hover away.
class ShortInterestPanel::Chart : public QWidget {
  public:
    explicit Chart(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);
        setMinimumHeight(84);
        setMaximumHeight(140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void set(const ShortHistory& h, std::optional<double> float_shares, const QString& empty) {
        rows_.clear();
        empty_ = empty;
        float_ = float_shares;
        // Oldest first for drawing.
        for (int i = h.rows.size() - 1; i >= 0; --i)
            rows_.push_back(h.rows[i]);
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        const int pad = 8, label_h = 16, axis_w = 54;
        if (rows_.isEmpty()) {
            p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
            p.drawText(rect().adjusted(pad, pad, -pad, -pad),
                       Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, empty_);
            return;
        }
        const QRect plot = rect().adjusted(pad + axis_w, pad + 14, -pad, -(pad + label_h));
        double hi = 0.0;
        for (const auto& r : rows_)
            hi = std::max(hi, value_of(r).value_or(0.0));
        if (hi <= 0.0)
            return;
        // A round ceiling so the axis label is a number a reader would say.
        const double top = nice_ceiling(hi);

        p.setPen(QColor(ui::colors::BORDER_DIM()));
        p.drawLine(plot.left(), plot.bottom(), plot.right(), plot.bottom());
        const int mid_y = plot.bottom() - static_cast<int>(plot.height() * 0.5);
        p.drawLine(plot.left(), mid_y, plot.right(), mid_y);
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(QRect(pad, plot.top() - 8, axis_w - 6, 14), Qt::AlignRight | Qt::AlignVCenter, label(top));
        p.drawText(QRect(pad, mid_y - 7, axis_w - 6, 14), Qt::AlignRight | Qt::AlignVCenter, label(top / 2));
        p.drawText(QRect(pad, plot.bottom() - 7, axis_w - 6, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0"));

        const int n = rows_.size();
        const int slot = std::max(6, plot.width() / n);
        const int bar_w = std::max(4, slot * 2 / 3);
        bars_.clear();
        for (int i = 0; i < n; ++i) {
            const auto v = value_of(rows_[i]);
            if (!v)
                continue;
            const int h = static_cast<int>(plot.height() * (*v / top));
            const int x = plot.left() + i * slot + (slot - bar_w) / 2;
            const QRect bar(x, plot.bottom() - h, bar_w, h);
            bars_.push_back({bar, i});
            QColor c(ui::colors::CYAN());
            c.setAlpha(i == n - 1 ? 230 : 130);
            p.fillRect(bar, c);
        }
        // Name the newest bar: the number the reader came for.
        if (!bars_.isEmpty()) {
            const auto& last = bars_.last();
            p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
            const QString txt = label(value_of(rows_[last.second]).value_or(0.0));
            const int w = p.fontMetrics().horizontalAdvance(txt) + 6;
            p.drawText(QRect(std::min(last.first.right() - w, plot.right() - w), plot.top() - 10, w, 14),
                       Qt::AlignRight | Qt::AlignVCenter, txt);
        }
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        const QRect labels(plot.left(), plot.bottom() + 2, plot.width(), label_h);
        p.drawText(labels, Qt::AlignLeft | Qt::AlignVCenter,
                   rows_.first().settlement.toString(QStringLiteral("d MMM yyyy")));
        p.drawText(labels, Qt::AlignRight | Qt::AlignVCenter,
                   rows_.last().settlement.toString(QStringLiteral("d MMM yyyy")));
        p.drawText(labels, Qt::AlignHCenter | Qt::AlignVCenter,
                   float_ ? QStringLiteral("shares short, % of float · one bar per settlement")
                          : QStringLiteral("shares short · one bar per settlement"));
    }

    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            auto* he = static_cast<QHelpEvent*>(e);
            for (const auto& b : bars_) {
                if (b.first.adjusted(-2, -40, 2, 0).contains(he->pos())) {
                    const auto& r = rows_[b.second];
                    QToolTip::showText(
                        he->globalPos(),
                        ui::tooltip_wrap(QStringLiteral("Settlement %1\nShares short %2%3\nDays to cover %4%5")
                                             .arg(r.settlement.toString(QStringLiteral("d MMM yyyy")),
                                                  compact_or_dash(r.shares_short),
                                                  float_ && r.shares_short
                                                      ? QStringLiteral(" (%1 of float)")
                                                            .arg(pct_or_dash(*r.shares_short / *float_, 2))
                                                      : QString(),
                                                  r.days_to_cover ? QString::number(*r.days_to_cover, 'f', 1)
                                                                  : fmt::placeholder(),
                                                  r.change_pct ? QStringLiteral("\n%1 vs prior settlement")
                                                                     .arg(pct_or_dash(r.change_pct, 1, true))
                                                               : QString())),
                        this);
                    return true;
                }
            }
            QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

  private:
    std::optional<double> value_of(const ShortReading& r) const {
        if (!r.shares_short)
            return std::nullopt;
        if (float_ && *float_ > 0)
            return *r.shares_short / *float_;
        return r.shares_short;
    }
    QString label(double v) const {
        return float_ ? fmt::format_percent(v * 100.0, v * 100.0 < 1.0 ? 2 : 1) : fmt::format_compact(v);
    }
    static double nice_ceiling(double v) {
        if (v <= 0)
            return 1.0;
        const double mag = std::pow(10.0, std::floor(std::log10(v)));
        const double m = v / mag;
        const double step = m <= 1.0 ? 1.0 : m <= 2.0 ? 2.0 : m <= 2.5 ? 2.5 : m <= 5.0 ? 5.0 : 10.0;
        return step * mag;
    }

    QVector<ShortReading> rows_;
    std::optional<double> float_;
    QString empty_;
    QVector<QPair<QRect, int>> bars_;
};

ShortInterestPanel::ShortInterestPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto title = section_title(QStringLiteral("SHORT INTEREST"));
    title_note_ = title.right;
    root->addWidget(title.widget);

    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    chart_ = new Chart;
    body->addWidget(chart_, 3);
    figures_ = new QLabel;
    figures_->setWordWrap(true);
    figures_->setTextFormat(Qt::RichText);
    figures_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    figures_->setStyleSheet(QString("color:%1;font-size:13px;").arg(ui::colors::TEXT_PRIMARY()));
    figures_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    figures_->setMinimumWidth(280);
    body->addWidget(figures_, 2);
    root->addLayout(body);

    foot_ = note_label();
    root->addWidget(foot_);
    clear();
}

void ShortInterestPanel::clear() {
    chart_->set({}, std::nullopt, {});
    title_note_->clear();
    figures_->clear();
    foot_->clear();
}

void ShortInterestPanel::set_snapshot(const OwnershipSnapshot& s, bool loading) {
    const HeaderStats h = derive_header(s);
    const auto& hist = s.short_history;
    const auto dim = [](const QString& t) {
        return QStringLiteral("<span style='color:%1;'>%2</span>").arg(ui::colors::TEXT_SECONDARY(), t);
    };

    if (!hist.has_data()) {
        chart_->set({}, std::nullopt,
                    loading && hist.error.isEmpty() ? QStringLiteral("Reading FINRA short interest…")
                                                    : hist.error.isEmpty() ? QStringLiteral("No FINRA short-interest rows.")
                                                                           : QStringLiteral("FINRA: ") + hist.error);
        title_note_->clear();
        // The vendor's single reading as a fallback, labelled as such.
        if (s.vendor.short_pct_float || s.vendor.shares_short) {
            figures_->setText(QStringLiteral("Vendor figure: <b>%1</b> of float short · %2 shares · days to cover %3<br>%4")
                                  .arg(pct_or_dash(s.vendor.short_pct_float, 1),
                                       compact_or_dash(s.vendor.shares_short),
                                       s.vendor.short_ratio ? QString::number(*s.vendor.short_ratio, 'f', 1)
                                                            : fmt::placeholder(),
                                       dim(s.vendor.short_as_of.isValid()
                                               ? QStringLiteral("as of %1").arg(s.vendor.short_as_of.toString(QStringLiteral("d MMM yyyy")))
                                               : QString())));
        } else {
            figures_->clear();
        }
        foot_->clear();
        return;
    }

    chart_->set(hist, s.vendor.float_shares, {});
    const auto* r = hist.latest();
    title_note_->setText(QStringLiteral("settlement %1 · published ~%2 · %3 readings · %4")
                             .arg(r->settlement.toString(QStringLiteral("d MMM yyyy")),
                                  r->published_after.toString(QStringLiteral("d MMM")))
                             .arg(hist.rows.size())
                             .arg(hist.source == QLatin1String("finra") ? QStringLiteral("FINRA")
                                                                        : QStringLiteral("local cache — FINRA unreachable")));

    QStringList lines;
    lines << QStringLiteral("Shares short <b>%1</b> %2")
                 .arg(compact_or_dash(r->shares_short),
                      r->prior ? dim(QStringLiteral("(prior %1, %2)")
                                         .arg(compact_or_dash(r->prior))
                                         .arg(r->change_pct
                                                  ? QStringLiteral("<span style='color:%1;'>%2</span>")
                                                        .arg(*r->change_pct > 0 ? ui::colors::RED() : ui::colors::GREEN(),
                                                             pct_or_dash(r->change_pct, 1, true))
                                                  : fmt::placeholder()))
                               : QString());
    if (h.si_pct_float)
        lines << QStringLiteral("<b>%1</b> of float %2").arg(pct_or_dash(h.si_pct_float, 2),
                                                             dim(QStringLiteral("(vendor float)")));
    lines << QStringLiteral("Days to cover <b>%1</b> %2")
                 .arg(r->days_to_cover ? QString::number(*r->days_to_cover, 'f', 1) : fmt::placeholder(),
                      dim(r->avg_daily_volume ? QStringLiteral("on %1 avg daily volume")
                                                    .arg(fmt::format_compact(*r->avg_daily_volume))
                                              : QString()));
    if (h.sirio) {
        QString sirio = QStringLiteral("Short interest ÷ 13F shares <b>%1</b>").arg(QString::number(*h.sirio, 'f', 3));
        if (s.sirio_percentile)
            sirio += QStringLiteral(" %1").arg(dim(QStringLiteral("— percentile %1 of %2 stocks")
                                                       .arg(qRound(*s.sirio_percentile * 100))
                                                       .arg(s.sirio_universe)));
        lines << sirio;
    }
    const auto& sv = s.short_volume;
    if (sv.has_data()) {
        lines << QStringLiteral("Daily short volume <b>%1</b> of volume %2")
                     .arg(fmt::format_percent(sv.latest * 100.0, 0),
                          dim(QStringLiteral("(20-day avg %1, %2)")
                                  .arg(fmt::format_percent(sv.avg_20 * 100.0, 0),
                                       sv.as_of.toString(QStringLiteral("d MMM")))));
    }
    figures_->setText(lines.join(QStringLiteral("<br>")));
    figures_->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "Days to cover (Hong et al. 2015) and short interest ÷ institutional shares (Drechsler & "
        "Drechsler, a proxy for the borrow fee) are the two short-side measures that replicate. "
        "Plain short interest as a share of float is the weakest of the three. Daily short volume "
        "is traded flow, not a position — much of it is market-maker inventory flat by the close.")));
    foot_->setText(QStringLiteral("Firms report two business days after settlement; FINRA publishes about eight "
                                  "business days after that, so a reading is two to four weeks old on screen."));
}

} // namespace fincept::screens

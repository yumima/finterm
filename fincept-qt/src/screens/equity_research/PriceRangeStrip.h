#pragma once
#include <QVector>
#include <QWidget>

#include "services/equity/EquityResearchModels.h"

namespace fincept::screens {

/// Where today's price sits in its own recent history — and what that is, and
/// is not, evidence for.
///
/// WHY THIS IS NOT A "BUY BELOW THE AVERAGE" INDICATOR
/// ---------------------------------------------------
/// The intuition that a stock trading under its 6-month or 1-year average is
/// therefore well priced is the one this widget deliberately refuses to
/// endorse, because the research runs the other way at exactly those horizons:
///
///   * George & Hwang (Journal of Finance, 2004) found that NEARNESS TO THE
///     52-WEEK HIGH predicts higher subsequent returns, and does so better
///     than past returns themselves. Being near the high is the bullish state,
///     not the expensive one.
///   * De Bondt & Thaler (1985) did find reversal — losers beating winners —
///     but over THREE TO FIVE YEARS, not three to twelve months.
///   * Short-horizon mean reversion is real but lives inside roughly two weeks,
///     far shorter than any average offered here.
///
/// So a 6M or 1Y average sits in the gap: too long for the short reversion
/// effect, far too short for the long one, and pointing the wrong way against
/// the 52-week-high evidence. A widget that drew a line and implied "below
/// this is cheap" would be inventing a signal the data does not support.
///
/// WHAT IT DOES SHOW
/// -----------------
/// Position and context, with the numbers a reader can act on themselves:
/// the 52-week range with today's price placed in it as a percentile; the
/// 50- and 200-day averages, which matter because they are the two levels the
/// most capital watches, marked as reference lines rather than verdicts; and
/// the distance to each expressed in standard deviations of THIS security's
/// own daily moves. That last point is the one raw percentages get wrong: 8%
/// below the 200-day is an ordinary week for a biotech and a serious
/// dislocation for a utility, and only volatility-normalising can tell them
/// apart.
class PriceRangeStrip : public QWidget {
    Q_OBJECT
  public:
    explicit PriceRangeStrip(QWidget* parent = nullptr);

    void set_candles(const QVector<services::equity::Candle>& candles,
                     const QString& currency_sym);
    void clear();

    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;

  private:
    /// Everything the strip draws, computed once per candle set.
    struct Stats {
        bool   valid = false;
        double last = 0.0;
        double hi52 = 0.0, lo52 = 0.0;
        double pct_of_range = 0.0;   ///< 0 at the 52-week low, 1 at the high
        double sma50 = 0.0, sma200 = 0.0;
        bool   has50 = false, has200 = false;
        /// Distance from each average in daily-return standard deviations.
        double z50 = 0.0, z200 = 0.0;
        double daily_vol = 0.0;      ///< stdev of daily log returns
        int    days = 0;
    };
    Stats stats_;
    QString ccy_;
};

} // namespace fincept::screens

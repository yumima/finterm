#pragma once
// Risk surfaces computed from real daily bars.
//
// These five used to be drawn by SurfaceDemoData's generators — smooth shapes
// with rand() noise, under a badge naming a Databento dataset. The bars needed
// to compute them for real were already being fetched and dropped into the
// inspector table, so the data was there the whole time; nothing joined it to
// the chart.
//
// Everything here is a pure function over a symbol -> closing-price series so
// it can be tested without Qt, a network, or a screen.

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace fincept::surface {

/// Daily closes for one symbol, oldest first.
using PriceSeries = std::vector<double>;
/// symbol -> closes, oldest first. std::map so the asset order is stable and
/// the matrix a reader sees is the same one on every refresh.
using PriceTable = std::map<std::string, PriceSeries>;

/// Simple daily returns. n closes give n-1 returns.
inline std::vector<double> daily_returns(const PriceSeries& closes) {
    std::vector<double> r;
    if (closes.size() < 2)
        return r;
    r.reserve(closes.size() - 1);
    for (size_t i = 1; i < closes.size(); ++i) {
        const double prev = closes[i - 1];
        // A zero or negative close is not a price; skipping keeps one bad bar
        // from poisoning every statistic computed downstream of it.
        if (prev > 0.0 && closes[i] > 0.0)
            r.push_back(closes[i] / prev - 1.0);
    }
    return r;
}

inline double mean(const std::vector<double>& v) {
    if (v.empty())
        return 0.0;
    double s = 0.0;
    for (double x : v) s += x;
    return s / double(v.size());
}

/// Sample standard deviation (n-1). Returns 0 for fewer than two points.
inline double stdev(const std::vector<double>& v) {
    if (v.size() < 2)
        return 0.0;
    const double m = mean(v);
    double ss = 0.0;
    for (double x : v) ss += (x - m) * (x - m);
    return std::sqrt(ss / double(v.size() - 1));
}

/// Pearson correlation over the overlapping prefix of two return series.
/// Returns NaN when either series is flat — a correlation with a constant is
/// undefined, and reporting 0 would read as "uncorrelated", which is a claim.
inline double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    if (n < 2)
        return std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> x(a.end() - long(n), a.end());
    const std::vector<double> y(b.end() - long(n), b.end());
    const double mx = mean(x), my = mean(y);
    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double dx = x[i] - mx, dy = y[i] - my;
        sxy += dx * dy;
        sxx += dx * dx;
        syy += dy * dy;
    }
    if (sxx <= 0.0 || syy <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    return sxy / std::sqrt(sxx * syy);
}

/// OLS slope of `asset` on `benchmark` — beta. NaN when the benchmark does not
/// move, for the same reason correlation() returns NaN on a flat series.
inline double beta(const std::vector<double>& asset, const std::vector<double>& benchmark) {
    const size_t n = std::min(asset.size(), benchmark.size());
    if (n < 2)
        return std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> y(asset.end() - long(n), asset.end());
    const std::vector<double> x(benchmark.end() - long(n), benchmark.end());
    const double mx = mean(x), my = mean(y);
    double sxy = 0.0, sxx = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sxy += (x[i] - mx) * (y[i] - my);
        sxx += (x[i] - mx) * (x[i] - mx);
    }
    if (sxx <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    return sxy / sxx;
}

/// Worst peak-to-trough decline over the last `window` closes, as a negative
/// percentage. NaN when there is not enough history to fill the window — a
/// drawdown measured over 40 days must not be labelled 90-day.
inline double max_drawdown_pct(const PriceSeries& closes, int window) {
    if (window < 2 || int(closes.size()) < window)
        return std::numeric_limits<double>::quiet_NaN();
    const auto begin = closes.end() - window;
    double peak = *begin, worst = 0.0;
    for (auto it = begin; it != closes.end(); ++it) {
        if (*it > peak)
            peak = *it;
        if (peak > 0.0)
            worst = std::min(worst, *it / peak - 1.0);
    }
    return worst * 100.0;
}

/// Historical VaR: the loss at the given confidence, scaled to `horizon_days`
/// by root-time. Returned POSITIVE as a percentage loss.
///
/// Historical, not parametric: the empirical quantile makes no claim about the
/// shape of the distribution, and daily equity returns are not normal. NaN when
/// the sample is too small for the quantile to mean anything — with 20 days,
/// a 99% VaR is the worst observation and not an estimate of anything.
inline double historical_var_pct(const std::vector<double>& returns, double confidence,
                                 int horizon_days) {
    const size_t need = size_t(std::ceil(1.0 / std::max(1e-9, 1.0 - confidence))) * 2;
    if (returns.size() < std::max<size_t>(30, need))
        return std::numeric_limits<double>::quiet_NaN();
    std::vector<double> sorted = returns;
    std::sort(sorted.begin(), sorted.end());
    // Lower tail: index of the (1 - confidence) quantile.
    const double pos = (1.0 - confidence) * double(sorted.size() - 1);
    const size_t lo = size_t(std::floor(pos));
    const size_t hi = std::min(sorted.size() - 1, lo + 1);
    const double frac = pos - double(lo);
    const double q = sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    return -q * std::sqrt(double(horizon_days)) * 100.0;
}

/// Correlation matrix of daily returns, assets in the table's own order.
struct CorrelationResult {
    std::vector<std::string> assets;
    std::vector<std::vector<float>> z;
    int observations = 0;   ///< overlapping return count actually used
};
CorrelationResult compute_correlation(const PriceTable& prices);

/// Principal components of the correlation matrix, by Jacobi eigenvalue
/// decomposition. z[factor][asset] is the loading; variance_explained is each
/// component's share of total variance.
struct PcaResult {
    std::vector<std::string> factors;
    std::vector<std::string> assets;
    std::vector<std::vector<float>> z;
    std::vector<float> variance_explained;
};
PcaResult compute_pca(const PriceTable& prices, int max_factors = 5);

} // namespace fincept::surface

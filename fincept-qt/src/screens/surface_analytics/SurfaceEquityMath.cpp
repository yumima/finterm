#include "screens/surface_analytics/SurfaceEquityMath.h"

#include <limits>

namespace fincept::surface {

namespace {

/// Returns for every symbol, in the table's order, plus the shortest length —
/// the matrix has to be built on a common window or the entries are not
/// comparable with one another.
std::vector<std::vector<double>> aligned_returns(const PriceTable& prices, size_t& common) {
    std::vector<std::vector<double>> out;
    common = std::numeric_limits<size_t>::max();
    out.reserve(prices.size());
    for (const auto& [sym, closes] : prices) {
        out.push_back(daily_returns(closes));
        common = std::min(common, out.back().size());
    }
    if (out.empty() || common == std::numeric_limits<size_t>::max())
        common = 0;
    // Trim every series to the common tail: the most RECENT `common` returns,
    // so a symbol with a longer history does not contribute older days the
    // others cannot match.
    for (auto& r : out)
        if (r.size() > common)
            r.erase(r.begin(), r.end() - long(common));
    return out;
}

} // namespace

CorrelationResult compute_correlation(const PriceTable& prices) {
    CorrelationResult res;
    size_t common = 0;
    const auto rets = aligned_returns(prices, common);
    for (const auto& [sym, _] : prices)
        res.assets.push_back(sym);
    res.observations = int(common);
    // Two points give a correlation of exactly +/-1 by construction. Refusing
    // is the honest answer; the panel shows the empty state.
    if (common < 20 || rets.size() < 2)
        return {};

    const size_t n = rets.size();
    res.z.assign(n, std::vector<float>(n, std::numeric_limits<float>::quiet_NaN()));
    for (size_t i = 0; i < n; ++i) {
        res.z[i][i] = 1.0f;
        for (size_t j = i + 1; j < n; ++j) {
            const float c = float(correlation(rets[i], rets[j]));
            res.z[i][j] = c;
            res.z[j][i] = c;   // symmetric by definition, computed once
        }
    }
    return res;
}

PcaResult compute_pca(const PriceTable& prices, int max_factors) {
    PcaResult res;
    const CorrelationResult corr = compute_correlation(prices);
    if (corr.z.empty())
        return res;
    const size_t n = corr.z.size();

    // Jacobi rotation on the correlation matrix. Correlation rather than
    // covariance so a high-priced name does not dominate the first component
    // purely by scale.
    std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j) {
            const float v = corr.z[i][j];
            if (std::isnan(v))
                return res;   // an undefined pair makes the whole decomposition meaningless
            a[i][j] = v;
        }
    std::vector<std::vector<double>> v(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) v[i][i] = 1.0;

    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0;
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j) off += a[i][j] * a[i][j];
        if (off < 1e-12)
            break;
        for (size_t p = 0; p < n; ++p) {
            for (size_t q = p + 1; q < n; ++q) {
                if (std::fabs(a[p][q]) < 1e-15)
                    continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) /
                                 (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (size_t k = 0; k < n; ++k) {
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (size_t k = 0; k < n; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (size_t k = 0; k < n; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }

    // Order components by eigenvalue, largest first.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t l, size_t r) { return a[l][l] > a[r][r]; });

    double total = 0.0;
    for (size_t i = 0; i < n; ++i) total += std::max(0.0, a[i][i]);
    if (total <= 0.0)
        return res;

    const size_t keep = std::min<size_t>(n, size_t(std::max(1, max_factors)));
    res.assets = corr.assets;
    for (size_t f = 0; f < keep; ++f) {
        const size_t idx = order[f];
        res.factors.push_back("PC" + std::to_string(f + 1));
        res.variance_explained.push_back(float(std::max(0.0, a[idx][idx]) / total * 100.0));
        std::vector<float> row;
        row.reserve(n);
        for (size_t asset = 0; asset < n; ++asset)
            row.push_back(float(v[asset][idx]));
        res.z.push_back(std::move(row));
    }
    return res;
}

} // namespace fincept::surface

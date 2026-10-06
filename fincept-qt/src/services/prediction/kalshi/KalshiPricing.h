#pragma once

#include <cmath>
#include <limits>

namespace fincept::services::prediction::kalshi_ns {

/// Implied YES probability (0..1) for a Kalshi binary market.
///
/// The best bid alone is not a probability — on a thin book it can sit far
/// below where the market actually trades. Order of preference:
///   1. mid of a two-sided book (bid > 0 and 0 < ask < 1; Kalshi reports an
///      empty ask side as $1.00),
///   2. last traded price,
///   3. NaN — no real price exists; renderers show the placeholder.
inline double kalshi_implied_yes_probability(double yes_bid, double yes_ask, double last_price) {
    const bool has_bid = std::isfinite(yes_bid) && yes_bid > 0.0;
    const bool has_ask = std::isfinite(yes_ask) && yes_ask > 0.0 && yes_ask < 1.0;
    if (has_bid && has_ask && yes_ask >= yes_bid)
        return (yes_bid + yes_ask) / 2.0;
    if (std::isfinite(last_price) && last_price > 0.0)
        return last_price;
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace fincept::services::prediction::kalshi_ns

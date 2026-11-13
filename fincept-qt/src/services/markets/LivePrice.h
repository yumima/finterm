#pragma once
// LivePrice.h — the one rule for laying a live (streamed) price on a quote.
//
// Store one number — the price — and derive the rest from the quote's own
// previous close: change / change_pct re-computed, the day range stretched to
// take the price in, the session volume replaced by the tick's. Shared by
// every quote shape that follows the stream (markets::QuoteData,
// equity::QuoteData) so the rule cannot drift between screens.

#include <cmath>

namespace fincept::services {

/// `prev_close` NaN or ≤ 0 → change left as it was. A missing (NaN or ≤ 0)
/// high/low is left missing — a tick is one print, not a day range.
template <class Quote>
void apply_live_price(Quote& q, double prev_close, double price, double volume) {
    q.price = price;
    if (std::isfinite(prev_close) && prev_close > 0) {
        q.change = price - prev_close;
        q.change_pct = q.change / prev_close * 100.0;
    }
    if (std::isfinite(q.high) && q.high > 0 && price > q.high)
        q.high = price;
    if (std::isfinite(q.low) && q.low > 0 && price < q.low)
        q.low = price;
    if (std::isfinite(volume) && volume > 0)
        q.volume = volume;
}

/// Whether a streamed print is measured against `close`. A push-stream tick
/// carries Yahoo's change against Yahoo's own previous close, so price −
/// change IS the close it was paired with; it may move a number measured
/// against `close` only when that is the same close. This is what keeps a
/// live print from crossing a session boundary the surface hasn't: the 16:00
/// bell (post-market prints pair with TODAY's close while an AFT% quote still
/// holds yesterday's), a CME day roll at 18:00 ET, an ex-dividend reference.
/// The tolerance covers the stream's float32 encoding (~1e-7) — far below
/// one cent on a stock or one tick on an index future.
inline bool tick_pairs_with(double close, double price, double change) {
    if (!std::isfinite(close) || close <= 0 || !std::isfinite(price) || price <= 0 || !std::isfinite(change))
        return false;
    return std::abs((price - change) - close) <= 1e-5 * close;
}

} // namespace fincept::services

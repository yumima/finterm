// src/screens/crypto_center/SwapMath.h
#pragma once
#include "services/wallet/PumpFunSwapService.h"

#include <QString>

#include <algorithm>

/// The two slippage numbers, and the rule that turns one into the other.
///
/// A swap has a CONFIGURED tolerance and an EFFECTIVE one, and they are not the
/// same value:
///
///   configured  what the user picked, in basis points. The settings slider
///               has offered 10…500 bps in 5 bps steps and rendered it to two
///               decimals ("0.25%").
///   effective   what actually goes into the transaction. PumpPortal's trade
///               API takes an INTEGER PERCENT (PumpFunSwapService clamps to
///               1…5), so the configured value is rounded UP to a whole
///               percent — up, so the swap is never tolerance-starved into a
///               failure the user did not ask for.
///
/// Those differ by up to 10x (10 bps configured -> 1% effective), and the UI
/// used them interchangeably: the swap panel and the CONFIRM DIALOG both
/// displayed the configured value while the transaction carried the effective
/// one. The dialog's own warning — "PumpSwap will reject the trade if execution
/// drifts more than the slippage tolerance above" — pointed at a number that
/// was not the tolerance in the transaction, on the last screen before an
/// irreversible on-chain swap.
///
/// So: everything user-facing quotes the EFFECTIVE tolerance, and the settings
/// only offer whole percent, which removes the gap at its source rather than
/// papering over it. This header is the single definition of the mapping.
namespace fincept::screens::swapmath {

/// The service's own range, taken from the service — not restated here. A
/// duplicated literal is a mirror that stops reflecting the moment one side
/// moves, and the UI silently capping users below what the venue accepts is
/// the same class of lie this header exists to remove.
inline constexpr int kMinPct = fincept::wallet::PumpFunSwapService::kSlippagePctMin;
inline constexpr int kMaxPct = fincept::wallet::PumpFunSwapService::kSlippagePctMax;

/// The tolerance that will actually be sent, in whole percent.
inline int effective_pct(int configured_bps) {
    const int pct = (configured_bps + 99) / 100;   // round UP, never under-tolerate
    return std::clamp(pct, kMinPct, kMaxPct);
}

/// The same figure in basis points, for display beside other bps values.
inline int effective_bps(int configured_bps) {
    return effective_pct(configured_bps) * 100;
}

/// Worst-case multiplier on a quoted output: 1% tolerance -> 0.99.
inline double factor(int configured_bps) {
    return 1.0 - effective_pct(configured_bps) / 100.0;
}

/// "1.00%" — always whole percent, because that is what a swap can express.
inline QString format_pct(int configured_bps) {
    return QString::number(effective_pct(configured_bps)) + QStringLiteral(".00%");
}

} // namespace fincept::screens::swapmath

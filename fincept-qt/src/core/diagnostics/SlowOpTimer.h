#pragma once
#include "core/logging/Logger.h"

#include <QElapsedTimer>
#include <QString>

namespace fincept::diagnostics {

// RAII helper that logs a WARN if the scope outlives a threshold.
// Use via the FT_TIME_SLOT macro so callers don't need to name the local.
//
// Cost when nothing exceeds the threshold: one QElapsedTimer start/elapsed
// pair (~50ns) and one branch — safe to leave in release builds. The point
// is to make latency regressions visible without bespoke profiling.
class SlowOpTimer {
  public:
    SlowOpTimer(const char* name, qint64 threshold_ms)
        : name_(name), threshold_ms_(threshold_ms) {
        timer_.start();
    }
    ~SlowOpTimer() {
        const qint64 elapsed = timer_.elapsed();
        if (elapsed >= threshold_ms_) {
            LOG_WARN_F("Perf", "Slow op '%1' took %2 ms (threshold %3 ms)",
                       QString::fromUtf8(name_), elapsed, threshold_ms_);
        }
    }

    SlowOpTimer(const SlowOpTimer&) = delete;
    SlowOpTimer& operator=(const SlowOpTimer&) = delete;

  private:
    const char* name_;
    qint64 threshold_ms_;
    QElapsedTimer timer_;
};

} // namespace fincept::diagnostics

// Token-pasting helper so multiple FT_TIME_SLOT uses in the same function
// generate unique local-variable names.
#define FT_TIME_SLOT_CAT_(a, b) a##b
#define FT_TIME_SLOT_CAT(a, b) FT_TIME_SLOT_CAT_(a, b)

// Time the enclosing scope; logs WARN to the "Perf" tag if the scope
// exceeds threshold_ms. Example:
//   FT_TIME_SLOT("AuthService.login_local", 50);
#define FT_TIME_SLOT(name, threshold_ms) \
    ::fincept::diagnostics::SlowOpTimer FT_TIME_SLOT_CAT(ft_slow_op_, __LINE__)(name, threshold_ms)

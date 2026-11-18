#pragma once
#include "core/result/Result.h"

#include <QJsonObject>
#include <QString>

namespace finterm {

/// Each screen's own UI state (ScreenStateManager), in cache.db's screen_state.
/// (Window, dock and tab layout — titles included — live in QSettings.)
class ScreenStateStore {
  public:
    static ScreenStateStore& instance();

    /// Persist screen UI state keyed by screen_key.
    /// state_version is stored alongside — if the loaded version doesn't match
    /// expected_version, an empty QJsonObject is returned so the screen starts fresh.
    Result<void> save_screen_state(const QString& key, const QJsonObject& state, int state_version,
                                   const QString& session_id = {});
    Result<QJsonObject> load_screen_state(const QString& key, int expected_version);

  private:
    ScreenStateStore() = default;
};

} // namespace finterm

#pragma once
#include "screens/markets/MarketPanelConfig.h"

#include <QVector>

namespace finterm::screens {

/// The market panels users configured on the old Markets PANELS page — read
/// once, to carry them onto a "MY PANELS" screen (MarketTemplates "panels").
class MarketPanelStore {
  public:
    static MarketPanelStore& instance();

    /// The user's panels (or the old built-in defaults when none were saved).
    QVector<MarketPanelConfig> load();
    /// Did the user ever save panels of their own?
    bool has_saved() const;

  private:
    MarketPanelStore() = default;
    static QVector<MarketPanelConfig> build_defaults();

    static constexpr const char* kSettingsKey = "markets_panel_configs";
    static constexpr const char* kCategory = "markets_panels";
};

} // namespace finterm::screens

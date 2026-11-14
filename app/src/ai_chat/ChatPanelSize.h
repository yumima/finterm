#pragma once
// ChatPanelSize.h — sizing rule for the AI assistant's chat panel.
// Header-only so it is testable without the widget (tests/ai_chat/).

#include <QSize>

#include <algorithm>

namespace finterm::ai_chat::panel_size {

/// The panel's original fixed size; the default is twice this.
inline constexpr int BASE_PANEL_W = 320;
inline constexpr int BASE_PANEL_H = 440;
/// Smallest size the header + input row still fit in.
inline constexpr int MIN_PANEL_W = 300;
inline constexpr int MIN_PANEL_H = 320;
/// The panel never takes more than this share of the host in either axis.
inline constexpr double MAX_HOST_FRACTION = 0.6;

/// `wanted` clamped to [minimum, 60% of host] per axis; the minimum wins on a
/// host too small for both.
inline QSize clamp_panel_size(QSize wanted, QSize host) {
    const int max_w = std::max(MIN_PANEL_W, static_cast<int>(host.width() * MAX_HOST_FRACTION));
    const int max_h = std::max(MIN_PANEL_H, static_cast<int>(host.height() * MAX_HOST_FRACTION));
    return {std::clamp(wanted.width(), MIN_PANEL_W, max_w), std::clamp(wanted.height(), MIN_PANEL_H, max_h)};
}

/// Default: 2x the original in each axis, clamped to the host.
inline QSize default_panel_size(QSize host) {
    return clamp_panel_size(QSize(2 * BASE_PANEL_W, 2 * BASE_PANEL_H), host);
}

} // namespace finterm::ai_chat::panel_size

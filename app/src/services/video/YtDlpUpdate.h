#pragma once
// YtDlpUpdate — keep the yt-dlp in finterm's Python environment current.
// YouTube changes its player every few weeks and an older yt-dlp then fails
// to resolve streams; setup installs it once and never again (its line in
// requirements-numpy2.txt is unpinned, so the requirements hash never
// changes). This upgrades it in the background, at most once a day: shortly
// after start, and when a resolve has just failed.

namespace finterm::services::video {

/// Upgrade yt-dlp[default] in venv-numpy2 when the last check is a day old or
/// more (`force`: when a resolve just failed, the day counts from the last
/// attempt all the same — never more than once an hour). Does nothing when
/// the environment isn't set up, or the yt-dlp in use is not that one.
void refresh_ytdlp(bool after_failure = false);

} // namespace finterm::services::video

#pragma once
// YtDlpUpdate — keep the yt-dlp in finterm's Python environment current.
// YouTube changes its player every few weeks and an older yt-dlp then fails
// to resolve streams; setup installs it once and never again (its line in
// requirements-numpy2.txt is unpinned, so the requirements hash never
// changes). This upgrades yt-dlp and its YouTube helper — nothing else, and
// within the pinned requirements — at most once a day (checked hourly while
// finterm runs), detached (quitting finterm doesn't cut it off), its output in
// logs/ytdlp-upgrade.log.

namespace finterm::services::video {

/// Upgrade yt-dlp in venv-numpy2 when the last attempt is a day old or more.
/// Does nothing when the environment isn't set up, or the yt-dlp in use is
/// not that one.
void refresh_ytdlp();

} // namespace finterm::services::video

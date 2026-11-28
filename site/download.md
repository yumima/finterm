---
title: Download
description: finterm installers for Linux (x86-64), Windows 10/11 (x64) and macOS 13+ (Apple Silicon).
permalink: /download/
---
{% assign r = site.data.release %}
{% if r %}
<div class="tiles">
  <div><a class="button" href="{{ r.linux }}">Linux · AppImage</a><span class="tile-count" id="count-linux"></span></div>
  <div><a class="button" href="{{ r.windows }}">Windows · installer</a><span class="tile-count" id="count-windows"></span></div>
  <div><a class="button" href="{{ r.macos }}">macOS · disk image</a><span class="tile-count" id="count-macos"></span></div>
</div>

{% include counter.html short=true %}

Version {{ r.version }}{% if r.released %} · released <time datetime="{{ r.released }}">{{ r.released | date: "%-d %B %Y, %H:%M" }} UTC</time>{% endif %}{% if r.build %} · build {{ r.build }}{% endif %} · [release notes]({{ r.page }}) · [all releases]({{ site.repo_url }}/releases)
{% else %}
<p><a class="button" href="{{ site.repo_url }}/releases/latest">Get the latest release</a></p>
{% endif %}

## Install

**Windows 10/11 (x64)**

1. Run the installer. At SmartScreen: **More info → Run anyway**. No administrator rights; installs per user to `%LOCALAPPDATA%\Programs\finterm`, with Start Menu and Desktop shortcuts.
2. Start **finterm** from the Start Menu.

**macOS 13+ (Apple Silicon)**

1. Open the disk image and start **finterm** from it. First launch: **right-click → Open → Open**, or System Settings → Privacy & Security → **Open Anyway** (macOS 15+).
2. **Install** copies it to Applications, keeps it in the Dock and ejects the disk image.

**Linux (x86-64)**

1. `chmod +x finterm-*.run`, then run it.
2. **Install** copies it to `~/Applications/finterm.AppImage`, with a menu entry, a GNOME dock favourite and a `finterm` command. No root. Without FUSE (Ubuntu: `sudo apt install libfuse2 fuse3`) it still runs, unpacking itself each start.

**Updating, moving, removing** — the same download over an installed finterm offers **Update**, keeping your data; when a version changes how data is stored, the data is backed up first (`finterm-backups/`, beside the data folder). A silent install (`setup.exe install --confirm-command`) can't update: the installer framework refuses an occupied folder before finterm's script runs. Remove the installed copy first with `"%LOCALAPPDATA%\Programs\finterm\FintermMaintenanceTool.exe" purge --default-answer --confirm-command` (your data stays), then install. Settings › Storage & Data › **Export…** / **Import…** moves your data to another computer: keep the file private (it holds your accounts); API keys stay in the keyring. Uninstall: Windows Settings › Apps; macOS `/Applications/finterm.app/Contents/MacOS/finterm --uninstall` (or drag it to the Bin); Linux `finterm --uninstall`. The data folder stays.

## First start

1. **Setup** — downloads Python and builds the data environment: 5–15 minutes, internet required, once.
2. **Local account** — username and a 4–6 digit PIN, then a **recovery key**, shown once: it sets a new PIN if you forget yours. Both stay on this computer.
3. **Dashboard** — the data process starts in the background. Later starts go to the PIN.

## Troubleshooting

If no data appears, close finterm and run the self-test. It repeats setup, starts the data process, fetches one quote and reports `"ok": true` or the stage that failed:

```
Windows   "%LOCALAPPDATA%\Programs\finterm\finterm.exe" --selftest "%TEMP%\finterm-selftest.json"
macOS     /Applications/finterm.app/Contents/MacOS/finterm --selftest /tmp/finterm-selftest.json
Linux     ~/Applications/finterm.AppImage --selftest /tmp/finterm-selftest.json
```

Every release passes the self-test on all three systems before publication.

## Signing and updates

- **Signing** — installers are unsigned, hence the Windows and macOS prompts. Built from [source]({{ site.repo_url }}) by public CI.
- **Updates** — off by default. Enable the daily check at the status-bar prompt or in About, or run Help › Check for Updates. Updates download in the background and install on restart or quit; workspace and data are kept, and a version that fails to start is rolled back, with its data as that version left it.

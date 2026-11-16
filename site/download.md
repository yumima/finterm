---
title: Download finterm
description: Free installers for Linux, Windows and macOS. No account, no subscription.
permalink: /download/
---
{% assign r = site.data.release %}
{% if r %}
<p>
  <a class="button" href="{{ r.linux }}">Linux · AppImage</a>
  <a class="button" href="{{ r.windows }}">Windows · installer</a>
  <a class="button" href="{{ r.macos }}">macOS · disk image</a>
</p>

Version {{ r.version }} · [release notes]({{ r.page }}) · [all releases]({{ site.repo_url }}/releases)
{% else %}
<p><a class="button" href="{{ site.repo_url }}/releases/latest">Get the latest release</a></p>
{% endif %}

## Install and start

**Windows 10/11 (x64)**

1. Run the installer. At the SmartScreen warning choose **More info → Run anyway**, accept the Windows prompt, and click through. It installs to `C:\Program Files\finterm` with Start Menu and Desktop shortcuts.
2. Start **finterm** from the Start Menu.

**macOS 13+ (Apple Silicon)**

1. Open the disk image and drag **finterm** to Applications.
2. The first time, **right-click the app → Open → Open**. After that, start it like any app.

**Linux (x86-64)**

1. `chmod +x finterm-*.run`
2. `./finterm-*.run` — an AppImage: nothing is installed, no root needed. On Ubuntu 22.04+ run `sudo apt install libfuse2` once.

## The first start

1. **Setup, once.** finterm downloads its own Python and builds its data environment: about 5–15 minutes, with a progress screen, and it needs internet. You install nothing else.
2. **Create a local account:** a username and PIN, then a 6-digit security PIN. Both stay on your machine.
3. **The dashboard opens with live data.** finterm runs its own data process in the background; there is nothing else to launch.

Later starts go straight to the PIN.

## If no data appears

Run the built-in check. Close finterm first. It repeats the setup, starts the data process, fetches one quote and writes a report that says `"ok": true` or names what failed:

```
Windows   "C:\Program Files\finterm\finterm.exe" --selftest "%TEMP%\finterm-selftest.json"
macOS     /Applications/finterm.app/Contents/MacOS/finterm --selftest /tmp/finterm-selftest.json
Linux     ./finterm-*.run --selftest /tmp/finterm-selftest.json
```

Every release is installed and put through this check on all three systems before it is published.

The installers are free, so they carry no paid signing certificate; that is the one extra click on Windows and
macOS. They are built from the [public source]({{ site.repo_url }}) by its public CI. Updates:
**Help › Check for Updates**.

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

## First launch

| System | What to do |
|---|---|
| **Linux** x86-64 | `chmod +x finterm-*.run && ./finterm-*.run` — an AppImage: nothing is installed, no root needed. On Ubuntu 22.04+ run `sudo apt install libfuse2` once. |
| **Windows** 10/11 x64 | Run the installer. If SmartScreen appears: **More info → Run anyway**. |
| **macOS** 13+ Apple Silicon | Open the disk image and drag finterm to Applications. The first time, **right-click the app → Open → Open**. |

The installers are free, so they carry no paid signing certificate; that is the one extra click on Windows and
macOS. They are built from the [public source]({{ site.repo_url }}) by its public CI.

The first start sets up finterm's Python environment, once, in about 5–15 minutes. Later updates:
**Help › Check for Updates**.

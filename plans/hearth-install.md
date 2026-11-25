# hearth as a separately installed engine

Approved 2026-11-24. hearth (engine) and mantel (chat client) become one package
with its own installer, releases and updates. finterm detects an installed
hearth, asks, connects and can disconnect; it does not install, update or remove it.

## Design

- One repository, version, lock file, CI, release and install command for hearth
  and mantel. Separate packages, Python environments and processes; mantel is
  optional and talks to hearth over HTTP only, as finterm does.
- Fixed port 11435 and a known install path are the whole of discovery. A strict
  same-origin check is the protection from web pages; hearth is single-user, and
  other accounts on a shared computer are a documented limit.
- hearth owns the models (detection, ranking, download, test, roles). Apps ask
  for a role.
- hearth runs on its own, with its own Ollama. Linux, Windows and macOS are
  released together; GPU types nobody has run are marked experimental.
- Website: tabs at yumima.github.io, each project's site at its own path.
  Packages are on GitHub Releases.

## Tasks

**1. Current problems**
- [x] 1.1 hearth: web search only for a request that asks; mantel and `hearth chat` ask
- [x] 1.2 finterm: says no in every request (C++ and agents), with a switch, off by default
- [x] 1.3 hearth: refuse requests from other web pages
- [x] 1.4 finterm: a blank hearth URL is hearth, not Ollama
- [x] 1.5 finterm: verify the `uv` download
- [x] 1.6 mantel: voice work, `mcp` 1.x, tests (CI itself runs on a push)
- [x] 1.7 hearth: CI and one place for the version (CI itself runs on a push)

**2. finterm connects**
- [x] 2.1 hearth: `start --background`
- [x] 2.2 Detect: ready, stopped, no model, unsupported version, absent
- [x] 2.3 Connect and disconnect; remember "no"; restore the previous provider
- [x] 2.4 The chat provider is a separate choice from memory and voices; a real "no engine" state
- [x] 2.5 Delete HearthSupervisor; keep "start hearth when finterm opens"
- [x] 2.6 One panel for first-run setup and Settings; current users migrate as connected

**3. One repository, reliable engine**
- [x] 3.1 Bring in mantel with its history
- [x] 3.2 One lock file, two environments
- [x] 3.3 Process handling for all three systems, then CI on all three
- [x] 3.4 Retire hearth's chat page and window; `hearth client` opens mantel
- [x] 3.5 Restart Ollama when it dies; keep its log
- [x] 3.6 Sharing the GPU: Ollama answers in order; a short request waited 85 s behind a
      1500-token one. No queue of ours changes that (nothing pre-empts a running answer);
      finterm gives an engine on this computer 5 minutes and says it may be busy
- [x] 3.7 Voices out of finterm's folder
- [x] 3.9 `hearth client install`: builds the window (source tree or release) and adds it to the app menu
- [x] 3.10 One name: mantel is hearth-client, shown as "Hearth"; plain `hearth` is the coding agent;
      commands grouped (model, voice, provider)
- [ ] 3.8 Switch the developer machine over (asks first): merge `engine` into main of hearth and
      finterm, run from there; then delete the mantel repository (local and GitHub; user
      decided 2026-11-24, every commit is in hearth) and the *-engine worktrees and branches

**4. Linux install**
- [x] 4.1 Hardware check: NVIDIA, AMD, Apple, RAM, disk
- [x] 4.2 Model catalog and ranked list
- [x] 4.3 Generic model tests
- [x] 4.4 `install.sh`, with optional mantel
- [x] 4.5 Private Ollama
- [x] 4.6 Setup page: models, voices, speech recognition
- [x] 4.7 Verified download, measure, then assign; one failure does not stop the rest
- [x] 4.8 Models page: list, add, switch, safe removal; the embedding model is fixed
- [x] 4.9 Login service
- [x] 4.10 `hearth update`: previous version kept, data backed up
- [x] 4.11 `hearth uninstall`, with "keep models"
- [x] 4.12 Licence review and notices

**5. Windows, macOS, release**
- [x] 5.1 Installers and login services
- [x] 5.2 Kokoro voices and speech recognition; Piper tutor voices Linux-only at first
- [x] 5.3 CI with a tiny model, then clean-machine tests (CI computers, all three systems)
- [ ] 5.4 Website: tabs at yumima.github.io, hearth's pages and `install.sh` (written; creating the
      repository asks first, then `hearth_url` in site/_config.yml)
- [ ] 5.5 First release on all three (asks first)

**6. Get hearth**
- [x] 6.1 finterm button (idle until `EngineRelease.h` has the first release's hashes), then offer to connect; AppImage library paths do not reach hearth
- [x] 6.2 End-to-end tests and docs

**Later:** native signed installers, llama.cpp, other engines, remote hearth,
shared-computer security.

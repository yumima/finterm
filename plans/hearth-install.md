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
- [ ] 2.2 Detect: ready, stopped, no model, unsupported version, absent
- [ ] 2.3 Connect and disconnect; remember "no"; restore the previous provider
- [ ] 2.4 The chat provider is a separate choice from memory and voices; a real "no engine" state
- [ ] 2.5 Delete HearthSupervisor; keep "start hearth when finterm opens"
- [ ] 2.6 One panel for first-run setup and Settings; current users migrate as connected

**3. One repository, reliable engine**
- [ ] 3.1 Bring in mantel with its history
- [ ] 3.2 One lock file, two environments
- [ ] 3.3 Process handling for all three systems, then CI on all three
- [ ] 3.4 Retire hearth's chat page and window; `hearth client` opens mantel
- [ ] 3.5 Restart Ollama when it dies; keep its log
- [ ] 3.6 Test finterm and mantel sharing the GPU; a queue only if that fails
- [ ] 3.7 Voices out of finterm's folder
- [ ] 3.8 Switch the developer machine over (asks first)

**4. Linux install**
- [ ] 4.1 Hardware check: NVIDIA, AMD, Apple, RAM, disk
- [ ] 4.2 Model catalog and ranked list
- [ ] 4.3 Generic model tests
- [ ] 4.4 `install.sh`, with optional mantel
- [ ] 4.5 Private Ollama
- [ ] 4.6 Setup page: models, voices, speech recognition
- [ ] 4.7 Verified download, measure, then assign; one failure does not stop the rest
- [ ] 4.8 Models page: list, add, switch, safe removal; the embedding model is fixed
- [ ] 4.9 Login service
- [ ] 4.10 `hearth update`: previous version kept, data backed up
- [ ] 4.11 `hearth uninstall`, with "keep models"
- [ ] 4.12 Licence review and notices

**5. Windows, macOS, release**
- [ ] 5.1 Installers and login services
- [ ] 5.2 Kokoro voices and speech recognition; Piper tutor voices Linux-only at first
- [ ] 5.3 CI with a tiny model, then clean-machine tests
- [ ] 5.4 Website: tabs at yumima.github.io, hearth's pages and `install.sh` (creating the repository asks first)
- [ ] 5.5 First release on all three (asks first)

**6. Get hearth**
- [ ] 6.1 finterm button, then offer to connect; AppImage library paths do not reach hearth
- [ ] 6.2 End-to-end tests and docs

**Later:** native signed installers, llama.cpp, other engines, remote hearth,
shared-computer security.

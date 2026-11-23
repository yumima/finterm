# In-place update

Status: BUILT 2026-11-23 — phases 1–5. Phase 1 @ 70d29f8eb/5908b91cd/7bb2b4daa (opt-in check, badge, dev builds never
check); phases 2–5 in the commit that adds UpdateInstaller (in-place apply on all three platforms + rollback).
Linux checked end to end on Xvfb (stand-in AppImage: download → READY → Restart Now → swap → relaunch → status;
two failed starts → rollback); the Windows and macOS helpers are exercised against the real installers in
release.yml (a ci/** branch runs them without publishing).

## Where we are

- `UpdateService` (app/src/services/updater/) reads `updates.json` (written by release.yml on every tag), compares semver, and asks
  **"Download && Install"**. It then downloads the full installer, checks its sha256, and launches it next to the running app.
  The user ends up re-running the installer wizard: a reinstall, not an upgrade.
- It only runs when asked (Help › Check for Updates, About). The launch and unlock calls (`check_for_updates(true)`,
  MainWindow.cpp ~1311 / ~1509) return straight away. Commit 243296cc0 set this rule: "no network request the user didn't ask for".
- Artifacts: Windows = QtIFW offline setup.exe (maintenance tool `FintermMaintenanceTool`, target `@HomeDir@/finterm`),
  Linux = AppImage (named `.run`), macOS = DMG holding an ad-hoc-signed `.app`.
- User data lives outside the install directory (AppPaths), so replacing the program files never touches it.

## The sequence

```
check (background) → download + verify (background) → "ready" badge, no dialog
      → user picks Restart now   OR   it applies the next time the app quits
      → relaunch on the new version; the workspace comes back as it was left
```

Differences from "check at restart → ask → relaunch":

1. **No modal at launch or unlock.** Those are the moments the user wants to get to work, often at the market open. The check runs
   in the background, and the dialog-shaped question becomes a small status-bar badge: `Update 4.0.11 ready · Restart`.
2. **Download before asking.** When the user does click, the restart is instant: nothing to wait for, no progress bar.
3. **Apply on quit.** If the user never clicks, the swap happens when they close the app anyway (the Chrome / VS Code pattern).
   The next launch is simply the new version. No one is ever forced to restart mid-session.
4. **Relaunch restores the session.** Reopen-as-left already does this (project_reopen_as_left). Restart now = save the workspace,
   quit, swap, exec the new binary.

### When to check

- 10 s after the main window is up (launch, or the first unlock), then every 24 h while running. The time of the last check is
  stored, so restarting the app often does not mean checking often.
- An unlock only triggers a check when ≥24 h have passed. It never shows anything.
- Help › Check for Updates stays: same pipeline, plus it reports "you're up to date" or the error.
- Dev builds (version not strict semver) never check. This is already the case.

### Consent (conflicts with 243296cc0)

An automatic check is a request the user didn't make each time. Proposed rule: **one setting, "Check for updates automatically"**, put to
the user once through a non-modal notice on the first launch of the build that ships this ("finterm can check GitHub once a day for
new versions. [Turn on] [Not now]"). Until they turn it on, behaviour is exactly as today. The request carries only
`finterm/<version> (<platform>)`, nothing about the user.

## Applying it, per platform

| Platform | Staged as | Swap | Relaunch |
|---|---|---|---|
| Linux AppImage | `<appimage>.new` in the same directory (same filesystem) | `rename()` over `$APPIMAGE`. Atomic, and safe while running (the old inode stays mapped) | `execv` the new path |
| Windows (IFW) | The verified setup.exe in the app data | After exit the helper copies the install to `<dir>.previous`, removes it with `FintermMaintenanceTool purge`, and runs the setup unattended into the same folder (`install --root … --accept-licenses --default-answer --confirm-command`, the command CI already proves); a failed install is copied back | Helper starts `finterm.exe` |

(Chosen over an IFW online repository: no second hosted artifact to keep in step with the release, and the
offline installer + its unattended mode are what CI already installs and self-tests.)
| macOS (DMG) | `.app` copied out of the mounted DMG with `ditto` into `~/Library/Caches/finterm/update/` | After exit, a helper renames the old bundle to `.old`, moves the new one into place, clears quarantine (`xattr -dr com.apple.quarantine`), then deletes `.old` | `open -n <bundle>` |

Windows and macOS can't replace a running binary, so the swap runs in a **tiny detached helper** (a script written by the app) that
waits for the app's PID to exit, swaps, relaunches, and exits. Linux doesn't need it.

### Safety

- sha256 from the manifest. This is already in place, so keep it. Download to a temp name, verify, then rename to the staged name.
  A partly downloaded file can never be staged.
- **Rollback:** keep the previous version (`.old` / the previous AppImage) until the new one reaches the main window and writes a
  "boot ok" marker. If two launches in a row fail to write the marker, the launcher (finterm.sh / helper) restores `.old`.
- Python: `scripts/` ships with the app, and the uv environments live in user data. A version bump must re-run the env sync that the
  installer path triggers today. Check this against `--selftest` in CI.
- Never apply while a second instance is running (SingleApplication already guarantees only one).
- Release.yml: add the IFW repository generation (`repogen`) + Pages publish for Windows. Linux/macOS need nothing new; the manifest
  already points at the right files.

## Phases

1. Background check + 24 h cadence + consent setting + status-bar badge (no apply yet; the badge opens today's flow).
2. Background download + staging + "Restart now" / apply-on-quit, **Linux AppImage first** (atomic rename, no helper).
3. macOS bundle swap helper.
4. Windows: IFW online repository in release.yml + `maintenancetool update`; offline-setup fallback.
5. Rollback marker + restore; ctest for semver/staging/marker logic; manual end-to-end per platform from N-1 → N.

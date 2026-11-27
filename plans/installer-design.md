# One download that installs, updates, migrates and adds the launcher

Status: design, 2026-11-27. Nothing here is built yet.

## Goal

Each platform's download is one file, and running it always does the right
thing for what is already on the computer:

| Found on the computer | What the download offers (the default comes first) |
|---|---|
| nothing | **Install** · Run once without installing · Import data from a backup |
| an older finterm | **Update** (keeps the data and upgrades it) · Update and start fresh · Cancel |
| the same version | **Open** · Repair (reinstall the files, keep the data) · Start fresh |
| a newer finterm | **Open the newer one** · Cancel. It never downgrades silently. |
| data but no app (it was uninstalled and the data kept) | **Install and keep the data** · Install and start fresh |

Whatever is chosen, the result is the same. The app is in a fixed place. It has
a launcher in the menu or Start, and in the Dock or favourites when that was
chosen. The data has been backed up, brought up to date and checked before the
new version uses it.

## What exists today (checked in the code)

| | Windows | Linux | macOS |
|---|---|---|---|
| Download | QtIFW `.exe` | AppImage named `.run` | DMG with a drag-to-Applications link |
| Installs | yes, per user, `%LOCALAPPDATA%\Programs\finterm` | **no**: it runs wherever it was saved, usually Downloads | only if the user drags it |
| Launcher | Start Menu and Desktop `.lnk` | **none**. Only the source tree's `finterm.sh install` writes one | none; the user drags it to the Dock |
| Running a newer download over an install | **not possible**. IFW refuses a folder that already holds an installation, so the user has to uninstall first | the user is left with two AppImages | Finder asks to replace it |
| In-app update | yes: swaps folders, keeps the previous version, rolls back | yes: replaces `$APPIMAGE`, which may be in Downloads | yes: swaps the bundle |
| Data location | `%LOCALAPPDATA%\finterm` | `~/.local/share/finterm` | `~/Library/Application Support/finterm` |
| Start fresh | in the app (lock screen, Settings) | same | same |

The data already lives outside the install, split into `user/` (personal) and
system folders (`AppPaths.h`). So an update cannot lose data by replacing
files. These are the gaps, each confirmed in the source:

1. **No backup before data is upgraded.** `MigrationRunner::run()` changes each
   database in place, one transaction per step. A step that commits but is
   wrong for the user's data cannot be undone.
2. **Older builds run on newer data.** `MigrationRunner` skips steps it already
   has and never checks for a schema *newer* than it knows. The same is true of
   `LayoutMigrations`. An older binary (a rollback, or a reinstalled old
   download) opens data written by a newer one and can corrupt it.
3. **A rollback restores the program, not the data.** The updater puts the
   previous version back when the new one fails to start twice. If the new one
   had already migrated the databases, the old one then runs on them, which is
   gap 2.
4. **Databases migrate only when opened.** A user's database `users/<name>.db`
   is migrated at that user's login (`AuthService::open_user_db` →
   `Database::open`), and another profile's data only when that profile is
   started. A failed migration shows up weeks after the update, for the user
   who logs in least often.
5. **Old files are left in the data folder**, for example
   `user/data/fincept.db.pre-multiuser.bak` from the multi-user move. Nothing
   tidies these away, and each release adds a few more.

## Principles

- **One code path for all three systems.** What happens to the data, and the
  questions asked about it, is shared C++ code in the finterm binary: the
  setup assistant. The platform package only puts files in place: the AppImage
  on Linux, the bundle on macOS, IFW on Windows. The app already does the
  migrations, Start fresh, the backups and the in-place update, so the
  installer reuses them and does not copy them into an installer scripting
  language.
- **Defaults are not user settings.** Installing or updating never resets a
  saved layout or setting. Changed defaults reach fresh installs only.
- **Nothing personal is deleted.** Start fresh, repair and tidying up all move
  things into an owner-only backup folder (`start_fresh::make_backup_dir`), as
  Start fresh and Reset this user do now. Only the user deletes backups, from
  the backups list.
- **Silent when nothing needs deciding.** An in-app update or an ordinary
  re-run asks nothing. The assistant opens only when the user has a real
  choice: first install, an upgrade over a manually run download, data newer
  than the app, or a failure.
- **The data is upgraded before the new version uses it**, all of it at once:
  every profile, every user and every database. The backup is made first, and
  each database is checked afterwards.

## Architecture

```
download (per OS)
  └─ platform shim: puts files in place (AppImage copy | bundle copy | IFW)
       └─ finterm --setup …  ← the setup assistant, shared C++ code
            ├─ InstallTarget   where the app lives, the launcher, Dock/favourites, uninstall entry
            ├─ DataInventory   what is on disk: profiles, users, schema versions, sizes, stray files
            ├─ DataUpgrade     backup → migrate everything → check → tidy → stamp
            └─ pages           Install / Update / Start fresh / Import / Newer data / Problem
```

New code goes in `app/src/core/setup/`. The pages are Qt widgets in the app's
own style, shown before the main window and before any database is opened, in
the same place in `main()` where `start_fresh::run_if_scheduled()` runs now.

### When the assistant opens

`main()` decides this before `AppPaths::migrate_root()` and before any
database opens:

1. **Not running from the install location** (Linux: `$APPIMAGE` is not the
   installed file; macOS: the bundle is not the one in `/Applications` or
   `~/Applications`, for example on the DMG, in Downloads, or translocated):
   the Install / Update / Open page from the Goal table. `--portable`, or
   "Run once without installing", skips it for that run.
2. **The data needs upgrading** (`data-version.json` is older than this build,
   or missing while data exists): run DataUpgrade. It runs with a progress line
   and no questions when the update came from the in-app updater or the user
   already chose Update on page 1. It opens the Update page when the user simply
   started a new build over old data, which is a deliberate choice of version.
3. **The data is newer than this build:** the Newer data page. It never opens
   the databases read-write. It offers to open the newer installed finterm if
   one is present, or to restore the backup made when that data was upgraded
   past this version, if there is one.
4. Anything else: straight to the lock screen, as today.

### DataUpgrade, in order

All of it runs under a lock held across the whole profile root, so it refuses
while another instance runs (`AppPaths::data_dir_in_use`).

1. **Inventory.** Covers every profile root (default plus `profiles/*`), and
   in each: `auth.db`, `cache.db`, `users/*.db`, `features/**.db`, workspaces,
   settings, and the keyring index. For each database it records the schema
   version against the highest this build knows.
2. **Backup**, only when a step is actually pending, so most patch releases
   skip it. Each database is copied with `VACUUM INTO`, which gives a
   consistent copy even with WAL files present. `files/`, `workspaces/` and
   `agents/` are copied as they are, along with an export of the settings.
   Keyring secrets stay where they are: no migration changes them, and copying
   them to disk would weaken them.
   - Destination: `finterm-backups/pre-upgrade-<from>-to-<to>-<time>/`,
     owner-only (0700), with a `manifest.json` that lists each file, its
     schema version and checksum.
   - The three most recent pre-upgrade backups are kept, and the assistant
     shows their size. Start fresh and deleted-user backups are never pruned.
3. **Layout migrations.** `AppPaths::migrate_root()` and `LayoutMigrations::run()`
   run as now.
4. **Schema migrations, for every database now**, through the existing
   `MigrationRunner`, with no change to its steps. A failure stops the whole
   upgrade. The failed database is restored from step 2 (its transaction has
   already rolled back), the error is shown, and the user can open the previous
   version or send the log.
5. **Check.** Each database gets `PRAGMA integrity_check` and
   `PRAGMA foreign_key_check`. A database that fails, and already failed before
   the upgrade, is reported with a "recover" offer: `.recover` into a new file,
   with the original kept. A failure that is new is treated as a migration
   failure (step 4).
6. **Tidy.** Registered, idempotent repairs bring the layout to what this build
   expects. Anything that is not in the expected layout is moved, never
   deleted, to `finterm-backups/tidied-<time>/` with a note saying why. For
   example, `fincept.db.pre-multiuser.bak`, which `main.cpp` leaves in
   `user/data` after the multi-user move, is moved once the user database it
   was converted into opens and passes its check. Each
   repair is a named, tested function, as the SQL migrations are. There is no
   general sweep, because an unknown file may be the user's own.
7. **Stamp** `data-version.json` in the root with the app version, layout
   version, each database's schema version, and the path of the backup from
   step 2. This file is what the next start compares against, and what a
   rollback reads.

### Guarding against newer data (gaps 2 and 3)

- `MigrationRunner::run()` returns an error when a database's version is
  higher than the highest migration this build has. `LayoutMigrations` does the
  same for its stored version. `main()` turns either error into the Newer data
  page and never runs on the data.
- **A rollback restores the data as well.** When the updater puts the previous
  version back after the new one failed to start, the previous version finds
  `data-version.json` newer than itself. It also finds the backup that the
  failed version made at its upgrade, and restores it before anything opens.
  Nothing is lost, because a rollback happens only when the new version never
  got its window up. The data from before the restore is kept as
  `rolled-back-<time>/` all the same.

## Per platform

### Linux (`finterm-<v>-linux-x64-setup.run`, still an AppImage)

- **Install location:** `~/Applications/finterm.AppImage`. The name has no
  version in it, so the launcher, pins and the updater's target never change.
  This is the folder that AppImageLauncher and Gear Lever use, and the user
  can see it. No root is needed.
- **Launcher:**
  - `~/.local/share/applications/finterm.desktop` with `StartupWMClass=finterm`
    and the marker `X-Finterm-Installer=1`.
  - The icon goes in `~/.local/share/icons/hicolor/256x256/apps/finterm.png`.
  - `~/.local/bin/finterm` is a symlink, so `finterm` works in a terminal.
  - `update-desktop-database` and the icon cache are refreshed.
- **An existing launcher.** If a `finterm.desktop` is there without our marker,
  for example the source tree's `finterm.sh install`, the assistant asks before
  replacing it and keeps the old one as `finterm.desktop.bak`. If it has the
  marker, it is rewritten in place. The desktop id stays `finterm.desktop`, so
  an existing favourite keeps working.
- **Dock / favourites:**
  - GNOME: `finterm.desktop` is added to
    `org.gnome.shell favorite-apps`, only on first install and only while
    "Add to favourites" is ticked (it is ticked by default).
  - The fact that we added it is recorded. If the user later removes it, no
    update adds it back.
  - KDE, XFCE and others get the menu entry only, and the page says so.
- **FUSE 2:** without `libfuse.so.2`, the AppImage cannot mount itself. The
  installer detects this and writes `Exec=env APPIMAGE_EXTRACT_AND_RUN=1 …`.
  The page explains the slower start and the one command that fixes it
  (`sudo apt install libfuse2`).
- **Update over an install:** run from Downloads, the newer file replaces
  `~/Applications/finterm.AppImage` through the same `apply-update.sh` path the
  updater uses. That includes the wait for a running instance and keeping the
  previous version. Then it starts the installed copy and offers to delete the
  downloaded file.
- **Uninstall:** `finterm --uninstall`, or Settings › About › Uninstall.
  - It removes the AppImage, the launcher, the icon, the symlink and our
    favourite.
  - It asks about the data, keeping it by default, and moves it to a backup
    rather than deleting it.
  - The README's "delete the .run file" becomes this.
- The in-app updater's target becomes the installed file. An AppImage still
  run from Downloads keeps updating itself where it is, as today.

### macOS (`finterm-<v>-macos-arm64-setup.dmg`)

- The DMG stays a DMG with a drag-to-Applications link. Opening the app from
  the DMG, or from anywhere that is not `/Applications` or `~/Applications`,
  shows the Install / Update page:
  - It copies the bundle to `/Applications`, or to `~/Applications` when the
    user cannot write there. This uses the `macapp` swap in `apply-update.sh`:
    copy beside, move the old one to `.finterm-previous.app`, rename, clear
    quarantine.
  - It relaunches from there and ejects the DMG.
  - That fixes translocation, where an app run from Downloads runs from a
    random read-only path and cannot update itself.
- **Dock:**
  - "Keep in Dock" is ticked on first install. It adds a `persistent-apps`
    entry through `defaults` and restarts the Dock.
  - This is not an Apple API, so it is done only on an explicit first install
    and recorded, never on update.
  - An existing Dock tile refers to the bundle's path. The bundle is swapped
    in place, so the tile keeps working without being touched.
- **Uninstall:** Settings › About › Uninstall does the same as on Linux.
  Dragging the app to the Bin still works and leaves the data, as today.

### Windows (`finterm-<v>-windows-x64-setup.exe`, QtIFW)

QtIFW stays. It already gives a per-user install, the Apps & Features entry,
shortcuts, and an uninstaller tested in CI. These are the changes:

- **Update over an install** (the gap in the table above): `installscript.qs`
  finds `FintermMaintenanceTool.exe` in the target folder and asks to update.
  Then:
  1. It runs that tool's `purge` silently, telling it to keep the data
     (`FINTERM_KEEP_DATA=1`, read by `onUninstallationStarted`, so there is no
     reliance on the question's default answer).
  2. It installs into the same folder.
  3. The same applies to the Program Files install from 4.0.10, which is moved
     instead of refused. The README's "uninstall it first" step goes away.
- **Shortcuts:** the Start Menu and Desktop `.lnk` keep their paths, so any
  taskbar pin made from them keeps working. Windows does not let an installer
  pin to the taskbar, so the finish page says how to pin.
- The finish page's "Start finterm" starts `finterm.exe`, and the setup
  assistant then handles the data with the same pages as the other systems.
  IFW itself asks nothing about data on install.

## Start fresh, import and export

- **Start fresh** at install or update uses `start_fresh::schedule()` as it is
  now. It backs up and then clears the data, keeps the keyring entries under
  `pre-reset-<tag>/`, and the new version then starts empty.
- **Export** (Settings › Data › Export, and `finterm --export FILE`) writes a
  `.finterm-backup` zip of one profile or all profiles.
  - It holds the step-2 backup format and its manifest.
  - Keyring secrets are left out by default. If the user ticks "include API
    keys", they are added encrypted with a passphrase the user enters, using
    the same scheme as the recovery key.
- **Import** (the first-install page, Settings, and `finterm --import FILE`):
  1. It checks the manifest.
  2. It puts the data in place: into an empty install, or into a new profile so
     nothing existing is overwritten.
  3. It runs DataUpgrade, which brings a backup from an older version up to
     date.
  4. It restores the secrets when the passphrase is given.

  This is how data moves to a new computer, and how any pre-upgrade backup is
  restored by hand.

## Command line (scripts, CI, support)

```
finterm --setup                       the assistant, as if run from the download
finterm --install [--no-launcher] [--no-dock] [--yes]
finterm --uninstall [--keep-data|--remove-data] [--yes]
finterm --upgrade-data [--check-only]  inventory + DataUpgrade, report as JSON
finterm --export FILE [--profile N] [--include-secrets]
finterm --import FILE [--as-profile N]
finterm --portable                    run without the install check
```

`--yes` takes the default answer, and the default always keeps data. Every
command writes one JSON line describing the outcome, as `--self-test` does now,
so CI and support read the same thing.

## Testing

Each of these runs in the release workflow, on the file a user downloads,
before the release is published:

- **Fresh install** on each OS:
  - the launcher exists and points at the installed file;
  - Linux only: the favourite is added on GNOME (gsettings under `dbus-run-session`);
  - the app starts and `--self-test` passes.
- **Upgrade from the previous release:**
  1. Install the last published version.
  2. Create a profile and a user, with known data.
  3. Run the new download.
  4. Check:
     - the data is intact;
     - every database is at the current schema;
     - one pre-upgrade backup exists, with 0700 permissions;
     - the launcher was updated, not duplicated.
  - Windows: this replaces the 4.0.10 refusal test with an upgrade test.
- **Newer data:** start the previous release on data the new one upgraded. It
  must show the Newer data page (in JSON mode, `"newer_data"`), and the data
  must not change (checksums compared).
- **Rollback restores data:** make the new version fail at start, as the
  existing rollback test does, then check that the databases match the
  pre-upgrade backup.
- **Failed migration:** inject a failing step behind a test-only flag, then
  check that every database is back to its backup and the old version still
  opens it.
- **Unit tests:**
  - DataInventory over prepared trees;
  - each tidy step, including that it moves the file and does not delete it;
  - the `.desktop` rewrite with and without our marker;
  - the newer-schema checks in `MigrationRunner` and `LayoutMigrations`.

## Phases

Each phase is shippable on its own and follows the usual loop: build →
/review → fix → test → commit.

1. **Data safety.** This closes gaps 1–4 and helps every user, whichever way
   they install.
   - The newer-schema guard, `data-version.json`, the pre-upgrade backup, and
     eager migration of every database.
   - A rollback restores the data.
   - `--upgrade-data`, plus the CI tests for newer data, rollback and failed
     migration.
2. **Linux install:** InstallTarget for Linux (install location, launcher,
   favourite, FUSE fallback, update over an install, `--install` /
   `--uninstall`), the Install / Update pages, and the CI tests for fresh install
   and upgrade.
3. **macOS install:** install from the DMG, Dock, relaunch and eject, and the CI
   tests.
4. **Windows update over an existing install**, and moving the Program Files
   install. The finish page hands over to the assistant.
5. **Tidy, export and import:** the registered repairs (gap 5), the
   `.finterm-backup` format, and the import page on first install.
6. The README, the release notes and the site describe one download per system:
   run it, and it installs or updates.

## Decisions taken

| Decision | Why |
|---|---|
| The assistant lives in the app, and installers only place files | One implementation of data handling, already tested. IFW scripting and shell would each need their own copy, and the copies would drift apart. |
| QtIFW stays on Windows | The Apps & Features entry, the uninstaller and the CI tests already work. Only "update over an install" is missing. |
| `~/Applications/finterm.AppImage`, with no version in the name | Stable target for the launcher, pins and the updater. It is the convention among AppImage users. |
| Dock and favourites added once, on first install, ticked by default | The user asked for it. Re-adding it on update would override a user who removed it, which goes against defaults ≠ user settings. |
| A backup only when a migration is pending, and the last three kept | Most releases change no schema. A backup on every update would fill the disk. |
| Keyring secrets are never written into backups unencrypted | Backups are files and can be copied off the computer. Secrets stay in the keyring unless the user exports them with a passphrase. |
| Tidying uses named repairs, not a sweep | An unknown file in the data folder may be the user's own. |

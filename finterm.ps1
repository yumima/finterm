#!/usr/bin/env pwsh
# finterm.ps1 — native-Windows one-stop launcher / builder / state utility.
#
# Windows sibling of finterm.sh (Linux/macOS). Same command vocabulary:
#   setup / start / build / repair / reset / stop / status / install / uninstall
#
# Why a separate script: native Windows has no bash, stores settings in the
# REGISTRY (not a file), and builds with MSVC — so the POSIX finterm.sh can't
# do the job here. This mirrors its behaviour using Windows-native tooling.
#
# Usage:   .\finterm.ps1 <command> [flags]      (or:  pwsh finterm.ps1 ...)
# Tip:     run `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` once if
#          PowerShell refuses to run the script.

# Parse args from $args directly rather than a param() block: with
# [CmdletBinding()], dashed tokens like `-y` / `--clean` are matched against
# parameter names and would throw a binding error instead of falling through.
# $args captures everything verbatim; $Rest is script-scoped so the command
# functions can read it.
$Command = if ($args.Count -ge 1) { [string]$args[0] } else { 'start' }
$Rest    = if ($args.Count -gt 1) { @($args[1..($args.Count - 1)]) } else { @() }

$ErrorActionPreference = 'Stop'

# ── Paths ─────────────────────────────────────────────────────────────────────
$RepoDir     = $PSScriptRoot
$AppDir      = Join-Path $RepoDir 'fincept-qt'
$BuildPreset = 'win-release'
$BuildDir    = Join-Path $AppDir "build\$BuildPreset"
$Bin         = Join-Path $BuildDir 'FinceptTerminal.exe'

# App state. AppPaths::root() on Windows = %LOCALAPPDATA%\com.fincept.terminal.
$Root        = Join-Path $env:LOCALAPPDATA 'com.fincept.terminal'
$DataDir     = Join-Path $Root 'data'
$Workspaces  = Join-Path $Root 'workspaces'
# Settings live in the registry (QSettings native format), org "Fincept".
$RegApp      = 'HKCU:\Software\Fincept\FinceptTerminal'
$RegOrg      = 'HKCU:\Software\Fincept'
# Portable data snapshots — outside the wiped trees so `reset` never deletes them.
$BackupDir   = if ($env:FINCEPT_BACKUP_DIR) { $env:FINCEPT_BACKUP_DIR } `
               else { Join-Path $env:LOCALAPPDATA 'finterm-backups' }

# Pinned build inputs (Windows keeps a pinned Qt — there is no system Qt here).
$QtVersion   = '6.8.3'
$QtArch      = 'win64_msvc2022_64'
$QtRoot      = if ($env:FINCEPT_QT_ROOT) { $env:FINCEPT_QT_ROOT } else { 'C:\Qt' }
$QtPrefix    = Join-Path $QtRoot "$QtVersion\msvc2022_64"

# ── Output helpers ────────────────────────────────────────────────────────────
function Info($m) { Write-Host "  $m" -ForegroundColor Yellow }
function Ok()      { Write-Host "  OK" -ForegroundColor Green }
function Fail($m) { Write-Host "  ERROR: $m" -ForegroundColor Red; exit 1 }

function Confirm-Action($prompt) {
    if ($Rest -contains '-y' -or $Rest -contains '--yes') { return $true }
    $r = Read-Host "  $prompt [y/N]"
    return ($r -match '^[Yy]$')
}

function Stop-App {
    $p = Get-Process FinceptTerminal -ErrorAction SilentlyContinue
    if ($p) {
        $p | Stop-Process -Force -ErrorAction SilentlyContinue
        # Wait for the process to actually exit so its DB/plist files are unlocked
        # before we snapshot or delete them (avoids a half-written snapshot).
        Wait-Process -Name FinceptTerminal -Timeout 10 -ErrorAction SilentlyContinue
    }
}

function Test-VenvHealthy {
    $py = Join-Path $Root 'venv-numpy2\Scripts\python.exe'
    if (-not (Test-Path (Join-Path $Root '.setup_complete'))) { return $false }
    if (-not (Test-Path $py)) { return $false }
    try { & $py -c 'import numpy' 2>$null; return ($LASTEXITCODE -eq 0) } catch { return $false }
}

# Copy the portable user data (portfolio DB, workspaces) + export settings to a
# snapshot dir. Used as insurance by both repair and reset. Regenerable state
# (caches, venv, models) is never captured.
function Snapshot-Data($dest) {
    New-Item -ItemType Directory -Force -Path (Join-Path $dest 'data'), (Join-Path $dest 'workspaces') | Out-Null
    if (Test-Path (Join-Path $DataDir 'users')) {
        Copy-Item (Join-Path $DataDir 'users') (Join-Path $dest 'data\users') -Recurse -Force -ErrorAction SilentlyContinue
    }
    foreach ($f in 'fincept.db','fincept.db-wal','fincept.db-shm','auth.db','auth.db-wal','auth.db-shm') {
        $src = Join-Path $DataDir $f
        if (Test-Path $src) { Copy-Item $src (Join-Path $dest 'data') -Force -ErrorAction SilentlyContinue }
    }
    if (Test-Path $Workspaces) {
        Copy-Item (Join-Path $Workspaces '*') (Join-Path $dest 'workspaces') -Recurse -Force -ErrorAction SilentlyContinue
    }
    # Settings (incl. API keys held in QSettings) → registry export.
    if (Test-Path $RegApp) {
        & reg.exe export 'HKCU\Software\Fincept' (Join-Path $dest 'settings.reg') /y | Out-Null
    }
    Write-Host "  data snapshot -> $dest"
}

# ── Command: setup ─────────────────────────────────────────────────────────────
function Cmd-Setup {
    Write-Host ""
    Write-Host "================================================"
    Write-Host "  Fincept Terminal - Setup (Windows)"
    Write-Host "  Qt $QtVersion (pinned) | MSVC 2022 | CMake + Ninja"
    Write-Host "================================================`n"

    # 1. Build tools via winget (idempotent; --silent where supported).
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Fail "winget not found. Install 'App Installer' from the Microsoft Store, then re-run."
    }
    Write-Host "[1/5] Installing build tools (winget)..."
    $pkgs = @(
        'Microsoft.VisualStudio.2022.BuildTools',
        'Kitware.CMake',
        'Ninja-build.Ninja',
        'Python.Python.3.11',
        'OpenJS.NodeJS.LTS'
    )
    foreach ($p in $pkgs) {
        winget install --id $p -e --accept-source-agreements --accept-package-agreements --silent 2>$null | Out-Null
    }
    Info "If Visual Studio Build Tools was just installed, ensure the 'Desktop development with C++' workload is present (winget installs the C++ workload by default for BuildTools)."
    Ok

    # 2. Qt via aqtinstall (Windows has no system Qt — keep the pinned install).
    Write-Host "[2/5] Installing Qt $QtVersion (aqtinstall) -> $QtRoot ..."
    if (Test-Path (Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake')) {
        Info "Qt already present at $QtPrefix"
    } else {
        & py -m pip install --quiet --upgrade aqtinstall
        if ($LASTEXITCODE -ne 0) { & python -m pip install --quiet --upgrade aqtinstall }
        $modules = 'qtcharts','qtwebsockets','qtmultimedia','qtspeech','qtpositioning','qtwebchannel','qtwebengine'
        # Passing the array to a native command auto-expands each element as a
        # separate arg (-> `--modules qtcharts qtwebsockets ...`).
        & py -m aqt install-qt windows desktop $QtVersion $QtArch --outputdir $QtRoot --modules $modules
        if ($LASTEXITCODE -ne 0) { Fail "aqtinstall failed. Install Qt $QtVersion (msvc2022_64) manually from qt.io." }
    }
    Ok

    # 3. Enter the MSVC developer environment (needed for the Ninja+cl build).
    Write-Host "[3/5] Locating Visual Studio toolchain..."
    Enter-VsDevEnv
    Ok

    # 4. Configure.
    Write-Host "[4/5] Configuring (preset: $BuildPreset)..."
    Push-Location $AppDir
    try {
        & cmake --preset $BuildPreset -DCMAKE_PREFIX_PATH="$QtPrefix"
        if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed." }
        Ok
        # 5. Build.
        Write-Host "[5/5] Building..."
        & cmake --build --preset $BuildPreset
        if ($LASTEXITCODE -ne 0) { Fail "Build failed." }
        Ok
    } finally { Pop-Location }

    Write-Host "`n================================================"
    Write-Host "  Setup complete."
    Write-Host "  Binary: $Bin"
    Write-Host "  Launch: .\finterm.ps1"
    Write-Host "================================================`n"
}

# Bring MSVC (cl/link) onto PATH for this session via vswhere + Enter-VsDevShell.
function Enter-VsDevEnv {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { Fail "vswhere not found — install Visual Studio 2022 Build Tools (C++)." }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
              Select-Object -First 1   # may list several editions; take one
    if (-not $vsPath) { Fail "No VS 2022 with C++ tools found. Run setup to install Build Tools." }
    $devShell = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# ── Command: start ─────────────────────────────────────────────────────────────
function Cmd-Start {
    if (-not (Test-Path $Bin)) {
        if ([Environment]::UserInteractive) {
            Write-Host "First run - provisioning finterm (installs dependencies + builds; one time)...`n"
            Cmd-Setup
        } else {
            Fail "finterm isn't set up yet. Open PowerShell and run:  $RepoDir\finterm.ps1 setup"
        }
    }
    # GPU note: on Windows, NVIDIA Optimus selects the GPU automatically; no glvnd
    # juggling needed, so there is no FINCEPT_GPU equivalent here.
    Start-Process -FilePath $Bin -WorkingDirectory $BuildDir
    Write-Host "Launched FinceptTerminal."
}

# ── Command: build ─────────────────────────────────────────────────────────────
function Cmd-Build {
    if (-not (Test-Path $Bin) -and -not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
        Fail "Not configured yet. Run:  .\finterm.ps1 setup"
    }
    Enter-VsDevEnv
    Push-Location $AppDir
    try {
        if ($Rest -contains '--clean') {
            & cmake --build $BuildDir --clean-first
        } else {
            & cmake --build $BuildDir
        }
        if ($LASTEXITCODE -ne 0) { Fail "Build failed." }
    } finally { Pop-Location }
    Write-Host "Built: $Bin"
}

# ── Command: repair ────────────────────────────────────────────────────────────
# Fix a misbehaving app, KEEPING your data (portfolio, API keys, GUI layout,
# workspaces). Clears caches + transient state + stale window layout, and
# rebuilds the venv only if a health check finds it broken. Surgical + safe.
function Cmd-Repair {
    Write-Host "=== Stopping app ==="
    Stop-App
    Write-Host "=== Insurance snapshot (your data is also kept in place) ==="
    Snapshot-Data (Join-Path $BackupDir ("repair-" + (Get-Date -Format 'yyyyMMdd-HHmmss')))

    Write-Host "=== Clearing caches + transient state ==="
    foreach ($f in 'cache.db','cache.db-wal','cache.db-shm') {
        Remove-Item (Join-Path $DataDir $f) -Force -ErrorAction SilentlyContinue
    }
    foreach ($d in 'cache','uv-cache','runtime','crashdumps') {
        Remove-Item (Join-Path $Root $d) -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host "  cleared: market/screen cache, uv cache, runtime, crash dumps"

    Write-Host "=== Resetting window/dock layout ==="
    # Window/dock geometry is stored as registry values/subkeys under the app key.
    if (Test-Path $RegApp) {
        Remove-ItemProperty -Path $RegApp -Name 'window_count','window_ids' -ErrorAction SilentlyContinue
        Get-ChildItem $RegApp -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -match '^window_\d+$' } |
            Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host "  window/dock arrangement reset to default"

    Write-Host "=== Checking Python venv ==="
    if (Test-VenvHealthy) {
        Write-Host "  venv: OK - kept"
    } else {
        Write-Host "  venv: broken/missing - removing (rebuilds on next launch, ~5-15 min)"
        foreach ($d in 'venv-numpy1','venv-numpy2') { Remove-Item (Join-Path $Root $d) -Recurse -Force -ErrorAction SilentlyContinue }
        Remove-Item (Join-Path $Root '.setup_complete') -Force -ErrorAction SilentlyContinue
    }
    Write-Host "`nRepair complete - kept your portfolio, API keys, GUI layout, and workspaces."
    Write-Host "Relaunch: .\finterm.ps1"
}

# ── Command: reset ─────────────────────────────────────────────────────────────
# True factory wipe. Snapshots your data first (printed path), then removes the
# app data dir AND the settings registry key.
function Cmd-Reset {
    if (-not (Confirm-Action "Wipe EVERYTHING to a clean install? ('repair' fixes problems without losing data)")) {
        Write-Host "Aborted - nothing changed."; return
    }
    Write-Host "=== Stopping app ==="
    Stop-App
    $snap = Join-Path $BackupDir ("pre-reset-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Write-Host "=== Snapshotting your data first ==="
    Snapshot-Data $snap
    # Safety gate: if there was data to snapshot but the snapshot came up empty
    # (e.g. files still locked), do NOT proceed to wipe.
    if ((Test-Path (Join-Path $DataDir 'users')) -and -not (Test-Path (Join-Path $snap 'data\users'))) {
        Fail "Snapshot looks incomplete (data\users not captured) - aborting before wipe. Is the app still running?"
    }
    Write-Host "=== Wiping app data + settings (factory reset) ==="
    Remove-Item $Root -Recurse -Force -ErrorAction SilentlyContinue
    # Remove the whole org key (matches the snapshot export and Linux's wholesale
    # config wipe); $RegApp is nested under it.
    Remove-Item $RegOrg -Recurse -Force -ErrorAction SilentlyContinue

    Write-Host "`nClean slate. Next launch reprovisions from scratch (~5-15 min)."
    Write-Host "Your data was snapshotted to:`n    $snap"
    Write-Host "  To recover after relaunching: stop the app, then"
    Write-Host "    Copy-Item `"$snap\data\*`"       `"$DataDir`" -Recurse -Force"
    Write-Host "    Copy-Item `"$snap\workspaces\*`" `"$Workspaces`" -Recurse -Force"
    Write-Host "    reg import `"$snap\settings.reg`"      # restores settings + API keys"
    Write-Host "Relaunch: .\finterm.ps1"
}

# ── Command: stop / status ─────────────────────────────────────────────────────
function Cmd-Stop {
    $p = Get-Process FinceptTerminal -ErrorAction SilentlyContinue
    if ($p) { $p | Stop-Process -Force; Write-Host "Stopped FinceptTerminal." }
    else    { Write-Host "Nothing to stop - FinceptTerminal is not running." }
}

function Cmd-Status {
    $p = Get-Process FinceptTerminal -ErrorAction SilentlyContinue
    if ($p) { Write-Host ("* FinceptTerminal: running (PID " + ($p.Id -join ', ') + ")") }
    else    { Write-Host "o FinceptTerminal: not running" }
}

# ── Command: install / uninstall (Start Menu shortcut) ─────────────────────────
function Cmd-Install {
    if (-not (Test-Path $Bin)) { Fail "Binary not found at $Bin - run '.\finterm.ps1 setup' first." }
    $startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
    $lnk = Join-Path $startMenu 'finterm.lnk'
    $wsh = New-Object -ComObject WScript.Shell
    $sc = $wsh.CreateShortcut($lnk)
    $sc.TargetPath       = $Bin
    $sc.WorkingDirectory = $BuildDir
    # Windows shortcuts take an icon from an .exe/.dll/.ico resource (not a PNG),
    # so use the executable's own embedded icon.
    $sc.IconLocation     = "$Bin,0"
    $sc.Description      = 'finterm (Qt)'
    $sc.Save()
    Write-Host "Installed Start Menu shortcut: $lnk"
}

function Cmd-Uninstall {
    $lnk = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\finterm.lnk'
    if (Test-Path $lnk) { Remove-Item $lnk -Force; Write-Host "Removed: $lnk" }
    else { Write-Host "Nothing to remove - $lnk not present." }
}

# ── Help ───────────────────────────────────────────────────────────────────────
function Cmd-Help {
@"
finterm (Windows) - local Fincept Terminal management

USAGE
    .\finterm.ps1 <command> [flags]

    Just run it - first launch sets everything up automatically:
        .\finterm.ps1                      Launch (sets up on first run)

  RUN
    .\finterm.ps1            (or start)    Launch the app
    .\finterm.ps1 stop                     Stop it
    .\finterm.ps1 status                   Show what's running

  FIX
    .\finterm.ps1 repair                   Fix a misbehaving app - keeps your data
    .\finterm.ps1 reset                    Wipe everything to a clean install

  SET UP
    .\finterm.ps1 setup                    Install deps + Qt and build
    .\finterm.ps1 build [--clean]          Rebuild after code changes (dev)

  DESKTOP
    .\finterm.ps1 install                  Add a Start Menu shortcut
    .\finterm.ps1 uninstall                Remove the shortcut

    .\finterm.ps1 help                     Show this message

Settings live in the registry (HKCU\Software\Fincept); app data under
%LOCALAPPDATA%\com.fincept.terminal. `reset`/`repair` snapshot your data to
$BackupDir first.
"@ | Write-Host
}

# ── Dispatch ────────────────────────────────────────────────────────────────────
switch ($Command.ToLower().TrimStart('-')) {
    'setup'     { Cmd-Setup }
    'start'     { Cmd-Start }
    'build'     { Cmd-Build }
    'repair'    { Cmd-Repair }
    'reset'     { Cmd-Reset }
    'stop'      { Cmd-Stop }
    'status'    { Cmd-Status }
    'install'   { Cmd-Install }
    'uninstall' { Cmd-Uninstall }
    { $_ -in 'help','h','?' } { Cmd-Help }
    default {
        Write-Host "unknown command: $Command`n" -ForegroundColor Red
        Cmd-Help
        exit 2
    }
}

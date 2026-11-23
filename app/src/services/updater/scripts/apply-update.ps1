# apply-update.ps1 — finterm's in-place updater for Windows (Qt Installer
# Framework install). The app writes this script out (it is embedded in the
# binary) and starts it detached when it quits with a verified installer staged:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File apply-update.ps1
#       -AppPid PID -Installer SETUP.EXE -InstallDir DIR -Previous DIR
#       -Status FILE -From VER -To VER [-Relaunch] [-Rollback] [-AppProfile NAME] [-Lock FILE]
#
# Update: waits for PID to exit (120 s; past that nothing changes), copies the
# install to PREVIOUS (for a rollback), removes the old install with its own
# maintenance tool ("purge") and installs the new version into the same
# folder, unattended. If the install fails, PREVIOUS is copied back.
# Rollback (-Rollback): PREVIOUS is copied back over the install.
# A one-line JSON STATUS is written for the next launch to report; -Relaunch
# starts finterm.exe again. The user's data lives elsewhere (AppData) and is
# never touched.

param(
    [int]$AppPid = 0,
    [string]$Installer = "",
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [Parameter(Mandatory = $true)][string]$Previous,
    [string]$Status = "",
    [string]$From = "",
    [string]$To = "",
    [switch]$Relaunch,
    [switch]$Rollback,
    [string]$AppProfile = "",
    [string]$Lock = "" # the app's record of this helper; removed when done, before any relaunch
)

$ErrorActionPreference = "Stop"
$Kind = if ($Rollback) { "ifw-rollback" } else { "ifw" }
$Exe = Join-Path $InstallDir "finterm.exe"
$Tool = Join-Path $InstallDir "FintermMaintenanceTool.exe"

function Write-Status([bool]$ok, [string]$message) {
    if ($Lock) { Remove-Item -Force $Lock -ErrorAction SilentlyContinue }
    if (-not $Status) { return }
    $dir = Split-Path -Parent $Status
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $obj = [ordered]@{ ok = $ok; kind = $Kind; from = $From; to = $To; profile = $AppProfile; message = $message;
                       at = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ") }
    # UTF-8 without a BOM: Windows PowerShell's "UTF8" writes one, and the
    # app's JSON reader rejects it.
    [System.IO.File]::WriteAllText("$Status.tmp", ($obj | ConvertTo-Json -Compress),
                                   (New-Object System.Text.UTF8Encoding $false))
    Move-Item -Force "$Status.tmp" $Status
}

function Start-App {
    if (-not ($Relaunch -and (Test-Path $Exe))) { return }
    if ($AppProfile) { Start-Process -FilePath $Exe -WorkingDirectory $InstallDir -ArgumentList @("--profile", $AppProfile) }
    else { Start-Process -FilePath $Exe -WorkingDirectory $InstallDir }
}

function Copy-Tree([string]$src, [string]$dst) {
    # /MIR mirrors (deletes extras); exit codes below 8 are success for robocopy.
    # By full path: PATH need not have System32.
    & (Join-Path $env:SystemRoot "System32\robocopy.exe") $src $dst /MIR /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $src -> $dst failed ($LASTEXITCODE)" }
}

# ── Wait for the app to be gone (it holds the files we replace) ─────────────
if ($AppPid -gt 0) {
    $p = Get-Process -Id $AppPid -ErrorAction SilentlyContinue
    if ($p -and -not $p.WaitForExit(120000)) {
        Write-Status $false "finterm (pid $AppPid) did not exit; nothing was changed"
        exit 1
    }
}

# … and for anything else running from the install (a finterm started in the
# meantime leaves by itself).
$prefix = $InstallDir.TrimEnd('\') + '\'
$deadline = (Get-Date).AddSeconds(120)
while (Get-Process -ErrorAction SilentlyContinue |
       Where-Object { $_.Path -and $_.Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) }) {
    if ((Get-Date) -gt $deadline) {
        Write-Status $false "a program is still running from $InstallDir; nothing was changed"
        Start-App # finterm itself has gone: bring it back
        exit 1
    }
    Start-Sleep -Milliseconds 500
}

$kept = $false   # the whole install is in PREVIOUS
$touched = $false # the install folder has been changed (purge started)
try {
    if ($Rollback) {
        if (-not (Test-Path $Previous)) { throw "no previous version to restore" }
        Copy-Tree $Previous $InstallDir
        Write-Status $true "restored"
        Start-App
        exit 0
    }

    if (-not (Test-Path $Installer)) { throw "the staged installer is missing: $Installer" }
    if (-not (Test-Path $Tool)) { throw "the maintenance tool is missing: $Tool" }

    # Keep the running version for a rollback.
    Copy-Tree $InstallDir $Previous
    $kept = $true

    # The installer refuses a folder that already holds an installation:
    # remove it with its own tool, then install the new version there.
    $touched = $true
    # --default-answer: any question takes its default (a command-line purge
    # does not run the uninstaller's question about the user's data at all).
    # -Wait also waits for the script the tool leaves to delete itself.
    $purge = Start-Process -FilePath $Tool -ArgumentList @("purge", "--default-answer", "--confirm-command") -Wait -PassThru -WindowStyle Hidden
    if ($purge.ExitCode -ne 0) { throw "removing the old version failed ($($purge.ExitCode))" }
    # What the tool leaves (files the app wrote beside itself, e.g. Python
    # caches) would make the installer refuse a non-empty folder. The whole
    # install is in PREVIOUS by now.
    for ($i = 0; (Test-Path $Tool) -and $i -lt 60; $i++) { Start-Sleep -Seconds 1 }
    # The contents, not the folder: a program may have it as its working
    # directory, and the installer takes an empty one.
    for ($i = 0; Test-Path $InstallDir; $i++) {
        try { Get-ChildItem -LiteralPath $InstallDir -Force | Remove-Item -Recurse -Force; break }
        catch { if ($i -ge 10) { throw }; Start-Sleep -Seconds 1 }
    }

    # The updater's own install is never "another finterm" (installscript.qs).
    $env:FINTERM_INSTALL_UPDATE = "1"
    $install = Start-Process -FilePath $Installer -Wait -PassThru -WindowStyle Hidden -ArgumentList @(
        "install", "--root", "`"$InstallDir`"", "--accept-licenses", "--default-answer", "--confirm-command")
    $env:FINTERM_INSTALL_UPDATE = $null # not for the finterm started next
    if ($install.ExitCode -ne 0 -or -not (Test-Path $Exe)) {
        throw "installing $To failed ($($install.ExitCode))"
    }

    Remove-Item -Force $Installer -ErrorAction SilentlyContinue
    Write-Status $true "applied"
    Start-App
    exit 0
}
catch {
    $env:FINTERM_INSTALL_UPDATE = $null
    $why = $_.Exception.Message
    # Whatever failed once the old install was touched (a locked file, the
    # tool, the installer), the previous version goes back: never leave the
    # user without a working finterm.
    if (-not $Rollback -and $touched -and $kept) {
        try {
            Copy-Tree $Previous $InstallDir
            $why = "$why; the previous version was restored"
        }
        catch {
            $why = "$why; restoring the previous version also failed: $($_.Exception.Message) (it is kept in $Previous)"
        }
    }
    Write-Status $false $why
    Start-App
    exit 1
}

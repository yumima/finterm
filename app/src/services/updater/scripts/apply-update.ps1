# apply-update.ps1 — finterm's in-place updater for Windows (an installer's
# install, per user). The app writes this script out (it is embedded in the
# binary) and starts it detached when it quits with a verified update staged:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File apply-update.ps1
#       -AppPid PID -Package UPDATE.ZIP -InstallDir DIR -Previous DIR
#       -Status FILE -From VER -To VER [-Relaunch] [-AppProfile NAME] [-Lock FILE]
#       [-Rollback]
#
# Update: the new version is unpacked beside the install (DIR.new), complete
# with the installer's own files (its uninstaller keeps working), and the
# folders change places by two renames: DIR -> PREVIOUS, DIR.new -> DIR. Until
# then nothing of the install is touched, so being cut short (logoff, power,
# a full disk) costs nothing; shortcuts and the Apps entry stay valid. Cut
# short between the two, the next logon renames PREVIOUS back (and does
# nothing at all when DIR is there).
# Rollback (-Rollback): PREVIOUS changes places with DIR.
#
# It waits for PID, and for anything running from DIR, to exit (120 s each;
# past that it changes nothing), writes a one-line JSON STATUS for the next
# launch to report, and with -Relaunch starts finterm.exe again. The user's
# data lives elsewhere (%LOCALAPPDATA%\finterm) and is never touched.

param(
    [int]$AppPid = 0,
    [string]$Package = "",
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
$InstallDir = $InstallDir.TrimEnd('\')
$Previous = $Previous.TrimEnd('\')
$Exe = Join-Path $InstallDir "finterm.exe"
$New = "$InstallDir.new"
# What the installer keeps beside the app; the update package has none of it.
$InstallerFiles = @("FintermMaintenanceTool*", "components.xml", "network.xml", "InstallationLog.txt",
                    "installer.dat", "installerResources", "Licenses")

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
    if (-not ($Relaunch -and (Test-Path -LiteralPath $Exe))) { return }
    # Not from the install: what finterm opens would keep the folder in use.
    if ($AppProfile) { Start-Process -FilePath $Exe -WorkingDirectory $env:USERPROFILE -ArgumentList @("--profile", $AppProfile) }
    else { Start-Process -FilePath $Exe -WorkingDirectory $env:USERPROFILE }
}

function Remove-Tree([string]$dir) {
    for ($i = 0; Test-Path -LiteralPath $dir; $i++) {
        try { Remove-Item -LiteralPath $dir -Recurse -Force }
        catch { if ($i -ge 10) { throw }; Start-Sleep -Milliseconds 500 }
    }
}

# A folder name that is free: $base, or another when a leftover won't go.
function Get-Fresh([string]$base) {
    try { Remove-Tree $base; return $base }
    catch { return "$base-" + (Get-Date).ToString("yyyyMMddHHmmss") }
}

# One folder rename, tried for 15 s (a virus scanner may have a file open). It
# happens whole or not at all.
function Rename-Dir([string]$src, [string]$dst) {
    for ($i = 0; ; $i++) {
        try { [System.IO.Directory]::Move($src, $dst); return }
        catch { if ($i -ge 30) { throw }; Start-Sleep -Milliseconds 500 }
    }
}

# The moment between the two renames: should this be cut short there, the next
# logon renames PREVIOUS back. Only if DIR is missing, and best effort (a
# managed PC may refuse it; a RunOnce value is at most 260 characters).
$runOnce = "HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce"
function Set-Recovery([string]$from) {
    try {
        $line = "cmd.exe /d /c if not exist `"$InstallDir`" move `"$from`" `"$InstallDir`""
        if ($line.Length -gt 260) { return }
        if (-not (Test-Path $runOnce)) { New-Item -Path $runOnce -Force | Out-Null }
        Set-ItemProperty -Path $runOnce -Name "finterm-update" -Value $line
    }
    catch {}
}
function Clear-Recovery { Remove-ItemProperty -Path $runOnce -Name "finterm-update" -ErrorAction SilentlyContinue }

# DIR -> $aside, $in -> DIR; on any failure DIR is as it was. $keep is the
# version a logon would put back.
function Switch-Install([string]$in, [string]$aside, [string]$keep) {
    Set-Recovery $keep
    try { Rename-Dir $InstallDir $aside }
    catch {
        Clear-Recovery
        throw "$InstallDir is in use (a program open in it?): $($_.Exception.Message)"
    }
    try { Rename-Dir $in $InstallDir }
    catch {
        $why = $_.Exception.Message
        for ($i = 0; -not (Test-Path -LiteralPath $InstallDir) -and $i -lt 4; $i++) {
            try { Rename-Dir $aside $InstallDir } catch {}
        }
        # (Still missing: the logon task stays.)
        if (Test-Path -LiteralPath $InstallDir) { Clear-Recovery }
        throw $why
    }
    Clear-Recovery
}

function Set-AppsVersion([string]$version) {
    $keys = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*"
    Get-ItemProperty $keys -ErrorAction SilentlyContinue |
        Where-Object { $_.InstallLocation -and $_.InstallLocation.Replace('/', '\').TrimEnd('\') -ieq $InstallDir } |
        ForEach-Object { Set-ItemProperty -LiteralPath $_.PSPath -Name DisplayVersion -Value $version }
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
$prefixes = @(($InstallDir + '\'), ($Previous + '\'))
$deadline = (Get-Date).AddSeconds(120)
while (Get-Process -ErrorAction SilentlyContinue |
       Where-Object { $path = $_.Path
                      $path -and ($prefixes | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }) }) {
    if ((Get-Date) -gt $deadline) {
        Write-Status $false "a program is still running from $InstallDir (or the previous version); nothing was changed"
        Start-App # finterm itself has gone: bring it back
        exit 1
    }
    Start-Sleep -Milliseconds 500
}

$done = "applied"
try {
    if ($Rollback) {
        if (-not (Test-Path -LiteralPath (Join-Path $Previous "finterm.exe"))) { throw "no previous version to restore" }
        $failed = Get-Fresh "$InstallDir.failed"
        Switch-Install $Previous $failed $Previous
        $done = "restored"
        try { Remove-Tree $failed } catch {}
        try { Set-AppsVersion $To } catch {}
    }
    else {
        if (-not (Test-Path -LiteralPath $Package)) { throw "the staged update is missing: $Package" }
        if (-not (Test-Path -LiteralPath $Exe)) { throw "the install to replace is missing: $InstallDir" }
        $New = Get-Fresh $New
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($Package, $New)
        if (-not (Test-Path -LiteralPath (Join-Path $New "finterm.exe"))) { throw "the update package holds no finterm" }
        Get-ChildItem -LiteralPath $InstallDir -Force |
            Where-Object { $n = $_.Name; $InstallerFiles | Where-Object { $n -like $_ } } |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $New -Recurse -Force }
        if (-not (Test-Path -LiteralPath (Join-Path $New "FintermMaintenanceTool.exe"))) { throw "the uninstaller could not be copied" }
        # On the disk before the renames are: what the app cannot start without.
        # For a power cut's sake only — a file a scanner has open is left.
        Get-ChildItem -LiteralPath $New -Recurse -File | Where-Object { $_.Extension -in ".exe", ".dll" } | ForEach-Object {
            try {
                $f = [System.IO.File]::Open($_.FullName, "Open", "ReadWrite", "ReadWrite, Delete")
                try { $f.Flush($true) } finally { $f.Close() }
            }
            catch {}
        }
        # It can be renamed (a scanner is done with it) before the install is.
        $ready = Get-Fresh "$InstallDir.ready"
        Rename-Dir $New $ready
        $New = $ready
        # The older previous version goes only once the new one is in.
        $older = Get-Fresh "$Previous.old"
        if (Test-Path -LiteralPath $Previous) {
            try { Rename-Dir $Previous $older }
            catch { throw "$Previous is in use (a program open in it?): $($_.Exception.Message)" }
        }
        try { Switch-Install $New $Previous $Previous }
        catch {
            if ((Test-Path -LiteralPath $older) -and -not (Test-Path -LiteralPath $Previous)) { Rename-Dir $older $Previous }
            throw
        }
        # Done: nothing after this may undo it.
        try { Remove-Item -Force -LiteralPath $Package } catch {}
        try { Remove-Tree $older } catch {}
        try { Set-AppsVersion $To } catch {}
    }
}
catch {
    $why = $_.Exception.Message
    # (Kept while there is no install: it is a whole version.)
    if (-not $Rollback -and (Test-Path -LiteralPath $Exe)) { try { Remove-Tree $New } catch {} }
    try { Write-Status $false $why } catch {}
    Start-App
    exit 1
}
try { Write-Status $true $done } catch {}
Start-App
exit 0

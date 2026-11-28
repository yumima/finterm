# The installer run as users run it — its output always shown (verbose; IFW's
# -v is --version).
#   Setup <installer> <what> [extra]: the command line, unattended
#     (`UpdateExisting=true` to update an installed finterm).
#   GuiSetup <installer> <what>: the wizard, clicked through by
#     ifw-gui-clicks.qs appended to the real control script (--script
#     replaces the embedded one). Ten minutes at most; must reach the end.

function Show-SetupOutput([string]$what, [string]$code, [string]$out, [string]$err) {
    Write-Host "── $what (exit $code) ──"
    foreach ($f in $out, $err) {
        Write-Host "· $(Split-Path -Leaf $f):"; Get-Content $f -ErrorAction SilentlyContinue | Write-Host
    }
}

function Setup([string]$file, [string]$what, [string[]]$extra = @()) {
    Get-Process -Name finterm, FintermMaintenanceTool -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "running before ${what}: $($_.Name) $($_.Id) $($_.Path)" }
    $out = Join-Path $env:RUNNER_TEMP "setup-out.txt"; $err = Join-Path $env:RUNNER_TEMP "setup-err.txt"
    $p = Start-Process -FilePath $file -Wait -PassThru -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList (@("--verbose", "install") + $extra + @("--accept-licenses", "--default-answer", "--confirm-command"))
    Show-SetupOutput $what $p.ExitCode $out $err
    if ($p.ExitCode -ne 0) { throw "$what failed ($($p.ExitCode))" }
}

function GuiSetup([string]$file, [string]$what) {
    $qs = Join-Path $env:RUNNER_TEMP "ci-control.qs"
    Get-Content (Join-Path $PSScriptRoot "../../app/packaging/installer/config/controlscript.qs"),
                (Join-Path $PSScriptRoot "ifw-gui-clicks.qs") | Set-Content -Encoding utf8 $qs
    $out = Join-Path $env:RUNNER_TEMP "gui-out.txt"; $err = Join-Path $env:RUNNER_TEMP "gui-err.txt"
    $p = Start-Process -FilePath $file -PassThru -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList @("--verbose", "--script", "`"$qs`"")
    $null = $p.Handle # else ExitCode is empty after a timed WaitForExit
    $done = $p.WaitForExit(600000)
    if (-not $done) { $p.Kill($true); $null = $p.WaitForExit(30000) }
    Show-SetupOutput $what $(if ($done) { $p.ExitCode } else { "killed after 10 min" }) $out $err
    if (-not $done) { throw "$what did not finish in 10 minutes" }
    if ($p.ExitCode -ne 0) { throw "$what failed ($($p.ExitCode))" }
    if (-not (Select-String -Path $out, $err -Pattern "\[ci\] finished" -Quiet)) { throw "$what did not reach the end" }
}

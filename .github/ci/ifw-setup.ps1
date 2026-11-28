# Setup <installer> <what>: the installer on the command line, as an
# unattended install or update runs it (installing over an install updates
# it: app/packaging/installer/config/controlscript.qs). Verbose (IFW's -v is
# --version); what it said is always shown; a non-zero exit fails.
function Setup([string]$file, [string]$what) {
    Get-Process -Name finterm, FintermMaintenanceTool -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "running before ${what}: $($_.Name) $($_.Id) $($_.Path)" }
    $out = Join-Path $env:RUNNER_TEMP "setup-out.txt"; $err = Join-Path $env:RUNNER_TEMP "setup-err.txt"
    $p = Start-Process -FilePath $file -Wait -PassThru -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList @("--verbose", "install", "--accept-licenses", "--default-answer", "--confirm-command")
    Write-Host "── $what (exit $($p.ExitCode)) ──"
    foreach ($f in $out, $err) {
        Write-Host "· $(Split-Path -Leaf $f):"; Get-Content $f -ErrorAction SilentlyContinue | Write-Host
    }
    if ($p.ExitCode -ne 0) { throw "$what failed ($($p.ExitCode))" }
}

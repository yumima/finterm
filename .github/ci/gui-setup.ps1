# GuiSetup <installer> <what>: the installer's GUI clicked through by
# ifw-gui-install.qs — how a user updates over an install. Ten minutes at
# most (then the whole process tree is killed); its output is shown; fails
# on a non-zero exit, a timeout, or a "[ci] refused:" line (with its reason).
function GuiSetup([string]$file, [string]$what) {
    $qs = (Resolve-Path (Join-Path $PSScriptRoot "ifw-gui-install.qs")).Path
    $out = Join-Path $env:RUNNER_TEMP "gui-out.txt"; $err = Join-Path $env:RUNNER_TEMP "gui-err.txt"
    $p = Start-Process -FilePath $file -PassThru -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList @("--verbose", "--script", "`"$qs`"")
    $null = $p.Handle # else ExitCode is empty after a timed WaitForExit
    $done = $p.WaitForExit(600000)
    if (-not $done) { $p.Kill($true) }
    Write-Host "── $what (exit $(if ($done) { $p.ExitCode } else { 'killed after 10 min' })) ──"
    foreach ($f in $out, $err) {
        Write-Host "· $(Split-Path -Leaf $f):"; Get-Content $f -ErrorAction SilentlyContinue | Write-Host
    }
    $refused = Select-String -Path $out, $err -Pattern "\[ci\] refused: (.*)" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($refused) { throw "$what refused: $($refused.Matches[0].Groups[1].Value)" }
    if (-not $done) { throw "$what did not finish in 10 minutes" }
    if ($p.ExitCode -ne 0) { throw "$what failed ($($p.ExitCode))" }
    if (-not (Select-String -Path $out, $err -Pattern "\[ci\] finished" -Quiet -ErrorAction SilentlyContinue)) {
        throw "$what exited without reaching the finished page"
    }
}

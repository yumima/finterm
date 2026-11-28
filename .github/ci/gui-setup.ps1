# GuiSetup <installer> <what>: the installer's GUI clicked through by
# ifw-gui-install.qs — how a user updates over an install. Its verbose log is
# watched: the first line that would stop a user (IFW's target-folder
# messages, or the script's "[ci] refused:") ends it at once, with that line
# as the reason. Ten minutes at most; fails on a non-zero exit or when the
# finished page was never reached.
function GuiSetup([string]$file, [string]$what) {
    $qs = (Resolve-Path (Join-Path $PSScriptRoot "ifw-gui-install.qs")).Path
    $out = Join-Path $env:RUNNER_TEMP "gui-out.txt"; $err = Join-Path $env:RUNNER_TEMP "gui-err.txt"
    $p = Start-Process -FilePath $file -PassThru -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList @("--verbose", "--script", "`"$qs`"")
    $null = $p.Handle # else ExitCode is empty after a timed WaitForExit
    $stop = "TargetDirectoryInUse|OverwriteTargetDirectory|\[ci\] refused: "
    $reason = $null
    for ($t = 0; -not $p.HasExited -and $t -lt 300; $t++) {
        Start-Sleep -Seconds 2
        $hit = Get-Content $out, $err -ErrorAction SilentlyContinue | Select-String -Pattern $stop | Select-Object -First 1
        if ($hit) { $reason = $hit.Line.Trim(); break }
    }
    if (-not $p.HasExited) {
        $p.Kill($true); $null = $p.WaitForExit(30000)
        if (-not $reason) { $reason = "did not finish in 10 minutes" }
    }
    Write-Host "── $what (exit $(if ($p.HasExited) { $p.ExitCode } else { '?' })) ──"
    foreach ($f in $out, $err) {
        Write-Host "· $(Split-Path -Leaf $f):"; Get-Content $f -ErrorAction SilentlyContinue | Write-Host
    }
    if (-not $reason) {
        $hit = Get-Content $out, $err -ErrorAction SilentlyContinue | Select-String -Pattern $stop | Select-Object -First 1
        if ($hit) { $reason = $hit.Line.Trim() }
    }
    if ($reason) { throw "${what}: $reason" }
    if ($p.ExitCode -ne 0) { throw "$what failed ($($p.ExitCode))" }
    if (-not (Select-String -Path $out, $err -Pattern "\[ci\] finished" -Quiet -ErrorAction SilentlyContinue)) {
        throw "$what exited without reaching the finished page"
    }
}

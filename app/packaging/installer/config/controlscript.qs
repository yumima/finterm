// controlscript.qs — finterm's QtIFW control script (embedded in the
// installer: CPACK_IFW_PACKAGE_CONTROL_SCRIPT).
//
// Windows: running the installer over an installed finterm updates it — in
// the wizard when the user agrees, on the command line with UpdateExisting=
// true. The installed copy (and an older one in Program Files, 4.0.10 and
// before) is removed by its own uninstaller, which keeps the user's data (its
// question defaults to No; --default-answer takes it), then this one installs.
//
// Why here, and how (Qt IFW 4.7/4.8 source):
//   - IFW refuses a folder that holds its maintenance tool
//     (installationAllowedToDirectory: TargetDirectoryInUse, whatever the
//     answer). The command line checks it right after loading this script and
//     before any component script; the GUI on the target page.
//   - GUI: the update runs from the introduction page (the wizard is up, and
//     the user is asked).
//   - Command line: this script is loaded for every command (install, search,
//     create-offline, …) and is not told which, so it removes nothing unless
//     asked: `setup.exe install UpdateExisting=true …` (a key=value argument
//     IFW sets as a value before this script loads). A custom folder is
//     given the same way, TargetDir=<folder>: --root is applied only after
//     this script has run, and the install takes the TargetDir value too.
//     Without UpdateExisting nothing is
//     removed and an installed finterm stops the install: in the target folder
//     by IFW's own check ("contains an installation"), elsewhere — an old
//     Program Files copy — by the refusal below.
//   - An uninstaller removes itself after it exits, from %TEMP%\uninstall.vbs:
//     it deletes the maintenance tool, then the whole target folder
//     (RemoveTargetDir, on: CMakeLists.txt). Installing before that is done
//     would have new files deleted under it — so the update waits until the
//     folder is gone.
//   - Stopping: IFW resets the status when it fetches the packages
//     (fetchRemotePackagesTree: Running), so an interrupt from here does not
//     stop a command-line install. A refusal is recorded instead —
//     FintermInstallRefused — and installscript.qs's createOperations throws
//     on it, which ends the install either way.
//
// Elsewhere (Linux, macOS) this script does nothing.

var updateDone = false;

function Controller()
{
    if (systemInfo.kernelType !== "winnt" || !installer.isInstaller())
        return;
    if (!installer.isCommandLineInstance())
        return; // the GUI: from the introduction page
    if (/^(1|true|yes)$/i.test(String(installer.value("UpdateExisting") || "")))
        updateExisting();
    else
        refuseOverExisting();
}

Controller.prototype.IntroductionPageCallback = function()
{
    if (systemInfo.kernelType === "winnt" && installer.isInstaller() && !updateDone)
        updateExisting();
};

// ---------------------------------------------------------------------------
// Windows' own tools, by full path — never whatever PATH finds first: a broken
// PATH made every call fail without a word (finterm "not running", the wait
// over at once), and a planted copy earlier on it would run instead.
// (Kept identical in installscript.qs; test_installscript.js checks.)
// ---------------------------------------------------------------------------

function winTool(name)
{
    var root = (installer.environmentVariable("SystemRoot") || installer.environmentVariable("windir") ||
                "C:\\Windows").replace(/[\\\/]+$/, "");
    return root + (name === "powershell.exe" ? "\\System32\\WindowsPowerShell\\v1.0\\" : "\\System32\\") + name;
}

// [stdout, exit code], or null when the program could not be started — a
// failure to report, never an answer.
function run(program, args)
{
    var r = installer.execute(program, args);
    if (!r || r.length === 0) {
        console.log("[finterm] could not start " + program);
        return null;
    }
    return [String(r[0]), r.length > 1 ? Number(r[1]) : 0];
}

function toWin(path)
{
    return String(path).replace(/\//g, "\\");
}

function psQuote(s)
{
    return "'" + String(s).replace(/'/g, "''") + "'";
}

// ---------------------------------------------------------------------------

function powershell(command)
{
    return run(winTool("powershell.exe"), ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command]);
}

// Remove the installation in `dir` by its own uninstaller — only if the folder
// can be removed now: Windows won't rename a folder something runs from
// (finterm, an uninstaller window left open — anyone's, elevated or not), so
// it is renamed aside and back first. Grace for a program still closing: two
// seconds at the wizard's first try (the user is asked, and it tries again),
// ten after that and on the command line (it can't ask); ten for the move
// back, which must not fail.
// One PowerShell script; for Program Files it runs elevated as a whole (the
// rename needs it too).
//
// Processes start through [Diagnostics.Process]::Start: it keeps the handle
// from the start (WaitForExit and ExitCode hold however fast the program
// ends), waits for that process only (Start-Process -Wait would also wait
// for the uninstaller's uninstall.vbs, unbounded — the folder wait after it
// is bounded), and a declined elevation prompt is its Win32Exception 1223.
// Exit codes, kept clear of the uninstaller's (IFW's 0..6):
var removeCodes = { purgeFailed: 101, inUse: 103, notBack: 104, toolNotStarted: 105, checkInTheWay: 106,
                    notPermitted: 107, notElevated: 109, restricted: 110 };

// PowerShell: start `file` with `args` (one string), `verb` ("" or "runas"),
// wait for it; $code = its exit code, or $failed = the start's Win32 error.
function psStart(file, args, verb)
{
    return "$code = $null; $failed = $null; try { $si = New-Object Diagnostics.ProcessStartInfo; " +
           "$si.FileName = " + file + "; $si.Arguments = " + args + "; $si.UseShellExecute = $true; " +
           (verb ? "$si.Verb = '" + verb + "'; " : "") +
           "$pr = [Diagnostics.Process]::Start($si); $pr.WaitForExit(); $code = $pr.ExitCode } " +
           "catch [ComponentModel.Win32Exception] { $failed = $_.Exception.NativeErrorCode } " +
           "catch { $failed = -1 }; ";
}

// `grace`: the rename test's seconds (see above).
function removeInstall(dir, grace)
{
    var d = toWin(dir), tool = toWin(dir + "/FintermMaintenanceTool.exe"), c = removeCodes;
    var tryMove = function(from, to, tries) {
        return "$ok = $false; for ($i = 0; $i -lt " + tries + " -and -not $ok; $i++) { try { [IO.Directory]::Move(" +
               from + ", " + to + "); $ok = $true } catch { Start-Sleep -Milliseconds 250 } }; ";
    };
    // A PowerShell restricted by policy (Constrained Language) blocks the
    // .NET calls below: said as such, rather than as "in use".
    var script = "if ($ExecutionContext.SessionState.LanguageMode -ne 'FullLanguage') { exit " + c.restricted + " }; " +
        "$d = " + psQuote(d) + "; $t = $d + '.finterm-check'; " +
        "if (Test-Path -LiteralPath $t) { exit " + c.checkInTheWay + " }; " +
        tryMove("$d", "$t", grace * 4) + "if (-not $ok) { exit " + c.inUse + " }; " +
        tryMove("$t", "$d", 40) + "if (-not $ok) { exit " + c.notBack + " }; " +
        psStart(psQuote(tool), "'purge --default-answer --confirm-command'", "") +
        "if ($failed -ne $null) { exit " + c.toolNotStarted + " }; if ($code -ne 0) { exit " + c.purgeFailed + " }; " +
        "exit 0";
    if (!inProgramFiles(dir))
        return powershell(script);
    // Elevated: the old install was made by an administrator. The script
    // has no double quotes, so it travels as one -Command "…" argument.
    return powershell(psStart(psQuote(winTool("powershell.exe")),
                              psQuote("-NoProfile -ExecutionPolicy Bypass -Command \"" + script + "\""), "runas") +
                      "if ($failed -eq 1223) { exit " + c.notPermitted + " }; " +
                      "if ($failed -ne $null) { exit " + c.notElevated + " }; exit $code");
}

// Where a finterm installation can be — the target folder, the per-user one,
// an old Program Files one — each once, with forward slashes.
function installCandidates()
{
    var raw = [installer.value("TargetDir"), installer.environmentVariable("LOCALAPPDATA") + "/Programs/finterm",
               installer.environmentVariable("ProgramFiles") + "/finterm"];
    var out = [], seen = {};
    for (var i = 0; i < raw.length; ++i) {
        var dir = String(raw[i] || "").replace(/\\/g, "/").replace(/\/+$/, "");
        if (dir && !seen[dir.toLowerCase()]) {
            seen[dir.toLowerCase()] = true;
            out.push(dir);
        }
    }
    return out;
}

// Every finterm installation found.
function existingInstalls()
{
    var found = installCandidates().filter(function(dir) {
        return installer.fileExists(dir + "/FintermMaintenanceTool.exe");
    });
    // Program Files first: in use, nothing is touched yet; a per-user copy in
    // use after it leaves a working finterm.
    return found.filter(inProgramFiles).concat(found.filter(function(d) { return !inProgramFiles(d); }));
}

// A copy an interrupted update left renamed aside (<install>.finterm-check):
// to be put back by hand before anything installs.
function strandedCopy()
{
    var found = installCandidates().filter(function(dir) {
        return installer.fileExists(dir + ".finterm-check/FintermMaintenanceTool.exe");
    });
    return found.length > 0 ? found[0] : "";
}

function strandedText(dir)
{
    var admin = inProgramFiles(dir) ? " (as an administrator)" : "";
    if (installer.fileExists(dir + "/FintermMaintenanceTool.exe")) // installed there again since
        return "An interrupted update left an old copy of finterm in " + toWin(dir) + ".finterm-check. Delete it" +
               admin + ", then run this installer again.";
    return "An interrupted update left finterm in " + toWin(dir) + ".finterm-check. Rename it back to " + toWin(dir) +
           admin + ", then run this installer again.";
}

function inProgramFiles(dir)
{
    var pf = String(installer.environmentVariable("ProgramFiles") || "").replace(/\\/g, "/").toLowerCase();
    return pf.length > 0 && dir.toLowerCase().indexOf(pf + "/") === 0;
}

// Nothing installs: recorded (installscript.qs's createOperations throws on
// it), and — when `tell` — said, and the wizard closed.
function refuse(text, id, title, tell)
{
    console.log("[finterm] refused" + (id ? " (" + id + ")" : "") + ": " + text);
    installer.setValue("FintermInstallRefused", text);
    if (!tell)
        return;
    if (id)
        QMessageBox.critical(id, title, text, QMessageBox.Ok);
    if (installer.isCommandLineInstance())
        installer.interrupt();
    else
        gui.rejectWithoutPrompt();
}

function stop(id, title, text)
{
    refuse(text, id, title, true);
}

function cannotCheck(what)
{
    stop("finterm.install.cannot_check", "finterm could not be installed",
         "The installer could not " + what + " (a Windows tool in " +
         toWin(installer.environmentVariable("SystemRoot") || "C:\\Windows") + " did not start or failed).");
}

// The command line without UpdateExisting: nothing is removed, and nothing
// installs beside or over an installed finterm. Recorded, not shown — a search or create-offline loads this
// too; an install stops on it (createOperations: the reason is its error),
// unless IFW's own folder check stopped it first.
function refuseOverExisting()
{
    var stranded = strandedCopy();
    if (stranded)
        return refuse(strandedText(stranded));
    var dirs = existingInstalls();
    if (dirs.length > 0)
        refuse("finterm is already installed in " + toWin(dirs.join(", ")) + ". To update it unattended, add " +
               "UpdateExisting=true: setup.exe install UpdateExisting=true --accept-licenses --default-answer " +
               "--confirm-command");
}

// The update (the GUI, or the command line with UpdateExisting). Returns when
// this install may go ahead; anything else is refused.
function updateExisting()
{
    updateDone = true;
    var stranded = strandedCopy();
    if (stranded)
        return stop("finterm.install.update_failed", "finterm could not be updated", strandedText(stranded));
    var dirs = existingInstalls();
    if (dirs.length === 0)
        return;
    var anyProgramFiles = false, list = "";
    for (var i = 0; i < dirs.length; ++i) {
        anyProgramFiles = anyProgramFiles || inProgramFiles(dirs[i]);
        list += "\n" + toWin(dirs[i]);
    }
    console.log("[finterm] existing installation(s): " + dirs.join(", "));
    var answer = QMessageBox.question(
        "finterm.install.update", "Update finterm",
        "finterm is already installed in:" + list + "\n\n" +
        "This installer replaces it with finterm " + installer.value("ProductVersion") + ": the installed copy " +
        "is removed now, then this one is installed. Your data, settings and saved keys are kept — if you stop " +
        "the installer after this, run it again to install finterm." +
        (anyProgramFiles ? "\n\nA copy in Program Files is removed with Windows' permission, and finterm then " +
                           "installs for your user only." : ""),
        QMessageBox.Yes | QMessageBox.No, QMessageBox.Yes);
    if (answer !== QMessageBox.Yes)
        return refuse("The update was declined; finterm stays as it was.", "", "", true);

    for (var j = 0; j < dirs.length; ++j) {
        var dir = dirs[j];
        // Its maintenance tool already gone: its uninstaller is removing it
        // (one left open, closed meanwhile) — only wait for that.
        for (var tries = 0; installer.fileExists(dir + "/FintermMaintenanceTool.exe"); ++tries) {
            var r = removeInstall(dir, tries > 0 || installer.isCommandLineInstance() ? 10 : 2);
            var code = r === null ? null : r[1], c = removeCodes, here = toWin(dir);
            console.log("[finterm] removing " + dir + ": " + (code === null ? "could not start" : "exit " + code));
            if (code === 0)
                break;
            if (code === null)
                return cannotCheck("remove the installed copy in " + here);
            var failed = function(why) {
                return stop("finterm.install.update_failed", "finterm could not be updated",
                            "The installed finterm in " + here + " " + why);
            };
            var byHand = ". Uninstall it from Settings > Apps (answer No when asked to remove your data — this " +
                         "version keeps it), then run this installer again.";
            if (code === c.notPermitted)
                return failed("was not removed: Windows' permission was not given" + byHand);
            if (code === c.restricted)
                return failed("was not removed: PowerShell is restricted on this computer (Constrained Language " +
                              "mode)" + byHand);
            if (code === c.notElevated)
                return failed("was not removed: it could not be run with Windows' permission" + byHand);
            if (code === c.toolNotStarted)
                return failed("was not removed: its uninstaller did not start" + byHand);
            if (code === c.checkInTheWay)
                return failed("was not removed: " + here + ".finterm-check, left by an interrupted update, is in " +
                              "the way. Delete it, then run this installer again.");
            if (code === c.notBack)
                return stop("finterm.install.update_failed", "finterm could not be updated", strandedText(dir));
            if (code === c.purgeFailed)
                return failed("could not be removed (its uninstaller failed)" + byHand);
            if (code !== c.inUse)
                return failed("could not be removed (error " + code + ")" + byHand);
            // In use: finterm, an uninstaller window left open, or another
            // program with a file of it open.
            var inUse = here + " is in use — finterm, a finterm uninstaller window, or another program has it " +
                        "open" + (inProgramFiles(dir) ? " (perhaps another user's)" : "") + " — or it can't be renamed.";
            if (tries >= 2 || installer.isCommandLineInstance())
                return stop("finterm.install.in_use", "finterm is in use",
                            inUse + " Close it, then run this installer again.");
            QMessageBox.warning("finterm.install.in_use_ask", "finterm is in use",
                                inUse + " Close it (every window), then click OK.", QMessageBox.Ok);
            // An uninstaller window closed: its uninstall.vbs takes the tool
            // (retrying each second), then the folder — five seconds for that,
            // so the folder is waited for rather than run into.
            powershell("for ($i = 0; (Test-Path -LiteralPath " + psQuote(toWin(dir + "/FintermMaintenanceTool.exe")) +
                       ") -and $i -lt 20; $i++) { Start-Sleep -Milliseconds 250 }");
        }
        // Until its uninstall.vbs has removed the folder: seconds, normally; a
        // minute at most (the wizard waits with it) — longer means a file is
        // held open, and the message says so.
        var waited = powershell("for ($i = 0; (Test-Path -LiteralPath " + psQuote(toWin(dir)) + ") -and $i -lt 240; " +
                                "$i++) { Start-Sleep -Milliseconds 250 }");
        if (waited === null || waited[1] !== 0)
            return cannotCheck("wait for the installed copy's removal");
        if (installer.fileExists(dir))
            return stop("finterm.install.update_incomplete", "finterm could not be updated",
                        "The installed finterm was uninstalled, but its folder " + toWin(dir) + " is still there " +
                        "(a file in it may be open). Close what uses it, delete the folder, then run this installer " +
                        "again. Your data is kept — it is not in that folder.");
    }
    console.log("[finterm] previous install(s) removed; installing.");
}

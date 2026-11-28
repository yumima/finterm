// controlscript.qs — finterm's QtIFW control script (embedded in the
// installer: CPACK_IFW_PACKAGE_CONTROL_SCRIPT).
//
// Windows: running the installer over an installed finterm UPDATES it — the
// installed copy (and an older one in Program Files, 4.0.10 and before) is
// removed by its own uninstaller, which keeps the user's data (its question
// defaults to No; --default-answer takes it), then this one installs.
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
//     IFW sets as a value before this script loads). Without it nothing is
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

// What runs from those install folders — the names ("" for nothing), or null
// when it could not be checked; waiting up to `seconds` for them to close. By
// each program's own path: another user's per-user finterm runs from their
// own folder and is none of this one's. A process whose path this user can't
// read (another account's, an elevated one) counts only for the shared
// Program Files folder, by its name.
function runningFrom(dirs, seconds)
{
    var prefixes = [], shared = false;
    for (var i = 0; i < dirs.length; ++i) {
        prefixes.push(psQuote(toWin(dirs[i]) + "\\"));
        shared = shared || inProgramFiles(dirs[i]);
    }
    var r = powershell(
        "$dirs = @(" + prefixes.join(",") + "); $shared = $" + (shared ? "true" : "false") + "; " +
        "for ($i = 0; ; $i++) { " +
        "$names = @(Get-Process | Where-Object { $p = $_.Path; " +
        "($p -and @($dirs | Where-Object { $p.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count) -or " +
        "($shared -and -not $p -and $_.ProcessName -match '^(finterm|FintermMaintenanceTool)$') } | " +
        "ForEach-Object { $_.ProcessName } | Sort-Object -Unique); " +
        "if ($names.Count -eq 0 -or $i -ge " + (seconds * 4) + ") { break }; Start-Sleep -Milliseconds 250 }; " +
        "$names -join ','");
    return r === null || r[1] !== 0 ? null : r[0].replace(/\s+/g, "");
}

// Every finterm installation found — the target folder, the per-user one, an
// old Program Files one — each once.
function existingInstalls()
{
    var candidates = [
        installer.value("TargetDir"),
        installer.environmentVariable("LOCALAPPDATA") + "/Programs/finterm",
        installer.environmentVariable("ProgramFiles") + "/finterm"
    ];
    var found = [], seen = {};
    for (var i = 0; i < candidates.length; ++i) {
        var dir = String(candidates[i] || "").replace(/\\/g, "/").replace(/\/+$/, "");
        var key = dir.toLowerCase();
        if (dir && !seen[key] && installer.fileExists(dir + "/FintermMaintenanceTool.exe")) {
            seen[key] = true;
            found.push(dir);
        }
    }
    return found;
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
         toWin(installer.environmentVariable("SystemRoot") || "C:\\Windows") + " did not start).");
}

// The command line without UpdateExisting: nothing is removed, and nothing
// installs beside or over an installed finterm. Recorded, not shown — a search or create-offline loads this
// too; an install stops on it (createOperations: the reason is its error),
// unless IFW's own folder check stopped it first.
function refuseOverExisting()
{
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

    // Nothing may run from a folder that is removed: finterm itself, or an
    // uninstaller still open on its last page (it removes itself when closed —
    // by then the new install's maintenance tool).
    for (var tries = 0;; ++tries) {
        var busy = runningFrom(dirs, tries > 0 ? 10 : 0);
        if (busy === null)
            return cannotCheck("check what runs from " + toWin(dirs.join(", ")));
        if (!busy)
            break;
        var uninstaller = /(^|,)FintermMaintenanceTool(,|$)/i.test(busy);
        var what = uninstaller ? "a finterm uninstaller window" : "finterm";
        if (tries >= 2 || installer.isCommandLineInstance())
            return stop(uninstaller ? "finterm.install.uninstaller_open" : "finterm.install.running",
                        uninstaller ? "Close the finterm uninstaller first" : "finterm is running",
                        "The installed finterm can't be removed while " + what + " is open (perhaps another " +
                        "user's, for a copy in Program Files). Close it, then run this installer again.");
        QMessageBox.warning("finterm.install.running_ask", uninstaller ? "Close the finterm uninstaller" :
                            "finterm is running",
                            "Close " + what + " (every window), then click OK — the installer then waits up to 10 " +
                            "seconds for it to close.", QMessageBox.Ok);
    }

    var args = ["purge", "--default-answer", "--confirm-command"];
    for (var j = 0; j < dirs.length; ++j) {
        var dir = dirs[j];
        var tool = toWin(dir + "/FintermMaintenanceTool.exe");
        var elevated = inProgramFiles(dir);
        var r = elevated
            // Elevated: the old install was made by an administrator.
            ? powershell("$p = Start-Process -FilePath " + psQuote(tool) + " -ArgumentList " +
                         args.map(psQuote).join(",") + " -Verb RunAs -Wait -PassThru; exit $p.ExitCode")
            : run(tool, args);
        console.log("[finterm] removing " + dir + ": " + (r === null ? "could not start" : "exit " + r[1]));
        if (r === null || r[1] !== 0)
            return stop("finterm.install.update_failed", "finterm could not be updated",
                        "The installed finterm in " + toWin(dir) + " could not be removed" +
                        (elevated ? " (Windows' permission is needed for Program Files)" : "") + ". Uninstall it " +
                        "from Settings > Apps (answer No when asked to remove your data — this version keeps it), " +
                        "then run this installer again.");
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

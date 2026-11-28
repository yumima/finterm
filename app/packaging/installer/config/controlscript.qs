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
//     before any component script; the GUI on the target page. So the old copy
//     goes from here: in the constructor on the command line (no page
//     callbacks there), on the introduction page in the GUI (the wizard is up,
//     and Cancel works).
//   - An uninstaller removes itself after it exits, from %TEMP%\uninstall.vbs:
//     it deletes the maintenance tool, then the whole target folder
//     (RemoveTargetDir, on: CMakeLists.txt). Installing before that is done
//     would have new files deleted under it — so the update waits until the
//     folder is gone.
//
// Elsewhere (Linux, macOS) this script does nothing.

var updateDone = false;

function Controller()
{
    if (systemInfo.kernelType !== "winnt" || !installer.isInstaller())
        return;
    if (installer.isCommandLineInstance())
        updateExisting();
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

function powershell(command)
{
    return run(winTool("powershell.exe"), ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command]);
}

// ---------------------------------------------------------------------------

// Whether a process of that image name runs: true, false, or null (unknown).
function processRunning(image)
{
    var r = run(winTool("tasklist.exe"), ["/FI", "IMAGENAME eq " + image, "/NH"]);
    return r === null ? null : r[0].toLowerCase().indexOf(image.toLowerCase()) >= 0;
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

// Stopped, with nothing installed: said first, then closed.
function stop(id, title, text)
{
    console.log("[finterm] stopped: " + id);
    QMessageBox.critical(id, title, text, QMessageBox.Ok);
    if (installer.isCommandLineInstance())
        installer.interrupt();
    else
        gui.rejectWithoutPrompt();
}

function cannotCheck(what)
{
    stop("finterm.install.cannot_check", "finterm could not be installed",
         "The installer could not " + what + " (a Windows tool in " +
         toWin(installer.environmentVariable("SystemRoot") || "C:\\Windows") + " did not start).");
}

// The update. Returns when this install may go ahead; anything else stops the
// installer.
function updateExisting()
{
    updateDone = true;
    // An uninstaller still open (on its last page) removes itself when it is
    // closed — by then the new install's maintenance tool.
    var open = processRunning("FintermMaintenanceTool.exe");
    if (open === null)
        return cannotCheck("check whether a finterm uninstaller is open");
    if (open)
        return stop("finterm.install.uninstaller_open", "Close the finterm uninstaller first",
                    "A finterm uninstaller window is still open. Close it (click Finish), then run this " +
                    "installer again.");

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
    if (answer !== QMessageBox.Yes) {
        console.log("[finterm] update declined");
        if (installer.isCommandLineInstance())
            installer.interrupt();
        else
            gui.rejectWithoutPrompt();
        return;
    }

    for (var tries = 0;; ++tries) {
        var running = processRunning("finterm.exe");
        if (running === null)
            return cannotCheck("check whether finterm is running");
        if (!running)
            break;
        if (tries >= 2 || installer.isCommandLineInstance())
            return stop("finterm.install.running", "finterm is running",
                        "Quit finterm, then run this installer again.");
        QMessageBox.warning("finterm.install.running_ask", "finterm is running",
                            "Quit finterm (every window), then click OK — the installer then waits up to 10 " +
                            "seconds for it to close.", QMessageBox.Ok);
        powershell("Wait-Process -Name finterm -Timeout 10 -ErrorAction SilentlyContinue");
    }

    var args = ["purge", "--default-answer", "--confirm-command"];
    for (var j = 0; j < dirs.length; ++j) {
        var dir = dirs[j];
        var tool = toWin(dir + "/FintermMaintenanceTool.exe");
        var r = inProgramFiles(dir)
            // Elevated: the old install was made by an administrator.
            ? powershell("$p = Start-Process -FilePath " + psQuote(tool) + " -ArgumentList " +
                         args.map(psQuote).join(",") + " -Verb RunAs -Wait -PassThru; exit $p.ExitCode")
            : run(tool, args);
        console.log("[finterm] removing " + dir + ": " + (r === null ? "could not start" : "exit " + r[1]));
        // Until its uninstall.vbs has removed the folder (a minute at most).
        if (powershell("for ($i = 0; (Test-Path -LiteralPath " + psQuote(toWin(dir)) + ") -and $i -lt 240; $i++) " +
                       "{ Start-Sleep -Milliseconds 250 }") === null)
            return cannotCheck("wait for the installed copy's removal");
        if (installer.fileExists(dir + "/FintermMaintenanceTool.exe"))
            return stop("finterm.install.update_failed", "finterm could not be updated",
                        "The installed finterm in " + toWin(dir) + " could not be removed. Uninstall it from " +
                        "Settings > Apps (answer No when asked to remove your data — this version keeps it), then " +
                        "run this installer again.");
        if (installer.fileExists(dir))
            return stop("finterm.install.update_incomplete", "finterm could not be updated",
                        "The installed finterm was uninstalled, but its folder " + toWin(dir) + " is still there " +
                        "(a file in it may be open). Close any program using it, delete the folder, then run this " +
                        "installer again.");
    }
    console.log("[finterm] previous install(s) removed; installing.");
}

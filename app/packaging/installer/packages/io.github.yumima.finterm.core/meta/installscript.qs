// installscript.qs -- finterm QtIFW component script
//
// Handles:
//   - Platform shortcuts on install (Start Menu, Desktop, .desktop entry)
//   - Full user-data cleanup on uninstall (with user confirmation)
//
// Data locations cleaned on uninstall:
//   Windows : %LOCALAPPDATA%\finterm\ and \finterm-backups\ (Start fresh)
//             %LOCALAPPDATA%\com.fincept.terminal\
//             %LOCALAPPDATA%\Fincept\*                  (legacy)
//             %LOCALAPPDATA%\FinceptTerminal\*          (legacy)
//             %APPDATA%\Fincept\*                       (QSettings roaming)
//             HKCU\Software\Fincept                     (registry)
//             Windows Credential Manager: FinceptTerminal/*
//             %TEMP%\fincept_*
//   macOS   : ~/Library/Application Support/com.fincept.terminal/
//             ~/Library/Preferences/com.fincept.FinceptTerminal.plist
//             ~/Library/Preferences/Fincept.plist (if present)
//             Keychain: com.fincept.terminal service entries
//             $TMPDIR/fincept_*, /tmp/fincept_*
//   Linux   : ~/.local/share/com.fincept.terminal/
//             ~/.config/Fincept/
//             /tmp/fincept_*
//             ~/.local/share/applications/fincept-terminal.desktop
//
// Debug: run the maintenance tool with `-v` (or `--verbose`) to see console.log output.

// ---------------------------------------------------------------------------
// Component constructor: wire up lifecycle hooks
// ---------------------------------------------------------------------------

function Component()
{
    console.log("[finterm] Component() constructor — isInstaller=" +
                installer.isInstaller() +
                " isUninstaller=" + installer.isUninstaller() +
                " isUpdater=" + installer.isUpdater() +
                " isPackageManager=" + installer.isPackageManager());

    // Connect signals using the 1-arg form. The 2-arg form (thisObj, fn) is
    // not reliably supported by the QJSEngine that backs QtIFW component
    // scripts — connections silently no-op on some versions.
    if (installer.isInstaller()) {
        installer.installationFinished.connect(onInstallationFinished);
        refuseWhileUninstallerOpen() || replaceExistingInstall();
    }

    if (installer.isUninstaller()) {
        installer.uninstallationStarted.connect(onUninstallationStarted);
        installer.uninstallationFinished.connect(onUninstallationFinished);
    }
}

// ---------------------------------------------------------------------------
// Install: create platform shortcuts
// ---------------------------------------------------------------------------

Component.prototype.createOperations = function()
{
    if (installer.isInstaller() && uninstallerRunning())
        throw new Error("a finterm uninstaller window is still open - close it first");
    // The update above removes an existing install first; one still there
    // (declined, or its removal failed) is never installed over or beside.
    var other = installer.isInstaller() ? existingInstall() : "";
    if (other)
        throw new Error("finterm is already installed in " + other + " - uninstall it first");

    // Always call base first so file extraction happens.
    component.createOperations();

    var targetDir = installer.value("TargetDir");

    if (systemInfo.kernelType === "winnt") {
        // Start Menu shortcut
        component.addOperation("CreateShortcut",
            targetDir + "/finterm.exe",
            "@StartMenuDir@/finterm.lnk",
            "workingDirectory=" + targetDir,
            "iconPath=" + targetDir + "/finterm.exe",
            "iconId=0",
            "description=Local-first Financial Research Terminal");

        // Desktop shortcut
        component.addOperation("CreateShortcut",
            targetDir + "/finterm.exe",
            "@DesktopDir@/finterm.lnk",
            "workingDirectory=" + targetDir,
            "iconPath=" + targetDir + "/finterm.exe",
            "iconId=0",
            "description=Local-first Financial Research Terminal");
    }

    if (systemInfo.kernelType === "linux") {
        component.addOperation("CreateDesktopEntry",
            "@HomeDir@/.local/share/applications/finterm.desktop",
            "Version=1.0\n" +
            "Type=Application\n" +
            "Name=finterm\n" +
            "GenericName=Financial Research Terminal\n" +
            "Comment=Local-first financial research terminal\n" +
            "Exec=" + targetDir + "/bin/finterm.bin %U\n" +
            "Icon=" + targetDir + "/share/icons/hicolor/256x256/apps/finterm.png\n" +
            "Terminal=false\n" +
            "StartupWMClass=finterm\n" +
            "StartupNotify=true\n" +
            "Categories=Finance;Office;Science;\n" +
            "Keywords=finance;trading;stocks;crypto;portfolio;AI;analytics;markets;\n"
        );
    }
    // macOS: .app bundle is self-contained, no shortcuts needed
};

// ---------------------------------------------------------------------------
// Windows: one install per machine. finterm installs per user since 4.1.0
// (%LOCALAPPDATA%\Programs\finterm); an earlier one in Program Files would
// stay beside it, both sharing one data folder — and the old one's uninstall
// could remove that data. Ask for the old one to go first.
// ---------------------------------------------------------------------------

function otherWindowsInstall(ownDir)
{
    if (systemInfo.kernelType !== "winnt")
        return "";
    var own = (ownDir || "").replace(/\\/g, "/").toLowerCase();
    var candidates = [
        installer.environmentVariable("ProgramFiles") + "/finterm",
        installer.environmentVariable("LOCALAPPDATA") + "/Programs/finterm"
    ];
    for (var i = 0; i < candidates.length; ++i) {
        var dir = candidates[i].replace(/\\/g, "/");
        if (dir.toLowerCase() !== own && installer.fileExists(dir + "/FintermMaintenanceTool.exe"))
            return dir;
    }
    return "";
}


// ---------------------------------------------------------------------------
// Windows: running the installer over an existing finterm UPDATES it. QtIFW
// refuses a folder that already holds an installation (on its target page,
// before any hook a component script has), and an older Program Files copy
// (4.0.10 and before) would stay beside a per-user one, sharing its data.
// So, when the user agrees at the start: the existing one is removed by its
// own uninstaller — which keeps the data (its question defaults to No, and
// --default-answer takes it) — then this one installs. Data is never touched
// here; the question says the removal happens at once.
// ---------------------------------------------------------------------------

// Every finterm installation found (the target folder, the per-user one, an
// old Program Files one), each once.
function existingInstalls()
{
    if (systemInfo.kernelType !== "winnt")
        return [];
    var candidates = [
        installer.value("TargetDir"),
        installer.environmentVariable("LOCALAPPDATA") + "/Programs/finterm",
        installer.environmentVariable("ProgramFiles") + "/finterm"
    ];
    var found = [], seen = {};
    for (var i = 0; i < candidates.length; ++i) {
        var dir = (candidates[i] || "").replace(/\\/g, "/");
        var key = dir.toLowerCase().replace(/\/+$/, "");
        if (dir && !seen[key] && installer.fileExists(dir + "/FintermMaintenanceTool.exe")) {
            seen[key] = true;
            found.push(dir);
        }
    }
    return found;
}

function existingInstall()
{
    var all = existingInstalls();
    return all.length > 0 ? all[0] : "";
}

function inProgramFiles(dir)
{
    var pf = (installer.environmentVariable("ProgramFiles") || "").replace(/\\/g, "/").toLowerCase();
    return pf.length > 0 && dir.toLowerCase().indexOf(pf + "/") === 0;
}

function fintermRunning()
{
    var r = installer.execute("tasklist.exe", ["/FI", "IMAGENAME eq finterm.exe", "/NH"]);
    return !!(r && r.length > 0 && /finterm\.exe/i.test(String(r[0])));
}

function stopInstaller()
{
    if (installer.isCommandLineInstance())
        installer.interrupt();
    else
        gui.rejectWithoutPrompt();
}

function psQuote(s)
{
    return "'" + String(s).replace(/'/g, "''") + "'";
}

function replaceExistingInstall()
{
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
        "finterm.install.update",
        "Update finterm",
        "finterm is already installed in:" + list + "\n\n" +
        "This installer replaces it with finterm " + installer.value("ProductVersion") + ": the installed copy " +
        "is removed now, then this one is installed. Your data, settings and saved keys are kept — if you stop " +
        "the installer after this, run it again to install finterm." +
        (anyProgramFiles ? "\n\nA copy in Program Files is removed with Windows' permission, and finterm then " +
                           "installs for your user only." : ""),
        QMessageBox.Yes | QMessageBox.No, QMessageBox.Yes);
    if (answer !== QMessageBox.Yes) {
        stopInstaller();
        return;
    }
    for (var tries = 0; fintermRunning(); ++tries) {
        if (tries >= 2 || installer.isCommandLineInstance()) {
            QMessageBox.critical("finterm.install.running", "finterm is running",
                                 "Quit finterm, then run this installer again.", QMessageBox.Ok);
            stopInstaller();
            return;
        }
        QMessageBox.warning("finterm.install.running_ask", "finterm is running",
                            "Quit finterm (every window), then click OK.", QMessageBox.Ok);
        // Closing takes a moment (its data daemon, the window): up to 10 s.
        installer.execute("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
            "Wait-Process -Name finterm -Timeout 10 -ErrorAction SilentlyContinue"]);
    }
    var args = ["purge", "--default-answer", "--confirm-command"];
    for (var j = 0; j < dirs.length; ++j) {
        var dir = dirs[j];
        var tool = dir + "/FintermMaintenanceTool.exe";
        var r;
        if (inProgramFiles(dir)) {
            // Elevated: the old install was made by an administrator.
            r = installer.execute("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                "$p = Start-Process -FilePath " + psQuote(toWin(tool)) + " -ArgumentList " +
                args.map(psQuote).join(",") + " -Verb RunAs -Wait -PassThru; exit $p.ExitCode"]);
        } else {
            r = installer.execute(toWin(tool), args);
        }
        console.log("[finterm] removing " + dir + ": exit " + (r && r.length > 1 ? r[1] : "?"));
        // The uninstaller deletes itself from a helper after it exits: wait for
        // that, or it would delete the maintenance tool this install puts there.
        installer.execute("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
            "for ($i = 0; (Test-Path -LiteralPath " + psQuote(toWin(tool)) + ") -and $i -lt 120; $i++) " +
            "{ Start-Sleep -Milliseconds 500 }"]);
        if (installer.fileExists(tool)) {
            QMessageBox.critical(
                "finterm.install.update_failed", "finterm could not be updated",
                "The installed finterm in " + toWin(dir) + " could not be removed. Uninstall it from Settings > " +
                "Apps (answer No when asked to remove your data — this version keeps it), then run this installer " +
                "again.",
                QMessageBox.Ok);
            stopInstaller();
            return;
        }
    }
    console.log("[finterm] previous install(s) removed; installing.");
}

// ---------------------------------------------------------------------------
// Windows: an uninstaller window still open (on its last page). IFW removes
// the uninstaller with a helper that waits for that window to close and then
// deletes FintermMaintenanceTool.* in the folder — by then the NEW install's,
// leaving it without an uninstaller. Ask for the window to be closed first.
// ---------------------------------------------------------------------------

function uninstallerRunning()
{
    if (systemInfo.kernelType !== "winnt")
        return false;
    var r = installer.execute("tasklist.exe", ["/FI", "IMAGENAME eq FintermMaintenanceTool.exe", "/NH"]);
    return !!(r && r.length > 0 && /FintermMaintenanceTool\.exe/i.test(String(r[0])));
}

function refuseWhileUninstallerOpen()
{
    if (!uninstallerRunning())
        return false;
    console.log("[finterm] an uninstaller window is still open");
    QMessageBox.critical(
        "finterm.install.uninstaller_open",
        "Close the finterm uninstaller first",
        "A finterm uninstaller window is still open. Close it (click Finish), then run this installer again.",
        QMessageBox.Ok);
    if (installer.isCommandLineInstance())
        installer.interrupt();
    else
        gui.rejectWithoutPrompt();
    return true;
}

function onInstallationFinished()
{
    console.log("[finterm] Installation finished.");
}

// ---------------------------------------------------------------------------
// Uninstall: ask the user, then clean everything
// ---------------------------------------------------------------------------

function onUninstallationStarted()
{
    // Another finterm install on this machine shares the data: never offer
    // to remove it (an old Program Files copy being uninstalled after the
    // per-user one went in, or the reverse).
    var other = otherWindowsInstall(installer.value("TargetDir"));
    if (other) {
        console.log("[finterm] another installation (" + other + ") uses the same data — keeping it.");
        return;
    }
    console.log("[finterm] uninstallationStarted — prompting for user-data removal.");

    var answer = QMessageBox.question(
        "finterm.uninstall.data",
        "Remove finterm user data?",
        "Do you want to remove all finterm user data?\n\n" +
        "This includes:\n" +
        "  - Databases (chat history, portfolio, watchlists)\n" +
        "  - Log files\n" +
        "  - Downloaded files and cached data\n" +
        "  - ML models (Whisper, etc.)\n" +
        "  - Python runtime and virtual environments\n" +
        "  - Workspaces and profiles\n" +
        "  - Saved credentials and API keys\n" +
        "  - Application settings\n\n" +
        "Choose 'No' to keep your data for a future reinstall.",
        QMessageBox.Yes | QMessageBox.No,
        QMessageBox.No
    );

    if (answer === QMessageBox.Yes) {
        console.log("[finterm] User chose Yes — cleaning all user data.");
        try {
            cleanUserData();
        } catch (e) {
            console.log("[finterm] cleanUserData threw: " + e);
        }
    } else {
        console.log("[finterm] User chose No — keeping user data.");
    }
}

function onUninstallationFinished()
{
    console.log("[finterm] Uninstallation finished.");
    removeUpdaterLeftovers();
}

// ---------------------------------------------------------------------------
// The in-place updater (apply-update.ps1) keeps the previous version beside
// the install — <TargetDir>.previous — for a rollback, and an interrupted
// update can leave <TargetDir>.new / .ready / .failed / .previous.old (each
// possibly with a -yyyyMMddHHmmss suffix). They are program files, not the
// user's data: they go with the program, whatever the data answer was.
// Before this, an uninstall left a whole copy of finterm behind.
// ---------------------------------------------------------------------------

function updaterLeftoverPattern(name)
{
    return "^" + name.replace(/[.^$*+?()[\]{}|\\]/g, "\\$&") +
           "\\.(previous(\\.old)?|new|ready|failed)(-\\d{14})?$";
}

function removeUpdaterLeftovers()
{
    if (systemInfo.kernelType !== "winnt")
        return;
    var target = (installer.value("TargetDir") || "").replace(/\\/g, "/").replace(/\/+$/, "");
    var cut = target.lastIndexOf("/");
    if (cut <= 0)
        return;
    var parent = target.substring(0, cut);
    var name = target.substring(cut + 1);
    if (!name)
        return;
    runAndLog("powershell.exe", [
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
        "Get-ChildItem -LiteralPath '" + toWin(parent).replace(/'/g, "''") + "' -Directory -ErrorAction SilentlyContinue | " +
        "Where-Object { $_.Name -match '" + updaterLeftoverPattern(name).replace(/'/g, "''") + "' } | " +
        "Remove-Item -Recurse -Force -ErrorAction SilentlyContinue"
    ]);
}

// ---------------------------------------------------------------------------
// Data cleanup implementation — dispatches to per-platform routines
// ---------------------------------------------------------------------------

function cleanUserData()
{
    if (systemInfo.kernelType === "winnt") {
        cleanUserDataWindows();
    } else if (systemInfo.kernelType === "darwin") {
        cleanUserDataMac();
    } else {
        cleanUserDataLinux();
    }
}

// ---------- Windows ----------

function cleanUserDataWindows()
{
    var localAppData = installer.environmentVariable("LOCALAPPDATA");
    var appData      = installer.environmentVariable("APPDATA");
    var tempDir      = installer.environmentVariable("TEMP");

    // 1. Main data root (current, and an older build's not yet moved)
    removeDirWindows(localAppData + "/finterm");
    removeDirWindows(localAppData + "/com.fincept.terminal");
    // Start fresh's backups of the user's earlier data (finterm-backups).
    removeDirWindows(localAppData + "/finterm-backups");

    // 2. Legacy data roots
    removeDirWindows(localAppData + "/Fincept/FinceptTerminal");
    removeDirWindows(localAppData + "/FinceptTerminal");
    // Remove the Fincept/ parent if it's now empty
    removeDirIfEmptyWindows(localAppData + "/Fincept");

    // 3. Roaming QSettings (INI fallback, rare but possible)
    removeDirWindows(appData + "/Fincept/FinceptTerminal");
    removeDirIfEmptyWindows(appData + "/Fincept");

    // 4. Registry — QSettings default format on Windows
    runAndLog("reg.exe", ["delete", "HKCU\\Software\\finterm", "/f"]);
    runAndLog("reg.exe", ["delete", "HKCU\\Software\\Fincept\\FinceptTerminal", "/f"]);
    runAndLog("reg.exe", ["delete", "HKCU\\Software\\Fincept\\FinceptTerminal-Secure", "/f"]);
    // Remove parent key last — only succeeds if no other Fincept apps remain.
    runAndLog("reg.exe", ["delete", "HKCU\\Software\\Fincept", "/f"]);

    // 5. Windows Credential Manager entries: FinceptTerminal/*
    //    cmdkey has no wildcard delete. Enumerate via PowerShell and delete each.
    runAndLog("powershell.exe", [
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
        "$list = cmdkey /list 2>$null | Select-String -Pattern '(finterm|FinceptTerminal)/'; " +
        "foreach ($line in $list) { " +
        "  $target = ($line.ToString() -replace '^\\s*Target:\\s*','').Trim(); " +
        "  if ($target) { cmdkey /delete:$target | Out-Null } " +
        "}"
    ]);

    // 6. Temp files — single shell string so wildcards expand inside cmd.
    //    Covers: fincept_cell_*, fincept_arg_*, fincept_report_autosave.*,
    //            fincept_paste_*, fincept_chart_*, fincept_spark_*, UpdateService temp.
    runAndLog("cmd.exe", ["/c",
        "del /q /f \"" + toWin(tempDir) + "\\fincept_*\" 2>nul & " +
        "del /q /f \"" + toWin(tempDir) + "\\fincept-boot.log\" 2>nul & " +
        "exit /b 0"]);

    // 7. Screenshots saved under %USERPROFILE% by MainWindow "save screenshot"
    //    with the strict pattern fincept_YYYYMMDD_HHMMSS.png. Only match that
    //    exact shape so we don't delete unrelated user files.
    var userProfile = installer.environmentVariable("USERPROFILE");
    if (userProfile) {
        runAndLog("powershell.exe", [
            "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
            "Get-ChildItem -LiteralPath '" + toWin(userProfile) + "' -File " +
            "-Filter 'fincept_*.png' -ErrorAction SilentlyContinue | " +
            "Where-Object { $_.Name -match '^fincept_\\d{8}_\\d{6}\\.png$' } | " +
            "Remove-Item -Force -ErrorAction SilentlyContinue"
        ]);
    }
}

function removeDirWindows(pathFwd)
{
    if (!pathFwd) return;
    if (!installer.fileExists(pathFwd)) {
        console.log("[finterm] skip (not present): " + pathFwd);
        return;
    }
    var win = toWin(pathFwd);
    // Quote the path inside a single shell string so spaces in the path
    // survive argv splitting. `exit /b 0` ensures we never surface an error.
    runAndLog("cmd.exe", ["/c",
        "rmdir /s /q \"" + win + "\" 2>nul & exit /b 0"]);
}

function removeDirIfEmptyWindows(pathFwd)
{
    if (!pathFwd || !installer.fileExists(pathFwd)) return;
    var win = toWin(pathFwd);
    // `rmdir` without /s fails if the directory isn't empty — which is what we want.
    runAndLog("cmd.exe", ["/c",
        "rmdir \"" + win + "\" 2>nul & exit /b 0"]);
}

function toWin(pathFwd)
{
    return pathFwd.replace(/\//g, "\\");
}

// ---------- macOS ----------

function cleanUserDataMac()
{
    var home = installer.environmentVariable("HOME");

    // 1. Main data root (current, and an older build's not yet moved)
    removeDirPosix(home + "/Library/Application Support/finterm");
    removeDirPosix(home + "/Library/Application Support/finterm-backups");
    removeDirPosix(home + "/Library/Application Support/com.fincept.terminal");
    runAndLog("/bin/bash", ["-c", "rm -f \"$HOME\"/Library/Preferences/com.finterm.*.plist; exit 0"]);

    // 2. Preferences / plist
    removeFilePosix(home + "/Library/Preferences/com.fincept.FinceptTerminal.plist");
    removeFilePosix(home + "/Library/Preferences/Fincept.FinceptTerminal.plist");
    removeFilePosix(home + "/Library/Preferences/Fincept.FinceptTerminal-Secure.plist");

    // 3. Caches (Qt/QSettings/logs occasionally land here)
    removeDirPosix(home + "/Library/Caches/io.github.yumima.finterm");
    removeDirPosix(home + "/Library/Caches/com.fincept.terminal");
    removeDirPosix(home + "/Library/Caches/Fincept");

    // 4. Saved application state
    removeDirPosix(home + "/Library/Saved Application State/io.github.yumima.finterm.savedState");
    removeDirPosix(home + "/Library/Saved Application State/com.fincept.terminal.savedState");

    // 5. Keychain: delete all entries under service "com.fincept.terminal".
    //    security(1) removes one entry per call — loop until it fails (no more).
    runAndLog("/bin/bash", ["-c",
        "while /usr/bin/security delete-generic-password -s 'finterm' >/dev/null 2>&1; do :; done; " +
        "while /usr/bin/security delete-generic-password -s 'com.fincept.terminal' >/dev/null 2>&1; do :; done; exit 0"
    ]);

    // 6. Temp files
    runAndLog("/bin/bash", ["-c",
        "rm -f /tmp/fincept_* /tmp/fincept-boot.log 2>/dev/null; " +
        "rm -f \"${TMPDIR:-/tmp}\"/fincept_* \"${TMPDIR:-/tmp}\"/fincept-boot.log 2>/dev/null; " +
        "exit 0"
    ]);

    // 7. Timestamped screenshots saved to $HOME by MainWindow save-screenshot
    //    Pattern: fincept_YYYYMMDD_HHMMSS.png — strict match to avoid collateral.
    runAndLog("/bin/bash", ["-c",
        "find \"" + shellEscape(home) + "\" -maxdepth 1 -type f " +
        "-regex '.*/fincept_[0-9]\\{8\\}_[0-9]\\{6\\}\\.png$' " +
        "-delete 2>/dev/null; exit 0"
    ]);
}

// ---------- Linux ----------

function cleanUserDataLinux()
{
    var home   = installer.environmentVariable("HOME");
    var xdgCfg = installer.environmentVariable("XDG_CONFIG_HOME");
    var xdgDat = installer.environmentVariable("XDG_DATA_HOME");
    var xdgCch = installer.environmentVariable("XDG_CACHE_HOME");

    if (!xdgCfg) xdgCfg = home + "/.config";
    if (!xdgDat) xdgDat = home + "/.local/share";
    if (!xdgCch) xdgCch = home + "/.cache";

    // 1. Main data root (respect XDG; current, and an older build's not yet moved)
    removeDirPosix(xdgDat + "/finterm");
    removeDirPosix(home   + "/.local/share/finterm");
    removeDirPosix(xdgDat + "/finterm-backups");
    removeDirPosix(xdgDat + "/com.fincept.terminal");
    removeDirPosix(home   + "/.local/share/com.fincept.terminal");

    // 2. QSettings .conf files
    removeDirPosix(xdgCfg + "/finterm");
    removeFilePosix(xdgCfg + "/Fincept/finterm.conf");
    removeFilePosix(xdgCfg + "/Fincept/finterm-Secure.conf");
    removeFilePosix(xdgCfg + "/Fincept/FinceptTerminal.conf");
    removeFilePosix(xdgCfg + "/Fincept/FinceptTerminal-Secure.conf");
    removeDirIfEmptyPosix(xdgCfg + "/Fincept");

    // 3. Cache dir (if the app used one)
    removeDirPosix(xdgCch + "/com.fincept.terminal");
    removeDirPosix(xdgCch + "/Fincept");

    // 4. Desktop entry (installed via CreateDesktopEntry at install time — IFW's
    //    own UNDO step removes it, but clean up any stale copies just in case).
    removeFilePosix(home + "/.local/share/applications/fincept-terminal.desktop");

    // 5. Temp files
    runAndLog("/bin/bash", ["-c",
        "rm -f /tmp/fincept_* /tmp/fincept-boot.log 2>/dev/null; " +
        "rm -f \"${TMPDIR:-/tmp}\"/fincept_* \"${TMPDIR:-/tmp}\"/fincept-boot.log 2>/dev/null; " +
        "exit 0"
    ]);

    // 6. Timestamped screenshots saved to $HOME by MainWindow save-screenshot
    runAndLog("/bin/bash", ["-c",
        "find \"" + shellEscape(home) + "\" -maxdepth 1 -type f " +
        "-regextype posix-extended " +
        "-regex '.*/fincept_[0-9]{8}_[0-9]{6}\\.png' " +
        "-delete 2>/dev/null; exit 0"
    ]);
}

// ---------- Unix helpers (mac + linux) ----------

function removeDirPosix(path)
{
    if (!path) return;
    if (!installer.fileExists(path)) {
        console.log("[finterm] skip (not present): " + path);
        return;
    }
    // Use /bin/rm with -rf so missing paths never error. Shell-wrap so the
    // path string survives any unusual characters (spaces in $HOME, etc).
    runAndLog("/bin/bash", ["-c", "rm -rf \"" + shellEscape(path) + "\"; exit 0"]);
}

function removeFilePosix(path)
{
    if (!path) return;
    if (!installer.fileExists(path)) {
        console.log("[finterm] skip (not present): " + path);
        return;
    }
    runAndLog("/bin/bash", ["-c", "rm -f \"" + shellEscape(path) + "\"; exit 0"]);
}

function removeDirIfEmptyPosix(path)
{
    if (!path || !installer.fileExists(path)) return;
    // rmdir fails if non-empty; ignore.
    runAndLog("/bin/bash", ["-c", "rmdir \"" + shellEscape(path) + "\" 2>/dev/null; exit 0"]);
}

function shellEscape(s)
{
    // Escape backslashes and double-quotes for embedding in a double-quoted shell string.
    return s.replace(/\\/g, "\\\\").replace(/"/g, "\\\"");
}

// ---------- Generic runner with logging ----------

function runAndLog(program, args)
{
    var result = installer.execute(program, args);
    // installer.execute returns [stdout, exitCode] on success,
    // or [] (empty) if the program failed to launch.
    if (!result || result.length === 0) {
        console.log("[finterm] FAILED to launch: " + program + " " + args.join(" "));
        return;
    }
    var exitCode = result.length >= 2 ? result[1] : "?";
    console.log("[finterm] ran " + program + " (exit=" + exitCode + ")");
}

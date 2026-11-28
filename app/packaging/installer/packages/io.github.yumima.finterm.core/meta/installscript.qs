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

    // Installing over an installed finterm (the update) is the control
    // script's (config/controlscript.qs): it runs before IFW's folder check.

    // Connect signals using the 1-arg form. The 2-arg form (thisObj, fn) is
    // not reliably supported by the QJSEngine that backs QtIFW component
    // scripts — connections silently no-op on some versions.
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
    // The control script refused this install (an installed finterm not
    // updated, an uninstaller open, …): throwing here ends it — on the
    // command line its own interrupt does not (IFW resets the status).
    var refused = installer.value("FintermInstallRefused");
    if (refused)
        throw new Error(refused);

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
// Windows' own tools, by full path — never whatever PATH finds first: a
// broken PATH made every call fail without a word (a data removal that
// removed nothing), and a planted copy earlier on it would run instead.
// (Kept identical in config/controlscript.qs; test_installscript.js checks.)
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

// ---------------------------------------------------------------------------
// Windows: another finterm install on this machine (an old Program Files one
// beside the per-user one) shares the data — its uninstall never offers to
// remove it.
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

function psQuote(s)
{
    return "'" + String(s).replace(/'/g, "''") + "'";
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
    runAndLog(winTool("powershell.exe"), [
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
    runAndLog(winTool("reg.exe"), ["delete", "HKCU\\Software\\finterm", "/f"]);
    runAndLog(winTool("reg.exe"), ["delete", "HKCU\\Software\\Fincept\\FinceptTerminal", "/f"]);
    runAndLog(winTool("reg.exe"), ["delete", "HKCU\\Software\\Fincept\\FinceptTerminal-Secure", "/f"]);
    // Remove parent key last — only succeeds if no other Fincept apps remain.
    runAndLog(winTool("reg.exe"), ["delete", "HKCU\\Software\\Fincept", "/f"]);

    // 5. Windows Credential Manager entries: FinceptTerminal/*
    //    cmdkey has no wildcard delete. Enumerate via PowerShell and delete each.
    runAndLog(winTool("powershell.exe"), [
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
        "$cmdkey = " + psQuote(winTool("cmdkey.exe")) + "; " +
        "$list = & $cmdkey /list 2>$null | Select-String -Pattern '(finterm|FinceptTerminal)/'; " +
        "foreach ($line in $list) { " +
        "  $target = ($line.ToString() -replace '^\\s*Target:\\s*','').Trim(); " +
        "  if ($target) { & $cmdkey /delete:$target | Out-Null } " +
        "}"
    ]);

    // 6. Temp files — single shell string so wildcards expand inside cmd.
    //    Covers: fincept_cell_*, fincept_arg_*, fincept_report_autosave.*,
    //            fincept_paste_*, fincept_chart_*, fincept_spark_*, UpdateService temp.
    runAndLog(winTool("cmd.exe"), ["/c",
        "del /q /f \"" + toWin(tempDir) + "\\fincept_*\" 2>nul & " +
        "del /q /f \"" + toWin(tempDir) + "\\fincept-boot.log\" 2>nul & " +
        "exit /b 0"]);

    // 7. Screenshots saved under %USERPROFILE% by MainWindow "save screenshot"
    //    with the strict pattern fincept_YYYYMMDD_HHMMSS.png. Only match that
    //    exact shape so we don't delete unrelated user files.
    var userProfile = installer.environmentVariable("USERPROFILE");
    if (userProfile) {
        runAndLog(winTool("powershell.exe"), [
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
    runAndLog(winTool("cmd.exe"), ["/c",
        "rmdir /s /q \"" + win + "\" 2>nul & exit /b 0"]);
}

function removeDirIfEmptyWindows(pathFwd)
{
    if (!pathFwd || !installer.fileExists(pathFwd)) return;
    var win = toWin(pathFwd);
    // `rmdir` without /s fails if the directory isn't empty — which is what we want.
    runAndLog(winTool("cmd.exe"), ["/c",
        "rmdir \"" + win + "\" 2>nul & exit /b 0"]);
}

function toWin(path)
{
    return String(path).replace(/\//g, "\\");
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
    var r = run(program, args);
    if (r !== null)
        console.log("[finterm] ran " + program + " (exit=" + r[1] + ")");
}

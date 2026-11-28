// tests/installer/test_installscript.js — the Windows rules of finterm's QtIFW
// scripts, run with IFW's objects stubbed:
//   - controlscript.qs: installing over an installed finterm (its own folder,
//     an old Program Files one, or one in a folder of its own that Windows'
//     Apps list knows) updates it — the old one is removed by its
//     own uninstaller (elevated for Program Files) and the installer waits
//     until its folder is gone; declined, failed or unverifiable, nothing
//     installs (createOperations throws on the recorded refusal). The GUI from
//     the introduction page; the command line only with UpdateExisting=true —
//     without it, nothing is removed and an installed finterm stops the install.
//   - installscript.qs: an uninstall offers to remove the user's data only
//     when no other install shares it, and removes it only on Yes.
//   - both: Windows' tools only by full path (the stub starts nothing else,
//     as a broken PATH wouldn't), and the helpers they share are identical.
// Usage: node test_installscript.js <installscript.qs> <controlscript.qs>
const fs = require("fs"), vm = require("vm");
const componentSource = fs.readFileSync(process.argv[2], "utf8");
const controlSource = fs.readFileSync(process.argv[3], "utf8");
let failures = 0;
function check(name, ok) { console.log((ok ? "ok   - " : "FAIL - ") + name); if (!ok) ++failures; }

const PF = "C:/Program Files/finterm", PU = "C:/Users/u/AppData/Local/Programs/finterm";
const norm = p => String(p).replace(/\\/g, "/").replace(/\/+$/, "");

// o: installs (folders with a maintenance tool), kernel, cli, update
// (UpdateExisting=true), answerYes, inUse (folders something runs from: the
// rename test fails), closedOnAsk (closed when the user is asked), vbsOnAsk
// (an uninstaller left open, closed when asked: its tool goes at once, its
// folder during the wait), purgeFails, removeCode (what the removal script
// exits with instead: its own codes), stranded (folders whose copy an
// interrupted update left at <folder>.finterm-check), folderStays,
// registered (InstallLocation values in the Apps list), appsFails,
// toolsBroken, uninstall.
function stubs(o) {
    const seen = { informed: [], questions: [], texts: [], purgedDirs: [], operations: 0, critical: [], warned: 0, asked: 0, rejected: 0, interrupted: 0, commands: [], unstarted: 0, hooks: {} };
    const hook = n => ({ connect: f => { seen.hooks[n] = f; } });
    const env = Object.assign({ SystemRoot: "C:\\Windows", ProgramFiles: "C:\\Program Files",
                                LOCALAPPDATA: "C:\\Users\\u\\AppData\\Local", APPDATA: "C:\\a", TEMP: "C:\\t",
                                USERPROFILE: "C:\\Users\\u" }, o.env || {});
    let installs = (o.installs || []).slice(), folders = installs.slice();
    const values = { TargetDir: o.target || PU, ProductVersion: "4.2.3" };
    if (o.update) values.UpdateExisting = "true";
    let inUse = (o.inUse || []).slice(), vbs = [], stranded = (o.stranded || []).slice();
    const inLine = (line, d) => norm(line).toLowerCase().includes(d.toLowerCase());
    const objects = {
        console: { log() {} },
        systemInfo: { kernelType: o.kernel || "winnt" },
        installer: {
            isInstaller: () => !o.uninstall, isUninstaller: () => !!o.uninstall, isUpdater: () => false,
            isPackageManager: () => false, isCommandLineInstance: () => !!o.cli,
            value: k => values[k] || "", setValue: (k, v) => { values[k] = v; },
            environmentVariable: n => env[n] || "",
            fileExists: p => {
                const q = norm(p);
                const ci = (a, b) => a.toLowerCase() === b.toLowerCase(); // Windows: case-insensitive
                return installs.some(d => ci(q, d + "/FintermMaintenanceTool.exe")) || folders.some(d => ci(q, d)) ||
                       stranded.some(d => ci(q, d + ".finterm-check/FintermMaintenanceTool.exe")) ||
                       /AppData\/Local\/finterm$/.test(q);
            },
            interrupt: () => { ++seen.interrupted; },
            execute: (p, a) => {
                const line = p + " " + (a || []).join(" ");
                seen.commands.push(line);
                // Only what a broken PATH can't touch starts: Windows' tools by
                // full path, and the installed copy's own uninstaller.
                const win = /^C:\\Windows\\System32\\/i.test(p), own = /FintermMaintenanceTool\.exe$/i.test(p);
                if (!(win || own) || (win && o.toolsBroken)) { ++seen.unstarted; return []; }
                // The rename test, then the uninstaller (one script; elevated
                // through RunAs for Program Files): its tool goes, then — its
                // uninstall.vbs — the folder.
                // The Apps list, as PowerShell prints it: each location escaped.
                if (/CurrentVersion\\Uninstall/.test(line))
                    return o.appsFails ? ["", 1]
                                       : [(o.registered || []).map(d => encodeURIComponent(d.replace(/\//g, "\\"))).join("\r\n"), 0];
                if (/IO\.Directory\]::Move/.test(line) && inUse.some(d => inLine(line, d))) return ["", 103];
                if (/IO\.Directory\]::Move/.test(line) && o.removeCode) {
                    if (o.removeCode === 104) { // moved aside, not back
                        const aside = installs.filter(d => inLine(line, d));
                        installs = installs.filter(d => !aside.includes(d));
                        stranded.push(...aside);
                    }
                    return ["", o.removeCode];
                }
                if (/Test-Path/.test(line) && !/Directory\]::Move/.test(line)) { // a wait: for a folder, or a tool
                    if (!/FintermMaintenanceTool\.exe/.test(line)) folders = folders.filter(d => !vbs.includes(d));
                    return ["", 0];
                }
                if (/purge/.test(line) && o.purgeFails) return ["", 101];
                if (/purge/.test(line)) {
                    const gone = installs.filter(d => norm(line).includes(d));
                    seen.purgedDirs.push(...gone);
                    installs = installs.filter(d => !gone.includes(d));
                    if (!o.folderStays) folders = folders.filter(d => !gone.includes(d));
                }
                return ["", 0];
            },
            uninstallationStarted: hook("uninstall"), uninstallationFinished: hook("uninstalled"),
        },
        gui: { rejectWithoutPrompt: () => { ++seen.rejected; } },
        component: { createOperations: () => { ++seen.operations; }, addOperation() {} },
        QMessageBox: { Yes: 1, No: 2, Ok: 4, critical: (id, title, text) => { seen.critical.push(id); seen.texts.push(String(text)); },
                       warning: () => {
                           ++seen.warned;
                           if (o.closedOnAsk) inUse = [];
                           if (o.vbsOnAsk) { vbs = inUse.slice(); installs = installs.filter(d => !vbs.includes(d)); inUse = []; }
                       },
                       information: (id, title, text) => { seen.informed.push(id); seen.texts.push(String(text)); },
                       question: (id, title, text) => { ++seen.asked; seen.questions.push(String(text)); return o.answerYes ? 1 : 2; } },
    };
    // IFW gives each script its own engine; they share the installer (and
    // its values).
    const engine = () => vm.createContext(Object.assign({}, objects));
    return { ctx: engine(), engine, seen, left: () => installs, values };
}

// The installer starting: the control script, as IFW runs it.
// Then, unless the wizard was closed, the install itself (createOperations).
function install(o) {
    const { ctx, engine, seen, left, values } = stubs(o);
    vm.runInContext(controlSource, ctx);
    const c = vm.runInContext("new Controller()", ctx);
    if (!o.cli && typeof c.IntroductionPageCallback === "function") {
        c.IntroductionPageCallback();
        c.IntroductionPageCallback(); // shown again (Back): asked once
    }
    seen.left = left();
    // IFW's own check next (installationAllowedToDirectory): a target folder
    // holding a maintenance tool is refused before any component script.
    seen.ifwRefused = !seen.rejected && seen.left.includes(norm(values.TargetDir));
    if (!seen.rejected && !seen.ifwRefused) {
        const comp = engine();
        vm.runInContext(componentSource, comp);
        try { vm.runInContext("new Component()", comp).createOperations(); } catch (e) { seen.threw = String(e); }
    }
    seen.installed = seen.operations === 1;
    seen.refused = !!values.FintermInstallRefused;
    // Stopped: nothing installed, and refused — by the control script, or by
    // IFW over the folder it left.
    seen.stopped = !seen.installed && (seen.refused || seen.ifwRefused);
    return seen;
}

function uninstall(o) {
    const { ctx, seen } = stubs(Object.assign({ uninstall: true }, o));
    vm.runInContext(componentSource, ctx);
    vm.runInContext("new Component()", ctx);
    seen.hooks.uninstall();
    return seen;
}

// Removed by its uninstaller, run as `purge --default-answer --confirm-command`.
const purged = (s, dir) => s.purgedDirs.includes(dir) &&
    s.commands.some(c => /purge --default-answer --confirm-command/.test(c) && norm(c).includes(dir));
const fullPaths = s => s.unstarted === 0 &&
    s.commands.every(c => /^C:\\Windows\\System32\\|FintermMaintenanceTool\.exe /i.test(c));

for (const cli of [false, true]) {
    const how = cli ? "command line (UpdateExisting)" : "GUI";
    const install1 = o => install(Object.assign({ cli, update: cli }, o));
    let s = install1({ installs: [PU], answerYes: true });
    check(how + ": over its own install, Yes: removed by its uninstaller, then goes ahead",
          s.asked === 1 && purged(s, PU) && s.left.length === 0 && s.installed && s.critical.length === 0);
    check(how + ": every Windows tool by full path", fullPaths(s));
    s = install1({ installs: [PF], answerYes: true });
    check(how + ": over Program Files: removed elevated, then goes ahead",
          purged(s, PF) && s.commands.some(c => /runas/i.test(c)) && s.installed);
    s = install1({ installs: [PU, PF], answerYes: true });
    check(how + ": per-user and Program Files: both removed, asked once",
          s.asked === 1 && purged(s, PU) && purged(s, PF) && s.installed);
    s = install1({ installs: [PU], answerYes: false });
    check(how + ": No: stopped, nothing removed", s.stopped && s.refused && !purged(s, PU) && s.left.length === 1);
    s = install1({ installs: [PF], answerYes: true, purgeFails: true });
    check(how + ": the uninstaller fails: told, stopped, no wait",
          s.critical.includes("finterm.install.update_failed") && s.stopped && s.refused &&
          !s.commands.some(c => /Test-Path/.test(c) && !/Directory\]::Move/.test(c)));
    s = install1({ installs: [PU], answerYes: true, folderStays: true });
    check(how + ": uninstalled but its folder stays: told, stopped (nothing installs under its uninstall.vbs)",
          s.critical.includes("finterm.install.update_incomplete") && s.stopped && s.refused);
    s = install1({ installs: [PU], answerYes: true, toolsBroken: true });
    check(how + ": a Windows tool won't start: told it can't check, stopped, nothing removed",
          s.critical.includes("finterm.install.cannot_check") && s.stopped && s.refused && !purged(s, PU));
    s = install1({ installs: [PU], answerYes: true, inUse: [PU] });
    check(how + ": something runs from it: told, stopped, nothing removed",
          s.critical.includes("finterm.install.in_use") && s.stopped && s.refused && !purged(s, PU) &&
          s.warned === (cli ? 0 : 2));
    if (!cli) {
        s = install1({ installs: [PU], answerYes: true, inUse: [PU], closedOnAsk: true });
        check(how + ": in use, closed when asked: removed, then goes ahead", s.warned === 1 && purged(s, PU) && s.installed);
        const moves = s.commands.filter(c => /Directory\]::Move/.test(c)).map(c => (c.match(/\$i -lt (\d+)/) || [])[1]);
        check(how + ": two seconds' grace at the first try, ten when tried again", moves[0] === "8" && moves[1] === "40");
        s = install1({ installs: [PU], answerYes: true, inUse: [PU], vbsOnAsk: true });
        check(how + ": an uninstaller left open, closed when asked: not purged again, its folder waited for, goes ahead",
              s.warned === 1 && !purged(s, PU) && s.installed &&
              s.commands.some(c => /Test-Path/.test(c) && !/Directory\]::Move|FintermMaintenanceTool/.test(c)));
    }
    for (const [where, code, says] of [
            [PF, 104, /left finterm in .*finterm-check\. Rename it back.*as an administrator/],
            [PU, 104, /left finterm in .*finterm-check\. Rename it back/], [PU, 105, /uninstaller did not start/],
            [PU, 106, /finterm-check.*in the way/], [PF, 107, /permission was not given/],
            [PF, 108, /error 108/], [PF, 109, /could not be run with Windows' permission/], [PU, 1, /error 1\)/],
            [PU, 110, /PowerShell is restricted/], [PF, 110, /PowerShell is restricted/]]) {
        s = install1({ installs: [where], answerYes: true, removeCode: code });
        check(how + ": the removal script's " + code + (where === PF ? " (Program Files)" : "") +
              ": said as such, stopped, nothing removed",
              s.critical.includes("finterm.install.update_failed") && s.stopped && !purged(s, where) &&
              says.test(s.texts.join(" ")));
    }
    s = install1({ installs: [PF], answerYes: true });
    const elevated = s.commands.find(c => /runas/i.test(c)) || "";
    check(how + ": restricted PowerShell checked before the elevation wrapper's own .NET calls",
          (c => c.startsWith("if ($ExecutionContext.SessionState.LanguageMode -ne 'FullLanguage') { exit 110 }; ") &&
                c.indexOf("exit 110") < c.indexOf("New-Object Diagnostics.ProcessStartInfo"))
              (elevated.replace(/^\S+ -NoProfile -ExecutionPolicy Bypass -Command /, "")));
    s = install1({ installs: [PU], answerYes: true });
    const removal = s.commands.find(c => /Directory\]::Move/.test(c)) || "";
    check(how + ": the per-user removal script: leftover check, the uninstaller waited for itself, a start failure caught",
          /Test-Path -LiteralPath \$t\) \{ exit 106/.test(removal) && /Process\]::Start\(\$si\); \$pr\.WaitForExit\(\)/.test(removal) &&
          !/Start-Process/.test(removal) && /if \(\$failed -ne \$null\) \{ exit 105/.test(removal));
    s = install1({ installs: [], answerYes: true, stranded: [PU] });
    check(how + ": a copy stranded at .finterm-check: told to rename it back, nothing installed",
          s.stopped && /Rename it back/.test(s.texts.join(" ")));
    s = install1({ installs: [PU], answerYes: true, stranded: [PU] });
    check(how + ": stranded, and installed there again since: told to delete the old copy, nothing removed",
          s.stopped && !purged(s, PU) && /old copy.*Delete it/.test(s.texts.join(" ")));
    s = install1({ installs: [PU, PF], answerYes: true, inUse: [PF] });
    check(how + ": Program Files in use beside a per-user copy: stopped before the per-user one is touched",
          s.stopped && !purged(s, PU) && !purged(s, PF));
    s = install1({ installs: [PF], answerYes: true, inUse: [PF] });
    check(how + ": Program Files in use: the rename test runs elevated with the purge, nothing removed",
          s.critical.includes("finterm.install.in_use") && !purged(s, PF) &&
          s.commands.some(c => /runas/i.test(c) && /IO\.Directory\]::Move/.test(c)));
    s = install1({ installs: [] });
    check(how + ": first install: goes ahead, nothing asked", s.installed && s.asked === 0 && s.critical.length === 0);
}
// The command line without UpdateExisting (any command — install, search,
// create-offline — loads the control script): nothing is removed.
let c0 = install({ cli: true, installs: [PU], answerYes: true });
check("command line, no UpdateExisting, over an install: nothing removed, IFW refuses the folder",
      !purged(c0, PU) && c0.ifwRefused && !c0.installed && c0.asked === 0);
c0 = install({ cli: true, installs: [PF], answerYes: true });
check("command line, no UpdateExisting, Program Files copy: nothing installs beside it, says how",
      !purged(c0, PF) && c0.stopped && !c0.ifwRefused && /UpdateExisting=true/.test(c0.threw || ""));
c0 = install({ cli: true, installs: [] });
check("command line, no UpdateExisting, first install: goes ahead", c0.installed && !c0.refused);
c0 = install({ cli: true, update: true, installs: ["D:/finterm"], target: "D:/finterm", answerYes: true });
check("command line, a custom folder given as TargetDir=: updated there", purged(c0, "D:/finterm") && c0.installed);
c0 = install({ cli: true, update: true, installs: [PU], answerYes: true });
check("command line: ten seconds' grace (it can't ask)",
      ((c0.commands.find(c => /Directory\]::Move/.test(c)) || "").match(/\$i -lt (\d+)/) || [])[1] === "40");
c0 = install({ cli: true, installs: [], stranded: [PU] });
check("command line, no UpdateExisting, a stranded copy: the install stops, says so",
      c0.stopped && /Rename it back/.test(c0.threw || ""));
// Commands other than the Apps-list lookup (which only reads).
const changes = s => s.commands.filter(c => !/CurrentVersion\\Uninstall/.test(c)).length;

// An install in a folder of its own, known from the Apps list.
const D = "D:/Börse & Co/finterm"; // non-ASCII and a space survive the console
let a0 = install({ installs: [D], registered: [D], answerYes: true });
check("GUI: an install in a folder of its own (Apps list): asked, removed, goes ahead",
      a0.asked === 1 && purged(a0, D) && a0.installed);
a0 = install({ cli: true, installs: [D], registered: [D] });
check("command line, no UpdateExisting: an install in a folder of its own stops the install",
      a0.stopped && !purged(a0, D) && (a0.threw || "").includes("D:\\Börse & Co\\finterm"));
a0 = install({ cli: true, installs: [], registered: ["D:/gone"] });
check("an Apps entry whose folder holds no install: ignored", a0.installed && !a0.refused);
a0 = install({ cli: true, installs: [PU], appsFails: true });
check("the Apps list unreadable: the usual folders still checked", a0.ifwRefused && !purged(a0, PU));
a0 = uninstall({ target: PU, installs: [PU], appsFails: true, answerYes: true });
check("uninstall, the Apps list unreadable: the data kept, and said so",
      a0.asked === 0 && changes(a0) === 0 && a0.informed.includes("finterm.uninstall.data_kept"));
a0 = install({ installs: [PU], answerYes: true });
check("GUI update question: the folder is chosen next", /\(you choose the folder next\)/.test(a0.questions[0] || ""));
a0 = install({ cli: true, update: true, installs: [PU], answerYes: true });
check("command line update question: installed in its target folder", / in its target folder\./.test(a0.questions[0] || ""));
a0 = uninstall({ target: PU, installs: [PU, D], registered: [D], answerYes: true });
check("uninstall beside an install in a folder of its own (Apps list): not asked, data stays",
      a0.asked === 0 && changes(a0) === 0);
for (const [own, entry] of [[PU, PU], [PU.replace(/\//g, "\\") + "\\", PU], [D, D], [D + "/", D.toUpperCase()]]) {
    a0 = uninstall({ target: own, installs: [norm(own)], registered: [entry], answerYes: true });
    check("uninstall alone, its own Apps entry (" + own + "): still asked, the data goes on Yes",
          a0.asked === 1 && a0.commands.some(c => /rmdir/.test(c)));
}

let s = install({ installs: [PU], answerYes: true, env: { ProgramFiles: "" } });
check("no ProgramFiles in the environment: a per-user install is not taken for one (no elevation)",
      purged(s, PU) && !s.commands.some(c => /runas/i.test(c)));
s = install({ kernel: "linux", target: "/home/u/finterm", installs: [PF] });
check("not a Windows rule elsewhere", s.installed && s.commands.length === 0 && s.asked === 0);

const removed = s => s.commands.some(c => /rmdir/.test(c) && /AppData.Local.finterm/.test(c));
s = uninstall({ target: PU, installs: [PU], answerYes: true });
check("uninstall, alone, Yes: the data goes", s.asked === 1 && removed(s));
check("uninstall's data removal: full paths", fullPaths(s));
s = uninstall({ target: PU, installs: [PU], answerYes: false });
check("uninstall, alone, No: the data stays", s.asked === 1 && changes(s) === 0);
s = uninstall({ target: PU, installs: [PU, PF], answerYes: true });
check("uninstall beside Program Files: not asked, data stays", s.asked === 0 && changes(s) === 0);
s = uninstall({ target: PF.replace(/\//g, "\\"), installs: [PU, PF], answerYes: true });
check("uninstall of Program Files beside per-user: not asked, data stays", s.asked === 0 && changes(s) === 0);

// The updater's copies beside the install go with the program, whatever the
// data answer — they are a whole finterm, not the user's data.
s = uninstall({ target: PU, installs: [PU], answerYes: false });
s.hooks.uninstalled();
const leftovers = s.commands.filter(c => /previous/.test(c));
check("uninstall removes the updater's copies (data kept)",
      leftovers.length === 1 && /AppData\\Local\\Programs'/.test(leftovers[0]) && fullPaths(s));
{
    const { ctx } = stubs({});
    vm.runInContext(componentSource, ctx);
    const re = new RegExp(vm.runInContext('updaterLeftoverPattern("finterm")', ctx));
    const yes = ["finterm.previous", "finterm.previous.old", "finterm.new", "finterm.ready-20261127120000",
                 "finterm.failed", "FINTERM.previous".toLowerCase()];
    const no = ["finterm", "finterm-backups", "finterm.previousX", "finterm.previous.bak", "otherfinterm.previous",
                "finterm.new-123"];
    check("leftover names matched", yes.every(n => re.test(n)));
    check("nothing else matched", no.every(n => !re.test(n)));
}

// One source of truth for what both scripts need (IFW gives each its own
// engine, so they cannot share a file).
const fn = (src, name) => (src.match(new RegExp("^function " + name + "\\([^]*?^}", "m")) || [""])[0];
for (const name of ["winTool", "run", "toWin", "psQuote", "powershell", "registeredInstalls"])
    check("shared helper identical in both scripts: " + name,
          fn(componentSource, name) !== "" && fn(componentSource, name) === fn(controlSource, name));

console.log(failures + " failure(s)");
process.exit(failures ? 1 : 0);

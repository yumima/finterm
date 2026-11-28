// tests/installer/test_installscript.js — the Windows rules of finterm's QtIFW
// scripts, run with IFW's objects stubbed:
//   - controlscript.qs: installing over an installed finterm (its own folder,
//     or an old Program Files one) updates it — the old one is removed by its
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
// (UpdateExisting=true), answerYes, running, uninstallerOpen, purgeFails,
// folderStays, toolsBroken, uninstall.
function stubs(o) {
    const seen = { operations: 0, critical: [], warned: 0, asked: 0, rejected: 0, interrupted: 0, commands: [], unstarted: 0, hooks: {} };
    const hook = n => ({ connect: f => { seen.hooks[n] = f; } });
    const env = Object.assign({ SystemRoot: "C:\\Windows", ProgramFiles: "C:\\Program Files",
                                LOCALAPPDATA: "C:\\Users\\u\\AppData\\Local", APPDATA: "C:\\a", TEMP: "C:\\t",
                                USERPROFILE: "C:\\Users\\u" }, o.env || {});
    let installs = (o.installs || []).slice(), folders = installs.slice();
    const values = { TargetDir: o.target || PU, ProductVersion: "4.2.3" };
    if (o.update) values.UpdateExisting = "true";
    // tasklist as Windows prints it: the table cuts the image name at 25
    // characters; CSV gives it whole.
    const tasklist = (a, image) => a.includes("CSV") ? '"' + image + '","4242","Console","1","9,000 K"'
                                                      : (image.slice(0, 25) + "      4242 Console    1   9,000 K");
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
                return installs.some(d => q === d + "/FintermMaintenanceTool.exe") || folders.includes(q) ||
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
                if (/tasklist/i.test(p)) {
                    const q = (a || []).join(" ");
                    if (/FintermMaintenanceTool/.test(q) && o.uninstallerOpen) return [tasklist(a, "FintermMaintenanceTool.exe"), 0];
                    if (/IMAGENAME eq finterm\.exe/.test(q) && o.running) return [tasklist(a, "finterm.exe"), 0];
                    return ["INFO: No tasks are running which match the specified criteria.", 0];
                }
                // The uninstaller (direct, or elevated through PowerShell): its
                // tool goes, then — its uninstall.vbs — the folder.
                if (/purge/.test(line) && o.purgeFails) return ["", 1];
                if (/purge/.test(line)) {
                    const gone = installs.filter(d => norm(line).includes(d));
                    installs = installs.filter(d => !gone.includes(d));
                    if (!o.folderStays) folders = folders.filter(d => !gone.includes(d));
                }
                return ["", 0];
            },
            uninstallationStarted: hook("uninstall"), uninstallationFinished: hook("uninstalled"),
        },
        gui: { rejectWithoutPrompt: () => { ++seen.rejected; } },
        component: { createOperations: () => { ++seen.operations; }, addOperation() {} },
        QMessageBox: { Yes: 1, No: 2, Ok: 4, critical: id => { seen.critical.push(id); }, warning: () => { ++seen.warned; },
                       question: () => { ++seen.asked; return o.answerYes ? 1 : 2; } },
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
    if (!seen.rejected) {
        const comp = engine();
        vm.runInContext(componentSource, comp);
        try { vm.runInContext("new Component()", comp).createOperations(); } catch (e) { seen.threw = String(e); }
    }
    seen.installed = seen.operations === 1;
    seen.refused = !!values.FintermInstallRefused;
    // Stopped: nothing installed, and the refusal recorded.
    seen.stopped = !seen.installed && seen.refused;
    return seen;
}

function uninstall(o) {
    const { ctx, seen } = stubs(Object.assign({ uninstall: true }, o));
    vm.runInContext(componentSource, ctx);
    vm.runInContext("new Component()", ctx);
    seen.hooks.uninstall();
    return seen;
}

const purged = (s, dir) => s.commands.some(c => /purge.*--default-answer.*--confirm-command/.test(c) && norm(c).includes(dir));
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
          purged(s, PF) && s.commands.some(c => /RunAs/.test(c)) && s.installed);
    s = install1({ installs: [PU, PF], answerYes: true });
    check(how + ": per-user and Program Files: both removed, asked once",
          s.asked === 1 && purged(s, PU) && purged(s, PF) && s.installed);
    s = install1({ installs: [PU], answerYes: false });
    check(how + ": No: stopped, nothing removed", s.stopped && !purged(s, PU) && s.left.length === 1);
    s = install1({ installs: [PF], answerYes: true, purgeFails: true });
    check(how + ": the uninstaller fails: told, stopped, no wait",
          s.critical.includes("finterm.install.update_failed") && s.stopped && !s.commands.some(c => /Test-Path/.test(c)));
    s = install1({ installs: [PU], answerYes: true, folderStays: true });
    check(how + ": uninstalled but its folder stays: told, stopped (nothing installs under its uninstall.vbs)",
          s.critical.includes("finterm.install.update_incomplete") && s.stopped);
    s = install1({ installs: [PU], answerYes: true, toolsBroken: true });
    check(how + ": a Windows tool won't start: told it can't check, stopped, nothing removed",
          s.critical.includes("finterm.install.cannot_check") && s.stopped && !purged(s, PU));
    s = install1({ installs: [], uninstallerOpen: true });
    check(how + ": an uninstaller window open: told, stopped",
          s.critical.includes("finterm.install.uninstaller_open") && s.stopped);
    s = install1({ installs: [] });
    check(how + ": first install: goes ahead, nothing asked", s.installed && s.asked === 0 && s.critical.length === 0);
}
// The command line without UpdateExisting (any command — install, search,
// create-offline — loads the control script): nothing is removed.
let c0 = install({ cli: true, installs: [PU], answerYes: true });
check("command line, no UpdateExisting, over an install: nothing removed, the install stops, says how",
      !purged(c0, PU) && c0.stopped && /UpdateExisting=true/.test(c0.threw || "") && c0.asked === 0);
c0 = install({ cli: true, installs: [PF], answerYes: true });
check("command line, no UpdateExisting, Program Files copy: nothing installs beside it", !purged(c0, PF) && c0.stopped);
c0 = install({ cli: true, installs: [], uninstallerOpen: true });
check("command line, no UpdateExisting, an uninstaller open: the install stops", c0.stopped);
c0 = install({ cli: true, installs: [] });
check("command line, no UpdateExisting, first install: goes ahead", c0.installed && !c0.refused);
let s = install({ cli: true, update: true, installs: [PU], answerYes: true, running: true });
check("finterm running, command line: told, stopped, nothing removed",
      s.critical.includes("finterm.install.running") && s.stopped && !purged(s, PU));
s = install({ installs: [PU], answerYes: true, running: true });
check("finterm running, GUI: asked to quit (twice), then told, stopped",
      s.warned === 2 && s.critical.includes("finterm.install.running") && s.stopped && !purged(s, PU));
s = install({ installs: [PU], answerYes: true, env: { ProgramFiles: "" } });
check("no ProgramFiles in the environment: a per-user install is not taken for one (no elevation)",
      purged(s, PU) && !s.commands.some(c => /RunAs/.test(c)));
s = install({ kernel: "linux", target: "/home/u/finterm", installs: [PF] });
check("not a Windows rule elsewhere", s.installed && s.commands.length === 0 && s.asked === 0);

const removed = s => s.commands.some(c => /rmdir/.test(c) && /AppData.Local.finterm/.test(c));
s = uninstall({ target: PU, installs: [PU], answerYes: true });
check("uninstall, alone, Yes: the data goes", s.asked === 1 && removed(s));
check("uninstall's data removal: full paths", fullPaths(s));
s = uninstall({ target: PU, installs: [PU], answerYes: false });
check("uninstall, alone, No: the data stays", s.asked === 1 && s.commands.length === 0);
s = uninstall({ target: PU, installs: [PU, PF], answerYes: true });
check("uninstall beside Program Files: not asked, data stays", s.asked === 0 && s.commands.length === 0);
s = uninstall({ target: PF.replace(/\//g, "\\"), installs: [PU, PF], answerYes: true });
check("uninstall of Program Files beside per-user: not asked, data stays", s.asked === 0 && s.commands.length === 0);

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
for (const name of ["winTool", "run", "toWin", "psQuote"])
    check("shared helper identical in both scripts: " + name,
          fn(componentSource, name) !== "" && fn(componentSource, name) === fn(controlSource, name));

console.log(failures + " failure(s)");
process.exit(failures ? 1 : 0);

// tests/installer/test_installscript.js — the Windows rules of installscript.qs,
// run with IFW's objects stubbed (a command-line purge never loads component
// scripts, so CI cannot reach them through the uninstaller):
//   - installing over an existing finterm (its own folder, or an old Program
//     Files one) updates it: the old one is removed by its own uninstaller
//     (elevated for Program Files) — or, declined or failed, nothing installs;
//   - an uninstall offers to remove the user's data only when no other install
//     shares it, and removes it only on Yes.
// Usage: node test_installscript.js <path to installscript.qs>
const fs = require("fs"), vm = require("vm");
const source = fs.readFileSync(process.argv[2], "utf8");
let failures = 0;
function check(name, ok) { console.log((ok ? "ok   - " : "FAIL - ") + name); if (!ok) ++failures; }

const PF = "C:/Program Files/finterm", PU = "C:/Users/u/AppData/Local/Programs/finterm";
function run(o) {
    const seen = { critical: 0, warned: 0, asked: 0, rejected: 0, interrupted: 0, commands: [], operations: 0, hooks: {} };
    const hook = n => ({ connect: f => { seen.hooks[n] = f; } });
    const env = Object.assign({ ProgramFiles: "C:\\Program Files", LOCALAPPDATA: "C:\\Users\\u\\AppData\\Local",
                                APPDATA: "C:\\a", TEMP: "C:\\t", USERPROFILE: "C:\\Users\\u" }, o.env || {});
    const ctx = {
        console: { log() {} },
        systemInfo: { kernelType: o.kernel || "winnt" },
        installer: {
            isInstaller: () => !o.uninstall, isUninstaller: () => !!o.uninstall, isUpdater: () => false,
            isPackageManager: () => false, isCommandLineInstance: () => !!o.cli,
            value: k => k === "TargetDir" ? o.target : k === "ProductVersion" ? "4.2.3" : "", environmentVariable: n => env[n] || "",
            fileExists: p => o.installs.some(d => p === d + "/FintermMaintenanceTool.exe") || /AppData\/Local\/finterm$/.test(p.replace(/\\/g, "/")),
            interrupt: () => { ++seen.interrupted; }, execute: (p, a) => {
                const line = p + " " + (a || []).join(" ");
                seen.commands.push(line);
                if (/tasklist/i.test(p)) {
                    const q = (a || []).join(" ");
                    if (/FintermMaintenanceTool/.test(q) && o.uninstallerOpen) return ["FintermMaintenanceTool.exe   4242 Console", 0];
                    if (/finterm\.exe/.test(q) && o.running) return ["finterm.exe   4243 Console", 0];
                    return ["INFO: No tasks", 0];
                }
                // The old install's uninstaller (direct, or elevated through PowerShell) removes it.
                if (/purge/.test(line) && !o.purgeFails)
                    o.installs = o.installs.filter(d => !line.replace(/\\/g, "/").includes(d));
                return ["", 0];
            },
            installationFinished: hook("installed"), uninstallationStarted: hook("uninstall"), uninstallationFinished: hook("uninstalled"),
        },
        gui: { rejectWithoutPrompt: () => { ++seen.rejected; } }, // defined on the command line too (IFW 4.7)
        component: { createOperations: () => { ++seen.operations; }, addOperation() {} },
        QMessageBox: { Yes: 1, No: 2, Ok: 4, critical: () => { ++seen.critical; }, warning: () => { ++seen.warned; },
                       question: () => { ++seen.asked; return o.answerYes ? 1 : 2; } },
    };
    vm.createContext(ctx);
    vm.runInContext(source, ctx);
    const c = vm.runInContext("new Component()", ctx);
    if (o.operations) try { c.createOperations(); } catch (e) { seen.threw = String(e); }
    if (o.uninstall) seen.hooks.uninstall();
    return seen;
}

const purged = (s, dir) => s.commands.some(c => /purge.*--default-answer.*--confirm-command/.test(c) &&
                                                 c.replace(/\\/g, "/").includes(dir));
let s = run({ target: PU, installs: [PU], operations: true, answerYes: true });
check("over its own install, Yes: removed by its uninstaller, then installed",
      s.asked === 1 && purged(s, PU) && !s.threw && s.operations === 1 && s.rejected === 0);
s = run({ target: PU, installs: [PF], operations: true, answerYes: true });
check("over Program Files, Yes: removed elevated, then installed per user",
      purged(s, PF) && s.commands.some(c => /RunAs/.test(c)) && !s.threw && s.operations === 1);
s = run({ target: PU, installs: [PF], cli: true, operations: true, answerYes: true });
check("command line over Program Files (default Yes): replaced", purged(s, PF) && s.operations === 1 && s.interrupted === 0);
s = run({ target: PU, installs: [PU], operations: true, answerYes: true, env: { ProgramFiles: "" } });
check("no ProgramFiles in the environment: a per-user install is not taken for one (no elevation)",
      purged(s, PU) && !s.commands.some(c => /RunAs/.test(c)) && s.operations === 1);
s = run({ target: PU, installs: [PU, PF], operations: true, answerYes: true });
check("per-user and Program Files both installed: both removed (asked once), then installed",
      s.asked === 1 && purged(s, PU) && purged(s, PF) && !s.threw && s.operations === 1);
s = run({ target: PU, installs: [PU], operations: true, answerYes: false });
check("over an install, No: closed, nothing removed or installed",
      s.rejected === 1 && !purged(s, PU) && !!s.threw && s.operations === 0);
s = run({ target: PU, installs: [PF], operations: true, answerYes: true, purgeFails: true });
check("old install not removed: told, closed, nothing installed",
      s.critical === 1 && s.rejected === 1 && !!s.threw && s.operations === 0);
s = run({ target: PU, installs: [PU], cli: true, operations: true, answerYes: true, running: true });
check("finterm running, command line: told, interrupted, nothing removed",
      s.critical === 1 && s.interrupted === 1 && !purged(s, PU) && s.operations === 0);
s = run({ target: PU, installs: [], operations: true });
check("first install goes in", s.critical === 0 && !s.threw && s.operations === 1);
s = run({ target: "/home/u/finterm", installs: [PF], kernel: "linux", operations: true });
check("not a Windows rule elsewhere", s.critical === 0 && !s.threw);

const removed = s => s.commands.some(c => /rmdir/.test(c) && /AppData.Local.finterm/.test(c));
s = run({ target: PU, installs: [PU], uninstall: true, answerYes: true });
check("uninstall, alone, Yes: the data goes (control)", s.asked === 1 && removed(s));
s = run({ target: PU, installs: [PU], uninstall: true, answerYes: false });
check("uninstall, alone, No: the data stays", s.asked === 1 && s.commands.length === 0);
s = run({ target: PU, installs: [PU, PF], uninstall: true, answerYes: true });
check("uninstall beside Program Files: not asked, data stays", s.asked === 0 && s.commands.length === 0);
s = run({ target: PF.replace(/\//g, "\\"), installs: [PU, PF], uninstall: true, answerYes: true });
check("uninstall of Program Files beside per-user: not asked, data stays", s.asked === 0 && s.commands.length === 0);
// An uninstaller still open: its self-delete would take the new install's.
s = run({ target: PU, installs: [], uninstallerOpen: true, operations: true });
check("uninstaller window open: told, closed, nothing installed",
      s.critical === 1 && s.rejected === 1 && !!s.threw && s.operations === 0);
s = run({ target: PU, installs: [], uninstallerOpen: true, cli: true, operations: true });
check("uninstaller open, command line: interrupted", s.interrupted === 1 && !!s.threw);

// The updater's copies beside the install go with the program, whatever the
// data answer — they are a whole finterm, not the user's data.
s = run({ target: PU, installs: [PU], uninstall: true, answerYes: false });
let leftovers = (() => { const seen2 = s; seen2.hooks.uninstalled(); return seen2.commands.filter(c => /previous/.test(c)); })();
check("uninstall removes the updater's copies (data kept)",
      leftovers.length === 1 && /AppData\\Local\\Programs'/.test(leftovers[0]));
{
    const ctx = { console: { log() {} } };
    vm.createContext(ctx);
    vm.runInContext(source, ctx);
    const re = new RegExp(vm.runInContext('updaterLeftoverPattern("finterm")', ctx));
    const yes = ["finterm.previous", "finterm.previous.old", "finterm.new", "finterm.ready-20261127120000",
                 "finterm.failed", "FINTERM.previous".toLowerCase()];
    const no = ["finterm", "finterm-backups", "finterm.previousX", "finterm.previous.bak", "otherfinterm.previous",
                "finterm.new-123"];
    check("leftover names matched", yes.every(n => re.test(n)));
    check("nothing else matched", no.every(n => !re.test(n)));
}
console.log(failures + " failure(s)");
process.exit(failures ? 1 : 0);

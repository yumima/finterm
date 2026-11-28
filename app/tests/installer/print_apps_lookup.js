// tests/installer/print_apps_lookup.js — prints the PowerShell the control
// script runs to read Windows' Apps list, so the Windows release job can run
// it for real (the node test checks it only by its structure).
// Usage: node print_apps_lookup.js <controlscript.qs>
const fs = require("fs"), vm = require("vm");
let command = "";
const ctx = {
    console: { log() {} },
    systemInfo: { kernelType: "winnt" },
    installer: { environmentVariable: n => process.env[n] || "", value: () => "",
                 execute: (p, a) => { command = a[a.length - 1]; return ["", 0]; } },
};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(process.argv[2], "utf8"), ctx);
vm.runInContext("registeredInstalls()", ctx);
process.stdout.write(command);

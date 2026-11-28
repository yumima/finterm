// The installer's own GUI, clicked through as a user would (IFW controller
// script: `setup.exe --script ifw-gui-install.qs`). CI uses it for updating
// over an existing install: only the GUI loads the component scripts — whose
// constructor removes the installed copy — before IFW checks the target
// folder; the command line checks it first and refuses.
//
// Nothing here waits on a dialog: what would stop a user is logged as
// "[ci] refused: …" and the installer is cancelled (gui-setup.ps1 reports it).

function Controller()
{
    // The update question (installscript.qs): the answer a user gives.
    installer.setMessageBoxAutomaticAnswer("finterm.install.update", QMessageBox.Yes);
    // Its failures: OK, after which the script itself stops the installer.
    var stops = ["finterm.install.update_failed", "finterm.install.running", "finterm.install.running_ask",
                 "finterm.install.uninstaller_open", "TargetDirectoryInUse", "OverwriteTargetDirectory"];
    for (var i = 0; i < stops.length; ++i)
        installer.setMessageBoxAutomaticAnswer(stops[i], QMessageBox.Ok);
    installer.installationFinished.connect(function() { gui.clickButton(buttons.NextButton); });
}

function next(page) { console.log("[ci] " + page); gui.clickButton(buttons.NextButton); }

function refuse(why)
{
    console.log("[ci] refused: " + why);
    gui.rejectWithoutPrompt();
}

Controller.prototype.IntroductionPageCallback = function() { next("introduction"); };

Controller.prototype.TargetDirectoryPageCallback = function()
{
    // By now the component constructor has removed the installed copy. What
    // IFW would stop on (an install still there; a folder not empty — a user
    // would be asked about it) is checked here instead.
    var dir = installer.value("TargetDir");
    if (installer.fileExists(dir + "/FintermMaintenanceTool.exe"))
        return refuse("an installation is still in " + dir);
    if (installer.fileExists(dir)) {
        var r = installer.execute("cmd.exe", ["/c", "dir /b /a \"" + dir.replace(/\//g, "\\") + "\""]);
        var left = r && r.length > 0 ? String(r[0]).trim() : "";
        if (left)
            return refuse(dir + " is not empty: " + left.replace(/\r?\n/g, ", "));
    }
    next("target " + dir);
};

Controller.prototype.ComponentSelectionPageCallback = function() { next("components"); };

Controller.prototype.LicenseAgreementPageCallback = function()
{
    var page = gui.currentPageWidget();
    var accept = page.AcceptLicenseCheckBox || page.AcceptLicenseRadioButton; // IFW 4.x / 3.x
    if (!accept)
        return refuse("no license checkbox on the license page");
    accept.setChecked(true);
    next("license");
};

Controller.prototype.StartMenuDirectoryPageCallback = function() { next("start menu"); };
Controller.prototype.ReadyForInstallationPageCallback = function() { next("ready"); };

Controller.prototype.FinishedPageCallback = function()
{
    console.log("[ci] finished");
    gui.clickButton(buttons.FinishButton);
};

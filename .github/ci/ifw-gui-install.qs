// The installer's own GUI, clicked through as a user would (IFW controller
// script: `setup.exe --script ifw-gui-install.qs`). CI uses it for updating
// over an existing install: only the GUI loads the component scripts — whose
// constructor removes the installed copy — before IFW checks the target
// folder; the command line checks it first and refuses.
//
// IFW judges the target folder itself; its messages (TargetDirectoryInUse,
// OverwriteTargetDirectory) are in the verbose log, which gui-setup.ps1
// watches — it stops the installer at the first one and reports it.

function Controller()
{
    // The update question (installscript.qs): the answer a user gives.
    installer.setMessageBoxAutomaticAnswer("finterm.install.update", QMessageBox.Yes);
    // Its failures: OK, after which the script itself stops the installer.
    var stops = ["finterm.install.update_failed", "finterm.install.running", "finterm.install.running_ask",
                 "finterm.install.uninstaller_open"];
    for (var i = 0; i < stops.length; ++i)
        installer.setMessageBoxAutomaticAnswer(stops[i], QMessageBox.Ok);
    // IFW's own: answered (and so logged) at once rather than left open.
    installer.setMessageBoxAutomaticAnswer("TargetDirectoryInUse", QMessageBox.Ok);
    installer.setMessageBoxAutomaticAnswer("OverwriteTargetDirectory", QMessageBox.No);
    installer.installationFinished.connect(function() { gui.clickButton(buttons.NextButton); });
}

function next(page) { console.log("[ci] " + page); gui.clickButton(buttons.NextButton); }

function refuse(why)
{
    console.log("[ci] refused: " + why);
    gui.rejectWithoutPrompt();
}

Controller.prototype.IntroductionPageCallback = function() { next("introduction"); };

Controller.prototype.TargetDirectoryPageCallback = function() { next("target " + installer.value("TargetDir")); };

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
